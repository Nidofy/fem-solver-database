#pragma once
// Internal native v2 implementation. Included after the common result schema.
#include <random>
#include <numeric>
#include <memory>
#include "typed_format.hpp"
namespace sdb::lifecycle {
struct ResultReadStats {
    std::uint64_t bytes = 0, decoded_value_bytes = 0;
};
namespace indexed_detail {
inline constexpr std::uint64_t magic = 0x32534552424453ull;
struct Ref {
    std::uint64_t offset = 0, size = 0;
    std::uint32_t crc = 0;
    template <class F> void visit(F f) { f(offset, size, crc); }
};
struct Block {
    Id first = 0, last = 0;
    std::uint32_t count = 0;
    Ref ref;
    ResultAddress first_address, last_address;
    template <class F> void visit(F f) { f(first, last, count, ref); }
};
struct FieldDirectory {
    ResultDescriptor descriptor;
    std::vector<Block> blocks;
    std::optional<TypedFieldPayload> typed;
    template <class F> void visit(F f) { f(descriptor, blocks); }
};
struct Directory {
    FrameInfo info;
    std::vector<FieldDirectory> fields;
    std::optional<ResultBinding> binding;
    template <class F> void visit(F f) { f(info, fields); }
};
struct Block3 : Block {
    template<class F> void visit(F f) { f(first, last, count, ref, first_address, last_address); }
};
struct FieldDirectory3 {
    ResultDescriptor descriptor;
    bool is_typed = false;
    TypedFieldPayload metadata;
    std::vector<Block3> blocks;
    template<class F> void visit(F f) { f(descriptor, is_typed); if (is_typed) f(metadata); f(blocks); }
};
struct Directory3 {
    FrameInfo info;
    ResultBinding binding;
    std::vector<FieldDirectory3> fields;
    template<class F> void visit(F f) { f(info, binding, fields); }
};
inline native::Bytes pack_directory(const Directory &d, std::uint64_t limit, bool typed) {
    if (!typed) return native::pack(d, limit);
    Directory3 w{d.info, d.binding.value(), {}};
    for (const auto &f : d.fields) {
        FieldDirectory3 v; v.descriptor = f.descriptor; v.is_typed = bool(f.typed);
        if (f.typed) v.metadata = *f.typed;
        for (auto &b : f.blocks) { Block3 q; static_cast<Block&>(q) = b; v.blocks.push_back(q); }
        w.fields.push_back(std::move(v));
    }
    return native::pack(w, limit);
}
inline Directory unpack_directory(const native::Bytes &bytes, std::uint64_t limit, bool typed) {
    if (!typed) return native::unpack<Directory>(bytes, limit);
    auto w = native::unpack<Directory3>(bytes, limit);
    Directory d; d.info = w.info; d.binding = w.binding;
    for (auto &v : w.fields) {
        FieldDirectory f; f.descriptor = std::move(v.descriptor);
        if (v.is_typed) f.typed = std::move(v.metadata);
        for (auto &b : v.blocks) f.blocks.push_back(b);
        d.fields.push_back(std::move(f));
    }
    return d;
}
struct Identity {
    std::uint64_t model = 0, nonce = 0;
    template <class F> void visit(F f) { f(model, nonce); }
};
struct Footer {
    Identity identity;
    std::uint64_t count = 0;
    FrameInfo info;
    Ref directory;
    std::array<std::uint64_t, 64> ancestors{};
    template <class F> void visit(F f) { f(identity, count, info, directory, ancestors); }
};
inline std::uint64_t footer_bytes() {
    return 16 + native::encoded_size(Footer{}, 4096);
}
inline Ref emit(std::ostream &out, std::uint32_t kind, const native::Bytes &b) {
    Ref ref{static_cast<std::uint64_t>(out.tellp()), b.size(), checkpoint_detail::crc(b)};
    native::record(out, kind, b);
    return ref;
}
class File {
    std::filesystem::path path_;
    std::uint64_t limit_, size_ = 0, begin_ = 0;
    Identity identity_;
    bool typed_ = false;
    mutable ResultReadStats stats_;
    mutable std::ifstream stream_; // single caller; reused for bounded random metadata reads
    native::Bytes at(const Ref &ref, std::uint32_t kind, std::uint64_t upper, std::uint64_t limit) const {
        if (ref.offset > upper || upper - ref.offset < 16 || ref.size > upper - ref.offset - 16 ||
            ref.size > limit)
            throw std::runtime_error("indexed reference outside committed range/budget");
        auto &in = stream_;
        in.clear();
        in.seekg(static_cast<std::streamoff>(ref.offset));
        auto h = native::header(in, limit);
        stats_.bytes += 16;
        if (h.kind != kind || h.size != ref.size || h.crc != ref.crc)
            throw std::runtime_error("indexed reference/header mismatch");
        auto b = native::body(in, h);
        stats_.bytes += b.size();
        return b;
    }
    Footer footer(std::uint64_t offset) const {
        if (offset < begin_ || offset > size_ || size_ - offset < footer_bytes())
            throw std::runtime_error("indexed footer offset outside file");
        auto &in = stream_;
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset));
        auto h = native::header(in, 4096);
        stats_.bytes += 16;
        if (h.kind != 5 || h.size != footer_bytes() - 16)
            throw std::runtime_error("incomplete indexed footer; explicit recovery required");
        auto b = native::body(in, h);
        stats_.bytes += b.size();
        auto f = native::unpack<Footer>(b, 4096);
        if (f.identity.model != identity_.model || f.identity.nonce != identity_.nonce ||
            f.count > size_ / footer_bytes())
            throw std::runtime_error("indexed identity/count mismatch");
        if (!f.count && (offset != begin_ || f.directory.offset || f.directory.size || f.directory.crc))
            throw std::runtime_error("indexed initial commit identity mismatch");
        if (f.count &&
            (f.directory.offset < begin_ || f.directory.offset >= offset ||
             offset - f.directory.offset < 16 || f.directory.size > offset - f.directory.offset - 16))
            throw std::runtime_error("indexed directory outside commit");
        for (unsigned k = 0; k < 64; ++k) {
            auto jump = std::uint64_t(1) << k;
            if (f.count >= jump ? (f.ancestors[k] < begin_ || f.ancestors[k] >= offset) : f.ancestors[k] != 0)
                throw std::runtime_error("indexed ancestor offset invalid");
        }
        if (f.count) {
            ResultFrame frame{f.info, {}};
            if (typed_) frame.binding = ResultBinding{model.mesh, model.definition, model.configuration,
                f.info.step, f.info.increment, f.info.time};
            validate_frame(frame, model, std::nullopt);
        }
        return f;
    }
    Directory directory(const Footer &f, std::uint64_t end) const {
        auto d = unpack_directory(at(f.directory, 4, end, limit_), limit_, typed_);
        if (typed_) validate_frame(ResultFrame{d.info, {}, d.binding}, model, std::nullopt);
        if (native::pack(d.info) != native::pack(f.info))
            throw std::runtime_error("indexed frame identity mismatch");
        std::set<std::tuple<std::string, std::string, bool, std::uint32_t, std::uint32_t>> keys;
        std::set<std::tuple<std::string,std::string,bool,std::string,std::uint32_t,native::Bytes>> typed_keys;
        for (auto &field : d.fields) {
            auto &desc = field.descriptor;
            if (field.typed) {
                auto &p = *field.typed;
                validate_typed_payload(p);
                if (!p.addresses.empty() || !p.validity.empty() || desc.name != p.schema.name ||
                    desc.components != p.schema.components || desc.basis != p.schema.basis ||
                    !typed_keys.emplace(desc.name,desc.region,desc.history,p.schema_id,p.schema_version,native::pack(p.schema)).second)
                    throw std::runtime_error("indexed typed directory metadata");
            } else if (!keys.emplace(desc.name, desc.region, desc.history, desc.integration_point,
                              desc.section_point)
                     .second ||
                desc.components.empty() || desc.components.size() > 9)
                throw std::runtime_error("indexed field schema");
            bool first = true;
            Id previous = 0;
            ResultAddress previous_address;
            for (auto &b : field.blocks) {
                if (!b.count || b.count > 256 || b.first > b.last ||
                    (field.typed ? (b.first_address > b.last_address || b.first != b.first_address.entity ||
                       b.last != b.last_address.entity || (!first && b.first_address <= previous_address)) :
                       (!first && b.first <= previous)) ||
                    b.ref.offset < begin_ || b.ref.offset >= f.directory.offset ||
                    f.directory.offset - b.ref.offset < 16 ||
                    b.ref.size > f.directory.offset - b.ref.offset - 16)
                    throw std::runtime_error("indexed entity block bounds/order");
                first = false;
                previous = b.last;
                previous_address = b.last_address;
            }
        }
        return d;
    }
    static std::size_t values_width(const FieldDirectory &d) { return d.typed->schema.component_count(); }
    ResultField read_field(const FieldDirectory &d, std::uint64_t end, std::optional<Id> entity,
                           std::uint64_t &budget, const std::set<Id> *selection = nullptr, std::optional<ResultAddress> exact = {}) const {
        ResultField field;
        field.descriptor = d.descriptor;
        if (d.typed) field.typed = typed_detail::metadata(*d.typed);
        std::size_t count = 0;
        auto relevant = [&](const Block &b) {
            if (exact && (*exact < b.first_address || *exact > b.last_address)) return false;
            if (entity && (*entity < b.first || *entity > b.last))
                return false;
            if (selection) {
                auto i = selection->lower_bound(b.first);
                if (i == selection->end() || *i > b.last)
                    return false;
            }
            return true;
        };
        for (auto &b : d.blocks)
            if (relevant(b))
                count = sum(count, d.typed ? b.count : (entity ? 1 : b.count));
        auto required =
            product(count, d.typed ? sum(2 * sizeof(ResultAddress) + 64, product(
                sum(typed_detail::scalar_bytes(d.typed->schema.scalar), 1), d.typed->schema.component_count())) :
                sum(sizeof(Id), product(sizeof(double), d.descriptor.components.size())));
        if (required > budget)
            throw std::length_error("indexed result allocation budget");
        budget -= required;
        if (d.typed) {
            field.typed->addresses.reserve(count);
            std::visit([&](auto &v) { v.reserve(product(count, d.typed->schema.component_count())); }, field.typed->values);
        } else field.ids.reserve(count);
        if (!d.typed) field.values.reserve(product(count, d.descriptor.components.size()));
        for (auto &b : d.blocks) {
            if (!relevant(b))
                continue;
            if (d.typed) {
                // Charge every relevant row, including all samples of one entity, plus decode scratch.
                if (b.ref.size > budget || product(b.count, sum(sizeof(ResultAddress) + 64,
                    product(values_width(d), sum(typed_detail::scalar_bytes(d.typed->schema.scalar), 1)))) > budget - b.ref.size)
                    throw std::length_error("typed block decode budget");
                auto values = native::unpack<TypedFieldPayload>(at(b.ref, 2, end, budget), budget);
                validate_typed_payload(values);
                if (native::pack(typed_detail::metadata(values)) != native::pack(*d.typed) ||
                    values.addresses.size() != b.count || values.addresses.front() != b.first_address ||
                    values.addresses.back() != b.last_address)
                    throw std::runtime_error("typed block schema/address bounds");
                stats_.decoded_value_bytes += product(product(b.count, values.schema.component_count()),
                    typed_detail::scalar_bytes(values.schema.scalar));
                for (std::size_t n = 0; n < values.addresses.size(); ++n) {
                    const auto &a = values.addresses[n];
                    validate_result_address(a, values.schema, model);
                    if (n && a <= values.addresses[n-1]) throw std::runtime_error("typed block order");
                    if ((exact && a != *exact) || (entity && a.entity != *entity) ||
                        (selection && !selection->contains(a.entity))) continue;
                    // If blocks differ only by all-valid mask representation, normalize masks.
                    auto &target = *field.typed;
                    if (!values.validity.empty() && target.validity.empty() && !target.addresses.empty())
                        target.validity.resize(product(target.addresses.size(), target.schema.component_count()), 1);
                    bool fill = values.validity.empty() && !target.validity.empty();
                    typed_detail::append_row(target, values, n);
                    if (fill) target.validity.insert(target.validity.end(), target.schema.component_count(), 1);
                }
                continue;
            }
            auto values = native::unpack<std::pair<std::vector<Id>, std::vector<double>>>(
                at(b.ref, 2, end, 32768), 65536);
            if (values.first.size() != b.count ||
                values.second.size() != product(b.count, d.descriptor.components.size()) ||
                values.first.front() != b.first || values.first.back() != b.last)
                throw std::runtime_error("indexed entity block shape");
            stats_.decoded_value_bytes += values.second.size() * 8;
            for (std::size_t n = 0; n < values.first.size(); ++n) {
                if (n && values.first[n] <= values.first[n - 1])
                    throw std::runtime_error("indexed entity order");
                if ((entity && values.first[n] != *entity) ||
                    (selection && !selection->contains(values.first[n])))
                    continue;
                field.ids.push_back(values.first[n]);
                auto w = d.descriptor.components.size();
                field.values.insert(field.values.end(), values.second.begin() + n * w,
                                    values.second.begin() + (n + 1) * w);
            }
        }
        return field;
    }

  public:
    ResultModel model;
    Footer latest;
    std::uint64_t latest_offset = 0, valid_end = 0;
    bool incomplete_tail = false;
    static void create(std::ostream &out, const ResultModel &model, std::uint64_t limit, bool typed = false) {
        native::prefix(out, typed ? typed_detail::magic : magic);
        auto bytes = typed_detail::pack_model(model, limit, typed);
        native::record(out, 0, bytes);
        std::random_device random;
        Identity id{native::hash(bytes), (std::uint64_t(random()) << 32) ^ random()};
        native::record(out, 10, native::pack(id));
        Footer f;
        f.identity = id;
        native::record(out, 5, native::pack(f));
        out.flush();
        if (!out)
            throw std::runtime_error("indexed output flush failed");
    }
    File(const std::filesystem::path &path, std::uint64_t limit, bool recover = false)
        : path_(path), limit_(limit), stream_(path, std::ios::binary) {
        size_ = std::filesystem::file_size(path);
        std::ifstream in(path, std::ios::binary);
        std::array<std::uint8_t,8> header{};
        native::read(in, header); native::Reader probe{header};
        typed_ = probe.integer<std::uint64_t>() == typed_detail::magic;
        in.seekg(0); native::prefix(in, typed_ ? typed_detail::magic : magic);
        auto b = native::record(in, 0, limit);
        model = typed_detail::unpack_model(b, limit, typed_);
        typed_detail::validate_model(model);
        identity_ = native::unpack<Identity>(native::record(in, 10, 1024), 1024);
        if (identity_.model != native::hash(b))
            throw std::runtime_error("indexed model identity mismatch");
        begin_ = static_cast<std::uint64_t>(in.tellg());
        stats_.bytes = begin_;
        if (size_ < begin_ + footer_bytes())
            throw std::runtime_error("incomplete indexed initial commit");
        if (!recover) {
            latest_offset = size_ - footer_bytes();
            latest = footer(latest_offset);
            valid_end = size_;
        } else {
            // Explicit repair scan validates every complete record checksum. It
            // never treats a CRC failure as a discardable tail or rebuild success.
            auto cursor = begin_;
            while (cursor < size_) {
                if (size_ - cursor < 16) {
                    incomplete_tail = true;
                    break;
                }
                in.seekg(static_cast<std::streamoff>(cursor));
                auto h = native::header(in, limit_);
                if (h.size > size_ - h.offset) {
                    incomplete_tail = true;
                    break;
                }
                auto body = native::body(in, h);
                stats_.bytes += 16 + body.size();
                if (h.kind == 5) {
                    auto f = footer(cursor);
                    if ((valid_end == 0 && f.count != 0) ||
                        (valid_end && (f.count != latest.count + 1 || f.ancestors[0] != latest_offset)))
                        throw std::runtime_error("indexed recovery commit sequence");
                    if (f.count) {
                        auto d = directory(f, cursor);
                        ResultFrame frame;
                        frame.info = d.info;
                        frame.binding = d.binding;
                        auto budget = limit_;
                        for (auto &v : d.fields)
                            frame.fields.push_back(read_field(v, f.directory.offset, {}, budget));
                        validate_frame(frame, model,
                                       latest.count ? std::optional<FrameInfo>(latest.info) : std::nullopt);
                    }
                    latest = f;
                    latest_offset = cursor;
                    valid_end = h.offset + h.size;
                } else if (h.kind != 2 && h.kind != 4)
                    throw std::runtime_error("unknown indexed record");
                cursor = h.offset + h.size;
            }
            incomplete_tail = incomplete_tail || valid_end != size_;
            if (!valid_end)
                throw std::runtime_error("no valid indexed commit prefix");
        }
    }
    bool typed() const { return typed_; }
    ResultReadStats stats() const { return stats_; }
    Footer frame_footer(std::size_t index) const {
        if (index >= latest.count)
            throw std::out_of_range("frame index");
        auto f = latest;
        auto distance = f.count - 1 - index;
        for (int k = 63; k >= 0; --k)
            if (distance & (std::uint64_t(1) << k)) {
                auto next = footer(f.ancestors[k]);
                if (next.count != f.count - (std::uint64_t(1) << k))
                    throw std::runtime_error("indexed ancestor count mismatch");
                f = next;
            }
        return f;
    }
    ResultFrame frame(std::size_t index, const std::string *name = nullptr, bool history = false,
                      std::optional<Id> entity = {},
                      std::function<std::set<Id>(const ResultDescriptor &)> region = {},
                      unsigned point = 1, std::optional<ResultAddress> exact = {}, bool typed_query = false, const std::string *schema_id = nullptr) const {
        auto f = frame_footer(index);
        auto d = directory(f, valid_end);
        ResultFrame result;
        result.info = d.info;
        result.binding = d.binding;
        auto budget = limit_;
        for (auto &v : d.fields)
            if ((!schema_id || (v.typed && v.typed->schema_id == *schema_id)) &&
                (!name || (v.descriptor.name == *name && (typed_query ? bool(v.typed) : !v.typed) &&
                          (typed_query || v.descriptor.history == history) &&
                          (typed_query || v.descriptor.location != Location::IntegrationPoint ||
                           v.descriptor.integration_point == point)))) {
                std::optional<std::set<Id>> selection;
                if (region)
                    selection = region(v.descriptor);
                result.fields.push_back(
                    read_field(v, f.directory.offset, entity, budget, selection ? &*selection : nullptr, exact));
            }
        validate_frame(result, model, std::nullopt);
        return result;
    }
    void append(std::ostream &out, const ResultFrame &frame) {
        Directory d;
        d.info = frame.info;
        d.binding = frame.binding;
        for (auto &field : frame.fields) {
            FieldDirectory fd;
            fd.descriptor = field.descriptor;
            if (field.typed) {
                if (!typed_) throw std::invalid_argument("typed field requires indexed3");
                const auto &p = *field.typed;
                fd.typed = typed_detail::metadata(p);
                if (product(p.addresses.size(), sizeof(std::size_t)) > limit_)
                    throw std::length_error("typed sort budget");
                std::vector<std::size_t> order(p.addresses.size());
                std::iota(order.begin(), order.end(), 0);
                std::sort(order.begin(), order.end(), [&](auto a, auto b) { return p.addresses[a] < p.addresses[b]; });
                for (std::size_t start = 0; start < order.size(); start += 256) {
                    auto block = typed_detail::metadata(p);
                    auto count = std::min<std::size_t>(256, order.size()-start);
                    for (std::size_t k = start; k < start+count; ++k) typed_detail::append_row(block,p,order[k]);
                    auto ref = emit(out, 2, native::pack(block, limit_));
                    Block b{block.addresses.front().entity, block.addresses.back().entity,
                        static_cast<std::uint32_t>(count), ref};
                    b.first_address = block.addresses.front(); b.last_address = block.addresses.back();
                    fd.blocks.push_back(b);
                }
                d.fields.push_back(std::move(fd));
                continue;
            }
            std::vector<std::size_t> order(field.ids.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(),
                      [&](auto a, auto b) { return field.ids[a] < field.ids[b]; });
            for (std::size_t start = 0; start < order.size(); start += 256) {
                auto count = std::min<std::size_t>(256, order.size() - start);
                std::pair<std::vector<Id>, std::vector<double>> block;
                for (std::size_t k = start; k < start + count; ++k) {
                    auto i = order[k];
                    auto w = field.descriptor.components.size();
                    block.first.push_back(field.ids[i]);
                    block.second.insert(block.second.end(), field.values.begin() + i * w,
                                        field.values.begin() + (i + 1) * w);
                }
                const auto block_bytes = native::pack(block, 32768);
                auto ref = emit(out, 2, block_bytes);
                fd.blocks.push_back(
                    {block.first.front(), block.first.back(), static_cast<std::uint32_t>(count), ref});
            }
            d.fields.push_back(std::move(fd));
        }
        Footer f;
        f.identity = identity_;
        f.count = latest.count + 1;
        f.info = frame.info;
        f.directory = emit(out, 4, pack_directory(d, limit_, typed_));
        f.ancestors[0] = latest_offset;
        for (unsigned k = 1; k < 64 && f.count >= (std::uint64_t(1) << k); ++k)
            f.ancestors[k] = footer(f.ancestors[k - 1]).ancestors[k - 1];
        auto ref = emit(out, 5, native::pack(f));
        out.flush();
        if (!out)
            throw std::runtime_error("indexed commit flush failed");
        latest = f;
        latest_offset = ref.offset;
        valid_end = size_ = ref.offset + 16 + ref.size;
    }
};
} // namespace indexed_detail
// Read-only metadata facade. Indexed files retain a bounded footer, not all
// FrameInfo objects. Legacy files keep their original scanned vector.
class FrameIndex {
    std::vector<FrameInfo> legacy_;
    std::shared_ptr<indexed_detail::File> file_;

  public:
    void bind(std::shared_ptr<indexed_detail::File> file) { file_ = std::move(file); }
    std::size_t size() const {
        return file_ ? static_cast<std::size_t>(file_->latest.count) : legacy_.size();
    }
    bool empty() const { return size() == 0; }
    FrameInfo at(std::size_t i) const { return file_ ? file_->frame_footer(i).info : legacy_.at(i); }
    FrameInfo operator[](std::size_t i) const { return at(i); }
    FrameInfo front() const { return at(0); }
    FrameInfo back() const { return at(size() - 1); }
    class const_iterator {
        const FrameIndex *owner_ = nullptr;
        std::size_t index_ = 0;

      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = FrameInfo;
        using difference_type = std::ptrdiff_t;
        using reference = FrameInfo;
        using pointer = void;
        const_iterator() = default;
        const_iterator(const FrameIndex *o, std::size_t i) : owner_(o), index_(i) {}
        FrameInfo operator*() const { return owner_->at(index_); }
        const_iterator &operator++() {
            ++index_;
            return *this;
        }
        const_iterator operator++(int) {
            auto old = *this;
            ++*this;
            return old;
        }
        bool operator==(const const_iterator &) const = default;
    };
    const_iterator begin() const { return {this, 0}; }
    const_iterator end() const { return {this, size()}; }
    void push_back(FrameInfo f) {
        if (file_)
            throw std::logic_error("read-only index");
        legacy_.push_back(f);
    }
};
} // namespace sdb::lifecycle

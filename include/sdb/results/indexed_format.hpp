#pragma once
// Internal native v2 implementation. Included after the common result schema.
#include <random>
#include <numeric>
#include <memory>
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
    template <class F> void visit(F f) { f(first, last, count, ref); }
};
struct FieldDirectory {
    ResultDescriptor descriptor;
    std::vector<Block> blocks;
    template <class F> void visit(F f) { f(descriptor, blocks); }
};
struct Directory {
    FrameInfo info;
    std::vector<FieldDirectory> fields;
    template <class F> void visit(F f) { f(info, fields); }
};
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
        if (f.count)
            validate_frame(ResultFrame{f.info, {}}, model, std::nullopt);
        return f;
    }
    Directory directory(const Footer &f, std::uint64_t end) const {
        auto d = native::unpack<Directory>(at(f.directory, 4, end, limit_), limit_);
        if (native::pack(d.info) != native::pack(f.info))
            throw std::runtime_error("indexed frame identity mismatch");
        std::set<std::tuple<std::string, std::string, bool, std::uint32_t, std::uint32_t>> keys;
        for (auto &field : d.fields) {
            auto &desc = field.descriptor;
            if (!keys.emplace(desc.name, desc.region, desc.history, desc.integration_point,
                              desc.section_point)
                     .second ||
                desc.components.empty() || desc.components.size() > 9)
                throw std::runtime_error("indexed field schema");
            bool first = true;
            Id previous = 0;
            for (auto &b : field.blocks) {
                if (!b.count || b.count > 256 || b.first > b.last || (!first && b.first <= previous) ||
                    b.ref.offset < begin_ || b.ref.offset >= f.directory.offset ||
                    f.directory.offset - b.ref.offset < 16 ||
                    b.ref.size > f.directory.offset - b.ref.offset - 16)
                    throw std::runtime_error("indexed entity block bounds/order");
                first = false;
                previous = b.last;
            }
        }
        return d;
    }
    ResultField read_field(const FieldDirectory &d, std::uint64_t end, std::optional<Id> entity,
                           std::uint64_t &budget, const std::set<Id> *selection = nullptr) const {
        ResultField field;
        field.descriptor = d.descriptor;
        std::size_t count = 0;
        auto relevant = [&](const Block &b) {
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
                count = sum(count, entity ? 1 : b.count);
        auto required =
            product(count, sum(sizeof(Id), product(sizeof(double), d.descriptor.components.size())));
        if (required > budget)
            throw std::length_error("indexed result allocation budget");
        budget -= required;
        field.ids.reserve(count);
        field.values.reserve(product(count, d.descriptor.components.size()));
        for (auto &b : d.blocks) {
            if (!relevant(b))
                continue;
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
    static void create(std::ostream &out, const ResultModel &model, std::uint64_t limit) {
        native::prefix(out, magic);
        auto bytes = native::pack(model, limit);
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
        native::prefix(in, magic);
        auto b = native::record(in, 0, limit);
        model = native::unpack<ResultModel>(b, limit);
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
                      unsigned point = 1) const {
        auto f = frame_footer(index);
        auto d = directory(f, valid_end);
        ResultFrame result;
        result.info = d.info;
        auto budget = limit_;
        for (auto &v : d.fields)
            if (!name || (v.descriptor.name == *name && v.descriptor.history == history &&
                          (v.descriptor.location != Location::IntegrationPoint ||
                           v.descriptor.integration_point == point))) {
                std::optional<std::set<Id>> selection;
                if (region)
                    selection = region(v.descriptor);
                result.fields.push_back(
                    read_field(v, f.directory.offset, entity, budget, selection ? &*selection : nullptr));
            }
        validate_frame(result, model, std::nullopt);
        return result;
    }
    void append(std::ostream &out, const ResultFrame &frame) {
        Directory d;
        d.info = frame.info;
        for (auto &field : frame.fields) {
            FieldDirectory fd;
            fd.descriptor = field.descriptor;
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
        f.directory = emit(out, 4, native::pack(d, limit_));
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

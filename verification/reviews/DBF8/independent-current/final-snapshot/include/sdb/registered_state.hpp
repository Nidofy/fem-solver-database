#pragma once
#include "field.hpp"
#include "state_layout.hpp"
#include <bit>
#include <array>
#include <span>
#include <typeindex>
#include <variant>

namespace sdb {
// Dispatch is per cold field/snapshot; kernels retain ordinary typed Field views.
using RegisteredValues = std::variant<std::vector<float>, std::vector<double>,
    std::vector<std::int32_t>, std::vector<std::uint32_t>, std::vector<std::int64_t>,
    std::vector<std::uint64_t>, std::vector<std::uint8_t>>;
namespace registered_detail {
using Bytes = std::vector<std::uint8_t>;
template<class X> void encode(Bytes &out, const X &x) {
    if constexpr (std::is_enum_v<X>) encode(out, static_cast<std::uint32_t>(x));
    else if constexpr (std::is_arithmetic_v<X>) {
        auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(X)>>(x);
        if constexpr (std::endian::native == std::endian::big) std::reverse(bytes.begin(), bytes.end());
        out.insert(out.end(), bytes.begin(), bytes.end());
    } else if constexpr (std::is_same_v<X, std::string>) {
        encode(out, std::uint64_t(x.size())); out.insert(out.end(), x.begin(), x.end());
    } else if constexpr (requires { const_cast<X&>(x).visit([](auto&...) {}); })
        const_cast<X&>(x).visit([&](const auto &...v) { (encode(out, v), ...); });
    else { encode(out, std::uint64_t(x.size())); for (const auto &v : x) encode(out, v); }
}
template<class V> V scalar(std::span<const std::uint8_t> bytes, std::size_t &pos) {
    if (sizeof(V) > bytes.size() - pos) throw std::runtime_error("truncated registered scalar");
    std::array<std::uint8_t, sizeof(V)> data;
    std::copy_n(bytes.begin() + pos, sizeof(V), data.begin()); pos += sizeof(V);
    if constexpr (std::endian::native == std::endian::big) std::reverse(data.begin(), data.end());
    return std::bit_cast<V>(data);
}
template<class L> struct Layout;
template<> struct Layout<SoA> { static constexpr std::uint32_t kind = 0, width = 0; };
template<std::size_t W> struct Layout<AoSoA<W>> {
    static_assert(W <= UINT32_MAX); static constexpr std::uint32_t kind = 1, width = W;
};
template<class F> struct Traits;
template<class V, std::size_t C, class L> struct Traits<Field<V,C,L>> { using layout = Layout<L>; };
}

class RegisteredFieldBase {
  protected:
    FieldDescriptor descriptor_;
    std::size_t count_;
    std::string schema_id_;
    std::uint32_t schema_version_, layout_, width_;
    std::uint64_t basis_id_;
    std::optional<Field<std::uint8_t>> validity_;
    std::optional<StateLayout> addressing_;
  public:
    RegisteredFieldBase(FieldDescriptor d, std::size_t n, std::string schema,
                        std::uint32_t version, std::uint32_t layout, std::uint32_t width,
                        std::uint64_t basis, bool allocate_validity=true)
        : descriptor_(std::move(d)), count_(n), schema_id_(std::move(schema)),
          schema_version_(version), layout_(layout), width_(width), basis_id_(basis) {
        if (allocate_validity && descriptor_.missing == MissingPolicy::ValidityMask) {
            validity_.emplace(product(n, descriptor_.component_count())); validity_->fill(1);
        }
    }
    virtual ~RegisteredFieldBase() = default;
    const FieldDescriptor &descriptor() const noexcept { return descriptor_; }
    std::size_t count() const noexcept { return count_; }
    const std::string &schema_id() const noexcept { return schema_id_; }
    std::uint32_t schema_version() const noexcept { return schema_version_; }
    std::uint64_t basis_id() const noexcept { return basis_id_; }
    std::uint32_t layout_kind() const noexcept { return layout_; }
    std::uint32_t layout_width() const noexcept { return width_; }
    const std::optional<StateLayout> &addressing() const noexcept { return addressing_; }
    std::size_t scalar_count() const { return product(count_, descriptor_.component_count()); }
    Field<std::uint8_t> &validity_field() {
        if (!validity_) throw std::logic_error("registered field forbids missing values");
        return *validity_;
    }
    std::vector<std::uint8_t> validity() const {
        std::vector<std::uint8_t> out;
        if (validity_) {
            out.resize(validity_->size()); auto v = validity_->read();
            for (std::size_t i=0; i<out.size(); ++i) out[i] = v(i);
        }
        return out;
    }
    virtual std::unique_ptr<RegisteredFieldBase> clone(bool initialized) const = 0;
  private:
    friend class RegisteredState;
    virtual std::unique_ptr<RegisteredFieldBase> stage_shared() const = 0;
    virtual void invalidate_borrows() const noexcept = 0;
  public:
    virtual registered_detail::Bytes initialization() const = 0;
    // Accounted payload (including padding), not allocator overhead or peak RSS.
    virtual std::size_t payload_bytes(std::size_t rows) const = 0;
    virtual std::unique_ptr<RegisteredFieldBase> remap(StateLayout) const = 0;
    virtual RegisteredValues values() const = 0; // entity-major, component-minor
    virtual std::type_index type() const noexcept = 0;
    virtual const void *field_pointer() const noexcept = 0;
    virtual void *field_pointer() noexcept = 0;
    virtual void validate_shape() const = 0;
    virtual void validate() const = 0;
    virtual registered_detail::Bytes block(std::size_t offset, std::size_t count) const = 0;
    virtual void restore_block(std::span<const std::uint8_t>, std::size_t offset, std::size_t count) = 0;
    virtual void manifest(registered_detail::Bytes &) const = 0;
};
template<class F> class RegisteredField final : public RegisteredFieldBase {
    using V = typename F::value_type;
    F field_;
    std::vector<V> initial_;
    struct SharedTag {};
    RegisteredField(const RegisteredField &other,SharedTag)
        : RegisteredFieldBase(other.descriptor_,other.count_,other.schema_id_,other.schema_version_,other.layout_,other.width_,other.basis_id_,false),
          field_(other.field_.stage_shared()), initial_(other.initial_) {
        addressing_=other.addressing_;
        if(other.validity_) validity_.emplace(other.validity_->stage_shared());
    }
  public:
    RegisteredField(FieldDescriptor d, std::size_t n, std::string schema,
                    std::uint32_t version, std::vector<V> initial, std::uint64_t basis,
                    std::optional<StateLayout> addressing = {})
        : RegisteredFieldBase(std::move(d), n, std::move(schema), version,
              registered_detail::Traits<F>::layout::kind, registered_detail::Traits<F>::layout::width, basis),
          field_(n), initial_(std::move(initial)) {
        addressing_ = std::move(addressing);
        if (initial_.empty()) initial_.resize(F::components, V{});
        if (initial_.size() != F::components) throw std::invalid_argument("registered initialization shape");
        auto view = field_.write();
        for (std::size_t i=0; i<n; ++i) for (std::size_t c=0; c<F::components; ++c) view(i,c) = initial_[c];
        validate();
    }
    std::unique_ptr<RegisteredFieldBase> clone(bool initialized) const override {
        if (initialized) return std::make_unique<RegisteredField>(descriptor_, count_, schema_id_, schema_version_, initial_, basis_id_, addressing_);
        return std::make_unique<RegisteredField>(*this);
    }
  private:
    std::unique_ptr<RegisteredFieldBase> stage_shared() const override {
        return std::unique_ptr<RegisteredFieldBase>(new RegisteredField(*this,SharedTag{}));
    }
    void invalidate_borrows() const noexcept override {
        field_.invalidate_borrows();
        if(validity_) validity_->invalidate_borrows();
    }
  public:
    registered_detail::Bytes initialization() const override {
        registered_detail::Bytes out;
        for(auto v:initial_) registered_detail::encode(out,v);
        return out;
    }
    std::size_t payload_bytes(std::size_t rows) const override {
        return sum(product(F::layout_type::size(rows,F::components),sizeof(V)),
                   sum(product(rows,validity_?F::components:0),product(initial_.size(),sizeof(V))));
    }
    std::unique_ptr<RegisteredFieldBase> remap(StateLayout next) const override {
        if(!addressing_) throw std::logic_error("implicit state layout cannot be remapped");
        next.validate(descriptor_); validate();
        const auto count=next.addresses.size();
        auto out=std::make_unique<RegisteredField>(descriptor_,count,schema_id_,schema_version_,initial_,basis_id_,std::move(next));
        std::map<lifecycle::ResultAddress,std::size_t> old;
        for(std::size_t i=0;i<count_;++i) old.emplace(addressing_->addresses[i],i);
        auto source=field_.read(); auto destination=out->field_.write();
        for(std::size_t i=0;i<count;++i) {
            auto it=old.find(out->addressing_->addresses[i]); if(it==old.end()) continue;
            // Retain exact typed scalars directly; no per-row encode/allocation/decode.
            for(std::size_t c=0;c<F::components;++c) {
                destination(i,c)=source(it->second,c);
                if(validity_) out->validity_->write()(i*F::components+c)=validity_->read()(it->second*F::components+c);
            }
        }
        out->validate(); return out;
    }
    std::type_index type() const noexcept override { return typeid(F); }
    const void *field_pointer() const noexcept override { return &field_; }
    void *field_pointer() noexcept override { return &field_; }
    RegisteredValues values() const override {
        validate(); std::vector<V> out; out.reserve(scalar_count()); auto view = field_.read();
        for (std::size_t i=0; i<count_; ++i) for (std::size_t c=0; c<F::components; ++c) out.push_back(view(i,c));
        return out;
    }
    void validate_shape() const override {
        if (field_.size() != count_ || (validity_ && validity_->size() != scalar_count()))
            throw std::logic_error("registered field shape changed");
        if(addressing_ && addressing_->addresses.size()!=count_) throw std::logic_error("registered address count changed");
    }
    void validate() const override {
        validate_shape();
        if(addressing_) addressing_->validate(descriptor_);
        for (auto value : initial_) if constexpr (std::is_floating_point_v<V>)
            if (!std::isfinite(value)) throw std::invalid_argument("registered nonfinite initialization");
        auto view = field_.read();
        for (std::size_t i=0; i<count_; ++i) for (std::size_t c=0; c<F::components; ++c) {
            auto value = view(i,c);
            if constexpr (std::is_floating_point_v<V>)
                if (!std::isfinite(value)) throw std::invalid_argument("registered nonfinite state");
            if (validity_) {
                auto valid = validity_->read()(i*F::components+c);
                if (valid > 1 || (!valid && value != V{})) throw std::invalid_argument("registered invalid slot must be zero");
                if constexpr (std::is_floating_point_v<V>)
                    if (!valid && std::signbit(value)) throw std::invalid_argument("registered invalid negative zero");
            }
        }
    }
    registered_detail::Bytes block(std::size_t offset, std::size_t count) const override {
        if (offset > scalar_count() || count > scalar_count()-offset) throw std::out_of_range("registered block");
        registered_detail::Bytes out; out.reserve(product(count, sizeof(V)+(validity_ ? 1 : 0)));
        auto view = field_.read();
        for (std::size_t k=offset; k<offset+count; ++k) {
            registered_detail::encode(out, view(k/F::components,k%F::components));
            if (validity_) registered_detail::encode(out, validity_->read()(k));
        }
        return out;
    }
    void restore_block(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count) override {
        if (offset > scalar_count() || count > scalar_count()-offset || bytes.size() != product(count,sizeof(V)+(validity_?1:0)))
            throw std::runtime_error("registered block length");
        std::size_t pos = 0; auto view = field_.write();
        for (std::size_t k=offset; k<offset+count; ++k) {
            view(k/F::components,k%F::components) = registered_detail::scalar<V>(bytes,pos);
            if (validity_) validity_->write()(k) = registered_detail::scalar<std::uint8_t>(bytes,pos);
        }
    }
    void manifest(registered_detail::Bytes &out) const override {
        using registered_detail::encode;
        encode(out, descriptor_); encode(out, std::uint64_t(count_)); encode(out,schema_id_);
        encode(out,schema_version_); encode(out,layout_); encode(out,width_); encode(out,basis_id_); encode(out,initial_);
    }
};
class RegisteredState {
    std::vector<std::unique_ptr<RegisteredFieldBase>> entries_;
    std::optional<std::vector<std::unique_ptr<RegisteredFieldBase>>> trial_;
    bool frozen_ = false;
    std::uint64_t model_context_ = 0, equation_context_ = 0, consumer_context_ = 0;
  public:
    RegisteredState() = default;
    RegisteredState(const RegisteredState &other) : frozen_(other.frozen_),
        model_context_(other.model_context_), equation_context_(other.equation_context_), consumer_context_(other.consumer_context_) {
        if(other.in_trial()) throw std::logic_error("cannot copy active registered trial");
        for (const auto &entry : other.entries_) entries_.push_back(entry->clone(false));
    }
    RegisteredState &operator=(const RegisteredState &other) {
        if (this != &other) { RegisteredState copy(other); *this = std::move(copy); } return *this;
    }
    RegisteredState(RegisteredState&&) noexcept = default;
    RegisteredState &operator=(RegisteredState&&) noexcept = default;
    class ConstEntries {
        const std::vector<std::unique_ptr<RegisteredFieldBase>> *entries_;
      public:
        struct Iterator {
            std::vector<std::unique_ptr<RegisteredFieldBase>>::const_iterator value;
            const RegisteredFieldBase *operator*() const noexcept { return value->get(); }
            Iterator &operator++() noexcept { ++value; return *this; }
            bool operator!=(const Iterator &other) const noexcept { return value != other.value; }
        };
        explicit ConstEntries(const std::vector<std::unique_ptr<RegisteredFieldBase>> &entries) : entries_(&entries) {}
        Iterator begin() const noexcept { return {entries_->begin()}; }
        Iterator end() const noexcept { return {entries_->end()}; }
        std::size_t size() const noexcept { return entries_->size(); }
        const RegisteredFieldBase *operator[](std::size_t i) const { return entries_->at(i).get(); }
    };
    ConstEntries entries() const noexcept { return ConstEntries(entries_); }
    bool empty() const noexcept { return entries_.empty(); }
    bool frozen() const noexcept { return frozen_; }
    void bind_context(std::uint64_t model, std::uint64_t equations, std::uint64_t consumer=0) {
        if(frozen_ || !model || !equations) throw std::invalid_argument("registered context must bind before freeze");
        model_context_=model; equation_context_=equations; consumer_context_=consumer;
    }
    std::uint64_t model_context() const noexcept { return model_context_; }
    std::uint64_t equation_context() const noexcept { return equation_context_; }
    std::uint64_t consumer_context() const noexcept { return consumer_context_; }
    bool in_trial() const noexcept { return trial_.has_value(); }
    void begin_trial() {
        if(in_trial()) throw std::logic_error("registered trial already active");
        validate(); std::vector<std::unique_ptr<RegisteredFieldBase>> copy;
        for(const auto &e:entries_) copy.push_back(e->clone(false));
        trial_.emplace(std::move(copy));
    }
    // Explicit COW phase boundary; caller must have joined every raw consumer.
    void begin_trial_cow() {
        if(in_trial()) throw std::logic_error("registered trial already active");
        validate(); std::vector<std::unique_ptr<RegisteredFieldBase>> copy;
        for(const auto &e:entries_) copy.push_back(e->stage_shared());
        // No throwing operations after invalidation: failed staging leaves all borrows intact.
        for(const auto &e:entries_) e->invalidate_borrows();
        trial_.emplace(std::move(copy));
    }
    void commit() {
        if(!in_trial()) throw std::logic_error("no registered trial");
        validate(); trial_.reset();
    }
    void rollback() {
        if(!in_trial()) throw std::logic_error("no registered trial");
        entries_=std::move(*trial_); trial_.reset();
    }
    // Call only at a joined phase boundary. Retain rows by their full identity, including generation.
    void remap(const std::string &name, StateLayout next) {
        if(!frozen_) throw std::logic_error("remap requires frozen registered schema");
        for(auto &e:entries_) if(e->descriptor().name==name) { auto staged=e->remap(std::move(next)); e=std::move(staged); return; }
        throw std::out_of_range("unknown registered field");
    }
    void freeze() { validate(false); frozen_ = true; }
    RegisteredFieldBase &find(const std::string &name) {
        for (auto &e : entries_) if (e->descriptor().name == name) return *e;
        throw std::out_of_range("unknown registered state field");
    }
    const RegisteredFieldBase &find(const std::string &name) const { return const_cast<RegisteredState*>(this)->find(name); }
    template<class F> F &get(const std::string &name) {
        auto &e = find(name); if (e.type() != typeid(F)) throw std::invalid_argument("registered field type mismatch");
        return *static_cast<F*>(e.field_pointer());
    }
    template<class F> const F &get(const std::string &name) const {
        const auto &e = find(name); if (e.type() != typeid(F)) throw std::invalid_argument("registered field type mismatch");
        return *static_cast<const F*>(e.field_pointer());
    }
    template<class F> F &add(FieldDescriptor d, std::size_t n, std::string schema,
                            std::uint32_t version, std::vector<typename F::value_type> initial, std::uint64_t basis,
                            std::optional<StateLayout> addressing = {}) {
        if (frozen_) throw std::logic_error("registered state layout frozen");
        d.validate();
        if (schema.empty() || !version || d.scalar != scalar_type<typename F::value_type>() ||
            d.component_count() != F::components || (!addressing && d.storage != StorageKind::Dense))
            throw std::invalid_argument("registered field descriptor/type/schema mismatch");
        if(addressing) {
            addressing->validate(d);
            if(addressing->addresses.size()!=n) throw std::invalid_argument("registered address/field shape");
        }
        for (const auto &e : entries_) if (e->descriptor().name == d.name) throw std::invalid_argument("duplicate registered field");
        auto entry = std::make_unique<RegisteredField<F>>(std::move(d),n,std::move(schema),version,std::move(initial),basis,std::move(addressing));
        auto *field = static_cast<F*>(entry->field_pointer()); entries_.push_back(std::move(entry)); return *field;
    }
    void validate(bool require_frozen = true) const {
        if (require_frozen && !frozen_) throw std::logic_error("registered state layout must be frozen");
        for (const auto &e : entries_) e->validate();
    }
    void validate_shape() const {
        if (!frozen_) throw std::logic_error("registered state layout must be frozen");
        for (const auto &e : entries_) e->validate_shape();
    }
    void restore_block(std::size_t index, std::span<const std::uint8_t> bytes,
                       std::size_t offset, std::size_t count) {
        entries_.at(index)->restore_block(bytes,offset,count);
    }
    RegisteredState initialized_copy() const {
        validate_shape(); RegisteredState out; out.frozen_ = true;
        out.model_context_=model_context_; out.equation_context_=equation_context_; out.consumer_context_=consumer_context_;
        for (const auto &e : entries_) out.entries_.push_back(e->clone(true));
        return out;
    }
    registered_detail::Bytes manifest() const {
        validate_shape(); registered_detail::Bytes out;
        bool explicit_layout=model_context_!=0;
        for(const auto &e:entries_) explicit_layout |= e->addressing().has_value();
        registered_detail::encode(out,std::uint32_t(explicit_layout ? 2 : 1)); registered_detail::encode(out,std::uint64_t(entries_.size()));
        if(explicit_layout) {
            registered_detail::encode(out,model_context_); registered_detail::encode(out,equation_context_);
            registered_detail::encode(out,consumer_context_);
        }
        for (const auto &e : entries_) {
            e->manifest(out);
            if(explicit_layout) {
                registered_detail::encode(out,std::uint8_t(e->addressing().has_value()));
                if(e->addressing()) registered_detail::encode(out,*e->addressing());
            }
        }
        return out;
    }
    std::uint64_t fingerprint() const {
        auto bytes = manifest(); std::uint64_t h=14695981039346656037ull;
        for (auto b : bytes) { h ^= b; h *= 1099511628211ull; } return h;
    }
};
} // namespace sdb

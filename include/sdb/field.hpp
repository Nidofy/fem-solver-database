#pragma once
#include "memory.hpp"
#include "schema.hpp"
#include "types.hpp"
#include <optional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

#ifdef __CUDACC__
#define SDB_HD __host__ __device__
#else
#define SDB_HD
#endif
namespace sdb {
struct SoA {
    static std::size_t size(std::size_t n, std::size_t c) { return product(n, c); }
    SDB_HD static std::size_t offset(std::size_t i, std::size_t c, std::size_t n, std::size_t) {
        return c * n + i;
    }
};
template <std::size_t Width = 32> struct AoSoA {
    static_assert(Width > 0);
    static std::size_t size(std::size_t n, std::size_t c) {
        return product(product(sum(n, Width - 1) / Width, Width), c);
    }
    SDB_HD static std::size_t offset(std::size_t i, std::size_t c, std::size_t, std::size_t nc) {
        return (i / Width) * nc * Width + c * Width + i % Width;
    }
};
template <class T, std::size_t C = 1, class Layout = SoA> struct View {
    T *data = nullptr;
    std::size_t count = 0;
    SDB_HD T &operator()(std::size_t i, std::size_t c = 0) const {
        return data[Layout::offset(i, c, count, C)];
    }
};
struct ViewLifetime {
    std::uint64_t generation = 0;
};
template <class T, std::size_t C, class Layout> class CheckedView {
    View<T, C, Layout> view_;
    std::weak_ptr<ViewLifetime> life_;
    std::uint64_t generation_;

  public:
    CheckedView(View<T, C, Layout> v, const std::shared_ptr<ViewLifetime> &l)
        : view_(v), life_(l), generation_(l->generation) {}
    void validate() const {
        auto life = life_.lock();
        if (!life || life->generation != generation_)
            throw std::logic_error("expired field view");
    }
    // Raw borrows are valid only while the caller prevents resize/move/swap/destruction.
    View<T, C, Layout> raw() const { validate(); return view_; }
    std::size_t size() const { validate(); return view_.count; }
    T &at(std::size_t i, std::size_t c = 0) const {
        validate();
        if (i >= view_.count || c >= C)
            throw std::out_of_range("field index");
        return view_(i, c);
    }
};
template <class T, std::size_t C = 1, class Layout = SoA> class Field {
    static_assert(C > 0 && std::is_arithmetic_v<T>);
    std::shared_ptr<AlignedVector<T>> data_=std::make_shared<AlignedVector<T>>();
    std::size_t count_ = 0;
    std::uint64_t version_ = 0;
    mutable std::shared_ptr<ViewLifetime> life_ = std::make_shared<ViewLifetime>();
    const std::shared_ptr<ViewLifetime> &lifetime() const {
        if (!life_)
            life_ = std::make_shared<ViewLifetime>();
        return life_;
    }

  public:
    using value_type = T;
    using layout_type = Layout;
    static constexpr std::size_t components = C;
    Field() = default;
    explicit Field(std::size_t n) : data_(std::make_shared<AlignedVector<T>>(Layout::size(n, C), T{})), count_(n) {}
    Field(const Field &f) : data_(f.data_?std::make_shared<AlignedVector<T>>(*f.data_):std::make_shared<AlignedVector<T>>()), count_(f.count_), version_(f.version_) {}
    Field &operator=(const Field &f) {
        if (this != &f) {
            auto copy = f.data_?std::make_shared<AlignedVector<T>>(*f.data_):std::make_shared<AlignedVector<T>>();
            if (life_)
                ++life_->generation;
            data_ = std::move(copy);
            count_ = f.count_;
            version_ = f.version_;
        }
        return *this;
    }
    Field(Field &&f) noexcept
        : data_(std::move(f.data_)), count_(std::exchange(f.count_, 0)),
          version_(std::exchange(f.version_, 0)), life_(std::move(f.life_)) {
        if (life_)
            ++life_->generation;
    }
    Field &operator=(Field &&f) noexcept {
        if (this != &f)
            swap(f);
        return *this;
    }
    void swap(Field &f) noexcept {
        if (life_)
            ++life_->generation;
        if (f.life_)
            ++f.life_->generation;
        data_.swap(f.data_);
        life_.swap(f.life_);
        std::swap(count_, f.count_);
        std::swap(version_, f.version_);
    }
    // Explicit phase-boundary sharing. No outstanding raw borrows may exist.
    // Ordinary Field copies remain deep; checked borrows are invalidated here.
    Field fork_shared() const {
        auto out=stage_shared();
        invalidate_borrows();
        return out;
    }
    bool storage_shared() const noexcept {return data_ && data_.use_count()>1;}
  private:
    template<class> friend class RegisteredField;
    void invalidate_borrows() const noexcept {if(life_) ++life_->generation;}
    Field stage_shared() const {
        Field out;
        out.data_=data_; out.count_=count_; out.version_=version_;
        return out;
    }
    void detach() {
        if(!data_) {data_=std::make_shared<AlignedVector<T>>();return;}
        if(data_.use_count()!=1) {
            auto next=std::make_shared<AlignedVector<T>>(*data_);
            data_=std::move(next); if(life_) ++life_->generation;
        }
    }
  public:
    void resize(std::size_t n) {
        Field next(n);
        auto a = read();
        auto b = next.write();
        for (std::size_t c = 0; c < C; ++c)
            for (std::size_t i = 0; i < std::min(n, count_); ++i)
                b(i, c) = a(i, c);
        next.version_ = version_ + 1;
        swap(next);
    }
    std::size_t size() const noexcept { return count_; }
    std::size_t bytes() const noexcept { return (data_?data_->size():0) * sizeof(T); }
    std::size_t storage_size() const noexcept { return data_?data_->size():0; }
    std::size_t capacity() const noexcept { return data_?data_->capacity():0; }
    std::uint64_t version() const noexcept { return version_; }
    std::uint64_t generation() const noexcept { return life_ ? life_->generation : 0; }
    View<const T, C, Layout> read() const noexcept { return {data_?data_->data():nullptr, count_}; }
    View<T, C, Layout> write() {
        detach();
        ++version_;
        return {data_?data_->data():nullptr, count_};
    }
    CheckedView<const T, C, Layout> checked_read() const { return {read(), lifetime()}; }
    CheckedView<T, C, Layout> checked_write() { return {write(), lifetime()}; }
    void fill(T v) {
        detach();
        ++version_;
        std::fill(data_->begin(), data_->end(), v);
    }
};

// Registry owns optional fields; handles are resolved once, never used in kernels.
class FieldRegistry {
    struct Entry {
        std::type_index type;
        std::shared_ptr<void> field;
        std::string group;
        std::optional<FieldDescriptor> descriptor;
        FieldId id;
    };
    std::unordered_map<std::string, Entry> entries_;
    std::vector<std::string> names_;
    bool frozen_ = false;

  public:
    template <class F> F &add(std::string name, std::size_t count, std::string group = {}) {
        if (frozen_)
            throw std::logic_error("registry frozen");
        if (entries_.contains(name)) throw std::invalid_argument("duplicate field");
        auto p = std::make_shared<F>(count);
        auto id = FieldId::checked(names_.size());
        names_.push_back(name);
        try {
            auto [it, ok] = entries_.emplace(std::move(name), Entry{typeid(F), p, std::move(group), {}, id});
            if (!ok) throw std::invalid_argument("duplicate field");
            return *static_cast<F *>(it->second.field.get());
        } catch (...) { names_.pop_back(); throw; }
    }
    template <class F> F &add(FieldDescriptor descriptor, std::size_t count, std::string group = {}) {
        descriptor.validate();
        if (descriptor.scalar != scalar_type<typename F::value_type>() ||
            descriptor.component_count() != F::components || descriptor.storage != StorageKind::Dense ||
            descriptor.missing != MissingPolicy::Forbidden)
            throw std::invalid_argument("descriptor disagrees with dense field storage");
        auto name = descriptor.name;
        auto &field = add<F>(name, count, std::move(group));
        entries_.at(name).descriptor = std::move(descriptor);
        return field;
    }
    const FieldDescriptor &descriptor(const std::string &name) const {
        const auto &d = entries_.at(name).descriptor;
        if (!d) throw std::logic_error("legacy field has no semantic descriptor");
        return *d;
    }
    template <class F> F &get(const std::string &name) const {
        const auto &e = entries_.at(name);
        if (e.type != typeid(F))
            throw std::invalid_argument("field type mismatch");
        return *static_cast<F *>(e.field.get());
    }
    FieldId field_id(const std::string &name) const { return entries_.at(name).id; }
    const std::string &name(FieldId id) const { return names_.at(id.value()); }
    template <class F> F &get(FieldId id) const { return get<F>(name(id)); }
    const FieldDescriptor &descriptor(FieldId id) const { return descriptor(name(id)); }
    void freeze() noexcept { frozen_ = true; }
    bool contains(const std::string &name) const { return entries_.contains(name); }
};
} // namespace sdb

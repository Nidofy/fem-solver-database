#pragma once
#include "memory.hpp"
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
    T &at(std::size_t i, std::size_t c = 0) const {
        auto life = life_.lock();
        if (!life || life->generation != generation_)
            throw std::logic_error("expired field view");
        if (i >= view_.count || c >= C)
            throw std::out_of_range("field index");
        return view_(i, c);
    }
};
template <class T, std::size_t C = 1, class Layout = SoA> class Field {
    static_assert(C > 0 && std::is_arithmetic_v<T>);
    AlignedVector<T> data_;
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
    static constexpr std::size_t components = C;
    Field() = default;
    explicit Field(std::size_t n) : data_(Layout::size(n, C), T{}), count_(n) {}
    Field(const Field &f) : data_(f.data_), count_(f.count_), version_(f.version_) {}
    Field &operator=(const Field &f) {
        if (this != &f) {
            auto copy = f.data_;
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
    std::size_t bytes() const noexcept { return data_.size() * sizeof(T); }
    std::size_t storage_size() const noexcept { return data_.size(); }
    std::size_t capacity() const noexcept { return data_.capacity(); }
    std::uint64_t version() const noexcept { return version_; }
    std::uint64_t generation() const noexcept { return life_ ? life_->generation : 0; }
    View<const T, C, Layout> read() const noexcept { return {data_.data(), count_}; }
    View<T, C, Layout> write() noexcept {
        ++version_;
        return {data_.data(), count_};
    }
    CheckedView<const T, C, Layout> checked_read() const { return {read(), lifetime()}; }
    CheckedView<T, C, Layout> checked_write() { return {write(), lifetime()}; }
    void fill(T v) {
        ++version_;
        std::fill(data_.begin(), data_.end(), v);
    }
};

// Registry owns optional fields; handles are resolved once, never used in kernels.
class FieldRegistry {
    struct Entry {
        std::type_index type;
        std::shared_ptr<void> field;
        std::string group;
    };
    std::unordered_map<std::string, Entry> entries_;
    bool frozen_ = false;

  public:
    template <class F> F &add(std::string name, std::size_t count, std::string group = {}) {
        if (frozen_)
            throw std::logic_error("registry frozen");
        auto p = std::make_shared<F>(count);
        auto [it, ok] = entries_.emplace(std::move(name), Entry{typeid(F), p, std::move(group)});
        if (!ok)
            throw std::invalid_argument("duplicate field");
        return *static_cast<F *>(it->second.field.get());
    }
    template <class F> F &get(const std::string &name) const {
        const auto &e = entries_.at(name);
        if (e.type != typeid(F))
            throw std::invalid_argument("field type mismatch");
        return *static_cast<F *>(e.field.get());
    }
    void freeze() noexcept { frozen_ = true; }
    bool contains(const std::string &name) const { return entries_.contains(name); }
};
} // namespace sdb

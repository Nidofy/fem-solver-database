#pragma once
#include "field.hpp"
#include "io/native.hpp"
#include <array>
#include <span>

namespace sdb {
// Portable DTOs contain logical entity-major values, never layout padding.
template <class T, std::size_t C = 1> struct RaggedData {
    std::uint32_t schema = 1;
    ScalarType scalar = scalar_type<T>();
    std::uint64_t components = C;
    std::vector<std::uint64_t> offsets{0};
    std::vector<std::array<T, C>> values;
    template <class F> void visit(F f) { f(schema, scalar, components, offsets, values); }
};
template <class T, std::size_t C = 1> struct SparseData {
    std::uint32_t schema = 1;
    ScalarType scalar = scalar_type<T>();
    std::uint64_t components = C, domain = 0;
    std::vector<std::uint32_t> indices;
    std::vector<std::array<T, C>> values;
    std::vector<std::uint8_t> validity;
    template <class F> void visit(F f) { f(schema, scalar, components, domain, indices, values, validity); }
};
namespace storage_detail {
template <class T, std::size_t C, class D> void validate(const D &d) {
    static_assert(C > 0);
    if (d.schema != 1 || d.scalar != scalar_type<T>() || d.components != C)
        throw std::invalid_argument("storage schema/scalar/components mismatch");
    (void)product(d.values.size(), C);
    if constexpr (std::is_floating_point_v<T>)
        for (const auto &v : d.values)
            for (auto x : v)
                if (!std::isfinite(x)) throw std::invalid_argument("nonfinite storage value");
}
template <class T, std::size_t C, class L>
Field<T, C, L> field(const std::vector<std::array<T, C>> &values) {
    Field<T, C, L> result(values.size());
    auto v = result.write();
    for (std::size_t i = 0; i < values.size(); ++i)
        for (std::size_t c = 0; c < C; ++c) v(i, c) = values[i][c];
    return result;
}
template <class T, std::size_t C, class L>
std::vector<std::array<T, C>> values(const Field<T, C, L> &field) {
    std::vector<std::array<T, C>> result(field.size());
    auto v = field.read();
    for (std::size_t i = 0; i < result.size(); ++i)
        for (std::size_t c = 0; c < C; ++c) result[i][c] = v(i, c);
    return result;
}
} // namespace storage_detail

template <class T, std::size_t C = 1, class Layout = SoA> class RaggedField {
    std::vector<std::uint64_t> offsets_{0};
    Field<T, C, Layout> values_;

  public:
    RaggedField() = default;
    RaggedField(const RaggedField &) = default;
    RaggedField(RaggedField &&) noexcept = default;
    RaggedField &operator=(RaggedField other) {
        offsets_.swap(other.offsets_);
        values_.swap(other.values_);
        return *this;
    }
    class Segment {
        CheckedView<const T, C, Layout> values_;
        std::size_t begin_, count_;
      public:
        Segment(CheckedView<const T, C, Layout> v, std::size_t b, std::size_t n)
            : values_(v), begin_(b), count_(n) {}
        std::size_t size() const noexcept { return count_; }
        const T &at(std::size_t i, std::size_t c = 0) const {
            if (i >= count_) throw std::out_of_range("ragged segment index");
            return values_.at(begin_ + i, c);
        }
    };
    static RaggedField from_data(const RaggedData<T, C> &d) {
        storage_detail::validate<T, C>(d);
        if (d.offsets.empty() || d.offsets.front() != 0 || d.offsets.back() != d.values.size() ||
            !std::is_sorted(d.offsets.begin(), d.offsets.end()))
            throw std::invalid_argument("invalid ragged offsets");
        RaggedField result;
        result.offsets_ = d.offsets;
        result.values_ = storage_detail::field<T, C, Layout>(d.values);
        return result;
    }
    std::size_t size() const noexcept { return values_.size(); }
    std::size_t segments() const noexcept { return offsets_.empty() ? 0 : offsets_.size() - 1; }
    // Metadata spans borrow the owner until assignment, move or destruction.
    std::span<const std::uint64_t> offsets() const noexcept { return offsets_; }
    auto checked_read() const { return values_.checked_read(); }
    auto checked_write() { return values_.checked_write(); }
    Segment segment(std::size_t i) const {
        if (i >= segments()) throw std::out_of_range("ragged segment");
        return {values_.checked_read(), static_cast<std::size_t>(offsets_[i]),
                static_cast<std::size_t>(offsets_[i + 1] - offsets_[i])};
    }
    RaggedData<T, C> data() const {
        RaggedData<T, C> d;
        d.offsets = offsets_.empty() ? std::vector<std::uint64_t>{0} : offsets_;
        d.values = storage_detail::values(values_);
        return d;
    }
    std::size_t encoded_bytes() const {
        return sum(32, sum(product(offsets_.empty() ? 1 : offsets_.size(), 8),
                           product(product(size(), C), sizeof(T))));
    }
    auto encode(std::uint64_t budget = lifecycle::native::default_limit) const {
        if (encoded_bytes() > budget) throw std::length_error("ragged encoding budget");
        auto d = data();
        storage_detail::validate<T, C>(d);
        return lifecycle::native::pack(d, budget);
    }
    static RaggedField decode(std::span<const std::uint8_t> bytes,
                              std::uint64_t budget = lifecycle::native::default_limit) {
        return from_data(lifecycle::native::unpack<RaggedData<T, C>>(bytes, budget));
    }
};

template <class T, std::size_t C = 1, class Layout = SoA> class SparseField {
    std::uint64_t domain_ = 0;
    std::vector<std::uint32_t> indices_;
    std::vector<std::uint8_t> validity_;
    Field<T, C, Layout> values_;
  public:
    SparseField() = default;
    SparseField(const SparseField &) = default;
    SparseField(SparseField &&) noexcept = default;
    SparseField &operator=(SparseField other) {
        std::swap(domain_, other.domain_);
        indices_.swap(other.indices_);
        validity_.swap(other.validity_);
        values_.swap(other.values_);
        return *this;
    }
    static SparseField from_data(const SparseData<T, C> &d) {
        storage_detail::validate<T, C>(d);
        if (d.domain > std::numeric_limits<std::uint32_t>::max() ||
            d.indices.size() != d.values.size() || d.validity.size() != d.values.size())
            throw std::invalid_argument("invalid sparse domain/shape");
        for (std::size_t i = 0; i < d.indices.size(); ++i)
            if (d.indices[i] >= d.domain || (i && d.indices[i] <= d.indices[i - 1]) || d.validity[i] > 1)
                throw std::invalid_argument("invalid sparse index/order/validity");
        SparseField result;
        result.domain_ = d.domain;
        result.indices_ = d.indices;
        result.validity_ = d.validity;
        result.values_ = storage_detail::field<T, C, Layout>(d.values);
        return result;
    }
    std::size_t size() const noexcept { return values_.size(); }
    std::uint64_t domain_count() const noexcept { return domain_; }
    // Metadata spans borrow the owner until assignment, move or destruction.
    std::span<const std::uint32_t> indices() const noexcept { return indices_; }
    std::span<const std::uint8_t> validity() const noexcept { return validity_; }
    auto checked_read() const { return values_.checked_read(); }
    auto checked_write() { return values_.checked_write(); }
    bool present(std::uint32_t entity) const {
        if (entity >= domain_) throw std::out_of_range("sparse domain index");
        auto it = std::lower_bound(indices_.begin(), indices_.end(), entity);
        return it != indices_.end() && *it == entity && validity_[it - indices_.begin()] != 0;
    }
    SparseData<T, C> data() const {
        SparseData<T, C> d;
        d.domain = domain_;
        d.indices = indices_;
        d.validity = validity_;
        d.values = storage_detail::values(values_);
        return d;
    }
    std::size_t encoded_bytes() const {
        return sum(48, sum(product(indices_.size(), 4),
                           sum(product(product(size(), C), sizeof(T)), validity_.size())));
    }
    auto encode(std::uint64_t budget = lifecycle::native::default_limit) const {
        if (encoded_bytes() > budget) throw std::length_error("sparse encoding budget");
        auto d = data();
        storage_detail::validate<T, C>(d);
        return lifecycle::native::pack(d, budget);
    }
    static SparseField decode(std::span<const std::uint8_t> bytes,
                              std::uint64_t budget = lifecycle::native::default_limit) {
        return from_data(lifecycle::native::unpack<SparseData<T, C>>(bytes, budget));
    }
};
} // namespace sdb

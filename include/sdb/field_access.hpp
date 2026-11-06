#pragma once
#include "field.hpp"
#include <span>

namespace sdb {
// Zero-copy contiguous component borrow. Same lifetime as the input raw View.
template<class T, std::size_t C>
std::span<T> component_span(View<T,C,SoA> view, std::size_t component) {
    if(component>=C) throw std::out_of_range("field component");
    if(view.count && !view.data) throw std::invalid_argument("null field storage");
    const auto offset=product(component,view.count);
    return {view.count?view.data+offset:view.data,view.count};
}

// A reusable, complete permutation of entity-major logical scalar positions.
// Layout-specific physical offsets are computed once, outside repeated traversals.
// Does not own field data or bypass Field's write/detach boundary.
template<std::size_t C, class Layout=SoA> class DenseAccessPlan {
    static_assert(C>0);
    std::size_t rows_;
    std::vector<std::size_t> offsets_;
  public:
    DenseAccessPlan(std::size_t rows,std::span<const std::size_t> logical_to_dense)
        :rows_(rows) {
        const auto n=product(rows,C);
        (void)Layout::size(rows,C); // Validate padded capacity as well.
        if(logical_to_dense.size()!=n) throw std::invalid_argument("permutation size");
        std::vector<bool> seen(n,false);
        offsets_.reserve(n);
        for(auto dense:logical_to_dense) {
            if(dense>=n || seen[dense]) throw std::invalid_argument("not a permutation");
            seen[dense]=true;
            offsets_.push_back(Layout::offset(dense/C,dense%C,rows,C));
        }
    }
    std::size_t size() const noexcept {return offsets_.size();}
    std::size_t bytes() const noexcept {return offsets_.size()*sizeof(std::size_t);}
    template<class T> class Borrow {
        T *data_;
        std::span<const std::size_t> offsets_;
      public:
        Borrow(T *data,std::span<const std::size_t> offsets):data_(data),offsets_(offsets) {}
        std::size_t size() const noexcept {return offsets_.size();}
        T &operator[](std::size_t logical) const noexcept {return data_[offsets_[logical]];}
        T &at(std::size_t logical) const {
            if(logical>=size())throw std::out_of_range("logical field index");
            return (*this)[logical];
        }
    };
    template<class T> Borrow<T> bind(View<T,C,Layout> view) const & {
        if(view.count!=rows_ || (view.count && !view.data))throw std::invalid_argument("field/plan shape mismatch");
        return {view.data,offsets_};
    }
    template<class T> Borrow<T> bind(View<T,C,Layout>) const && = delete;
};
} // namespace sdb

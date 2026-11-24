#pragma once
#include "consumer_contract.hpp"

namespace sdb::consumer::v1 {
// A job owns its elements exactly once. Child order is part of its dispatch and restart identity.
class BatchCatalog {
    std::vector<Batch> batches_;
    std::vector<std::uint64_t> ids_;
    std::uint64_t identity_;
  public:
    explicit BatchCatalog(std::vector<Batch> batches) : batches_(std::move(batches)) {
        if (batches_.empty()) throw std::invalid_argument("empty batch catalog");
        const auto common = batches_.front().dofs_;
        std::size_t total = 0;
        for (const auto &child : batches_) {
            if (child.data().model != common->data().model || child.dofs().identity() != common->identity())
                throw std::invalid_argument("batch catalog model/DOF mismatch");
            total = sum(total,child.data().elements.size());
        }
        std::vector<std::uint64_t> elements;
        elements.reserve(total);
        ids_.reserve(batches_.size());
        for (auto &child : batches_) {
            const auto &owned = child.data().elements;
            elements.insert(elements.end(),owned.begin(),owned.end());
            ids_.push_back(child.identity());
            child.dofs_ = common;
        }
        if (!std::ranges::is_sorted(elements)) std::ranges::sort(elements);
        if (std::ranges::adjacent_find(elements) != elements.end())
            throw std::invalid_argument("batch catalog overlapping element ownership");
        std::vector<std::uint64_t> scratch;
        flat_detail::unique_sorted<std::uint64_t>(ids_,scratch,"duplicate batch identity");
        SemanticHasher hash;
        hash.append(UINT64_C(0x31544143424453)); // SDBCAT1 domain tag
        hash.append(common->data().model);
        hash.append(common->identity());
        hash.append(ids_); // length and order
        identity_ = hash.value();
    }
    const DofSpace &dofs() const { return batches_.front().dofs(); }
    const auto &identities() const noexcept { return ids_; }
    std::size_t size() const noexcept { return batches_.size(); }
    const Batch &operator[](std::size_t i) const { return batches_.at(i); }
    std::uint64_t identity() const noexcept { return identity_; }
};
template<class F> FieldBinding<F,false> bind_read(const RegisteredState &state,const BatchCatalog &catalog,const FieldRequirement &r) {
    check_field(state,catalog.dofs(),r,catalog.identity());
    const auto &f=state.template get<F>(r.descriptor.name);
    return {f.checked_read(),f.bytes()};
}
template<class F> FieldBinding<F,true> bind_write(RegisteredState &state,const BatchCatalog &catalog,const FieldRequirement &r) {
    check_field(state,catalog.dofs(),r,catalog.identity());
    auto &f=state.template get<F>(r.descriptor.name);
    return {f.checked_write(),f.bytes()};
}
} // namespace sdb::consumer::v1

namespace sdb {
using BatchCatalog = consumer::v1::BatchCatalog;
} // namespace sdb

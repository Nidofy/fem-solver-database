#pragma once
#include "model.hpp"

namespace sdb {
// Non-owning read access to existing fixed-width batch storage. No packing/copying.
// Mesh lifetime and address stability must cover every copy/subspan of this view.
template<class T, std::size_t N> class FixedBatchRead {
    static_assert(N>0);
    std::span<const Index> elements_;
    std::span<const Index> connectivity_;
    View<const T,3> coordinates_;
    const Material<T> *material_;
    Topology topology_;
    std::uint32_t points_;
    std::size_t stride_,first_=0;
  public:
    explicit FixedBatchRead(const MeshDB<T>& mesh,std::size_t batch_index) {
        const auto &batch=mesh.batches().at(batch_index);
        if(arity(batch.topology)!=N)throw std::invalid_argument("fixed batch width mismatch");
        if(batch.connectivity.size()!=product(batch.elements.size(),N))
            throw std::invalid_argument("fixed batch storage size");
        elements_=batch.elements;connectivity_=batch.connectivity;stride_=elements_.size();
        coordinates_=mesh.reference();material_=&mesh.materials()[batch.material];
        topology_=batch.topology;points_=batch.integration_points;
    }
    FixedBatchRead(MeshDB<T>&&,std::size_t)=delete;
    FixedBatchRead(const MeshDB<T>&&,std::size_t)=delete;
    std::size_t size() const noexcept {return elements_.size();}
    static constexpr std::size_t width=N;
    Topology topology() const noexcept {return topology_;}
    std::uint32_t integration_points() const noexcept {return points_;}
    std::span<const Index> elements() const noexcept {return elements_;}
    View<const T,3> coordinates() const noexcept {return coordinates_;}
    const Material<T>& material() const noexcept {return *material_;}
    // Checked once per local-node column, then contiguous access over the batch.
    std::span<const Index> column(std::size_t local_node) const {
        if(local_node>=N)throw std::out_of_range("local node");
        return connectivity_.subspan(local_node*stride_+first_,size());
    }
    // Unchecked fast access; require row<size() and local_node<N.
    Index node(std::size_t row,std::size_t local_node) const noexcept {
        return connectivity_[local_node*stride_+first_+row];
    }
    Index at(std::size_t row,std::size_t local_node) const {
        if(row>=size() || local_node>=N)throw std::out_of_range("batch node");
        return node(row,local_node);
    }
    FixedBatchRead subspan(std::size_t first,std::size_t count=std::dynamic_extent) const {
        if(first>size())throw std::out_of_range("batch subspan start");
        if(count==std::dynamic_extent)count=size()-first;
        if(count>size()-first)throw std::out_of_range("batch subspan length");
        auto out=*this;out.elements_=elements_.subspan(first,count);out.first_+=first;return out;
    }
};
} // namespace sdb

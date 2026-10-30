#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace sdb {
enum class Topology : std::uint8_t { Point1, Beam2, Tri3, Quad4, Tet4, Hex8, Polygon };
inline unsigned arity(Topology t) {
    switch (t) {
    case Topology::Point1: return 1;
    case Topology::Beam2: return 2;
    case Topology::Tri3: return 3;
    case Topology::Quad4: case Topology::Tet4: return 4;
    case Topology::Hex8: return 8;
    case Topology::Polygon: return 0;
    }
    throw std::invalid_argument("unknown topology");
}
inline unsigned face_count(Topology t) {
    (void)arity(t);
    switch (t) {
    case Topology::Tet4: return 4;
    case Topology::Hex8: return 6;
    default: return 1;
    }
}
// Version 1 freezes the existing mesh/kernel numbering. Local node/edge/face
// indices are zero based; persisted integration/section/layer point numbers are
// one based, with zero reserved for an absent axis. No quadrature is inferred.
inline constexpr std::uint32_t topology_numbering_version = 1;
enum class FaceOrdering : std::uint8_t { Kernel, Abaqus };
struct TopologyLayout {
    std::uint32_t version = topology_numbering_version;
    Topology topology = Topology::Tet4;
    std::uint32_t nodes = 4;
    void validate() const {
        if (version != topology_numbering_version) throw std::invalid_argument("unknown topology numbering version");
        auto n = arity(topology);
        if ((n && nodes != n) || (!n && nodes < 3)) throw std::invalid_argument("topology node count");
        if (nodes > 65535) throw std::length_error("local topology numbering capacity");
    }
    template<class F> void visit(F f) { f(version, topology, nodes); }
};
inline std::vector<unsigned> topology_face_nodes(Topology t, unsigned face,
    FaceOrdering order = FaceOrdering::Kernel, int side = 1, unsigned polygon_nodes = 0,
    std::uint32_t version = topology_numbering_version) {
    TopologyLayout layout{version,t,arity(t) ? arity(t) : polygon_nodes}; layout.validate();
    if (face >= face_count(t) || (side != 1 && side != -1) ||
        (order != FaceOrdering::Kernel && order != FaceOrdering::Abaqus))
        throw std::invalid_argument("invalid local face/order/side");
    constexpr unsigned tet[4][3] = {{0,2,1},{0,1,3},{1,2,3},{2,0,3}};
    constexpr unsigned hex[6][4] = {{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
    constexpr unsigned atet[4][3] = {{0,1,2},{0,3,1},{1,3,2},{2,3,0}};
    constexpr unsigned ahex[6][4] = {{0,1,2,3},{4,7,6,5},{0,4,5,1},{1,5,6,2},{2,6,7,3},{3,7,4,0}};
    std::vector<unsigned> out;
    if (t == Topology::Tet4) {
        auto p = order == FaceOrdering::Kernel ? tet[face] : atet[face]; out.assign(p,p+3);
    } else if (t == Topology::Hex8) {
        auto p = order == FaceOrdering::Kernel ? hex[face] : ahex[face]; out.assign(p,p+4);
    } else {
        for (unsigned i=0;i<layout.nodes;++i) out.push_back(i);
    }
    if (side < 0) std::reverse(out.begin(),out.end());
    return out;
}
inline unsigned topology_edge_count(Topology t, unsigned polygon_nodes = 0) {
    TopologyLayout{1,t,arity(t) ? arity(t) : polygon_nodes}.validate();
    switch(t) {
    case Topology::Point1: return 0;
    case Topology::Beam2: return 1;
    case Topology::Tri3: return 3;
    case Topology::Quad4: return 4;
    case Topology::Tet4: return 6;
    case Topology::Hex8: return 12;
    case Topology::Polygon: return polygon_nodes;
    }
    throw std::invalid_argument("unknown topology");
}
inline std::array<unsigned,2> topology_edge_nodes(Topology t, unsigned edge,
    unsigned polygon_nodes = 0, std::uint32_t version = topology_numbering_version) {
    if (version != topology_numbering_version) throw std::invalid_argument("unknown topology numbering version");
    auto count = topology_edge_count(t,polygon_nodes);
    if (edge >= count) throw std::out_of_range("local edge");
    constexpr std::array<unsigned,2> tet[] = {{0,1},{1,2},{2,0},{0,3},{1,3},{2,3}};
    constexpr std::array<unsigned,2> hex[] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    if (t == Topology::Tet4) return tet[edge];
    if (t == Topology::Hex8) return hex[edge];
    if (t == Topology::Beam2) return {0,1};
    return {edge,(edge+1)%count};
}
// Return the permutation mapping an input face's vertex order onto the kernel
// face order. Face membership and side are unchanged; this is not a new normal.
inline std::vector<unsigned> topology_face_permutation(Topology t, unsigned face,
    FaceOrdering from, FaceOrdering to, int side = 1, unsigned polygon_nodes = 0) {
    auto a=topology_face_nodes(t,face,from,side,polygon_nodes);
    auto b=topology_face_nodes(t,face,to,side,polygon_nodes);
    std::vector<unsigned> p;
    for (auto node:b) p.push_back(static_cast<unsigned>(std::find(a.begin(),a.end(),node)-a.begin()));
    return p;
}
inline std::uint32_t local_point_index(std::uint32_t number, std::uint32_t count,
    std::uint32_t version = topology_numbering_version) {
    if (version != topology_numbering_version) throw std::invalid_argument("unknown local point version");
    if (!number || number > count) throw std::out_of_range("local point number");
    return number-1;
}
} // namespace sdb

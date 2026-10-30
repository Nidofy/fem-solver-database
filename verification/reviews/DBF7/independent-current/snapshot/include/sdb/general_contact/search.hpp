#pragma once
#include "mechanics.hpp"
#include <numeric>

namespace sdb::general_contact {
template <class T> Vec<T> subtract(const Vec<T> &a, const Vec<T> &b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
template <class T> Vec<T> cross(const Vec<T> &a, const Vec<T> &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
template <class T> struct SearchPoint {
    Index node = 0;
    T area = 1;
};
struct SearchStatistics {
    std::size_t queries = 0, box_candidates = 0, accepted = 0;
};
template <class T> struct SearchResult {
    std::vector<Witness<T>> witnesses;
    SearchStatistics statistics;
};
namespace detail {
template <class F> void for_each(std::size_t count, Execution execution, F fn) {
#ifndef _OPENMP
    if (execution == Execution::OpenMP)
        throw std::runtime_error("OpenMP search backend not compiled");
#endif
    if (execution != Execution::Serial && execution != Execution::OpenMP)
        throw std::invalid_argument("search execution");
    const auto end = static_cast<std::int64_t>(count);
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (execution == Execution::OpenMP)
#endif
    for (std::int64_t i = 0; i < end; ++i)
        fn(static_cast<std::size_t>(i));
}
template <class T> struct Box {
    Vec<T> lower{std::numeric_limits<T>::max(), std::numeric_limits<T>::max(), std::numeric_limits<T>::max()};
    Vec<T> upper{std::numeric_limits<T>::lowest(), std::numeric_limits<T>::lowest(),
                 std::numeric_limits<T>::lowest()};
    void add(const Vec<T> &p) {
        for (unsigned c = 0; c < 3; ++c) {
            lower[c] = std::min(lower[c], p[c]);
            upper[c] = std::max(upper[c], p[c]);
        }
    }
    bool overlaps_point(const Vec<T> &p, T band) const {
        for (unsigned c = 0; c < 3; ++c)
            if (p[c] < lower[c] - band || p[c] > upper[c] + band)
                return false;
        return true;
    }
};
template <class T> class BVH {
    struct Node {
        Box<T> box;
        Index left = invalid_index, right = invalid_index, face = invalid_index;
    };
    std::vector<Node> tree_;
    std::vector<Index> order_;
    std::span<const Box<T>> boxes_;
    Index build(std::size_t begin, std::size_t end) {
        const Index index = index_of(tree_.size());
        tree_.push_back({});
        Box<T> box;
        for (auto i = begin; i < end; ++i) {
            box.add(boxes_[order_[i]].lower);
            box.add(boxes_[order_[i]].upper);
        }
        tree_[index].box = box;
        if (end - begin == 1) {
            tree_[index].face = order_[begin];
            return index;
        }
        unsigned axis = 0;
        for (unsigned c = 1; c < 3; ++c)
            if (box.upper[c] - box.lower[c] > box.upper[axis] - box.lower[axis])
                axis = c;
        const auto mid = begin + (end - begin) / 2;
        std::nth_element(order_.begin() + begin, order_.begin() + mid, order_.begin() + end,
                         [&](Index a, Index b) {
                             const T ca = boxes_[a].lower[axis] / 2 + boxes_[a].upper[axis] / 2,
                                     cb = boxes_[b].lower[axis] / 2 + boxes_[b].upper[axis] / 2;
                             return ca == cb ? a < b : ca < cb;
                         });
        const Index left = build(begin, mid), right = build(mid, end);
        tree_[index].left = left;
        tree_[index].right = right;
        return index;
    }

  public:
    explicit BVH(std::span<const Box<T>> boxes) : boxes_(boxes) {
        index_of(product(boxes.size(), 2));
        order_.resize(boxes.size());
        std::iota(order_.begin(), order_.end(), Index(0));
        tree_.reserve(product(boxes.size(), 2));
        if (!boxes.empty())
            build(0, boxes.size());
    }
    template <class F> void query(const Vec<T> &point, T band, F fn) const noexcept {
        if (tree_.empty())
            return;
        // Balanced median tree, fewer than 2^32 nodes => depth < 33.
        std::array<Index, 64> stack{};
        unsigned size = 1;
        stack[0] = 0;
        while (size) {
            const auto &n = tree_[stack[--size]];
            if (!n.box.overlaps_point(point, band))
                continue;
            if (n.face != invalid_index)
                fn(n.face);
            else {
                stack[size++] = n.right;
                stack[size++] = n.left;
            }
        }
    }
};
} // namespace detail
// Directed node/Tet4-triangle reference search. Current-configuration discrete search only.
// No edge-edge, CCD, shell offsets, adjacency-ring exclusion or universal two-pass weighting.
template <class T>
SearchResult<T> detect_node_triangle(const MeshDB<T> &mesh, std::span<const Facet> faces,
                                     std::span<const SearchPoint<T>> points, std::span<const Vec<T>> current,
                                     std::span<const Vec<T>> previous, T band, Id interaction, Id property,
                                     std::uint64_t epoch, std::size_t candidate_capacity,
                                     Execution execution = Execution::Serial,
                                     const std::set<Key> &exclusions = {}) {
    finite(band);
    if (band <= 0 || !interaction || !property || current.size() != mesh.nodes() ||
        previous.size() != mesh.nodes())
        throw std::invalid_argument("invalid search domain");
    for (const auto &positions : {current, previous})
        for (const auto &p : positions)
            for (T x : p)
                finite(x);
    std::set<Index> unique_points;
    for (const auto &p : points) {
        finite(p.area);
        if (p.node >= mesh.nodes() || p.area <= 0 || !unique_points.insert(p.node).second)
            throw std::invalid_argument("invalid search point");
    }
    constexpr unsigned tet[4][3] = {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}};
    std::vector<detail::Box<T>> boxes(faces.size());
    std::vector<Vec<T>> normals(faces.size());
    std::set<FaceId> unique_faces;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const auto &f = faces[i];
        mesh.validate_face(f.identity);
        auto e = mesh.element_index(f.identity.element);
        if (f.count != 3 || f.identity.side != 1 || mesh.element(e).topology != Topology::Tet4 ||
            !unique_faces.insert(f.identity).second)
            throw std::invalid_argument("reference search requires unique outward Tet4 triangles");
        auto conn = mesh.connectivity(e);
        for (unsigned j = 0; j < 3; ++j) {
            if (f.nodes[j] != conn[tet[f.identity.local][j]])
                throw std::invalid_argument("facet topology mismatch");
            boxes[i].add(current[f.nodes[j]]);
        }
        const auto u = subtract(current[f.nodes[1]], current[f.nodes[0]]),
                   v = subtract(current[f.nodes[2]], current[f.nodes[0]]);
        auto n = cross(u, v);
        const T n2 = dot(n, n), scale = dot(u, u) * dot(v, v);
        finite(n2);
        finite(scale);
        if (n2 <= T(256) * std::numeric_limits<T>::epsilon() * scale || n2 <= 0)
            throw std::invalid_argument("degenerate contact triangle");
        for (unsigned c = 0; c < 3; ++c)
            normals[i][c] = n[c] / std::sqrt(n2);
    }
    detail::BVH<T> bvh(boxes);
    auto key_for = [&](std::size_t p, Index f) {
        return Key{interaction,
                   {FeatureKind::Node, mesh.node_id(points[p].node), 0, 1},
                   {FeatureKind::Face, faces[f].identity.element, faces[f].identity.local, 1},
                   0};
    };
    auto allowed = [&](std::size_t p, Index f) {
        const auto node = points[p].node;
        for (unsigned j = 0; j < 3; ++j)
            if (faces[f].nodes[j] == node)
                return false;
        return !exclusions.contains(key_for(p, f));
    };
    std::vector<std::size_t> offsets(points.size() + 1);
    detail::for_each(points.size(), execution, [&](std::size_t p) {
        std::size_t count = 0;
        bvh.query(current[points[p].node], band, [&](Index f) {
            if (allowed(p, f))
                ++count;
        });
        offsets[p + 1] = count;
    });
    for (std::size_t p = 0; p < points.size(); ++p) {
        offsets[p + 1] = sum(offsets[p], offsets[p + 1]);
        if (offsets[p + 1] > candidate_capacity)
            throw std::length_error("contact candidate capacity exceeded; no partial search published");
    }
    std::vector<std::pair<std::size_t, Index>> candidates(offsets.back());
    detail::for_each(points.size(), execution, [&](std::size_t p) {
        auto out = offsets[p];
        bvh.query(current[points[p].node], band, [&](Index f) {
            if (allowed(p, f))
                candidates[out++] = {p, f};
        });
    });
    std::vector<Witness<T>> rows(candidates.size());
    std::vector<std::uint8_t> accepted(candidates.size());
    detail::for_each(candidates.size(), execution, [&](std::size_t index) {
        const auto [p, f] = candidates[index];
        const auto &face = faces[f];
        const auto node = points[p].node;
        const auto a = current[face.nodes[0]], u = subtract(current[face.nodes[1]], a),
                   v = subtract(current[face.nodes[2]], a);
        const auto relative = subtract(current[node], a);
        const T gap = dot(relative, normals[f]);
        if (std::abs(gap) > band)
            return;
        const T uu = dot(u, u), uv = dot(u, v), vv = dot(v, v), ru = dot(relative, u), rv = dot(relative, v),
                det = uu * vv - uv * uv;
        const T b1 = (ru * vv - rv * uv) / det, b2 = (rv * uu - ru * uv) / det, b0 = 1 - b1 - b2;
        // Strict interior including exact boundary; no clamped edge extrapolation.
        if (b0 < 0 || b1 < 0 || b2 < 0)
            return;
        auto &w = rows[index];
        w.key = key_for(p, f);
        w.property = property;
        w.geometry_epoch = epoch;
        w.count = 4;
        w.nodes = {node, face.nodes[0], face.nodes[1], face.nodes[2]};
        w.weights = {1, -b0, -b1, -b2};
        w.normal = normals[f];
        w.gap = gap;
        w.area = points[p].area;
        w.relative_increment = subtract(current[node], previous[node]);
        for (unsigned j = 1; j < 4; ++j)
            for (unsigned c = 0; c < 3; ++c)
                w.relative_increment[c] += w.weights[j] * (current[w.nodes[j]][c] - previous[w.nodes[j]][c]);
        accepted[index] = 1;
    });
    SearchResult<T> result;
    result.statistics = {points.size(), candidates.size(), 0};
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (accepted[i]) {
            rows[i].validate(mesh.nodes(), epoch);
            result.witnesses.push_back(rows[i]);
        }
    std::sort(result.witnesses.begin(), result.witnesses.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
    result.statistics.accepted = result.witnesses.size();
    return result;
}
} // namespace sdb::general_contact

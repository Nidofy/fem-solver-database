#pragma once
#include "field.hpp"
#include "schema.hpp"
#include "types.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <compare>
#include <map>
#include <span>
#include <tuple>
#include <unordered_set>

namespace sdb {
using Id = std::uint64_t;
using Index = std::uint32_t;
inline constexpr Index invalid_index = std::numeric_limits<Index>::max();
inline Index index_of(std::size_t n) {
    if (n >= invalid_index)
        throw std::length_error("32-bit index exhausted");
    return static_cast<Index>(n);
}
enum class Topology : std::uint8_t { Point1, Beam2, Tri3, Quad4, Tet4, Hex8, Polygon };
inline unsigned arity(Topology t) {
    switch (t) {
    case Topology::Point1:
        return 1;
    case Topology::Beam2:
        return 2;
    case Topology::Tri3:
        return 3;
    case Topology::Quad4:
    case Topology::Tet4:
        return 4;
    case Topology::Hex8:
        return 8;
    case Topology::Polygon:
        return 0;
    }
    throw std::invalid_argument("unknown topology");
}
inline unsigned face_count(Topology t) {
    switch (t) {
    case Topology::Tet4:
        return 4;
    case Topology::Hex8:
        return 6;
    default:
        return 1;
    }
}
struct FaceId {
    Id element = 0;
    std::uint16_t local = 0;
    std::int8_t side = 1;
    auto operator<=>(const FaceId &) const = default;
};
struct Surface {
    Id id;
    std::vector<FaceId> faces;
};
struct Part {
    Id id;
    Id property;
};
struct Constraint {
    Index node;
    std::uint8_t component;
    double value;
};
struct NodeSet {
    Id id;
    std::vector<Index> nodes;
};
struct ElementSet {
    Id id;
    std::vector<Index> elements;
};
template <class T> struct Material {
    Id id = 0;
    std::uint32_t model = 1; // 1: isotropic linear elastic [rho,E,nu]; others caller-defined schema
    std::vector<T> parameters;
    std::uint32_t history_variables = 0;
    HistorySchema history_schema{};
};
template <class T> class MaterialDB {
    std::vector<Material<T>> materials_;
    std::unordered_map<Id, Index> ids_;

  public:
    Index add(Material<T> m) {
        if (!m.history_schema.empty() && m.history_schema.template validate<T>() != m.history_variables)
            throw std::invalid_argument("history schema/count mismatch");
        if (ids_.contains(m.id))
            throw std::invalid_argument("duplicate material ID");
        for (T p : m.parameters)
            if (!std::isfinite(p))
                throw std::invalid_argument("nonfinite material");
        if (m.model == 1 && (m.parameters.size() != 3 || m.parameters[0] <= 0 || m.parameters[1] <= 0 ||
                             m.parameters[2] <= T(-1) || m.parameters[2] >= T(.5)))
            throw std::invalid_argument("invalid isotropic material");
        const auto i = index_of(materials_.size());
        materials_.push_back(std::move(m));
        try {
            ids_.emplace(materials_.back().id, i);
        } catch (...) {
            materials_.pop_back();
            throw;
        }
        return i;
    }
    Index index(Id id) const { return ids_.at(id); }
    const Material<T> &operator[](Index i) const { return materials_.at(i); }
    std::size_t size() const noexcept { return materials_.size(); }
};
struct Element {
    Id id;
    Topology topology;
    Index material, part;
    std::uint32_t integration_points;
};
struct SamplePoint {
    std::uint32_t integration_point = 1, layer = 0, section_point = 0;
    Index material = 0;
    auto operator<=>(const SamplePoint &) const = default;
    template <class F> void visit(F f) { f(integration_point, layer, section_point, material); }
};
struct ElementBatch {
    Topology topology;
    Index material;
    std::uint32_t integration_points;
    std::vector<Index> elements;
    // Local-node-major SoA; variable polygon connectivity stays in global CSR.
    AlignedVector<Index> connectivity;
};
template <class T> class ModelBuilder;
template <class T> class MeshDB {
    friend class ModelBuilder<T>;
    std::uint64_t fingerprint_ = 0;
    Field<T, 3> reference_;
    std::vector<Id> node_ids_;
    std::vector<std::uint8_t> dofs_;
    std::unordered_map<Id, Index> node_map_, element_map_;
    std::vector<Element> elements_;
    std::vector<std::size_t> offsets_{0};
    AlignedVector<Index> connectivity_;
    std::vector<Part> parts_;
    MaterialDB<T> materials_;
    std::vector<ElementBatch> batches_;
    std::vector<NodeSet> node_sets_;
    std::vector<ElementSet> element_sets_;
    std::vector<Constraint> constraints_;
    std::vector<Surface> surfaces_;
    bool storage_only_ = false;
    std::uint64_t semantic_contract_ = 0;
    std::map<Index, std::vector<SamplePoint>> sample_points_;
    MeshDB() = default;

  public:
    MeshDB(MeshDB &&) = default;
    MeshDB &operator=(MeshDB &&) = delete;
    MeshDB(const MeshDB &) = delete;
    std::size_t nodes() const noexcept { return node_ids_.size(); }
    std::size_t elements() const noexcept { return elements_.size(); }
    View<const T, 3> reference() const { return reference_.read(); }
    Id node_id(Index i) const { return node_ids_.at(i); }
    std::uint8_t dofs(Index i) const { return dofs_.at(i); }
    Index node_index(Id id) const { return node_map_.at(id); }
    Index element_index(Id id) const { return element_map_.at(id); }
    const Element &element(Index i) const { return elements_.at(i); }
    std::span<const Index> connectivity(Index i) const {
        if (i >= elements())
            throw std::out_of_range("element index");
        return {connectivity_.data() + offsets_[i], offsets_[i + 1] - offsets_[i]};
    }
    const auto &materials() const { return materials_; }
    const auto &batches() const { return batches_; }
    const auto &parts() const { return parts_; }
    const auto &node_sets() const { return node_sets_; }
    const auto &element_sets() const { return element_sets_; }
    const auto &constraints() const { return constraints_; }
    const auto &surfaces() const { return surfaces_; }
    bool requires_storage_only() const noexcept { return storage_only_; }
    bool has_sample_layout() const noexcept { return !sample_points_.empty(); }
    std::size_t sample_point_count(Index e) const {
        const auto &el = element(e);
        auto it = sample_points_.find(e);
        return it == sample_points_.end() ? el.integration_points : it->second.size();
    }
    SamplePoint sample_point(Index e, std::size_t q) const {
        const auto &el = element(e);
        if (q >= sample_point_count(e)) throw std::out_of_range("sample point");
        auto it = sample_points_.find(e);
        return it == sample_points_.end() ? SamplePoint{static_cast<std::uint32_t>(q + 1), 0, 0, el.material}
                                          : it->second[q];
    }
    ExternalLabel external_label(NodeId i) const { return ExternalLabel::checked(node_id(i.value())); }
    NodeId node(ExternalLabel label) const {
        if (!label.valid()) throw std::invalid_argument("invalid typed external label");
        return NodeId(node_index(label.value()));
    }
    const Element &element(ElementId i) const { return element(i.value()); }
    void validate_face(FaceId f) const {
        const auto &e = element(element_index(f.element));
        if (f.local >= face_count(e.topology) || (f.side != 1 && f.side != -1))
            throw std::invalid_argument("invalid face/side");
    }
    // Stable canonical fingerprint: little-endian scalars, independent of padding and hash iteration.
    std::uint64_t fingerprint() const noexcept { return fingerprint_; }

  private:
    std::uint64_t compute_fingerprint() const {
        std::uint64_t h = 14695981039346656037ull;
        auto integer = [&](std::uint64_t v) {
            for (int k = 0; k < 8; ++k) {
                h ^= (v >> (k * 8)) & 255;
                h *= 1099511628211ull;
            }
        };
        auto real = [&](T v) {
            if constexpr (sizeof(T) == 4)
                integer(std::bit_cast<std::uint32_t>(v));
            else
                integer(std::bit_cast<std::uint64_t>(v));
        };
        integer(sizeof(T));
        integer(nodes());
        integer(elements());
        for (Index i = 0; i < nodes(); ++i) {
            integer(node_id(i));
            integer(dofs(i));
            for (int c = 0; c < 3; ++c)
                real(reference()(i, c));
        }
        integer(materials_.size());
        for (Index i = 0; i < materials_.size(); ++i) {
            const auto &m = materials_[i];
            integer(m.id);
            integer(m.model);
            integer(m.history_variables);
            integer(m.parameters.size());
            for (auto v : m.parameters)
                real(v);
        }
        integer(parts_.size());
        for (const auto &p : parts_) {
            integer(p.id);
            integer(p.property);
        }
        for (Index i = 0; i < elements(); ++i) {
            const auto &e = element(i);
            integer(e.id);
            integer(static_cast<unsigned>(e.topology));
            integer(e.material);
            integer(e.part);
            integer(e.integration_points);
            integer(connectivity(i).size());
            for (auto n : connectivity(i))
                integer(n);
        }
        integer(constraints_.size());
        for (const auto &c : constraints_) {
            integer(c.node);
            integer(c.component);
            integer(std::bit_cast<std::uint64_t>(c.value));
        }
        integer(node_sets_.size());
        for (const auto &s : node_sets_) {
            integer(s.id);
            integer(s.nodes.size());
            for (auto n : s.nodes)
                integer(n);
        }
        integer(element_sets_.size());
        for (const auto &s : element_sets_) {
            integer(s.id);
            integer(s.elements.size());
            for (auto n : s.elements)
                integer(n);
        }
        integer(surfaces_.size());
        for (const auto &s : surfaces_) {
            integer(s.id);
            integer(s.faces.size());
            for (auto f : s.faces) {
                integer(f.element);
                integer(f.local);
                integer(static_cast<std::uint8_t>(f.side));
            }
        }
        bool explicit_history = false;
        for (Index i = 0; i < materials_.size(); ++i)
            explicit_history |= !materials_[i].history_schema.empty();
        if (explicit_history) {
            // Legacy hashes remain byte-for-byte identical without explicit schemas.
            integer(0x5344424849535431ull); // SDBHIST1 domain
            auto semantic = [&](auto &&self, const auto &value) -> void {
                using X = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::is_enum_v<X> || std::is_integral_v<X>)
                    integer(static_cast<std::uint64_t>(value));
                else if constexpr (std::is_floating_point_v<X>)
                    integer(std::bit_cast<std::uint64_t>(static_cast<double>(value)));
                else if constexpr (std::is_same_v<X, std::string>) {
                    integer(value.size());
                    for (unsigned char c : value) integer(c);
                } else if constexpr (requires(X &x) { x.visit([](auto &...) {}); }) {
                    // visit is also the canonical native wire traversal; it does not mutate.
                    const_cast<X &>(value).visit([&](const auto &...v) { (self(self, v), ...); });
                } else {
                    integer(value.size());
                    for (const auto &v : value) self(self, v);
                }
            };
            integer(materials_.size());
            for (Index i = 0; i < materials_.size(); ++i)
                semantic(semantic, materials_[i].history_schema);
        }
        if (storage_only_ || !sample_points_.empty()) {
            integer(0x534442434f4e5433ull);
            integer(storage_only_);
            integer(semantic_contract_);
            integer(sample_points_.size());
            for (const auto &[element, points] : sample_points_) {
                integer(element); integer(points.size());
                for (const auto &p : points) {
                    integer(p.integration_point); integer(p.layer); integer(p.section_point); integer(p.material);
                }
            }
        }
        return h;
    }
};
template <class T> class ModelBuilder {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
    struct NodeInput {
        Id id;
        std::array<T, 3> x;
        std::uint8_t dofs;
    };
    struct ElementInput {
        Id id;
        Topology topology;
        std::vector<Id> nodes;
        Id material, part;
        std::uint32_t ips;
    };
    std::vector<NodeInput> nodes_;
    std::vector<ElementInput> elements_;
    MaterialDB<T> materials_;
    std::vector<Part> parts_;
    std::vector<std::pair<Id, std::vector<Id>>> node_sets_, element_sets_;
    std::vector<std::tuple<Id, std::uint8_t, double>> constraints_;
    std::vector<Surface> surfaces_;
    bool storage_only_ = false;
    std::uint64_t semantic_contract_ = 0;
    std::map<Id, std::vector<SamplePoint>> sample_points_;
    bool finalized_ = false;
    void building() const {
        if (finalized_)
            throw std::logic_error("builder finalized");
    }

  public:
    void set_storage_contract(std::uint64_t identity) {
        building(); storage_only_ = true; semantic_contract_ = identity;
    }
    void set_sample_points(Id element, std::vector<SamplePoint> points) {
        building();
        if (points.empty() || !sample_points_.emplace(element, std::move(points)).second)
            throw std::invalid_argument("empty/duplicate sample point layout");
        storage_only_ = true;
    }
    void reserve(std::size_t nodes, std::size_t elements) {
        building();
        index_of(nodes);
        index_of(elements);
        nodes_.reserve(nodes);
        elements_.reserve(elements);
    }
    void add_node(Id id, std::array<T, 3> x, std::uint8_t dofs = 3) {
        building();
        nodes_.push_back({id, x, dofs});
    }
    void add_material(Material<T> m) {
        building();
        materials_.add(std::move(m));
    }
    void add_part(Id id, Id property = 0) {
        building();
        parts_.push_back({id, property});
    }
    void add_element(Id id, Topology t, std::vector<Id> nodes, Id material, Id part, std::uint32_t ips = 1) {
        building();
        elements_.push_back({id, t, std::move(nodes), material, part, ips});
    }
    void add_node_set(Id id, std::vector<Id> nodes) {
        building();
        node_sets_.emplace_back(id, std::move(nodes));
    }
    void add_element_set(Id id, std::vector<Id> elements) {
        building();
        element_sets_.emplace_back(id, std::move(elements));
    }
    void constrain(Id node, std::uint8_t component, double value = 0) {
        building();
        constraints_.emplace_back(node, component, value);
    }
    void add_surface(Id id, std::vector<FaceId> faces) {
        building();
        surfaces_.push_back({id, std::move(faces)});
    }
    MeshDB<T> finalize() {
        building();
        MeshDB<T> m;
        index_of(nodes_.size());
        index_of(elements_.size());
        if (nodes_.empty())
            throw std::invalid_argument("empty model");
        m.reference_.resize(nodes_.size());
        auto x = m.reference_.write();
        for (const auto &n : nodes_) {
            const auto i = index_of(m.node_ids_.size());
            if (!m.node_map_.emplace(n.id, i).second)
                throw std::invalid_argument("duplicate node ID");
            if (n.dofs != 3 && n.dofs != 6)
                throw std::invalid_argument("node needs 3 or 6 DOFs");
            m.node_ids_.push_back(n.id);
            m.dofs_.push_back(n.dofs);
            for (int c = 0; c < 3; ++c) {
                if (!std::isfinite(n.x[c]))
                    throw std::invalid_argument("nonfinite coordinate");
                x(i, c) = n.x[c];
            }
        }
        m.materials_ = materials_;
        m.parts_ = parts_;
        std::unordered_map<Id, Index> parts;
        for (const auto &p : parts_)
            if (!parts.emplace(p.id, index_of(parts.size())).second)
                throw std::invalid_argument("duplicate part ID");
        std::map<std::tuple<Topology, Index, std::uint32_t>, std::vector<Index>> groups;
        for (const auto &e : elements_) {
            const auto i = index_of(m.elements_.size());
            if (!m.element_map_.emplace(e.id, i).second)
                throw std::invalid_argument("duplicate element ID");
            if (!e.ips || (arity(e.topology) ? e.nodes.size() != arity(e.topology) : e.nodes.size() < 3))
                throw std::invalid_argument("invalid topology/connectivity/IP count");
            const auto material = materials_.index(e.material), part = parts.at(e.part);
            std::unordered_set<Id> distinct;
            for (auto n : e.nodes) {
                if (!distinct.insert(n).second)
                    throw std::invalid_argument("repeated element node");
                m.connectivity_.push_back(m.node_index(n));
            }
            m.offsets_.push_back(m.connectivity_.size());
            m.elements_.push_back({e.id, e.topology, material, part, e.ips});
            if (e.topology == Topology::Tet4) {
                auto ns = m.connectivity(i);
                T a[3], b[3], c[3];
                for (int d = 0; d < 3; ++d) {
                    a[d] = x(ns[1], d) - x(ns[0], d);
                    b[d] = x(ns[2], d) - x(ns[0], d);
                    c[d] = x(ns[3], d) - x(ns[0], d);
                }
                T det = a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) +
                        a[2] * (b[0] * c[1] - b[1] * c[0]);
                if (!(det > 0) || !std::isfinite(det))
                    throw std::invalid_argument("nonpositive tetrahedron Jacobian");
            }
            groups[{e.topology, material, e.ips}].push_back(i);
        }
        for (auto &[key, es] : groups) {
            auto [top, mat, ips] = key;
            ElementBatch batch{top, mat, ips, std::move(es), {}};
            const auto n = batch.elements.size();
            batch.connectivity.resize(product(n, arity(top)));
            for (std::size_t k = 0; k < n; ++k)
                for (unsigned c = 0; c < arity(top); ++c)
                    batch.connectivity[c * n + k] = m.connectivity(batch.elements[k])[c];
            m.batches_.push_back(std::move(batch));
        }
        std::unordered_set<Id> ids;
        for (const auto &[id, ns] : node_sets_) {
            if (!ids.insert(id).second)
                throw std::invalid_argument("duplicate node set");
            NodeSet s{id, {}};
            for (auto n : ns)
                s.nodes.push_back(m.node_index(n));
            m.node_sets_.push_back(std::move(s));
        }
        ids.clear();
        for (const auto &[id, es] : element_sets_) {
            if (!ids.insert(id).second)
                throw std::invalid_argument("duplicate element set");
            ElementSet s{id, {}};
            for (auto e : es)
                s.elements.push_back(m.element_index(e));
            m.element_sets_.push_back(std::move(s));
        }
        std::unordered_set<std::uint64_t> constrained;
        for (auto [id, c, v] : constraints_) {
            auto n = m.node_index(id);
            if (c >= m.dofs(n) || !std::isfinite(v) || !constrained.insert(std::uint64_t(n) * 6 + c).second)
                throw std::invalid_argument("invalid/duplicate constraint");
            m.constraints_.push_back({n, c, v});
        }
        ids.clear();
        for (const auto &s : surfaces_) {
            if (!ids.insert(s.id).second)
                throw std::invalid_argument("duplicate surface");
            for (auto f : s.faces)
                m.validate_face(f);
            m.surfaces_.push_back(s);
        }
        m.storage_only_ = storage_only_;
        m.semantic_contract_ = semantic_contract_;
        for (const auto &[id, points] : sample_points_) {
            auto e = m.element_index(id);
            std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> seen;
            for (const auto &p : points)
                if (!p.integration_point || p.integration_point > m.element(e).integration_points ||
                    p.material >= m.materials_.size() || bool(p.layer) != bool(p.section_point) ||
                    !seen.emplace(p.integration_point, p.layer, p.section_point).second)
                    throw std::invalid_argument("invalid/duplicate state sample point");
            m.sample_points_.emplace(e, points);
        }
        m.fingerprint_ = m.compute_fingerprint();
        finalized_ = true;
        return m;
    }
};
} // namespace sdb

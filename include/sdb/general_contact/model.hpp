#pragma once
#include "io.hpp"

namespace sdb::general_contact {
struct Region {
    Id id = 0;
    std::vector<Feature> faces;
    template <class F> void visit(F f) { f(id, faces); }
};
struct Rule {
    Id id = 0, region_a = 0, region_b = 0, property = 0; // region 0 = all exterior; property 0 = exclusion
    std::int32_t priority = 0;
    template <class F> void visit(F f) { f(id, region_a, region_b, property, priority); }
};
template <class T> struct EdgeRadius {
    Feature feature;
    T radius = 0;
    bool operator==(const EdgeRadius &) const = default;
    template <class F> void visit(F f) { f(feature, radius); }
};
template <class T> struct SearchControls {
    T band = T(.01), minimum_opposition = 0, edge_area = 1;
    std::uint64_t candidate_capacity = 1000000;
    bool node_face = true;
    std::vector<EdgeRadius<T>> rounded_edges;
    bool operator==(const SearchControls &) const = default;
    template <class F> void visit(F f) {
        f(band, minimum_opposition, edge_area, candidate_capacity, node_face, rounded_edges);
    }
};
template <class T> struct Definition {
    std::uint32_t schema = 2;
    std::uint64_t mesh = 0;
    std::vector<Property<T>> properties;
    std::vector<Region> regions;
    std::vector<Rule> rules;
    bool exclude_shared_nodes = true;
    SearchControls<T> controls;
    template <class F> void visit(F f) {
        f(schema, mesh, properties, regions, rules, exclude_shared_nodes);
        if (schema >= 2)
            f(controls);
    }
};
struct Edge {
    std::array<Index, 2> nodes{};
    std::vector<Index> incident_faces;
};
struct Assignment {
    Id rule = 0, property = 0;
};
// Immutable cold model compiled once; no property strings or legacy DB access in kernels.
template <class T> class Model {
    Definition<T> definition_;
    std::vector<Facet> facets_;
    std::vector<Edge> edges_;
    std::vector<std::vector<Index>> node_faces_;
    std::map<FaceId, Index> face_index_;
    std::map<Id, std::set<Index>> regions_;
    std::uint64_t fingerprint_ = 0;

  public:
    Model(const MeshDB<T> &mesh, Definition<T> definition)
        : definition_(std::move(definition)), facets_(exterior(mesh)) {
        if ((definition_.schema != 1 && definition_.schema != 2) || definition_.mesh != mesh.fingerprint())
            throw std::invalid_argument("contact definition schema/mesh mismatch");
        auto &p = definition_.properties;
        std::sort(p.begin(), p.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
        std::set<Id> property_ids;
        for (const auto &law : p) {
            law.validate();
            if (!property_ids.insert(law.id).second)
                throw std::invalid_argument("duplicate contact property");
        }
        std::map<std::array<Index, 2>, std::vector<Index>> edges;
        for (std::size_t i = 0; i < facets_.size(); ++i) {
            const auto &f = facets_[i];
            face_index_.emplace(f.identity, index_of(i));
            for (unsigned j = 0; j < f.count; ++j) {
                std::array<Index, 2> edge{f.nodes[j], f.nodes[(j + 1) % f.count]};
                std::sort(edge.begin(), edge.end());
                edges[edge].push_back(index_of(i));
            }
        }
        for (auto &[nodes, faces] : edges)
            edges_.push_back({nodes, std::move(faces)});
        node_faces_.resize(mesh.nodes());
        for (Index f = 0; f < facets_.size(); ++f)
            for (unsigned j = 0; j < facets_[f].count; ++j)
                node_faces_[facets_[f].nodes[j]].push_back(f);
        auto &regions = definition_.regions;
        std::sort(regions.begin(), regions.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
        for (auto &region : regions) {
            if (!region.id || regions_.contains(region.id))
                throw std::invalid_argument("invalid contact region identity");
            std::set<Index> members;
            std::sort(region.faces.begin(), region.faces.end());
            for (const auto &f : region.faces) {
                if (f.kind != FeatureKind::Face || f.side != 1 ||
                    f.local > std::numeric_limits<std::uint16_t>::max())
                    throw std::invalid_argument("unsupported contact region feature");
                auto it = face_index_.find({f.entity, static_cast<std::uint16_t>(f.local), 1});
                if (it == face_index_.end() || !members.insert(it->second).second)
                    throw std::invalid_argument("non-exterior or duplicate contact region face");
            }
            regions_.emplace(region.id, std::move(members));
        }
        auto &rules = definition_.rules;
        std::sort(rules.begin(), rules.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
        std::set<Id> rule_ids;
        for (auto &r : rules) {
            if (!r.id || !rule_ids.insert(r.id).second || (r.region_a && !regions_.contains(r.region_a)) ||
                (r.region_b && !regions_.contains(r.region_b)) ||
                (r.property && !property_ids.contains(r.property)))
                throw std::invalid_argument("unresolved contact rule");
            if (r.region_b < r.region_a)
                std::swap(r.region_a, r.region_b);
        }
        auto &controls = definition_.controls;
        finite(controls.band);
        finite(controls.minimum_opposition);
        finite(controls.edge_area);
        if (controls.band <= 0 || controls.minimum_opposition < 0 || controls.minimum_opposition >= 1 ||
            controls.edge_area <= 0 || !controls.candidate_capacity)
            throw std::invalid_argument("invalid persisted contact search controls");
        if (definition_.schema == 1 && controls != SearchControls<T>{})
            throw std::invalid_argument(
                "schema 1 cannot persist search controls; upgrade definition to schema 2");
        std::set<Feature> edge_features;
        for (Index i = 0; i < edges_.size(); ++i)
            edge_features.insert(edge_identity(i));
        std::sort(controls.rounded_edges.begin(), controls.rounded_edges.end(),
                  [](const auto &a, const auto &b) { return a.feature < b.feature; });
        for (std::size_t i = 0; i < controls.rounded_edges.size(); ++i) {
            const auto &e = controls.rounded_edges[i];
            finite(e.radius);
            if (e.radius <= 0 || !edge_features.contains(e.feature) ||
                (i && controls.rounded_edges[i - 1].feature == e.feature))
                throw std::invalid_argument("invalid rounded edge assignment");
        }
        fingerprint_ = lifecycle::native::hash(lifecycle::native::pack(definition_));
    }
    const Definition<T> &definition() const { return definition_; }
    const std::vector<Facet> &facets() const { return facets_; }
    const std::vector<Edge> &edges() const { return edges_; }
    const std::vector<std::vector<Index>> &node_faces() const { return node_faces_; }
    std::uint64_t fingerprint() const { return fingerprint_; }
    Feature edge_identity(Index index) const {
        const auto &e = edges_.at(index);
        const auto &f = facets_.at(e.incident_faces.at(0));
        for (unsigned j = 0; j < f.count; ++j) {
            std::array<Index, 2> nodes{f.nodes[j], f.nodes[(j + 1) % f.count]};
            std::sort(nodes.begin(), nodes.end());
            if (nodes == e.nodes)
                return {FeatureKind::Edge, f.identity.element,
                        static_cast<std::uint32_t>(f.identity.local) * 4 + j, 1};
        }
        throw std::logic_error("invalid contact edge incidence");
    }
    Index face_index(FaceId id) const { return face_index_.at(id); }
    // Call during serial candidate policy resolution, before any worker mutation.
    std::optional<Assignment> resolve(Index a, Index b) const {
        if (a >= facets_.size() || b >= facets_.size())
            throw std::out_of_range("contact policy facet");
        if (a == b)
            return std::nullopt;
        if (definition_.exclude_shared_nodes)
            for (unsigned i = 0; i < facets_[a].count; ++i)
                for (unsigned j = 0; j < facets_[b].count; ++j)
                    if (facets_[a].nodes[i] == facets_[b].nodes[j])
                        return std::nullopt;
        auto member = [&](Id region, Index face) { return !region || regions_.at(region).contains(face); };
        std::optional<Assignment> result;
        auto priority = std::numeric_limits<std::int32_t>::lowest();
        bool excluded = false, conflict = false;
        for (const auto &r : definition_.rules) {
            if (!((member(r.region_a, a) && member(r.region_b, b)) ||
                  (member(r.region_a, b) && member(r.region_b, a))))
                continue;
            if (!r.property) {
                excluded = true;
                continue;
            }
            if (!result || r.priority > priority) {
                result = Assignment{r.id, r.property};
                priority = r.priority;
                conflict = false;
            } else if (r.priority == priority && r.property != result->property)
                conflict = true;
        }
        if (excluded)
            return std::nullopt;
        if (conflict)
            throw std::invalid_argument("ambiguous equal-priority contact property assignment");
        return result;
    }
};
inline constexpr std::uint64_t definition_magic = 0x314d434342445355ull;
template <class T>
void save_model(const std::filesystem::path &path, const Model<T> &model,
                std::uint64_t budget = lifecycle::native::default_limit) {
    auto bytes = lifecycle::native::pack(model.definition(), budget);
    lifecycle::native::atomic_stream(path, [&](std::ostream &out) {
        lifecycle::native::prefix(out, definition_magic);
        lifecycle::native::record(out, sizeof(T), bytes);
    });
}
template <class T>
Model<T> load_model(const std::filesystem::path &path, const MeshDB<T> &mesh,
                    std::uint64_t budget = lifecycle::native::default_limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("contact model open failed");
    lifecycle::native::prefix(in, definition_magic);
    auto bytes = lifecycle::native::record(in, sizeof(T), budget);
    lifecycle::native::end(in);
    return Model<T>(mesh, lifecycle::native::unpack<Definition<T>>(bytes, budget));
}
} // namespace sdb::general_contact

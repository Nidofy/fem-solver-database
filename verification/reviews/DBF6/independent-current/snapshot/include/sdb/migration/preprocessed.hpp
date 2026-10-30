#pragma once
#include "../state.hpp"
#include "../consumer_views.hpp"
#include <algorithm>
#include <set>
#include <string>

// SDK-independent transfer seam. A production-side translator owns capture and
// family qualification; this header never reads CAC files or calls old kernels.
namespace sdb::migration {
struct FnlSnapshot {
    std::vector<Id> ndlb; // NDLB_PTR_ID, in sequential-node order
    // Sparse extraction of SEQND_PTR_ID: (original label, ONE-based sequence).
    std::vector<std::pair<Id, std::int64_t>> seqnd;
    std::vector<std::array<double, 3>> reference; // Coord_{x,y,z}ptr, sequence order
    std::vector<std::array<std::int32_t, 3>> idarray; // idComponent[0..2], sequence order
    std::uint32_t equations = 0; // zero inactive; positive equation IDs are ONE-based
};
struct FegElement {
    Id egroup_elab = 0, egroup_mset = 0, egroup_gset = 0;
    std::int32_t egroup_mCsys = 0;
    bool egroup_lcns_exists = false, hybrid_or_internal_nodes = false;
    // Explicit qualification by translator; never guess a topology from arity.
    Topology topology = Topology::Tet4;
    std::uint32_t integration_points = 1;
    std::vector<Id> egroup_elndlb;
    std::vector<std::int64_t> egroup_elseqnd;
    std::vector<float> egroup_xyz; // one element's x[n], y[n], z[n]
    std::vector<std::int32_t> egroup_eldof; // node-major XYZ, copied from FEG
};
struct PropertyBinding {
    Id legacy_gset = 0, part = 0, property = 0;
};
struct PreprocessedSnapshot {
    FnlSnapshot fnl;
    std::vector<FegElement> elements;
    std::vector<Material<double>> materials; // explicitly translated, never inferred from mset
    std::vector<PropertyBinding> properties;
    // Capture must inventory additional populated fields. Unsupported fields are
    // rejected, not silently discarded. This is not a raw/native FEG ABI.
    std::vector<std::string> unsupported_fields;
};
inline void require(bool condition, const char *message) {
    if (!condition) throw std::invalid_argument(message);
}
inline bool legacy_label(Id value) {
    return value > 0 && value <= static_cast<Id>(std::numeric_limits<std::int32_t>::max());
}
inline void validate_fnl(const FnlSnapshot &s) {
    const auto n = s.ndlb.size();
    require(n != 0 && n < invalid_index, "FNL empty/oversized NDLB");
    require(s.reference.size() == n && s.idarray.size() == n && s.seqnd.size() == n,
            "FNL NDLB/SEQND/coordinate/IDARRAY shape mismatch");
    require(s.equations <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
            "FNL equation count exceeds signed legacy ID range");
    std::unordered_map<Id, std::size_t> labels;
    for (std::size_t i = 0; i < n; ++i) {
        require(legacy_label(s.ndlb[i]) && labels.emplace(s.ndlb[i], i).second,
                "FNL zero/duplicate node label or out-of-range node label");
        for (unsigned c = 0; c < 3; ++c) {
            require(std::isfinite(s.reference[i][c]), "FNL nonfinite coordinate");
            const auto eq = s.idarray[i][c];
            require(eq >= 0 && static_cast<std::uint32_t>(eq) <= s.equations,
                    "FNL unsupported negative/out-of-range equation ID");
        }
    }
    std::set<Id> reverse;
    for (const auto &[label, sequence] : s.seqnd) {
        require(reverse.insert(label).second && labels.contains(label), "FNL duplicate/unknown SEQND label");
        require(sequence > 0 && static_cast<std::uint64_t>(sequence) <= n &&
                    labels.at(label) == static_cast<std::size_t>(sequence - 1),
                "FNL SEQND must be one-based inverse of NDLB");
    }
}
struct LegacyElementBinding {
    Id label = 0;
    std::vector<std::int32_t> one_based_equations;
    std::vector<std::array<float, 3>> feg_reference;
};
// Additional equation identity belongs to the adapter, not to MeshDB's node DOFs.
// No SPC, MPC, material, orientation or load semantics are inferred from it.
struct MigratedModel {
    MeshDB<double> mesh;
    FnlSnapshot fnl;
    std::vector<LegacyElementBinding> elements; // same order as frozen mesh
};
inline MigratedModel import_preprocessed(const PreprocessedSnapshot &s) {
    require(s.unsupported_fields.empty(), "unsupported populated preprocessed fields");
    validate_fnl(s.fnl);
    require(!s.elements.empty(), "FEG empty element snapshot");
    ModelBuilder<double> builder;
    builder.reserve(s.fnl.ndlb.size(), s.elements.size());
    // A captured equation map is not an executable constraint definition.
    // Explicit storage-only prevents accidental analysis on an incomplete model.
    builder.set_storage_contract(0x44424635464e4c31ull); // DBF5FNL1
    for (std::size_t i = 0; i < s.fnl.ndlb.size(); ++i)
        builder.add_node(s.fnl.ndlb[i], s.fnl.reference[i]);
    std::set<Id> material_ids;
    for (const auto &m : s.materials) {
        require(legacy_label(m.id), "zero/out-of-range translated material label");
        material_ids.insert(m.id);
        builder.add_material(m);
    }
    std::map<Id, PropertyBinding> property;
    for (const auto &p : s.properties) {
        require(legacy_label(p.legacy_gset) && p.part && property.emplace(p.legacy_gset, p).second,
                "zero/duplicate property binding");
        builder.add_part(p.part, p.property);
    }
    std::vector<LegacyElementBinding> bindings;
    std::set<Id> labels;
    for (const auto &e : s.elements) {
        require(legacy_label(e.egroup_elab) && labels.insert(e.egroup_elab).second,
                "FEG zero/duplicate element label or out-of-range element label");
        require(e.topology == Topology::Tet4 || e.topology == Topology::Hex8,
                "unsupported FEG family; qualify Tet4/Hex8 explicitly");
        require(!e.egroup_lcns_exists && e.egroup_mCsys == 0 && !e.hybrid_or_internal_nodes,
                "unsupported local coordinate/orientation/hybrid fields");
        const auto n = static_cast<std::size_t>(arity(e.topology));
        require(e.egroup_elndlb.size() == n && e.egroup_elseqnd.size() == n &&
                    e.egroup_xyz.size() == n * 3 && e.egroup_eldof.size() == n * 3,
                "FEG topology/node/coordinate/LM shape mismatch");
        require(material_ids.contains(e.egroup_mset), "FEG missing translated material");
        require(property.contains(e.egroup_gset), "FEG missing explicit property binding");
        for (std::size_t j = 0; j < n; ++j) {
            const auto seq = e.egroup_elseqnd[j];
            require(seq > 0 && static_cast<std::uint64_t>(seq) <= s.fnl.ndlb.size(),
                    "FEG sequence must be one-based and in range");
            const auto node = static_cast<std::size_t>(seq - 1);
            require(e.egroup_elndlb[j] == s.fnl.ndlb[node], "FEG node label/sequence mismatch");
            for (unsigned c = 0; c < 3; ++c) {
                const auto packed = e.egroup_xyz[c * n + j];
                require(std::isfinite(packed) && packed == static_cast<float>(s.fnl.reference[node][c]),
                        "FEG packed coordinate differs from float reference");
                require(e.egroup_eldof[j * 3 + c] == s.fnl.idarray[node][c], "FEG LM/IDARRAY mismatch");
            }
        }
        builder.add_element(e.egroup_elab, e.topology, e.egroup_elndlb, e.egroup_mset,
                            property.at(e.egroup_gset).part, e.integration_points);
        LegacyElementBinding binding{e.egroup_elab, e.egroup_eldof, {}};
        for (std::size_t j = 0; j < n; ++j)
            binding.feg_reference.push_back({e.egroup_xyz[j], e.egroup_xyz[n + j], e.egroup_xyz[2 * n + j]});
        bindings.push_back(std::move(binding));
    }
    return {builder.finalize(), s.fnl, std::move(bindings)};
}
// Equation-ordered old vectors need an explicit value for inactive equations.
// Zero ID is NOT sufficient evidence that displacement must be zero (SPCD).
inline std::vector<std::array<double, 3>> gather_equations(
    const FnlSnapshot &s, std::span<const double> equations,
    std::span<const std::array<double, 3>> inactive_values) {
    validate_fnl(s);
    require(equations.size() == s.equations && inactive_values.size() == s.ndlb.size(),
            "equation gather shape mismatch");
    for (auto value : equations) require(std::isfinite(value), "nonfinite equation value");
    std::vector<std::array<double, 3>> out(s.ndlb.size());
    for (std::size_t n = 0; n < out.size(); ++n)
        for (unsigned c = 0; c < 3; ++c) {
            const auto eq = s.idarray[n][c];
            out[n][c] = eq ? equations[static_cast<std::size_t>(eq - 1)] : inactive_values[n][c];
            require(std::isfinite(out[n][c]), "nonfinite inactive DOF value");
        }
    return out;
}
struct NodalKinematics {
    std::vector<std::array<double, 3>> displacement, velocity, acceleration;
};
inline void initialize_kinematics(const MigratedModel &m, StateDB<double> &state, const NodalKinematics &k) {
    require(state.model_id() == m.mesh.fingerprint(), "kinematics state/model mismatch");
    require(!state.in_trial(), "cannot initialize kinematics during trial");
    const auto n = m.mesh.nodes();
    for (const auto *values : {&k.displacement, &k.velocity, &k.acceleration}) {
        require(values->size() == n, "kinematics shape mismatch");
        for (const auto &row : *values)
            for (double v : row) require(std::isfinite(v), "nonfinite kinematics");
    }
    state.validate_shape();
    auto &data = state.data();
    auto u = data.displacement.write(), v = data.velocity.write(), a = data.acceleration.write();
    for (std::size_t i = 0; i < n; ++i)
        for (unsigned c = 0; c < 3; ++c) {
            u(i, c) = k.displacement[i][c]; v(i, c) = k.velocity[i][c]; a(i, c) = k.acceleration[i][c];
        }
}
struct PrivateElementScratch {
    Id label = 0;
    std::vector<Id> node_labels;
    std::vector<Index> zero_based_nodes;
    std::vector<std::int32_t> el_dof; // one-based equations, 0 inactive; privately mutable
    std::vector<std::array<double, 3>> reference, displacement;
    // Preserve FEG float precision separately from canonical reference. Filling
    // native Felem::coords/new_coords requires an explicit family policy.
    std::vector<std::array<float, 3>> legacy_reference;
};
// Checked const views are supplied by the consumer contract.
// A real View->Felem translator copies these buffers into native private storage.
inline PrivateElementScratch make_private_scratch(const MigratedModel &m, ElementId element,
                                                 const consumer::NodeStateView<double> &node_state) {
    node_state.validate();
    require(node_state.model_id == m.mesh.fingerprint(), "scratch state/model mismatch");
    const auto displacement = node_state.displacement.resolve();
    const auto i = element.value();
    require(element.valid() && i < m.mesh.elements(), "scratch invalid element");
    require(displacement.count == m.mesh.nodes(), "scratch displacement shape mismatch");
    const auto &e = m.mesh.element(i);
    const auto &binding = m.elements.at(i);
    require(binding.label == e.id, "scratch legacy element identity mismatch");
    require(binding.one_based_equations.size() == m.mesh.connectivity(i).size() * 3 &&
                binding.feg_reference.size() == m.mesh.connectivity(i).size(),
            "scratch legacy binding shape mismatch");
    PrivateElementScratch out;
    out.label = e.id; out.el_dof = binding.one_based_equations;
    out.legacy_reference = binding.feg_reference;
    auto x = m.mesh.reference();
    for (Index node : m.mesh.connectivity(i)) {
        out.zero_based_nodes.push_back(node); out.node_labels.push_back(m.mesh.node_id(node));
        out.reference.push_back({x(node, 0), x(node, 1), x(node, 2)});
        out.displacement.push_back({displacement(node, 0), displacement(node, 1), displacement(node, 2)});
    }
    return out;
}
struct NodalContribution {
    ElementId element;
    std::uint32_t local_node = 0;
    std::uint8_t component = 0;
    double value = 0;
};
// Reduction is a standalone adapter template, not a database assembly algorithm.
// Fully validate/sum into owned storage before backwrite. Stable element-label
// order is independent of worker completion order. Repeated keys are rejected;
// callers need separate passes for distinct force categories.
inline std::vector<std::array<double, 3>> reduce_nodal(const MeshDB<double> &mesh,
                                                     std::vector<NodalContribution> values) {
    for (const auto &v : values) {
        require(v.element.valid() && v.element.value() < mesh.elements(), "reducer invalid element");
        require(v.local_node < mesh.connectivity(v.element).size() && v.component < 3,
                "reducer invalid local node/component");
        require(std::isfinite(v.value), "reducer nonfinite value");
    }
    auto key = [&](const NodalContribution &v) {
        return std::tuple{mesh.element(v.element).id, v.local_node, v.component};
    };
    std::sort(values.begin(), values.end(), [&](const auto &a, const auto &b) { return key(a) < key(b); });
    std::vector<std::array<double, 3>> out(mesh.nodes());
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto &v = values[i];
        require(i == 0 || key(values[i - 1]) != key(v), "reducer duplicate contribution key");
        auto &sum = out[mesh.connectivity(v.element)[v.local_node]][v.component];
        sum += v.value;
        require(std::isfinite(sum), "reducer nonfinite sum");
    }
    return out;
}
template <class Sink>
void backwrite_nodal(const MeshDB<double> &mesh, std::span<const std::array<double, 3>> values, Sink &&sink) {
    require(values.size() == mesh.nodes(), "backwrite shape mismatch");
    for (const auto &v : values)
        for (double x : v) require(std::isfinite(x), "backwrite nonfinite value");
    // Callback receives stable original node labels. Publication/transaction is
    // the sink owner's responsibility; a throwing external sink may be partial.
    for (Index i = 0; i < mesh.nodes(); ++i) sink(mesh.node_id(i), values[i]);
}
} // namespace sdb::migration

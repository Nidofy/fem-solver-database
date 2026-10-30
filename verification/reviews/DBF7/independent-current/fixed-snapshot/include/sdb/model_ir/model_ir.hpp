#pragma once
#include "foundation.hpp"
#include <filesystem>
#include <sstream>
#include <set>
#include <functional>

namespace sdb::lifecycle {
struct Diagnostic {
    Source source;
    std::string message;
    bool warning = false;
};
enum class ImportMode : std::uint8_t { Solve, MeshOnly };
struct ImportReport {
    bool parsed = false, semantically_mapped = false, database_stored = false, executable_cpu = false,
         executable_cuda = false;
    std::vector<Diagnostic> diagnostics;
};
class ImportError : public std::runtime_error {
  public:
    ImportReport report;
    ImportError(const Source &source, const std::string &message)
        : std::runtime_error(source.str() + ": " + message) {
        report.diagnostics.push_back({source, message, false});
    }
};
[[noreturn]] inline void fail(const Source &s, const std::string &m) {
    throw ImportError(s, m);
}
enum class Formulation : std::uint8_t { Solid, Shell, Beam };
enum class Integration : std::uint8_t { Full, Reduced };
struct ElementDescriptor {
    Topology shape = Topology::Tet4;
    std::uint8_t order = 1;
    Formulation formulation = Formulation::Solid;
    Integration integration = Integration::Full;
    std::uint32_t points = 1;
    std::uint32_t section_points = 1;
    std::string token = "C3D4", section;
    template <class F> void visit(F f) {
        f(shape, order, formulation, integration, points, section_points, token, section);
    }
};
inline ElementDescriptor descriptor(const std::string &type, const Source &s) {
    ElementDescriptor d;
    d.token = type;
    if (type == "C3D4")
        return d;
    if (type == "C3D8" || type == "C3D8R") {
        d.shape = Topology::Hex8;
        d.points = type == "C3D8" ? 8 : 1;
    } else if (type == "S4R") {
        d.shape = Topology::Quad4;
        d.formulation = Formulation::Shell;
        d.points = 1;
        d.section_points = 5;
    } else if (type == "B31") {
        d.shape = Topology::Beam2;
        d.formulation = Formulation::Beam;
        d.section_points = 0; // Section integration is not derived for the unsupported beam kernel.
    } else
        fail(s, "unsupported element type: " + type);
    if (type == "C3D8R" || type == "S4R")
        d.integration = Integration::Reduced;
    return d;
}
struct IRNode {
    Id label = 0;
    std::array<double, 3> x{};
    Source source;
    template <class F> void visit(F f) { f(label, x, source); }
};
struct IRElement {
    Id label = 0;
    ElementDescriptor descriptor;
    std::vector<Id> nodes;
    Source source;
    template <class F> void visit(F f) { f(label, descriptor, nodes, source); }
};
struct IRPart {
    std::string name;
    std::vector<IRNode> nodes;
    std::vector<IRElement> elements;
    Source source;
    template <class F> void visit(F f) { f(name, nodes, elements, source); }
};
struct IRInstance {
    std::string name, part;
    std::array<double, 3> translation{};
    std::array<double, 7> rotation{0, 0, 0, 0, 0, 1, 0};
    Source source;
    template <class F> void visit(F f) { f(name, part, translation, rotation, source); }
};
struct IRSet {
    std::string scope, name, instance;
    bool nodes = true;
    std::vector<std::string> members;
    Source source;
    template <class F> void visit(F f) { f(scope, name, instance, nodes, members, source); }
};
struct IRMaterial {
    std::string name;
    double density = 0, young = 0, poisson = 0;
    Source source;
    template <class F> void visit(F f) { f(name, density, young, poisson, source); }
};
struct IRSection {
    std::string scope, set, material, kind = "SOLID SECTION";
    std::vector<double> properties;
    Source source;
    template <class F> void visit(F f) { f(scope, set, material, kind, properties, source); }
};
struct IRSurface {
    std::string scope, name;
    std::vector<std::pair<std::string, std::string>> faces;
    Source source;
    template <class F> void visit(F f) { f(scope, name, faces, source); }
};
struct IRAmplitude {
    std::string name;
    bool total_time = false;
    std::vector<std::pair<double, double>> points;
    Source source;
    template <class F> void visit(F f) { f(name, total_time, points, source); }
};
struct IRAction {
    std::string target, amplitude;
    std::uint8_t first = 0, last = 0;
    double value = 0;
    Source source;
    template <class F> void visit(F f) { f(target, amplitude, first, last, value, source); }
};
struct IRActionBlock {
    bool reset = false;
    std::vector<IRAction> actions;
    Source source;
    template <class F> void visit(F f) { f(reset, actions, source); }
};
struct IROutput {
    bool history = false;
    std::uint64_t frequency = 1;
    std::uint64_t intervals = 0;
    std::string region;
    std::vector<std::string> variables;
    Source source;
    template <class F> void visit(F f) { f(history, frequency, intervals, region, variables, source); }
};
struct IRStep {
    std::string name;
    double increment = 0, period = 0;
    bool direct = false;
    std::vector<IRActionBlock> boundaries, loads;
    std::vector<IROutput> outputs;
    Source source;
    template <class F> void visit(F f) {
        f(name, increment, period, direct, boundaries, loads, outputs, source);
    }
};
struct OpaqueBlock {
    std::string keyword;
    std::vector<std::string> data;
    Source source;
    template <class F> void visit(F f) { f(keyword, data, source); }
};
struct IRHistoryBinding {
    std::string material;
    std::uint32_t model = 1;
    HistorySchema history_schema;
    template <class F> void visit(F f) { f(material, model, history_schema); }
};
struct ModelIR {
    std::uint32_t schema = 1;
    std::string units = "unspecified", assembly;
    ImportMode mode = ImportMode::Solve;
    std::vector<IRPart> parts;
    std::vector<IRInstance> instances;
    std::vector<IRSet> sets;
    std::vector<IRMaterial> materials;
    std::vector<IRSection> sections;
    std::vector<IRSurface> surfaces;
    std::vector<IRAmplitude> amplitudes;
    std::vector<IRActionBlock> initial_boundaries;
    std::vector<IRStep> steps;
    std::vector<OpaqueBlock> opaque;
    std::vector<IRHistoryBinding> history_bindings;
    ModelFoundation foundation;
    ModelExtensions extensions;
    template <class F> void visit(F f) {
        f(schema);
        if (schema != 1 && schema != 2 && schema != 3 && schema != 4)
            throw std::runtime_error("unsupported ModelIR schema");
        f(units, assembly, mode, parts, instances, sets, materials, sections, surfaces, amplitudes,
          initial_boundaries, steps, opaque);
        if (schema >= 2) f(history_bindings);
        else if (!history_bindings.empty())
            throw std::runtime_error("history bindings require ModelIR schema 2");
        if (schema >= 3) f(foundation);
        else if (!foundation.empty()) throw std::runtime_error("foundation requires ModelIR schema 3");
        if(schema>=4) f(extensions);
        else if(!extensions.empty()) throw std::runtime_error("model extensions require ModelIR schema 4");
    }
};
struct SourceEntry {
    SourceKey key;
    Id id = 0;
    Index index = 0;
    Source source;
    template <class F> void visit(F f) { f(key, id, index, source); }
};
struct SourceMap {
    std::vector<SourceEntry> nodes, elements;
    std::map<SourceKey, Index> node_lookup, element_lookup;
    const SourceEntry &lookup(bool node, const SourceKey &key) const {
        return node ? nodes.at(node_lookup.at(key)) : elements.at(element_lookup.at(key));
    }
    const SourceEntry &reverse(bool node, Index i) const { return node ? nodes.at(i) : elements.at(i); }
    NodeId node_id(const SourceKey &key) const { return NodeId(lookup(true,key).index); }
    ElementId element_id(const SourceKey &key) const { return ElementId(lookup(false,key).index); }
    const SourceEntry &reverse(NodeId id) const { return nodes.at(id.value()); }
    const SourceEntry &reverse(ElementId id) const { return elements.at(id.value()); }
    void add(bool node, SourceEntry e) {
        auto &v = node ? nodes : elements;
        auto &m = node ? node_lookup : element_lookup;
        if (e.index != v.size()) fail(e.source, "SourceMap index must append densely");
        if (v.size() >= invalid_index || e.id != Id(e.index) + 1 || !e.key.label)
            fail(e.source, "SourceMap ID/label invalid");
        if (!m.emplace(e.key, e.index).second)
            fail(e.source, "duplicate scoped label");
        try { v.push_back(e); }
        catch (...) { m.erase(e.key); throw; }
    }
};
struct SectionRecord {
    Index element = 0, material = 0;
    std::string kind;
    std::vector<double> properties;
    Source source;
};
using SectionDB = std::vector<SectionRecord>;
struct Action {
    Index node = 0;
    std::uint8_t component = 0;
    double value = 0;
    std::string amplitude;
    // Runtime provenance only; ModelIR/wire identity remains unchanged. Unspecified
    // preserves local-time evaluation for manually constructed AnalysisDB actions.
    std::uint64_t origin_step = UINT64_MAX;
};
struct AnalysisStep {
    std::string name;
    double period = 0, increment = 0;
    bool direct = false;
    std::vector<Action> boundaries, loads;
    Source source;
};
struct AnalysisDB {
    std::vector<AnalysisStep> steps;
    std::map<std::string, IRAmplitude> amplitudes;
};
using OutputRequestDB = std::vector<std::vector<IROutput>>;
} // namespace sdb::lifecycle

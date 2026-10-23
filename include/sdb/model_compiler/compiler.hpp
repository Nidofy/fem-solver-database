#pragma once
#include "../input/abaqus.hpp"
#include "../state.hpp"
#include <numbers>

namespace sdb::lifecycle {
// Abaqus face label membership/order, distinct from outward-oriented kernel face order.
inline std::vector<unsigned> abaqus_face(const ElementDescriptor &d, const std::string &name,
                                         const Source &at = {}) {
    static const unsigned tet[4][3] = {{0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {2, 3, 0}};
    static const unsigned hex[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
                                       {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
    if (d.formulation == Formulation::Shell) {
        if (name == "SPOS")
            return {0, 1, 2, 3};
        if (name == "SNEG")
            return {3, 2, 1, 0};
    }
    if (name.size() == 2 && name[0] == 'S' && name[1] >= '1') {
        unsigned n = static_cast<unsigned>(name[1] - '1');
        if (d.shape == Topology::Tet4 && n < 4)
            return {tet[n], tet[n] + 3};
        if (d.shape == Topology::Hex8 && n < 6)
            return {hex[n], hex[n] + 4};
    }
    fail(at, "invalid surface face " + name + " for " + d.token);
}
template <class T> struct CompiledModel {
    ModelIR definition;
    MeshDB<T> mesh;
    SourceMap sources;
    SectionDB sections;
    AnalysisDB analysis;
    OutputRequestDB outputs;
    std::vector<ElementDescriptor> descriptors;
    std::map<std::string, std::vector<Index>> regions;
    ImportReport report;
    CompiledModel(ModelIR ir, MeshDB<T> &&m) : definition(std::move(ir)), mesh(std::move(m)) {}
    const std::vector<Index> &region(bool node, const std::string &name) const {
        auto it = regions.find((node ? "N:" : "E:") + upper(name));
        if (it == regions.end())
            throw std::invalid_argument("unknown region: " + name);
        return it->second;
    }
    void require_cpu() const {
        if (!report.executable_cpu) {
            std::string text = "model has no executable CPU capability";
            for (auto &d : report.diagnostics)
                text += "\n" + d.source.str() + ": " + d.message;
            throw std::runtime_error(text);
        }
    }
};
class ModelCompiler {
  public:
    template <class T> CompiledModel<T> compile(ModelIR ir) const {
        if (ir.schema != 1)
            throw std::runtime_error("unsupported ModelIR schema");
        if (ir.parts.empty() || (ir.mode != ImportMode::Solve && ir.mode != ImportMode::MeshOnly))
            throw std::runtime_error("invalid ModelIR root/mode");
        ModelBuilder<T> b;
        SourceMap source;
        SectionDB sections;
        AnalysisDB analysis;
        OutputRequestDB outputs;
        std::vector<ElementDescriptor> descriptions;
        ImportReport report;
        report.parsed = true;
        bool executable = ir.mode == ImportMode::Solve && ir.opaque.empty();
        auto incapable = [&](const Source &s, const std::string &why) {
            executable = false;
            report.diagnostics.push_back({s, why, false});
        };
        for (auto &o : ir.opaque)
            incapable(o.source, "opaque unsupported semantics: " + o.keyword);
        std::map<std::string, const IRPart *> parts;
        for (auto &p : ir.parts)
            if (!parts.emplace(p.name, &p).second)
                fail(p.source, "duplicate part: " + p.name);
        struct Context {
            const IRPart *part;
            const IRInstance *instance;
            Id id;
        };
        std::map<std::string, Context> contexts;
        if (parts.contains("") && !parts.at("")->nodes.empty())
            contexts.emplace("", Context{parts.at(""), nullptr, 1});
        for (auto &i : ir.instances) {
            if (!parts.contains(i.part))
                fail(i.source, "unknown instance part: " + i.part);
            if (!contexts.emplace(i.name, Context{parts.at(i.part), &i, contexts.size() + 1}).second)
                fail(i.source, "duplicate instance: " + i.name);
        }
        if (contexts.empty())
            throw std::runtime_error("no root mesh or instantiated part");
        std::map<std::pair<bool, std::string>, std::map<Id, Index>> lookup;
        for (auto &[name, c] : contexts) {
            b.add_part(c.id);
            std::uint8_t dofs = 3;
            for (auto &e : c.part->elements)
                if (e.descriptor.formulation != Formulation::Solid) {
                    dofs = 6;
                    break;
                }
            for (auto &n : c.part->nodes) {
                auto x = n.x;
                if (c.instance) {
                    auto &i = *c.instance;
                    for (int k = 0; k < 3; ++k)
                        x[k] += i.translation[k];
                    if (i.rotation[6] != 0) {
                        std::array<double, 3> axis{}, v{}, cross{};
                        double norm = 0, dot = 0;
                        for (int k = 0; k < 3; ++k) {
                            axis[k] = i.rotation[k + 3] - i.rotation[k];
                            norm += axis[k] * axis[k];
                            v[k] = x[k] - i.rotation[k];
                        }
                        if (!(norm > 0))
                            fail(i.source, "zero rotation axis");
                        for (auto &a : axis)
                            a /= std::sqrt(norm);
                        for (int k = 0; k < 3; ++k) {
                            dot += axis[k] * v[k];
                            cross[k] =
                                axis[(k + 1) % 3] * v[(k + 2) % 3] - axis[(k + 2) % 3] * v[(k + 1) % 3];
                        }
                        double angle = i.rotation[6] * std::numbers::pi / 180;
                        for (int k = 0; k < 3; ++k)
                            x[k] = i.rotation[k] + std::cos(angle) * v[k] + std::sin(angle) * cross[k] +
                                   (1 - std::cos(angle)) * dot * axis[k];
                    }
                }
                Index index = index_of(source.nodes.size());
                if (!lookup[{true, name}].emplace(n.label, index).second)
                    fail(n.source, "duplicate node label");
                b.add_node(Id(index) + 1, {static_cast<T>(x[0]), static_cast<T>(x[1]), static_cast<T>(x[2])},
                           dofs);
                source.add(true, {{c.part->name, name, n.label}, Id(index) + 1, index, n.source});
            }
            for (auto &e : c.part->elements) {
                auto expected = descriptor(e.descriptor.token, e.source);
                if (e.descriptor.shape != expected.shape || e.descriptor.order != expected.order ||
                    e.descriptor.formulation != expected.formulation ||
                    e.descriptor.integration != expected.integration ||
                    e.descriptor.points != expected.points)
                    fail(e.source, "element descriptor disagrees with original type token");
                Index index = index_of(source.elements.size());
                if (!lookup[{false, name}].emplace(e.label, index).second)
                    fail(e.source, "duplicate element label");
                source.add(false, {{c.part->name, name, e.label}, Id(index) + 1, index, e.source});
            }
        }
        using Key = std::tuple<bool, std::string, std::string>;
        std::map<Key, std::vector<Index>> cache;
        std::set<Key> resolving;
        std::function<std::vector<Index>(bool, std::string, std::string, Source)> resolve;
        resolve = [&](bool node, std::string target, std::string ctx, Source at) -> std::vector<Index> {
            auto dot = target.find('.');
            if (dot != target.npos) {
                ctx = target.substr(0, dot);
                target = target.substr(dot + 1);
                if (!contexts.contains(ctx))
                    fail(at, "unknown instance: " + ctx);
            }
            if (is_label(target)) {
                auto &map = lookup[{node, ctx}];
                auto it = map.find(label(target, at));
                if (it == map.end())
                    fail(at, "unresolved scoped label: " + ctx + "." + target);
                return {it->second};
            }
            Key key{node, ctx, target};
            if (cache.contains(key))
                return cache.at(key);
            if (!resolving.insert(key).second)
                fail(at, "cyclic set reference: " + target);
            std::vector<Index> ids;
            bool found = false;
            std::string part_name = contexts.contains(ctx) ? contexts.at(ctx).part->name : "";
            for (auto &s : ir.sets)
                if (s.nodes == node && s.name == target && s.scope == (ctx.empty() ? "" : part_name)) {
                    found = true;
                    std::string memberctx = s.instance.empty() ? ctx : s.instance;
                    if (!memberctx.empty() && !contexts.contains(memberctx))
                        fail(s.source, "unknown set INSTANCE: " + memberctx);
                    for (auto &member : s.members) {
                        auto more = resolve(node, member, memberctx, s.source);
                        ids.insert(ids.end(), more.begin(), more.end());
                    }
                }
            if (!found)
                fail(at, "unresolved " + std::string(node ? "NSET " : "ELSET ") + ctx + "." + target);
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            resolving.erase(key);
            cache.emplace(key, ids);
            return ids;
        };
        std::map<std::string, std::vector<Index>> regions;
        for (auto &s : ir.sets) {
            std::vector<std::string> cs;
            if (s.scope.empty())
                cs.push_back("");
            else
                for (auto &[name, c] : contexts)
                    if (c.part->name == s.scope)
                        cs.push_back(name);
            for (auto &ctx : cs)
                regions[(s.nodes ? "N:" : "E:") + (ctx.empty() ? "" : ctx + ".") + s.name] =
                    resolve(s.nodes, s.name, ctx, s.source);
        }
        Id setid = 1;
        for (auto &[key, ids] : regions) {
            std::vector<Id> labels;
            for (auto i : ids)
                labels.push_back(Id(i) + 1);
            if (key.starts_with("N:"))
                b.add_node_set(setid++, std::move(labels));
            else
                b.add_element_set(setid++, std::move(labels));
        }
        std::map<std::string, Index> materials;
        for (auto &m : ir.materials) {
            Index idx = index_of(materials.size());
            if (!materials.emplace(m.name, idx).second)
                fail(m.source, "duplicate material name");
            if (!(m.density > 0 && m.young > 0 && m.poisson > -1 && m.poisson < .5))
                fail(m.source, "material requires positive density/E and -1 < nu < 0.5");
            b.add_material({Id(idx) + 1,
                            1,
                            {static_cast<T>(m.density), static_cast<T>(m.young), static_cast<T>(m.poisson)},
                            0});
        }
        std::vector<const IRSection *> assignment(source.elements.size(), nullptr);
        for (auto &s : ir.sections) {
            if (!materials.contains(s.material))
                fail(s.source, "unknown section material: " + s.material);
            std::vector<std::string> cs;
            if (s.scope.empty())
                cs.push_back("");
            else
                for (auto &[name, c] : contexts)
                    if (c.part->name == s.scope)
                        cs.push_back(name);
            for (auto &ctx : cs)
                for (auto e : resolve(false, s.set, ctx, s.source)) {
                    if (assignment[e])
                        fail(s.source, "overlapping section assignments");
                    assignment[e] = &s;
                }
        }
        bool unassigned_material = false;
        for (auto &[name, c] : contexts)
            for (auto &e : c.part->elements) {
                auto ei = lookup.at({false, name}).at(e.label);
                auto *s = assignment[ei];
                auto d = e.descriptor;
                Id mat = 0;
                if (!s) {
                    if (ir.mode == ImportMode::Solve)
                        fail(e.source, "element has no section");
                    incapable(e.source, "unassigned section/material");
                    if (!unassigned_material) {
                        b.add_material({0, 0, {}, 0});
                        unassigned_material = true;
                    }
                } else {
                    mat = Id(materials.at(s->material)) + 1;
                    d.section = s->scope + "/" + s->set;
                    sections.push_back({ei, materials.at(s->material), s->kind, s->properties, s->source});
                    bool match =
                        d.formulation == Formulation::Solid
                            ? s->kind == "SOLID SECTION"
                            : (d.formulation == Formulation::Shell ? s->kind == "SHELL SECTION"
                                                                   : s->kind.starts_with("BEAM SECTION"));
                    if (!match)
                        fail(s->source, "section incompatible with element formulation");
                    if (d.formulation == Formulation::Shell) {
                        if (s->properties.empty() || s->properties.size() > 2 || s->properties[0] <= 0)
                            fail(s->source, "shell section requires positive constant thickness and optional "
                                            "Simpson point count");
                        if (s->properties.size() == 2) {
                            auto count = s->properties[1];
                            if (count < 3 || count > 99 || std::floor(count) != count ||
                                static_cast<int>(count) % 2 == 0)
                                fail(s->source, "shell Simpson section point count must be odd, 3..99");
                            d.section_points = static_cast<std::uint32_t>(count);
                        }
                    }
                    if (d.token == "C3D4" && !s->properties.empty() &&
                        (s->properties.size() != 1 || s->properties[0] != 1))
                        fail(s->source, "3D solid section supports blank/unit data only");
                }
                if (d.token != "C3D4")
                    incapable(e.source,
                              d.token + " stored with formulation/integration; CPU kernel unavailable");
                std::vector<Id> nodes;
                for (auto n : e.nodes) {
                    auto it = lookup.at({true, name}).find(n);
                    if (it == lookup.at({true, name}).end())
                        fail(e.source, "element references unknown node");
                    nodes.push_back(Id(it->second) + 1);
                }
                b.add_element(Id(ei) + 1, d.shape, std::move(nodes), mat, c.id, d.points);
                descriptions.push_back(d);
            }
        for (auto &s : ir.surfaces) {
            std::vector<std::string> cs;
            if (s.scope.empty())
                cs.push_back("");
            else
                for (auto &[name, c] : contexts)
                    if (c.part->name == s.scope)
                        cs.push_back(name);
            for (auto &ctx : cs) {
                std::vector<FaceId> faces;
                for (auto &[target, face] : s.faces)
                    for (auto e : resolve(false, target, ctx, s.source)) {
                        auto &d = descriptions.at(e);
                        (void)abaqus_face(d, face, s.source);
                        faces.push_back({Id(e) + 1,
                                         static_cast<std::uint16_t>(
                                             d.formulation == Formulation::Shell ? 0 : face[1] - '1'),
                                         static_cast<std::int8_t>(face == "SNEG" ? -1 : 1)});
                    }
                b.add_surface(setid++, std::move(faces));
            }
        }
        for (auto &a : ir.amplitudes) {
            if (a.points.empty())
                fail(a.source, "empty amplitude");
            for (std::size_t i = 0; i < a.points.size(); ++i)
                if (!std::isfinite(a.points[i].first) || !std::isfinite(a.points[i].second) ||
                    (i && a.points[i].first <= a.points[i - 1].first))
                    fail(a.source, "invalid amplitude table");
            if (!analysis.amplitudes.emplace(a.name, a).second)
                fail(a.source, "duplicate amplitude");
        }
        using ActionKey = std::pair<Index, std::uint8_t>;
        std::map<ActionKey, Action> boundaries, loads;
        auto actions = [&](const std::vector<IRActionBlock> &blocks, std::map<ActionKey, Action> &current) {
            bool reset = false;
            for (auto &block : blocks) {
                if (block.reset != blocks.front().reset)
                    fail(block.source, "mixed OP NEW/MOD blocks in one step unsupported");
                reset |= block.reset;
            }
            if (reset)
                current.clear();
            std::set<ActionKey> updated;
            for (auto &block : blocks)
                for (auto &a : block.actions) {
                    if (a.first > a.last || a.last >= 3 || !std::isfinite(a.value))
                        fail(a.source, "invalid action DOF/value");
                    if (!a.amplitude.empty() && !analysis.amplitudes.contains(a.amplitude))
                        fail(a.source, "unknown amplitude: " + a.amplitude);
                    for (auto n : resolve(true, a.target, "", a.source))
                        for (auto c = a.first; c <= a.last; ++c) {
                            ActionKey key{n, c};
                            if (!updated.insert(key).second)
                                fail(a.source, "overlapping action targets in same step; combine explicitly");
                            current[key] = {n, c, a.value, a.amplitude};
                        }
                }
        };
        actions(ir.initial_boundaries, boundaries);
        // Mesh holds only initial constraints. Runtime step masks come from AnalysisDB.
        for (auto &[key, a] : boundaries) {
            if (a.value != 0 || !a.amplitude.empty())
                throw std::runtime_error("only homogeneous constant initial boundary supported");
            b.constrain(Id(a.node) + 1, a.component, a.value);
        }
        std::set<std::string> step_names;
        for (auto &step : ir.steps) {
            if (!step_names.insert(step.name).second || !std::isfinite(step.period) || step.period <= 0 ||
                !std::isfinite(step.increment) || step.increment < 0)
                fail(step.source, "invalid/duplicate step");
            actions(step.boundaries, boundaries);
            actions(step.loads, loads);
            AnalysisStep compiled{step.name, step.period, step.increment, step.direct, {}, {}, step.source};
            for (auto &[k, a] : boundaries) {
                if (a.value != 0 || !a.amplitude.empty())
                    fail(step.source, "only homogeneous constant boundary supported");
                compiled.boundaries.push_back(a);
            }
            for (auto &[k, a] : loads)
                compiled.loads.push_back(a);
            analysis.steps.push_back(std::move(compiled));
            outputs.push_back(step.outputs.empty() && !outputs.empty() ? outputs.back() : step.outputs);
            for (auto &o : step.outputs) {
                if (o.variables.empty() || o.intervals > 100000000)
                    fail(o.source, "invalid output selection/schedule");
                for (auto &v : o.variables)
                    if (v != "U" && v != "V" && v != "A" && v != "RF" && v != "S" && v != "E" &&
                        v != "ALLKE" && v != "ALLSE")
                        fail(o.source, "unsupported output variable: " + v);
                if (!o.region.empty()) {
                    bool node = o.variables.front() == "U" || o.variables.front() == "V" ||
                                o.variables.front() == "A" || o.variables.front() == "RF";
                    auto key = (node ? "N:" : "E:") + o.region;
                    if (!regions.contains(key))
                        fail(o.source, "unknown output region: " + o.region);
                }
            }
        }
        if (ir.steps.empty())
            incapable(ir.parts.front().source, "no executable analysis step");
        auto mesh = b.finalize();
        CompiledModel<T> result(std::move(ir), std::move(mesh));
        result.sources = std::move(source);
        result.sections = std::move(sections);
        result.descriptors = std::move(descriptions);
        result.regions = std::move(regions);
        result.analysis = std::move(analysis);
        result.outputs = std::move(outputs);
        report.semantically_mapped = result.definition.opaque.empty();
        report.database_stored = true;
        report.executable_cpu = executable;
        report.executable_cuda = false;
        result.report = std::move(report);
        return result;
    }
};
class LoadEvaluator {
  public:
    static double amplitude(const IRAmplitude &a, double local, double total) {
        auto t = a.total_time ? total : local;
        if (t <= a.points.front().first)
            return a.points.front().second;
        if (t >= a.points.back().first)
            return a.points.back().second;
        auto hi = std::upper_bound(a.points.begin(), a.points.end(), t,
                                   [](double v, const auto &p) { return v < p.first; });
        auto lo = hi - 1;
        return lo->second + (hi->second - lo->second) * (t - lo->first) / (hi->first - lo->first);
    }
    template <class T>
    static void apply(const AnalysisDB &db, std::size_t step, double local, double total, StateDB<T> &state) {
        state.data().external_force.fill(0);
        auto f = state.data().external_force.write();
        for (auto &a : db.steps.at(step).loads) {
            double scale = a.amplitude.empty() ? 1 : amplitude(db.amplitudes.at(a.amplitude), local, total);
            f(a.node, a.component) += static_cast<T>(scale * a.value);
        }
    }
};
} // namespace sdb::lifecycle

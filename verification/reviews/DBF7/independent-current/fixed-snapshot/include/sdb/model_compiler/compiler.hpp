#pragma once
#include "../input/abaqus.hpp"
#include "../state.hpp"
#include "../model_ir/foundation_validate.hpp"
#include <numbers>

namespace sdb::lifecycle {
// Abaqus face label membership/order, distinct from outward-oriented kernel face order.
inline std::vector<unsigned> abaqus_face(const ElementDescriptor &d, const std::string &name,
                                         const Source &at = {}) {
    try {
        if(d.formulation==Formulation::Shell && (name=="SPOS" || name=="SNEG"))
            return topology_face_nodes(d.shape,0,FaceOrdering::Abaqus,name=="SNEG"?-1:1);
        if(name.size()==2 && name[0]=='S' && name[1]>='1' && (d.shape==Topology::Tet4 || d.shape==Topology::Hex8))
            return topology_face_nodes(d.shape,static_cast<unsigned>(name[1]-'1'),FaceOrdering::Abaqus);
    } catch(const std::exception &) {}

    fail(at, "invalid surface face " + name + " for " + d.token);
}
// Semantic identity ignores source text location and ordering of identity-keyed
// definitions. Connectivity, layer/axis/component/history/step order stays meaningful.
inline ModelIR canonical_definition(ModelIR ir) {
    auto strip=[](auto &&self,auto &x)->void {
        using X=std::remove_cvref_t<decltype(x)>;
        if constexpr(std::is_same_v<X,Source>) x={};
        else if constexpr(requires { x.first; x.second; }) { self(self,x.first); self(self,x.second); }
        else if constexpr(requires { x.visit([](auto &...){}); }) x.visit([&](auto &...v){(self(self,v),...);});
        else if constexpr(!std::is_same_v<X,std::string> && requires { x.begin(); x.end(); }) for(auto &v:x) self(self,v);
    };
    strip(strip,ir);
    auto by_name=[](auto &v) { std::sort(v.begin(),v.end(),[](auto &a,auto &b){return a.name<b.name;}); };
    by_name(ir.parts); by_name(ir.instances); by_name(ir.materials); by_name(ir.amplitudes);
    for(auto &p:ir.parts) {
        std::sort(p.nodes.begin(),p.nodes.end(),[](auto &a,auto &b){return a.label<b.label;});
        std::sort(p.elements.begin(),p.elements.end(),[](auto &a,auto &b){return a.label<b.label;});
    }
    for(auto &s:ir.sets) { std::sort(s.members.begin(),s.members.end()); s.members.erase(std::unique(s.members.begin(),s.members.end()),s.members.end()); }
    std::sort(ir.sets.begin(),ir.sets.end(),[](auto &a,auto &b){return std::tie(a.scope,a.name,a.nodes,a.instance,a.members)<std::tie(b.scope,b.name,b.nodes,b.instance,b.members);});
    std::sort(ir.sections.begin(),ir.sections.end(),[](auto &a,auto &b){return std::tie(a.scope,a.set)<std::tie(b.scope,b.set);});
    std::sort(ir.surfaces.begin(),ir.surfaces.end(),[](auto &a,auto &b){return std::tie(a.scope,a.name)<std::tie(b.scope,b.name);});
    for(auto &s:ir.surfaces) std::sort(s.faces.begin(),s.faces.end());
    std::sort(ir.history_bindings.begin(),ir.history_bindings.end(),[](auto &a,auto &b){return a.material<b.material;});
    auto &f=ir.foundation;
    auto by_id=[](auto &v) { std::sort(v.begin(),v.end(),[](auto &a,auto &b){return a.id<b.id;}); };
    by_id(f.coordinates); by_id(f.orientations); by_id(f.equations); by_id(f.ties); by_id(f.rigids); by_id(f.initial_conditions); by_id(f.activations);
    auto by_assignment=[](auto &v) { std::sort(v.begin(),v.end(),[](auto &a,auto &b){return a.assignment.id<b.assignment.id;}); };
    by_assignment(f.solid_sections); by_assignment(f.shell_sections); by_assignment(f.beam_sections);
    auto by_event=[](auto &v) { std::sort(v.begin(),v.end(),[](auto &a,auto &b){return std::tie(a.definition.id,a.definition.step)<std::tie(b.definition.id,b.definition.step);}); };
    by_event(f.nodal_loads); by_event(f.face_loads); by_event(f.body_loads);
    std::sort(f.material_definitions.begin(),f.material_definitions.end(),[](auto &a,auto &b){return a.material<b.material;});
    for(auto &m:f.material_definitions) { by_name(m.parameters); by_name(m.tables); }
    auto &x=ir.extensions; by_id(x.couplings); by_id(x.property_distributions);
    std::sort(x.amplitude_policies.begin(),x.amplitude_policies.end(),[](auto &a,auto &b){return a.amplitude<b.amplitude;});
    std::sort(x.initial_events.begin(),x.initial_events.end(),[](auto &a,auto &b){return std::tie(a.step,a.condition.id)<std::tie(b.step,b.condition.id);});
    std::sort(x.activation_policies.begin(),x.activation_policies.end(),[](auto &a,auto &b){return a.activation<b.activation;});
    for(auto &c:x.couplings) std::sort(c.weights.begin(),c.weights.end(),[](auto &a,auto &b){return a.node<b.node;});
    return ir;
}
inline std::uint64_t canonical_semantic_hash(const ModelIR &ir) { return semantic_hash(canonical_definition(ir)); }
struct AnalysisIdentity {
    std::vector<IRAmplitude> amplitudes;
    std::vector<IRActionBlock> initial_boundaries;
    std::vector<IRStep> steps;
    std::vector<IRNodalLoad> nodal_loads;
    std::vector<IRFaceLoad> face_loads;
    std::vector<IRBodyLoad> body_loads;
    std::vector<IRInitialCondition> initial_conditions;
    std::vector<IRActivation> activations;
    template<class F> void visit(F f) { f(amplitudes,initial_boundaries,steps,nodal_loads,face_loads,body_loads,initial_conditions,activations); }
};
template <class T> struct CompiledModel {
    ModelIR definition;
    MeshDB<T> mesh;
    SourceMap sources;
    CompiledFoundation foundation;
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
    std::uint64_t layout_identity() const { return mesh.fingerprint(); }
    std::uint64_t semantic_identity() const { return canonical_semantic_hash(definition); }
    std::uint64_t analysis_identity() const {
        auto ir=canonical_definition(definition);
        AnalysisIdentity base{ir.amplitudes,ir.initial_boundaries,ir.steps,ir.foundation.nodal_loads,ir.foundation.face_loads,ir.foundation.body_loads,ir.foundation.initial_conditions,ir.foundation.activations};
        if(ir.schema<4) return semantic_hash(base);
        return semantic_hash(std::array<std::uint64_t,5>{semantic_hash(base),semantic_hash(ir.extensions.amplitude_policies),semantic_hash(ir.extensions.initial_events),semantic_hash(ir.extensions.activation_policies),semantic_hash(ir.extensions.sample_axis)});
    }
    const IRInstance &instance(InstanceId id) const {
        if(!id.valid()) throw std::out_of_range("invalid instance ID");
        std::vector<const IRInstance*> instances; for(const auto &i:definition.instances) instances.push_back(&i);
        std::sort(instances.begin(),instances.end(),[](auto a,auto b){return a->name<b->name;});
        return *instances.at(id.value());
    }
    InstanceId instance_id(const std::string &name) const {
        std::vector<std::string> names; for(const auto &i:definition.instances) names.push_back(i.name); std::sort(names.begin(),names.end());
        auto it=std::lower_bound(names.begin(),names.end(),name); if(it==names.end() || *it!=name) throw std::out_of_range("unknown instance name"); return InstanceId(index_of(static_cast<std::size_t>(it-names.begin())));
    }
    const IRStep &step(StepId id) const { foundation.require_step(id); return definition.steps.at(id.value()); }
    StepId step_id(const std::string &name) const {
        for(std::size_t i=0;i<definition.steps.size();++i) if(definition.steps[i].name==name) return StepId(index_of(i));
        throw std::out_of_range("unknown step name");
    }
    MaterialId material_id(const std::string &name) const { return foundation.material_ids.at(name); }
    SurfaceId surface_id(const std::string &name) const { return foundation.surface_ids.at(name); }
    double amplitude_value(const std::string &name,double local,double total) const {
        const auto &a=analysis.amplitudes.at(name); const auto *p=foundation.amplitude_policy(name);
        if(!std::isfinite(local) || !std::isfinite(total)) throw std::invalid_argument("nonfinite amplitude query time");
        const double t=a.total_time?total:local;
        const auto interpolation=p?p->interpolation:InterpolationPolicy::Linear;
        const auto extrapolation=p?p->extrapolation:ExtrapolationPolicy::Clamp;
        const auto &points=a.points;
        if(t<points.front().first || t>points.back().first) {
            if(extrapolation==ExtrapolationPolicy::Reject) throw std::out_of_range("amplitude query outside table");
            if(extrapolation==ExtrapolationPolicy::Clamp) return t<points.front().first?points.front().second:points.back().second;
        }
        if(t==points.back().first) return points.back().second;
        auto hi=std::upper_bound(points.begin(),points.end(),t,[](double time,const auto &v){return time<v.first;});
        if(hi==points.begin()) ++hi;
        if(hi==points.end()) --hi;
        auto lo=hi-1;
        if(interpolation==InterpolationPolicy::Constant) return lo->second;
        double ratio=(t-lo->first)/(hi->first-lo->first);
        double result=std::lerp(lo->second,hi->second,ratio);
        if(!std::isfinite(result)) throw std::overflow_error("amplitude interpolation/extrapolation overflow");
        return result;
    }
    double load_amplitude(const std::string &name,StepId introduced,StepId current,double local,double total) const {
        foundation.require_step(introduced); foundation.require_step(current);
        if(introduced>current) throw std::invalid_argument("load introduced after query step");
        if(name.empty()) return 1;
        // A propagated step-time amplitude holds its originating step's final value.
        return amplitude_value(name,introduced==current?local:definition.steps[introduced.value()].period,total);
    }
    std::vector<LoadContribution> effective_load_values(StepId current,double local,double total) const {
        std::vector<LoadContribution> values;
        for(const auto &load:foundation.effective_loads(current)) for(auto introduced:load.contributions) {
            auto contribution=foundation.load_contribution(load.id,introduced);
            auto factor=load_amplitude(contribution.definition.amplitude,introduced,current,local,total);
            contribution.scalar*=factor; foundation_real<T>(contribution.scalar,contribution.definition.source);
            for(auto &v:contribution.vector) {v*=factor;foundation_real<T>(v,contribution.definition.source);}
            // Coordinate/follower metadata remains explicit; this cold query applies
            // amplitudes only and never invents a deformed geometry transformation.
            values.push_back(std::move(contribution));
        }
        return values;
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
        if (ir.schema != 1 && ir.schema != 2 && ir.schema != 3 && ir.schema != 4)
            throw std::runtime_error("unsupported ModelIR schema");
        if (ir.schema == 1 && !ir.history_bindings.empty())
            throw std::runtime_error("history bindings require ModelIR schema 2");
        if (ir.parts.empty() || (ir.mode != ImportMode::Solve && ir.mode != ImportMode::MeshOnly))
            throw std::runtime_error("invalid ModelIR root/mode");
        if (ir.schema < 3 && !ir.foundation.empty())
            throw std::runtime_error("foundation requires ModelIR schema 3");
        if(ir.schema<4 && !ir.extensions.empty()) throw std::runtime_error("model extensions require ModelIR schema 4");
        auto foundation = validate_foundation<T>(ir.foundation);
        ModelBuilder<T> b;
        std::vector<std::uint8_t> node_dofs;
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
        if (!ir.foundation.empty()) {
            b.set_storage_contract(semantic_hash(ir.foundation));
            incapable({}, "schema-3 foundation semantics are storage-only; CPU kernel unavailable");
        }
        if(ir.schema>=4) {
            b.set_storage_contract(semantic_hash(std::array<std::uint64_t,2>{semantic_hash(ir.foundation),semantic_hash(ir.extensions)}));
            incapable({},"schema-4 model extensions are storage-only; CPU kernel unavailable");
        }
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
        std::vector<const IRElement*> coupling_elements;
        for (auto &[name, c] : contexts) {
            b.add_part(c.id);
            std::uint8_t dofs = 3;
            for (auto &e : c.part->elements)
                if (e.descriptor.formulation != Formulation::Solid) {
                    dofs = 6;
                    break;
                }
            std::set<Id> rotation_nodes;
            if (ir.schema >= 3)
                for (auto &e : c.part->elements)
                    if (e.descriptor.formulation != Formulation::Solid)
                        rotation_nodes.insert(e.nodes.begin(), e.nodes.end());
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
                           ir.schema >= 3 ? (rotation_nodes.contains(n.label) ? 6 : 3) : dofs);
                node_dofs.push_back(ir.schema >= 3 ? (rotation_nodes.contains(n.label) ? 6 : 3) : dofs);
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
                if(!ir.extensions.couplings.empty()) coupling_elements.push_back(&e);
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
            if(s.name.empty() || (!s.scope.empty() && !parts.contains(s.scope))) fail(s.source,"invalid set name/scope");
            if(!s.instance.empty() && !contexts.contains(s.instance)) fail(s.source,"unknown set INSTANCE");
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
        std::map<std::string, const IRHistoryBinding *> history_bindings;
        for (const auto &binding : ir.history_bindings) {
            if (binding.material.empty() || !history_bindings.emplace(binding.material, &binding).second)
                throw std::invalid_argument("empty/duplicate material history binding");
            binding.history_schema.template validate<T>();
        }
        std::map<std::string, const MaterialDefinition*> typed_materials;
        for (auto &m : ir.foundation.material_definitions) {
            if(std::none_of(ir.materials.begin(),ir.materials.end(),[&](const auto &anchor){return anchor.name==m.material;}))
                fail(m.source,"typed material missing material anchor");
            typed_materials.emplace(m.material,&m);
        }
        for (auto &m : ir.materials) {
            Index idx = index_of(materials.size());
            if (!materials.emplace(m.name, idx).second)
                fail(m.source, "duplicate material name");
            for(double x:{m.density,m.young,m.poisson}) foundation_real<T>(x,m.source);
            Material<T> material;
            material.id = Id(idx) + 1;
            if (typed_materials.contains(m.name)) {
                // Model 0 is a cold, declarative material. The actual algorithm and typed
                // parameters remain in foundation; never fabricate an elastic surrogate.
                material.model = 0;
                incapable(m.source, "typed material schema is storage-only");
            } else {
                if (!(m.density > 0 && m.young > 0 && m.poisson > -1 && m.poisson < .5))
                    fail(m.source, "material requires positive density/E and -1 < nu < 0.5");
                material.model = 1;
                material.parameters = {static_cast<T>(m.density), static_cast<T>(m.young), static_cast<T>(m.poisson)};
            }
            foundation.material_ids.emplace(m.name, MaterialId(idx));
            if (auto it = history_bindings.find(m.name); it != history_bindings.end()) {
                if (typed_materials.contains(m.name) && typed_materials.at(m.name)->algorithm != it->second->model)
                    fail(m.source,"typed material/history algorithm identity mismatch");
                if (!typed_materials.contains(m.name)) material.model = it->second->model;
                material.history_schema = it->second->history_schema;
                material.history_variables = material.history_schema.template validate<T>();
                incapable(m.source, "explicit material history is storage-only; CPU kernel unavailable");
            }
            b.add_material(std::move(material));
        }
        for (const auto &[name, definition] : typed_materials)
            if (!materials.contains(name)) fail(definition->source,"typed material missing material anchor");
        for (const auto &[name, binding] : history_bindings)
            if (!materials.contains(name))
                throw std::invalid_argument("unknown material history binding: " + name);
        // Normalize typed sections only in a local compilation view. The original IR
        // remains canonical and round-trippable, including layers and orientation IDs.
        std::vector<IRSection> all_sections = ir.sections;
        const auto legacy_sections = all_sections.size();
        std::vector<const SectionAssignment*> typed_assignments;
        std::vector<const IRShellSection*> typed_shells;
        std::set<SectionId> section_ids;
        auto add_section = [&](const SectionAssignment &a, std::string kind, std::vector<double> properties, const IRShellSection *shell = nullptr) {
            if (!a.id.valid() || a.set.empty() || !section_ids.insert(a.id).second)
                fail(a.source,"invalid/duplicate typed section ID");
            if (a.orientation.valid()) (void)foundation.orientation(a.orientation);
            all_sections.push_back({a.scope,a.set,a.material,std::move(kind),std::move(properties),a.source});
            typed_assignments.push_back(&a); typed_shells.push_back(shell);
        };
        for (auto &s : ir.foundation.solid_sections) add_section(s.assignment,"SOLID SECTION",{});
        for (auto &s : ir.foundation.shell_sections) {
            foundation_real<T>(s.thickness,s.assignment.source); foundation_real<T>(s.offset,s.assignment.source);
            if (!(s.thickness>0) || std::abs(s.offset)>.5 || !s.section_points || s.section_points>99)
                fail(s.assignment.source,"invalid shell thickness/offset/section points");
            double sum=0;
            for (auto &l:s.layers) {
                foundation_real<T>(l.thickness,s.assignment.source); foundation_real<T>(l.angle_degrees,s.assignment.source);
                if (!(l.thickness>0) || !l.section_points || l.section_points>99 || !materials.contains(l.material))
                    fail(s.assignment.source,"invalid shell layer thickness/material/points");
                if(l.orientation.valid()) (void)foundation.orientation(l.orientation);
                sum+=l.thickness;
            }
            if (!s.layers.empty() && (!std::isfinite(sum) || std::abs(sum-s.thickness)>1e-10*std::max(sum,s.thickness)))
                fail(s.assignment.source,"shell layer thickness sum differs from section thickness");
            add_section(s.assignment,"SHELL SECTION",{s.thickness},&s);
        }
        for (auto &s : ir.foundation.beam_sections) {
            for(double x:{s.area,s.i11,s.i22,s.i12,s.torsion}) foundation_real<T>(x,s.assignment.source);
            if (!(s.area>0 && s.i11>0 && s.i22>0 && s.torsion>0) ||
                std::abs(s.i12)/std::sqrt(s.i11)>=std::sqrt(s.i22) || !s.assignment.orientation.valid())
                fail(s.assignment.source,"invalid beam area/inertia/torsion/orientation");
            add_section(s.assignment,"BEAM SECTION",{s.area,s.i11,s.i22,s.i12,s.torsion});
        }
        std::vector<const IRSection *> assignment(source.elements.size(), nullptr);
        std::vector<const IRShellSection*> shell_assignment(source.elements.size(),nullptr);
        for (std::size_t si=0;si<all_sections.size();++si) {
            auto &s=all_sections[si];
            if (!materials.contains(s.material))
                fail(s.source, "unknown section material: " + s.material);
            std::vector<std::string> cs;
            if (s.scope.empty())
                cs.push_back("");
            else
                for (auto &[name, c] : contexts)
                    if (c.part->name == s.scope)
                        cs.push_back(name);
            if (cs.empty()) fail(s.source,"unknown section scope");
            ResolvedSection resolved;
            if(si>=legacy_sections) {
                resolved.id=typed_assignments[si-legacy_sections]->id;
                resolved.material=MaterialId(materials.at(s.material));
            }
            for (auto &ctx : cs)
                for (auto e : resolve(false, s.set, ctx, s.source)) {
                    if (assignment[e])
                        fail(s.source, "overlapping section assignments");
                    assignment[e] = &s;
                    if(si>=legacy_sections) {
                        shell_assignment[e]=typed_shells[si-legacy_sections];
                        resolved.elements.push_back(ElementId(e));
                    }
                }
            if(si>=legacy_sections) {
                if(resolved.elements.empty()) fail(s.source,"empty typed section assignment");
                foundation.sections.push_back(std::move(resolved));
            }
        }
        std::uint64_t sample_layout_count=0;
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
                    if ((d.token == "C3D4" || d.token == "C3D8") && !s->properties.empty() &&
                        (s->properties.size() != 1 || s->properties[0] != 1))
                        fail(s->source, "3D solid section supports blank/unit data only");
                }
                if (d.token != "C3D4" && d.token != "C3D8")
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
                if (auto *shell=shell_assignment[ei]) {
                    std::vector<SamplePoint> points;
                    std::uint64_t per_ip=shell->layers.empty()?shell->section_points:0;
                    for(auto &l:shell->layers) per_ip+=l.section_points;
                    auto point_count=foundation_product(per_ip,d.points,shell->assignment.source);
                    constexpr std::uint64_t sample_budget=(256ull*1024*1024)/sizeof(SamplePoint);
                    if(point_count>sample_budget-sample_layout_count) fail(shell->assignment.source,"sample layout metadata budget");
                    sample_layout_count+=point_count;
                    points.reserve(static_cast<std::size_t>(point_count));
                    for(std::uint32_t ip=1;ip<=d.points;++ip) {
                        if(shell->layers.empty()) {
                            for(std::uint32_t sp=1;sp<=shell->section_points;++sp)
                                points.push_back({ip,1,sp,materials.at(shell->assignment.material)});
                        } else for(std::size_t layer=0;layer<shell->layers.size();++layer) {
                            auto &l=shell->layers[layer];
                            for(std::uint32_t sp=1;sp<=l.section_points;++sp)
                                points.push_back({ip,static_cast<std::uint32_t>(layer+1),sp,materials.at(l.material)});
                        }
                    }
                    b.set_sample_points(Id(ei)+1,std::move(points));
                    d.section_points=static_cast<std::uint32_t>(per_ip);
                }
                descriptions.push_back(d);
            }
        for (auto &s : ir.surfaces) {
            if(s.name.empty() || (!s.scope.empty() && !parts.contains(s.scope))) fail(s.source,"invalid surface name/scope");
            std::vector<std::string> cs;
            if (s.scope.empty())
                cs.push_back("");
            else
                for (auto &[name, c] : contexts)
                    if (c.part->name == s.scope)
                        cs.push_back(name);
            if(cs.empty()) fail(s.source,"uninstantiated surface scope");
            for (auto &ctx : cs) {
                std::vector<FaceId> faces;
                std::set<NodeId> surface_node_set;
                const auto semantic_surface=(ctx.empty()?"":ctx+".")+s.name;
                const bool collect_nodes=std::any_of(ir.extensions.couplings.begin(),ir.extensions.couplings.end(),[&](const auto &c){return c.surface==semantic_surface;});
                for (auto &[target, face] : s.faces)
                    for (auto e : resolve(false, target, ctx, s.source)) {
                        auto &d = descriptions.at(e);
                        auto local_nodes=abaqus_face(d, face, s.source);
                        if(collect_nodes) {
                            const auto &entry=source.elements.at(e); const auto *element=coupling_elements.at(e);
                            for(auto local:local_nodes) surface_node_set.insert(source.node_id({entry.key.part,entry.key.instance,element->nodes.at(local)}));
                        }
                        faces.push_back({Id(e) + 1,
                                         static_cast<std::uint16_t>(
                                             d.formulation == Formulation::Shell ? 0 : face[1] - '1'),
                                         static_cast<std::int8_t>(face == "SNEG" ? -1 : 1)});
                    }
                auto surface_name=(ctx.empty()?"":ctx+".")+s.name;
                if(faces.empty() || !foundation.surface_ids.emplace(surface_name,SurfaceId(index_of(foundation.surface_ids.size()))).second)
                    fail(s.source,"empty/duplicate semantic surface");
                if(collect_nodes) foundation.surface_nodes.emplace(surface_name,std::vector<NodeId>(surface_node_set.begin(),surface_node_set.end()));
                b.add_surface(setid++, std::move(faces));
            }
        }
        for (auto &a : ir.amplitudes) {
            if (a.points.empty())
                fail(a.source, "empty amplitude");
            for (std::size_t i = 0; i < a.points.size(); ++i)
                if (!std::isfinite(a.points[i].first) || !std::isfinite(a.points[i].second) ||
                    (i && (a.points[i].first <= a.points[i - 1].first ||
                           !std::isfinite(a.points[i].first - a.points[i - 1].first))))
                    fail(a.source, "invalid amplitude table");
            if (!analysis.amplitudes.emplace(a.name, a).second)
                fail(a.source, "duplicate amplitude");
        }
        using ActionKey = std::pair<Index, std::uint8_t>;
        std::map<ActionKey, Action> boundaries, loads;
        auto actions = [&](const std::vector<IRActionBlock> &blocks, std::map<ActionKey, Action> &current, std::uint64_t origin_step) {
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
                    double peak_scale = 1;
                    if (!a.amplitude.empty()) {
                        peak_scale = 0;
                        for (const auto &p : analysis.amplitudes.at(a.amplitude).points)
                            peak_scale = std::max(peak_scale, std::abs(p.second));
                    }
                    if (peak_scale > 0 &&
                        std::abs(a.value) > double(std::numeric_limits<T>::max()) / peak_scale)
                        fail(a.source, "scaled action exceeds selected precision range");
                    for (auto n : resolve(true, a.target, "", a.source))
                        for (auto c = a.first; c <= a.last; ++c) {
                            ActionKey key{n, c};
                            if (!updated.insert(key).second)
                                fail(a.source, "overlapping action targets in same step; combine explicitly");
                            current[key] = {n, c, a.value, a.amplitude, origin_step};
                        }
                }
        };
        actions(ir.initial_boundaries, boundaries, UINT64_MAX);
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
            actions(step.boundaries, boundaries, analysis.steps.size());
            actions(step.loads, loads, analysis.steps.size());
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
        resolve_foundation<T>(ir,foundation,source,node_dofs,resolve);
        foundation.element_materials.resize(source.elements.size()); foundation.element_orientations.resize(source.elements.size());
        for(const auto &s:sections) foundation.element_materials.at(s.element)=MaterialId(s.material);
        for(const auto &s:foundation.sections) {
            OrientationId orientation;
            auto find_orientation=[&](const auto &records){for(const auto &record:records) if(record.assignment.id==s.id) orientation=record.assignment.orientation;};
            find_orientation(ir.foundation.solid_sections); find_orientation(ir.foundation.shell_sections); find_orientation(ir.foundation.beam_sections);
            for(auto e:s.elements) foundation.element_orientations.at(e.value())=orientation;
        }
        auto scoped_resolve=[&](bool node,const std::string &scope,const std::string &target,const Source &at) {
            if(scope.empty()) return resolve(node,target,"",at);
            if(!parts.contains(scope)) fail(at,"unknown property region scope");
            auto dot=target.find('.');
            if(dot!=target.npos) {
                auto instance=contexts.find(target.substr(0,dot));
                if(instance==contexts.end() || instance->second.part->name!=scope) fail(at,"qualified property region outside scope");
                return resolve(node,target,"",at);
            }
            std::vector<Index> ids;
            for(const auto &[name,c]:contexts) if(c.part->name==scope) {auto more=resolve(node,target,name,at);ids.insert(ids.end(),more.begin(),more.end());}
            std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end()); return ids;
        };
        resolve_extensions<T>(ir,foundation,source,node_dofs,scoped_resolve);
        auto mesh = b.finalize();
        CompiledModel<T> result(std::move(ir), std::move(mesh));
        result.sources = std::move(source);
        result.foundation = std::move(foundation);
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
        auto value = lo->second + (hi->second - lo->second) * (t - lo->first) / (hi->first - lo->first);
        if (std::isfinite(value))
            return value; // preserve ordinary legacy rounding
        return std::lerp(lo->second, hi->second, (t - lo->first) / (hi->first - lo->first));
    }
    template <class T>
    static void apply(const AnalysisDB &db, std::size_t step, double local, double total, StateDB<T> &state) {
        state.data().external_force.fill(0);
        auto f = state.data().external_force.write();
        for (auto &a : db.steps.at(step).loads) {
            double scale = 1;
            if (!a.amplitude.empty()) {
                const auto &curve = db.amplitudes.at(a.amplitude);
                double evaluation_local = local;
                if (!curve.total_time && a.origin_step != UINT64_MAX) {
                    if (a.origin_step > step)
                        throw std::invalid_argument("load origin step is after evaluation step");
                    if (a.origin_step < step)
                        evaluation_local = db.steps.at(static_cast<std::size_t>(a.origin_step)).period;
                }
                scale = amplitude(curve, evaluation_local, total);
            }
            double value = scale * a.value;
            if (!std::isfinite(value) || std::abs(value) > double(std::numeric_limits<T>::max()))
                throw std::runtime_error("load exceeds selected precision range");
            auto sum = f(a.node, a.component) + static_cast<T>(value);
            if (!std::isfinite(sum))
                throw std::runtime_error("load accumulation exceeds selected precision range");
            f(a.node, a.component) = sum;
        }
    }
};
} // namespace sdb::lifecycle

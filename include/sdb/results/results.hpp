#pragma once
#include "../io/model_store.hpp"
#include "../dynamics.hpp"
#include <functional>
#include "typed.hpp"

namespace sdb::lifecycle {
enum class Location : std::uint8_t { Node, Element, IntegrationPoint, SectionPoint, Face, Contact, Global };
enum class TimeLevel : std::uint8_t { Current, HalfStep };
struct ResultDescriptor {
    std::string name, region, basis = "global Cartesian", provenance;
    Location location = Location::Node;
    TimeLevel time_level = TimeLevel::Current;
    std::vector<std::string> components;
    std::uint32_t integration_point = 0, section_point = 0;
    bool tensor_shear = false, history = false;
    template <class F> void visit(F f) {
        f(name, region, basis, provenance, location, time_level, components, integration_point, section_point,
          tensor_shear, history);
    }
};
struct ResultField {
    ResultDescriptor descriptor;
    std::vector<Id> ids;
    std::vector<double> values; // entity-major, lossless widening of float source
    std::optional<TypedFieldPayload> typed;
    template <class F> void visit(F f) {
        if (typed) throw std::invalid_argument("typed result requires indexed3 codec");
        f(descriptor, ids, values);
    }
};
struct FrameInfo {
    std::uint64_t id = 0, step = 0, increment = 0, mesh_revision = 0;
    double time = 0, local_time = 0;
    std::uint8_t precision = 8, storage_precision = 8;
    template <class F> void visit(F f) {
        f(id, step, increment, mesh_revision, time, local_time, precision, storage_precision);
    }
};
struct ResultFrame {
    FrameInfo info;
    std::vector<ResultField> fields;
    std::optional<ResultBinding> binding;
    template <class F> void visit(F f) { f(info, fields); }
};
struct ResultModel {
    std::uint64_t mesh = 0, definition = 0, configuration = 0;
    std::uint8_t precision = 8;
    std::vector<std::array<double, 3>> coordinates;
    std::vector<std::vector<Index>> connectivity;
    std::vector<ElementDescriptor> descriptors;
    std::vector<SourceEntry> nodes, elements;
    std::vector<std::pair<std::string, std::vector<Index>>> regions;
    std::vector<std::vector<ResultSample>> samples;
    std::vector<ResultBasis> bases;
    template <class F> void visit(F f) {
        f(mesh, definition, configuration, precision, coordinates, connectivity, descriptors, nodes, elements,
          regions);
    }
    template <class T> static ResultModel from(const CompiledModel<T> &m, std::uint64_t config) {
        ResultModel out;
        out.mesh = m.mesh.fingerprint();
        out.definition = ModelStore::identity(m);
        out.configuration = config;
        out.precision = sizeof(T);
        out.descriptors = m.descriptors;
        out.nodes = m.sources.nodes;
        out.elements = m.sources.elements;
        for (auto &pair : m.regions)
            out.regions.push_back(pair);
        for (Index n = 0; n < m.mesh.nodes(); ++n)
            out.coordinates.push_back(
                {m.mesh.reference()(n, 0), m.mesh.reference()(n, 1), m.mesh.reference()(n, 2)});
        for (Index e = 0; e < m.mesh.elements(); ++e) {
            auto ns = m.mesh.connectivity(e);
            out.connectivity.emplace_back(ns.begin(), ns.end());
        }
        bool extended = false;
        if (m.mesh.has_sample_layout()) for (Index e = 0; e < m.mesh.elements(); ++e) {
            std::vector<ResultSample> points;
            for (std::size_t q = 0; q < m.mesh.sample_point_count(e); ++q) {
                auto p = m.mesh.sample_point(e, q);
                points.push_back({p.integration_point, p.layer, p.section_point, p.material});
                extended = extended || p.layer || p.section_point ||
                    p.integration_point != q + 1 || p.material != m.mesh.element(e).material;
            }
            out.samples.push_back(std::move(points));
        }
        if (!extended) out.samples.clear();
        // Wire basis namespaces: 0=global; 1+OrientationId; 2^32+CoordinateId.
        // Every stored matrix maps local vectors to global, row major; no origins.
        for (const auto &[id,rotation] : m.foundation.orientation_bases)
            out.bases.push_back({1 + std::uint64_t(id.value()),rotation});
        for (const auto &coordinate : m.foundation.coordinates)
            if (coordinate.id.value() != 0)
                out.bases.push_back({(std::uint64_t(1)<<32) + coordinate.id.value(),coordinate.rotation});
        return out;
    }
};
inline void bind_result_frame(ResultFrame &frame, const ResultModel &model) {
    frame.binding = ResultBinding{model.mesh, model.definition, model.configuration,
        frame.info.step, frame.info.increment, frame.info.time};
    frame.info.mesh_revision = model.mesh;
    frame.info.precision = model.precision;
    frame.info.storage_precision = 0;
}
template<class T> ResultField make_typed_field(FieldDescriptor schema, std::vector<ResultAddress> addresses,
    std::vector<T> values, std::vector<std::uint8_t> validity = {}, const std::string &region = "", bool history = false) {
    if (schema.scalar != scalar_type<T>()) throw std::invalid_argument("typed field scalar mismatch");
    ResultField out;
    auto &d = out.descriptor;
    d.name = schema.name; d.region = region; d.history = history; d.basis = schema.basis;
    d.components = schema.components;
    d.tensor_shear = schema.shear == ShearConvention::Tensor;
    d.time_level = schema.time == PhysicalTime::HalfStep ? TimeLevel::HalfStep : TimeLevel::Current;
    d.location = schema.association == Association::Node ? Location::Node :
        schema.association == Association::Face ? Location::Face :
        schema.association == Association::Contact ? Location::Contact :
        schema.association == Association::Global ? Location::Global :
        schema.location == FieldLocation::IntegrationPoint ? Location::IntegrationPoint :
        schema.location == FieldLocation::SectionPoint ? Location::SectionPoint : Location::Element;
    out.typed = TypedFieldPayload{std::move(schema), std::move(addresses), std::move(values), std::move(validity)};
    validate_typed_payload(*out.typed);
    return out;
}
template<class T> const std::vector<T> &typed_values(const ResultField &field) {
    if (!field.typed) throw std::invalid_argument("legacy result is not typed");
    return std::get<std::vector<T>>(field.typed->values);
}
// Unqualified global bases accepted for legacy/default descriptors. Every other
// basis label requires a nonzero resolved catalog reference when published.
inline bool global_result_basis(const std::string &name) {
    auto normalized=upper(name);
    return normalized=="UNSPECIFIED" || normalized=="GLOBAL" || normalized=="GLOBAL CARTESIAN";
}
class OutputScheduler {
  public:
    static bool due(const IROutput &r, std::uint64_t increment, std::uint64_t total) {
        if (r.intervals) {
            if (increment == 0 || increment == total)
                return true;
            return (increment * r.intervals / total) != ((increment - 1) * r.intervals / total);
        }
        return r.frequency && (increment == 0 || increment == total || increment % r.frequency == 0);
    }
};
class ResultPublisher {
  public:
    template<class T> static void append_history(ResultFrame &frame, const CompiledModel<T> &model,
        const StateDB<T> &state, std::vector<std::string> names = {}, std::uint64_t budget = native::default_limit) {
        if (state.in_trial() || state.model_id() != model.mesh.fingerprint())
            throw std::invalid_argument("history snapshot requires matching committed state");
        state.validate_shape();
        std::vector<ResultField> fields;
        std::map<std::string,std::size_t> groups;
        std::set<std::string> found;
        std::size_t used = 0;
        auto history = state.data().history.read();
        for (Index e = 0; e < model.mesh.elements(); ++e)
            for (std::size_t q = 0; q < model.mesh.sample_point_count(e); ++q) {
                auto point = model.mesh.sample_point(e,q);
                auto &schema = model.mesh.materials()[point.material].history_schema;
                for (const auto &v : schema.variables) {
                    if (!names.empty() && std::find(names.begin(), names.end(), v.field.name) == names.end()) continue;
                    if (!global_result_basis(v.field.basis))
                        throw std::invalid_argument("history snapshot requires explicit resolved local basis adapter");
                    found.insert(v.field.name);
                    auto signature = native::pack(v.field);
                    auto key = schema.id + ":" + std::to_string(schema.version) + ":" +
                        std::string(reinterpret_cast<const char*>(signature.data()),signature.size());
                    auto [it, added] = groups.emplace(key, fields.size());
                    if (added) {
                        auto field = make_typed_field<T>(v.field, {}, {}, {}, "", true);
                        field.typed->schema_id = schema.id;
                        field.typed->schema_version = schema.version;
                        used = sum(used, sum(signature.size()*3, sizeof(ResultField) + key.size()*2 + 256));
                        if (used > budget) throw std::length_error("history snapshot metadata budget");
                        fields.push_back(std::move(field));
                    }
                    used = sum(used, product(2, sum(sizeof(ResultAddress), product(sizeof(T), v.field.component_count()))));
                    if (used > budget) throw std::length_error("history snapshot memory budget");
                    auto &p = *fields[it->second].typed;
                    ResultAddress a; a.entity = Id(e)+1; a.integration_point = point.integration_point;
                    a.layer = point.layer; a.section_point = point.section_point;
                    p.addresses.push_back(a);
                    auto &values = std::get<std::vector<T>>(p.values);
                    for (std::size_t c = 0; c < v.field.component_count(); ++c)
                        values.push_back(history(state.history_address_at(model.mesh,e,q,v.field.name,c)));
                }
            }
        for (const auto &name : names) if (!found.contains(name)) throw std::out_of_range("unknown history snapshot variable");
        auto config = frame.binding ? frame.binding->configuration : 0;
        ResultBinding binding{model.mesh.fingerprint(),ModelStore::identity(model),config,
            frame.info.step,frame.info.increment,frame.info.time};
        if (frame.binding && *frame.binding != binding) throw std::invalid_argument("history frame binding mismatch");
        for (const auto &field : fields) {
            validate_typed_payload(*field.typed);
            for (const auto &old : frame.fields)
                if (old.typed && old.descriptor.name == field.descriptor.name && old.descriptor.history &&
                    old.descriptor.region.empty() && old.typed->schema_id == field.typed->schema_id &&
                    old.typed->schema_version == field.typed->schema_version &&
                    native::pack(old.typed->schema)==native::pack(field.typed->schema))
                    throw std::invalid_argument("duplicate history snapshot field");
        }
        auto needed=sum(frame.fields.size(),fields.size());
        if (product(needed,sizeof(ResultField)) > budget-used) throw std::length_error("history frame directory budget");
        frame.fields.reserve(needed); // all potentially throwing work precedes publication
        frame.fields.insert(frame.fields.end(),std::make_move_iterator(fields.begin()),std::make_move_iterator(fields.end()));
        frame.binding=binding;
        frame.info.mesh_revision=binding.mesh; frame.info.precision=sizeof(T); frame.info.storage_precision=0;
    }
    template<class T> static ResultFrame snapshot_history(const CompiledModel<T> &model, const StateDB<T> &state,
        FrameInfo info = {}, std::vector<std::string> names = {}, std::uint64_t budget = native::default_limit,
        std::uint64_t configuration = 0) {
        // A default FrameInfo snapshots the committed state's physical time and increment.
        // StateDB::step is the accumulated integration count, not analysis-step ID.
        if (info.id==0 && info.step==0 && info.increment==0 && info.time==0 && info.local_time==0) {
            info.id=state.data().step; info.increment=state.data().step;
            info.time=state.data().time; info.local_time=state.data().time;
        }
        ResultFrame frame; frame.info=info;
        frame.binding=ResultBinding{model.mesh.fingerprint(),ModelStore::identity(model),configuration,
            info.step,info.increment,info.time};
        append_history(frame,model,state,std::move(names),budget);
        return frame;
    }
    template <class T>
    static ResultFrame publish(const CompiledModel<T> &model, const StateDB<T> &state, CpuExplicit<T> &cpu,
                               FrameInfo info, const std::vector<IROutput> &requests,
                               std::uint64_t increments, std::uint64_t budget = native::default_limit) {
        // Due-selection happens before any constitutive re-evaluation: a non-output
        // increment must not pay for a force pass, and a pure-displacement frame
        // needs no force data at all. Evaluation below is pure linear constitutive
        // re-evaluation; it never advances history or modifies velocity.
        std::vector<const IROutput *> due;
        bool needs_forces = false;
        for (auto &r : requests)
            if (OutputScheduler::due(r, info.increment, increments)) {
                due.push_back(&r);
                for (auto &name : r.variables)
                    if (name != "U")
                        needs_forces = true;
            }
        const ForceSnapshot<T> *derived = needs_forces ? &cpu.output_forces(state) : nullptr;
        auto &s = state.data();
        auto u = s.displacement.read(), v = s.velocity.read(),
             f = derived ? derived->internal_force.read() : s.internal_force.read(),
             ext = s.external_force.read(), contact = s.contact_force.read();
        auto stress = derived ? derived->stress.read() : s.stress.read();
        auto strain = derived ? derived->strain.read() : s.strain.read();
        auto fixed = cpu.plan().fixed.read();
        auto inv = s.inverse_mass.read();
        auto acceleration = [&](Index n, unsigned c) -> double {
            return fixed(n, c) ? 0 : double((ext(n, c) + contact(n, c) - f(n, c)) * inv(n));
        };
        auto velocity = [&](Index n, unsigned c) -> double {
            return fixed(n, c) ? 0 : double(v(n, c)) + .5 * s.dt * acceleration(n, c);
        };
        ResultFrame frame;
        frame.info = info;
        std::uint64_t used = 0;
        std::set<std::tuple<std::string, std::string, bool, unsigned>> emitted;
        for (auto *request : due) {
            auto &r = *request;
            for (auto &name : r.variables) {
                unsigned points = 1;
                if (name == "S" || name == "E")
                    for (const auto &d : model.descriptors)
                        points = std::max(points, d.points);
                for (unsigned point = 1; point <= points; ++point) {
                    if (!emitted.emplace(name, r.region, r.history, point).second)
                        continue;
                    ResultField field;
                    auto &d = field.descriptor;
                    d.name = name;
                    d.region = r.region;
                    d.history = r.history;
                    d.provenance =
                        cpu.plan().hex
                            ? "CPU linear TET4/HEX8 full; current displacement constitutive evaluation"
                            : "CPU linear TET4; current displacement constitutive evaluation";
                    bool nodal = name == "U" || name == "V" || name == "A" || name == "RF";
                    bool global = name == "ALLKE" || name == "ALLSE";
                    d.location =
                        global ? Location::Global : (nodal ? Location::Node : Location::IntegrationPoint);
                    d.components =
                        global ? std::vector<std::string>{name}
                               : (nodal ? std::vector<std::string>{"X", "Y", "Z"}
                                        : std::vector<std::string>{"XX", "YY", "ZZ", "XY", "YZ", "ZX"});
                    if (!nodal && !global) {
                        d.integration_point = point;
                        d.tensor_shear = name == "E";
                    }
                    if (name == "V")
                        d.provenance = "v(t-dt/2) + dt/2 * a(t); full-time reconstruction";
                    if (name == "RF")
                        d.provenance =
                            "f_internal - f_applied - f_contact on constrained DOFs; zero elsewhere";
                    std::vector<Index> ids;
                    if (!r.region.empty())
                        ids = model.region(nodal, r.region);
                    else {
                        std::size_t count = global ? 1 : (nodal ? model.mesh.nodes() : model.mesh.elements());
                        ids.resize(count);
                        for (Index i = 0; i < count; ++i)
                            ids[i] = i;
                    }
                    if (!nodal && !global) {
                        std::erase_if(
                            ids, [&](Index e) { return model.mesh.element(e).integration_points < point; });
                        if (ids.empty())
                            continue;
                    }
                    auto width = d.components.size();
                    auto need = std::uint64_t(ids.size()) * (8 + 8 * width);
                    if (need > budget - used)
                        throw std::length_error("result snapshot memory budget");
                    used += need;
                    field.values.reserve(ids.size() * width);
                    field.ids.reserve(ids.size());
                    for (auto i : ids) {
                        field.ids.push_back(global ? 0 : Id(i) + 1);
                        if (global) {
                            double energy = 0;
                            if (name == "ALLKE") {
                                for (Index n = 0; n < model.mesh.nodes(); ++n)
                                    for (unsigned c = 0; c < 3; ++c) {
                                        auto speed = velocity(n, c);
                                        energy += .5 * double(s.mass.read()(n)) * speed * speed;
                                    }
                            } else
                                for (std::size_t e = 0; e < s.stress.size(); ++e)
                                    for (unsigned c = 0; c < 6; ++c)
                                        energy += .5 * double(cpu.plan().integration_weight.read()(e)) *
                                                  double(stress(e, c)) * double(strain(e, c)) *
                                                  (c < 3 ? 1 : 2);
                            field.values.push_back(energy);
                        } else if (nodal)
                            for (unsigned c = 0; c < 3; ++c)
                                field.values.push_back(
                                    name == "U"
                                        ? double(u(i, c))
                                        : (name == "V"
                                               ? velocity(i, c)
                                               : (name == "A" ? acceleration(i, c)
                                                              : (fixed(i, c) ? double(f(i, c) - ext(i, c) -
                                                                                      contact(i, c))
                                                                             : 0))));
                        else
                            for (unsigned c = 0; c < 6; ++c)
                                field.values.push_back(
                                    name == "S" ? double(stress(state.ip_offsets()[i] + point - 1, c))
                                                : double(strain(state.ip_offsets()[i] + point - 1, c)));
                    }
                    frame.fields.push_back(std::move(field));
                }
            }
        }
        return frame;
    }
};
inline constexpr std::uint64_t result_magic = 0x31534552424453ull;
inline void validate_result_address(const ResultAddress &a, const FieldDescriptor &d, const ResultModel &m) {
    const ResultEndpoint empty;
    if (a.domain > 1 || a.side < -1 || a.side > 1) throw std::runtime_error("invalid result domain/side");
    if (d.association == Association::Contact) {
        if (a.domain != 1 || a.entity || !a.contact_generation || !a.interaction || !a.contact_point ||
            a.integration_point || a.layer || a.section_point || a.element_node || a.face || a.side)
            throw std::runtime_error("contact result needs stable identity");
        auto endpoint = [&](const ResultEndpoint &e) {
            if (e.kind > 2 || !e.entity || e.side < -1 || e.side > 1 ||
                e.entity > (e.kind == 0 ? m.nodes.size() : m.elements.size()) ||
                (e.kind == 0 ? (e.local != 0 || e.side != 0) : !e.local))
                throw std::runtime_error("contact endpoint outside model");
            if (e.kind == 1 && (e.side != 0 || e.local > m.connectivity.at(e.entity - 1).size()))
                throw std::runtime_error("contact endpoint local node out of bounds");
            if (e.kind == 2 && (!e.side || e.local > face_count(m.descriptors.at(e.entity-1).shape)))
                throw std::runtime_error("contact endpoint face out of bounds");
        };
        endpoint(a.endpoint_a); endpoint(a.endpoint_b);
        if (a.endpoint_a == a.endpoint_b) throw std::runtime_error("identical contact endpoints");
        return;
    }
    if (a.domain || a.contact_generation || a.interaction || a.contact_point ||
        a.endpoint_a != empty || a.endpoint_b != empty) throw std::runtime_error("mesh result has contact identity");
    if (d.association == Association::Global) {
        if (a != ResultAddress{}) throw std::runtime_error("global result address");
        return;
    }
    if (!a.entity || a.entity > (d.association == Association::Node ? m.nodes.size() : m.elements.size()))
        throw std::runtime_error("typed result entity out of bounds");
    if (d.association == Association::Face) {
        auto maxface = face_count(m.descriptors.at(a.entity-1).shape);
        if (!a.face || !a.side || a.face > maxface || a.integration_point || a.layer || a.section_point || a.element_node)
            throw std::runtime_error("face result address");
        return;
    }
    if (a.face || a.side) throw std::runtime_error("nonface result has face identity");
    if (d.location == FieldLocation::Entity) {
        if (a.integration_point || a.layer || a.section_point || a.element_node)
            throw std::runtime_error("entity result has sample address");
    } else if (d.location == FieldLocation::ElementNode) {
        if (!a.element_node || a.element_node > m.connectivity.at(a.entity-1).size() ||
            a.integration_point || a.layer || a.section_point) throw std::runtime_error("element node address");
    } else {
        if (!a.integration_point || a.element_node || (bool(a.layer) != bool(a.section_point)))
            throw std::runtime_error("invalid sample address");
        if (d.location == FieldLocation::SectionPoint && !a.section_point)
            throw std::runtime_error("section point address required");
        if (!m.samples.empty()) {
            const auto &samples = m.samples.at(a.entity-1);
            if (std::none_of(samples.begin(), samples.end(), [&](const auto &q) {
                return q.integration_point == a.integration_point && q.layer == a.layer && q.section_point == a.section_point;
            })) throw std::runtime_error("sample address outside model");
        } else if (a.layer || a.section_point || a.integration_point > m.descriptors.at(a.entity-1).points)
            throw std::runtime_error("sample address outside legacy model");
    }
}
inline void validate_frame(const ResultFrame &f, const ResultModel &model,
                           const std::optional<FrameInfo> &previous) {
    auto &i = f.info;
    if (i.mesh_revision != model.mesh || i.precision != model.precision || (i.storage_precision != 8 && i.storage_precision != 0) ||
        !std::isfinite(i.time) || !std::isfinite(i.local_time) || i.time < 0 || i.local_time < 0)
        throw std::runtime_error("invalid frame metadata");
    if (previous && (i.id <= previous->id || i.time <= previous->time || i.step < previous->step ||
                     (i.step == previous->step && i.increment <= previous->increment)))
        throw std::runtime_error("duplicate or nonmonotonic result frame");
    if (i.storage_precision == 0 && (!f.binding || *f.binding != ResultBinding{
        model.mesh, model.definition, model.configuration, i.step, i.increment, i.time}))
        throw std::runtime_error("typed result binding mismatch");
    if (i.storage_precision == 8 && f.binding) throw std::runtime_error("legacy result has typed binding");
    std::set<std::tuple<std::string, std::string, bool, std::uint32_t, std::uint32_t>> keys;
    std::set<std::tuple<std::string, std::string, bool, std::string, std::uint32_t,native::Bytes>> typed_keys;
    for (auto &field : f.fields) {
        auto &d = field.descriptor;
        if (field.typed) {
            const auto &p = *field.typed;
            validate_typed_payload(p);
            if (!p.basis_id && !global_result_basis(p.schema.basis))
                throw std::runtime_error("local result basis requires catalog reference");
            if (p.basis_id && std::none_of(model.bases.begin(),model.bases.end(),[&](const auto &b){return b.id==p.basis_id;}))
                throw std::runtime_error("unknown typed result basis reference");
            const auto expected_location = p.schema.association == Association::Node ? Location::Node :
                p.schema.association == Association::Face ? Location::Face :
                p.schema.association == Association::Contact ? Location::Contact :
                p.schema.association == Association::Global ? Location::Global :
                p.schema.location == FieldLocation::IntegrationPoint ? Location::IntegrationPoint :
                p.schema.location == FieldLocation::SectionPoint ? Location::SectionPoint : Location::Element;
            if (i.storage_precision != 0 || !field.ids.empty() || !field.values.empty() ||
                d.location != expected_location || d.integration_point || d.section_point ||
                d.tensor_shear != (p.schema.shear == ShearConvention::Tensor) ||
                d.time_level != (p.schema.time == PhysicalTime::HalfStep ? TimeLevel::HalfStep : TimeLevel::Current) ||
                d.name != p.schema.name || d.components != p.schema.components || d.basis != p.schema.basis ||
                !typed_keys.emplace(d.name, d.region, d.history, p.schema_id, p.schema_version,native::pack(p.schema)).second)
                throw std::runtime_error("typed field envelope mismatch/duplicate");
            for (const auto &a : p.addresses) validate_result_address(a, p.schema, model);
            continue;
        }
        if (!keys.emplace(d.name, d.region, d.history, d.integration_point, d.section_point).second ||
            d.components.empty() || d.components.size() > 9 ||
            field.values.size() != product(field.ids.size(), d.components.size()) ||
            static_cast<unsigned>(d.location) > static_cast<unsigned>(Location::Global) ||
            static_cast<unsigned>(d.time_level) > 1)
            throw std::runtime_error("invalid result field schema");
        std::set<Id> ids;
        for (auto id : field.ids) {
            if (!ids.insert(id).second)
                throw std::runtime_error("duplicate result entity");
            if (d.location == Location::Global) {
                if (id != 0)
                    throw std::runtime_error("invalid global entity");
            } else if (id == 0 ||
                       id > (d.location == Location::Node ? model.nodes.size() : model.elements.size()))
                throw std::runtime_error("result entity out of bounds");
        }
        if (d.location == Location::IntegrationPoint) {
            if (d.integration_point == 0)
                throw std::runtime_error("invalid integration point");
            for (auto id : field.ids)
                if (d.integration_point > model.descriptors.at(id - 1).points)
                    throw std::runtime_error("integration point outside element");
        }
        for (auto v : field.values)
            if (!std::isfinite(v))
                throw std::runtime_error("nonfinite result");
    }
}
} // namespace sdb::lifecycle
#include "indexed_format.hpp"
namespace sdb::lifecycle {
class ResultReader {
    std::filesystem::path path_;
    std::uint64_t limit_;
    std::vector<native::Record> records_;
    std::shared_ptr<indexed_detail::File> indexed_;

  public:
    ResultModel model;
    FrameIndex index;
    std::uint64_t valid_end = 0;
    bool incomplete_tail = false;
    explicit ResultReader(const std::filesystem::path &path, bool allow_incomplete_tail = false,
                          std::uint64_t limit = native::default_limit)
        : path_(path), limit_(limit) {
        std::ifstream in(path, std::ios::binary);
        std::array<std::uint8_t, 8> magic_bytes{};
        native::read(in, magic_bytes);
        native::Reader probe{magic_bytes};
        auto file_magic = probe.integer<std::uint64_t>();
        if (file_magic == indexed_detail::magic || file_magic == typed_detail::magic) {
            indexed_ = std::make_shared<indexed_detail::File>(path, limit, allow_incomplete_tail);
            model = indexed_->model;
            index.bind(indexed_);
            valid_end = indexed_->valid_end;
            incomplete_tail = indexed_->incomplete_tail;
        } else {
            in.seekg(0);
            native::prefix(in, result_magic);
            model = native::unpack<ResultModel>(native::record(in, 0, limit), limit);
        }
        if (model.precision != 4 && model.precision != 8)
            throw std::runtime_error("result precision invalid");
        if (model.coordinates.size() != model.nodes.size() ||
            model.connectivity.size() != model.elements.size() ||
            model.descriptors.size() != model.elements.size())
            throw std::runtime_error("result mesh shape invalid");
        for (auto &ns : model.connectivity)
            for (auto n : ns)
                if (n >= model.nodes.size())
                    throw std::runtime_error("result mesh connectivity invalid");
        if (indexed_)
            return;
        valid_end = static_cast<std::uint64_t>(in.tellg());
        auto size = std::filesystem::file_size(path);
        while (valid_end < size) {
            if (size - valid_end < 16) {
                incomplete_tail = true;
                break;
            }
            auto rec = native::header(in, limit);
            if (rec.kind != 1)
                throw std::runtime_error("unknown result record type");
            if (rec.size > size - rec.offset) {
                incomplete_tail = true;
                break;
            }
            auto frame = native::unpack<ResultFrame>(native::body(in, rec), limit);
            validate_frame(frame, model,
                           index.empty() ? std::nullopt : std::optional<FrameInfo>{index.back()});
            records_.push_back(rec);
            index.push_back(frame.info);
            valid_end = static_cast<std::uint64_t>(in.tellg());
        }
        if (incomplete_tail && !allow_incomplete_tail)
            throw std::runtime_error("incomplete result tail (explicit recovery required)");
    }
    bool indexed() const { return bool(indexed_); }
    ResultFormat format() const { return !indexed_ ? ResultFormat::Native1 :
        indexed_->typed() ? ResultFormat::TypedIndexed3 : ResultFormat::Indexed2; }
    ResultField typed_field(std::size_t frame_index, const std::string &name,
        std::optional<ResultAddress> exact = {}, std::optional<Id> entity = {}, const std::string &schema_id = "") const {
        if (!indexed_ || !indexed_->typed()) throw std::invalid_argument("typed query requires indexed3");
        auto f = indexed_->frame(frame_index, &name, false, entity, {}, 1, exact, true, schema_id.empty() ? nullptr : &schema_id);
        if (f.fields.empty()) throw std::out_of_range("typed field not stored");
        if (f.fields.size() != 1) throw std::invalid_argument("ambiguous typed field schemas/regions");
        return std::move(f.fields.front());
    }
    ResultReadStats read_stats() const { return indexed_ ? indexed_->stats() : ResultReadStats{}; }
    ResultFrame frame(std::size_t i) const {
        if (indexed_)
            return indexed_->frame(i);
        auto rec = records_.at(i);
        std::ifstream in(path_, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(rec.offset));
        auto result = native::unpack<ResultFrame>(native::body(in, rec), limit_);
        validate_frame(result, model, std::nullopt);
        return result;
    }
    std::size_t by_time(double time) const {
        if (!std::isfinite(time) || index.empty() || time < index.front().time || time > index.back().time)
            throw std::out_of_range("result time outside stored range");
        std::size_t lo = 0, hi = index.size();
        while (lo < hi) {
            auto mid = lo + (hi - lo) / 2;
            if (index[mid].time < time)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }
    std::set<Id> region(bool node, const std::string &name) const {
        auto normalized = upper(name);
        if (normalized.starts_with("@INSTANCE:") || normalized.starts_with("@PART:")) {
            bool instance = normalized.starts_with("@INSTANCE:");
            auto match = normalized.substr(instance ? 10 : 6);
            std::set<Id> out;
            if (match.empty())
                throw std::invalid_argument("empty instance/part region");
            for (auto &e : node ? model.nodes : model.elements)
                if ((instance ? e.key.instance : e.key.part) == match)
                    out.insert(e.id);
            if (out.empty())
                throw std::invalid_argument("unknown result instance/part: " + name);
            return out;
        }
        auto key = (node ? "N:" : "E:") + upper(name);
        for (auto &[k, ids] : model.regions)
            if (k == key) {
                std::set<Id> out;
                for (auto i : ids)
                    out.insert(Id(i) + 1);
                return out;
            }
        throw std::invalid_argument("unknown result region: " + name);
    }
    ResultField entity_field(std::size_t frame_index, const std::string &name, Id entity,
                             const std::string &region_name = "", bool history = false,
                             unsigned point = 1) const {
        return field(frame_index, name, region_name, history, entity, point);
    }
    ResultField field(std::size_t frame_index, const std::string &name, const std::string &region_name = "",
                      bool history = false, std::optional<Id> entity = {}, unsigned point = 1) const {
        auto normalized = upper(name);
        std::function<std::set<Id>(const ResultDescriptor &)> selection;
        if (!region_name.empty())
            selection = [&](const ResultDescriptor &d) {
                if (d.location == Location::Global)
                    throw std::invalid_argument("global field cannot have a mesh region");
                return region(d.location == Location::Node, region_name);
            };
        auto f = indexed_ ? indexed_->frame(frame_index, &normalized, history, entity, selection, point)
                          : frame(frame_index);
        if (entity && !indexed_)
            for (auto &v : f.fields) {
                ResultField selected;
                selected.descriptor = v.descriptor;
                auto w = v.descriptor.components.size();
                for (std::size_t i = 0; i < v.ids.size(); ++i)
                    if (v.ids[i] == *entity) {
                        selected.ids.push_back(*entity);
                        selected.values.insert(selected.values.end(), v.values.begin() + i * w,
                                               v.values.begin() + (i + 1) * w);
                    }
                v = std::move(selected);
            }
        std::optional<ResultField> found;
        std::vector<const ResultField *> matching;
        for (auto &v : f.fields)
            if (v.descriptor.name == upper(name) && v.descriptor.history == history &&
                (v.descriptor.location != Location::IntegrationPoint ||
                 v.descriptor.integration_point == point)) {
                matching.push_back(&v);
                if (v.descriptor.region == upper(region_name)) {
                    found = std::move(v);
                    break;
                }
                if (v.descriptor.region.empty())
                    found = v;
            }
        if (!found && !matching.empty()) {
            ResultField combined;
            combined.descriptor = matching.front()->descriptor;
            combined.descriptor.region.clear();
            std::map<Id, std::vector<double>> entities;
            auto width = combined.descriptor.components.size();
            for (auto *part : matching) {
                if (part->descriptor.components != combined.descriptor.components ||
                    part->descriptor.location != combined.descriptor.location ||
                    part->descriptor.basis != combined.descriptor.basis ||
                    part->descriptor.time_level != combined.descriptor.time_level ||
                    part->descriptor.tensor_shear != combined.descriptor.tensor_shear ||
                    part->descriptor.integration_point != combined.descriptor.integration_point ||
                    part->descriptor.section_point != combined.descriptor.section_point)
                    throw std::runtime_error("incompatible regional field descriptors");
                for (std::size_t i = 0; i < part->ids.size(); ++i) {
                    std::vector<double> values(part->values.begin() + i * width,
                                               part->values.begin() + (i + 1) * width);
                    auto [it, added] = entities.emplace(part->ids[i], values);
                    if (!added && it->second != values)
                        throw std::runtime_error("conflicting regional field values");
                }
            }
            for (auto &[id, values] : entities) {
                combined.ids.push_back(id);
                combined.values.insert(combined.values.end(), values.begin(), values.end());
            }
            found = std::move(combined);
        }
        if (!found)
            throw std::out_of_range("requested field/region was not stored: " + name);
        if (region_name.empty() || found->descriptor.region == upper(region_name))
            return *found;
        if (found->descriptor.location == Location::Global)
            throw std::invalid_argument("global field cannot have a mesh region");
        auto selected = region(found->descriptor.location == Location::Node, region_name);
        ResultField out;
        out.descriptor = found->descriptor;
        out.descriptor.region = upper(region_name);
        auto width = out.descriptor.components.size();
        for (std::size_t i = 0; i < found->ids.size(); ++i)
            if (selected.contains(found->ids[i])) {
                out.ids.push_back(found->ids[i]);
                out.values.insert(out.values.end(), found->values.begin() + i * width,
                                  found->values.begin() + (i + 1) * width);
            }
        return out;
    }
};
class ResultQuery {
  public:
    template<class T> static std::vector<std::pair<double,std::optional<T>>> typed_history(
        const ResultReader &reader, const std::string &name, ResultAddress address, unsigned component = 0,
        double time_begin = 0, double time_end = std::numeric_limits<double>::max(), const std::string &schema_id = "") {
        if (!std::isfinite(time_begin) || !std::isfinite(time_end) || time_begin > time_end)
            throw std::invalid_argument("invalid typed history interval");
        std::vector<std::pair<double,std::optional<T>>> values;
        for (std::size_t i=0; i<reader.index.size(); ++i) {
            auto time = reader.index[i].time;
            if (time < time_begin) continue;
            if (time > time_end) break;
            ResultField field;
            try { field=reader.typed_field(i,name,address,{},schema_id); } catch(const std::out_of_range&) { continue; }
            auto &p=*field.typed;
            if (component >= p.schema.component_count()) throw std::out_of_range("typed history component");
            auto &v=typed_values<T>(field);
            if (!p.addresses.empty()) values.emplace_back(time,
                p.validity.empty() || p.validity[component] ? std::optional<T>(v[component]) : std::nullopt);
        }
        return values;
    }
    static std::vector<std::pair<double, double>>
    history(const ResultReader &reader, const std::string &field, Id entity, unsigned component = 0,
            const std::string &region = "", bool history_request = false, double time_begin = 0,
            double time_end = std::numeric_limits<double>::max(), unsigned point = 1) {
        if (!region.empty())
            (void)reader.region(upper(field) == "U" || upper(field) == "V" || upper(field) == "A" ||
                                    upper(field) == "RF",
                                region);
        if (!std::isfinite(time_begin) || !std::isfinite(time_end) || time_begin > time_end)
            throw std::invalid_argument("invalid history time interval");
        std::vector<std::pair<double, double>> values;
        for (std::size_t i = 0; i < reader.index.size(); ++i) {
            auto time = reader.index[i].time;
            if (time < time_begin)
                continue;
            if (time > time_end)
                break;
            ResultField f;
            try {
                f = reader.entity_field(i, field, entity, region, history_request, point);
            } catch (const std::out_of_range &) {
                continue;
            }
            if (component >= f.descriptor.components.size())
                throw std::out_of_range("component");
            auto at = std::find(f.ids.begin(), f.ids.end(), entity);
            if (at != f.ids.end())
                values.emplace_back(
                    reader.index[i].time,
                    f.values[static_cast<std::size_t>(at - f.ids.begin()) * f.descriptor.components.size() +
                             component]);
        }
        return values;
    }
};
class IResultStorage {
  public:
    virtual ~IResultStorage() = default;
    virtual void append(const ResultFrame &) = 0;
    virtual void flush() = 0;
    virtual std::optional<FrameInfo> last() const = 0;
};
class NativeResultStorage final : public IResultStorage {
    native::FileLease lease_; // destroyed after output stream closes
    std::ofstream out_;
    ResultModel model_;
    std::optional<FrameInfo> last_;
    std::uint64_t limit_;
    bool failed_ = false;
    std::unique_ptr<indexed_detail::File> indexed_;

  public:
    NativeResultStorage(const std::filesystem::path &path, ResultModel model, bool append = false,
                        bool recover = false, std::uint64_t limit = native::default_limit,
                        std::function<void()> append_preflight = {}, bool indexed = false,
                        ResultFormat format = ResultFormat::Auto)
        : lease_(path), model_(std::move(model)), limit_(limit) {
        if (upper(path.extension().string()) == ".H5" || upper(path.extension().string()) == ".HDF5")
            throw std::invalid_argument("native backend requires a non-HDF5 suffix (.sdbres)");
        typed_detail::validate_model(model_);
        if (format != ResultFormat::Auto && format != ResultFormat::Native1 &&
            format != ResultFormat::Indexed2 && format != ResultFormat::TypedIndexed3)
            throw std::invalid_argument("unknown result format");
        auto requested = format == ResultFormat::Auto ? (indexed ? ResultFormat::Indexed2 : ResultFormat::Native1) : format;
        if (!append && requested != ResultFormat::TypedIndexed3 && (!model_.samples.empty() || !model_.bases.empty()))
            throw std::invalid_argument("extended result model requires indexed3");
        if (append) {
            if (append_preflight)
                append_preflight();
            ResultReader existing(path, recover, limit);
            if (format != ResultFormat::Auto && existing.format() != format)
                throw std::runtime_error("result append requested format mismatch");
            const bool typed = existing.format() == ResultFormat::TypedIndexed3;
            if (!typed && (!model_.samples.empty() || !model_.bases.empty()))
                throw std::invalid_argument("extended model cannot append to legacy result");
            if (typed_detail::pack_model(existing.model, limit_, typed) != typed_detail::pack_model(model_, limit_, typed))
                throw std::runtime_error("result append model/configuration mismatch");
            if (existing.incomplete_tail)
                std::filesystem::resize_file(path, existing.valid_end);
            if (!existing.index.empty())
                last_ = existing.index.back();
            if (existing.indexed())
                indexed_ = std::make_unique<indexed_detail::File>(path, limit_);
            out_.open(path, std::ios::binary | std::ios::app);
            out_.seekp(0, std::ios::end);
        } else {
            if (std::filesystem::exists(path))
                throw std::runtime_error("result exists; use explicit append or a new output path");
            out_.open(path, std::ios::binary | std::ios::trunc);
            if (requested == ResultFormat::Indexed2 || requested == ResultFormat::TypedIndexed3) {
                indexed_detail::File::create(out_, model_, limit_, requested == ResultFormat::TypedIndexed3);
                indexed_ = std::make_unique<indexed_detail::File>(path, limit_);
            } else {
                native::prefix(out_, result_magic);
                native::record(out_, 0, native::pack(model_, limit_));
                flush();
            }
        }
        if (!out_)
            throw std::runtime_error("cannot open result output");
    }
    void append(const ResultFrame &frame) override {
        if (failed_)
            throw std::runtime_error("result writer is failed");
        if (indexed_ && indexed_->typed()) {
            if (frame.info.storage_precision != 0) throw std::invalid_argument("indexed3 requires bound frame");
        } else if (frame.binding || frame.info.storage_precision != 8 ||
            std::any_of(frame.fields.begin(),frame.fields.end(),[](const auto &f){return bool(f.typed);}))
            throw std::invalid_argument("legacy format rejects typed result before writing");
        validate_frame(frame, model_, last_);
        try {
            if (indexed_)
                indexed_->append(out_, frame);
            else {
                auto bytes = native::pack(frame, limit_);
                native::record(out_, 1, bytes);
                flush();
            }
            last_ = frame.info;
        } catch (...) {
            failed_ = true;
            throw;
        }
    }
    void flush() override {
        out_.flush();
        if (!out_) {
            failed_ = true;
            throw std::runtime_error("result flush failed");
        }
    }
    std::optional<FrameInfo> last() const override { return last_; }
};
class ResultWriter {
    IResultStorage &storage_;

  public:
    explicit ResultWriter(IResultStorage &s) : storage_(s) {}
    void write(const ResultFrame &f) {
        if (!f.fields.empty())
            storage_.append(f);
    }
    void flush() { storage_.flush(); }
};
} // namespace sdb::lifecycle

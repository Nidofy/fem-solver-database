#pragma once
#include "../io/model_store.hpp"
#include "../dynamics.hpp"

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
    template <class F> void visit(F f) { f(descriptor, ids, values); }
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
        return out;
    }
};
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
    template <class T>
    static ResultFrame publish(const CompiledModel<T> &model, StateDB<T> &state, CpuExplicit<T> &cpu,
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
        if (needs_forces)
            cpu.forces(state);
        auto &s = state.data();
        auto u = s.displacement.read(), v = s.velocity.read(), f = s.internal_force.read(),
             ext = s.external_force.read(), contact = s.contact_force.read();
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
        std::set<std::tuple<std::string, std::string, bool>> emitted;
        for (auto *request : due) {
            auto &r = *request;
            for (auto &name : r.variables) {
                if (!emitted.emplace(name, r.region, r.history).second)
                    continue;
                ResultField field;
                auto &d = field.descriptor;
                d.name = name;
                d.region = r.region;
                d.history = r.history;
                d.provenance = "CPU linear TET4; current displacement constitutive evaluation";
                bool nodal = name == "U" || name == "V" || name == "A" || name == "RF";
                bool global = name == "ALLKE" || name == "ALLSE";
                d.location =
                    global ? Location::Global : (nodal ? Location::Node : Location::IntegrationPoint);
                d.components = global
                                   ? std::vector<std::string>{name}
                                   : (nodal ? std::vector<std::string>{"X", "Y", "Z"}
                                            : std::vector<std::string>{"XX", "YY", "ZZ", "XY", "YZ", "ZX"});
                if (!nodal && !global) {
                    d.integration_point = 1;
                    d.tensor_shear = name == "E";
                }
                if (name == "V")
                    d.provenance = "v(t-dt/2) + dt/2 * a(t); full-time reconstruction";
                if (name == "RF")
                    d.provenance = "f_internal - f_applied - f_contact on constrained DOFs; zero elsewhere";
                std::vector<Index> ids;
                if (!r.region.empty())
                    ids = model.region(nodal, r.region);
                else {
                    std::size_t count = global ? 1 : (nodal ? model.mesh.nodes() : model.mesh.elements());
                    ids.resize(count);
                    for (Index i = 0; i < count; ++i)
                        ids[i] = i;
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
                            for (Index e = 0; e < model.mesh.elements(); ++e)
                                for (unsigned c = 0; c < 6; ++c)
                                    energy += .5 * double(cpu.plan().volume.read()(e)) *
                                              double(s.stress.read()(e, c)) * double(s.strain.read()(e, c)) *
                                              (c < 3 ? 1 : 2);
                        field.values.push_back(energy);
                    } else if (nodal)
                        for (unsigned c = 0; c < 3; ++c)
                            field.values.push_back(
                                name == "U"
                                    ? double(u(i, c))
                                    : (name == "V"
                                           ? velocity(i, c)
                                           : (name == "A"
                                                  ? acceleration(i, c)
                                                  : (fixed(i, c) ? double(f(i, c) - ext(i, c) - contact(i, c))
                                                                 : 0))));
                    else
                        for (unsigned c = 0; c < 6; ++c)
                            field.values.push_back(name == "S" ? double(s.stress.read()(i, c))
                                                               : double(s.strain.read()(i, c)));
                }
                frame.fields.push_back(std::move(field));
            }
        }
        return frame;
    }
};
inline constexpr std::uint64_t result_magic = 0x31534552424453ull;
inline void validate_frame(const ResultFrame &f, const ResultModel &model,
                           const std::optional<FrameInfo> &previous) {
    auto &i = f.info;
    if (i.mesh_revision != model.mesh || i.precision != model.precision || i.storage_precision != 8 ||
        !std::isfinite(i.time) || !std::isfinite(i.local_time) || i.time < 0 || i.local_time < 0)
        throw std::runtime_error("invalid frame metadata");
    if (previous && (i.id <= previous->id || i.time <= previous->time || i.step < previous->step ||
                     (i.step == previous->step && i.increment <= previous->increment)))
        throw std::runtime_error("duplicate or nonmonotonic result frame");
    std::set<std::tuple<std::string, std::string, bool>> keys;
    for (auto &field : f.fields) {
        auto &d = field.descriptor;
        if (!keys.emplace(d.name, d.region, d.history).second || d.components.empty() ||
            d.components.size() > 9 ||
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
        for (auto v : field.values)
            if (!std::isfinite(v))
                throw std::runtime_error("nonfinite result");
    }
}
class ResultReader {
    std::filesystem::path path_;
    std::uint64_t limit_;
    std::vector<native::Record> records_;

  public:
    ResultModel model;
    std::vector<FrameInfo> index;
    std::uint64_t valid_end = 0;
    bool incomplete_tail = false;
    explicit ResultReader(const std::filesystem::path &path, bool allow_incomplete_tail = false,
                          std::uint64_t limit = native::default_limit)
        : path_(path), limit_(limit) {
        std::ifstream in(path, std::ios::binary);
        native::prefix(in, result_magic);
        model = native::unpack<ResultModel>(native::record(in, 0, limit), limit);
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
    ResultFrame frame(std::size_t i) const {
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
        auto it =
            std::lower_bound(index.begin(), index.end(), time, [](auto &i, double t) { return i.time < t; });
        return static_cast<std::size_t>(it - index.begin());
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
    ResultField field(std::size_t frame_index, const std::string &name, const std::string &region_name = "",
                      bool history = false) const {
        auto f = frame(frame_index);
        std::optional<ResultField> found;
        std::vector<const ResultField *> matching;
        for (auto &v : f.fields)
            if (v.descriptor.name == upper(name) && v.descriptor.history == history) {
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
                    part->descriptor.location != combined.descriptor.location)
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
    static std::vector<std::pair<double, double>>
    history(const ResultReader &reader, const std::string &field, Id entity, unsigned component = 0,
            const std::string &region = "", bool history_request = false) {
        if (!region.empty())
            (void)reader.region(field == "U" || field == "V" || field == "A" || field == "RF", region);
        std::vector<std::pair<double, double>> values;
        for (std::size_t i = 0; i < reader.index.size(); ++i) {
            ResultField f;
            try {
                f = reader.field(i, field, region, history_request);
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
    std::ofstream out_;
    ResultModel model_;
    std::optional<FrameInfo> last_;
    std::uint64_t limit_;
    bool failed_ = false;

  public:
    NativeResultStorage(const std::filesystem::path &path, ResultModel model, bool append = false,
                        bool recover = false, std::uint64_t limit = native::default_limit)
        : model_(std::move(model)), limit_(limit) {
        if (upper(path.extension().string()) == ".H5" || upper(path.extension().string()) == ".HDF5")
            throw std::invalid_argument("native backend requires a non-HDF5 suffix (.sdbres)");
        if (append) {
            ResultReader existing(path, recover, limit);
            if (native::pack(existing.model, limit_) != native::pack(model_, limit_))
                throw std::runtime_error("result append model/configuration mismatch");
            if (existing.incomplete_tail)
                std::filesystem::resize_file(path, existing.valid_end);
            if (!existing.index.empty())
                last_ = existing.index.back();
            out_.open(path, std::ios::binary | std::ios::app);
        } else {
            if (std::filesystem::exists(path))
                throw std::runtime_error("result exists; use explicit append or a new output path");
            out_.open(path, std::ios::binary | std::ios::trunc);
            native::prefix(out_, result_magic);
            native::record(out_, 0, native::pack(model_, limit_));
            flush();
        }
        if (!out_)
            throw std::runtime_error("cannot open result output");
    }
    void append(const ResultFrame &frame) override {
        if (failed_)
            throw std::runtime_error("result writer is failed");
        validate_frame(frame, model_, last_);
        auto bytes = native::pack(frame, limit_);
        try {
            native::record(out_, 1, bytes);
            flush();
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

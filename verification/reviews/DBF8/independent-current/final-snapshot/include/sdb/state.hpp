#pragma once
#include "model.hpp"
#include "registered_state.hpp"
#include <optional>

namespace sdb {
template <class T> struct CurrentCoordinates {
    View<const T, 3> reference, displacement;
    SDB_HD T operator()(std::size_t node, unsigned component) const {
        return reference(node, component) + displacement(node, component);
    }
};
template <class T> struct RotationState {
    Field<T, 3> angle, velocity, acceleration, inertia, inverse_inertia, torque;
    explicit RotationState(std::size_t n)
        : angle(n), velocity(n), acceleration(n), inertia(n), inverse_inertia(n), torque(n) {}
};
template <class T> struct StateData {
    Field<T, 3> displacement, velocity, acceleration, internal_force, external_force, contact_force;
    Field<T> mass, inverse_mass, stable_dt;
    Field<std::uint8_t> node_active, element_active;
    Field<T, 6> stress, strain;
    Field<T> history;
    std::optional<RotationState<T>> rotation;
    // Disengaged for the default mechanical state: no extension allocations.
    std::optional<RegisteredState> registered;
    std::uint64_t step = 0;
    double time = 0, dt = 0;
    template <class F> void fields(F &&f) {
        f(displacement);
        f(velocity);
        f(acceleration);
        f(internal_force);
        f(external_force);
        f(contact_force);
        f(mass);
        f(inverse_mass);
        f(stable_dt);
        f(node_active);
        f(element_active);
        f(stress);
        f(strain);
        f(history);
        if (rotation) {
            f(rotation->angle);
            f(rotation->velocity);
            f(rotation->acceleration);
            f(rotation->inertia);
            f(rotation->inverse_inertia);
            f(rotation->torque);
        }
    }
    template <class F> void fields(F &&f) const {
        f(displacement);
        f(velocity);
        f(acceleration);
        f(internal_force);
        f(external_force);
        f(contact_force);
        f(mass);
        f(inverse_mass);
        f(stable_dt);
        f(node_active);
        f(element_active);
        f(stress);
        f(strain);
        f(history);
        if (rotation) {
            f(rotation->angle);
            f(rotation->velocity);
            f(rotation->acceleration);
            f(rotation->inertia);
            f(rotation->inverse_inertia);
            f(rotation->torque);
        }
    }
};
template <class T> class StateDB {
    StateData<T> data_;
    std::optional<StateData<T>> backup_;
    std::vector<std::size_t> ip_offsets_{0}, history_offsets_{0};
    // Allocated only for heterogeneous/layered sample layouts.
    std::vector<std::size_t> point_history_offsets_;
    std::uint64_t model_id_;
    std::size_t node_count_;

  public:
    explicit StateDB(const MeshDB<T> &mesh, bool rotation = false)
        : model_id_(mesh.fingerprint()), node_count_(mesh.nodes()) {
        const auto n = mesh.nodes(), e = mesh.elements();
        data_.displacement.resize(n);
        data_.velocity.resize(n);
        data_.acceleration.resize(n);
        data_.internal_force.resize(n);
        data_.external_force.resize(n);
        data_.contact_force.resize(n);
        data_.mass.resize(n);
        data_.inverse_mass.resize(n);
        data_.stable_dt.resize(e);
        data_.node_active.resize(n);
        data_.node_active.fill(1);
        data_.element_active.resize(e);
        data_.element_active.fill(1);
        if (mesh.has_sample_layout()) point_history_offsets_.push_back(0);
        for (Index i = 0; i < e; ++i) {
            const auto &el = mesh.element(i);
            const auto points = mesh.sample_point_count(i);
            ip_offsets_.push_back(sum(ip_offsets_.back(), points));
            if (mesh.has_sample_layout()) {
                for (std::size_t q = 0; q < points; ++q)
                    point_history_offsets_.push_back(sum(point_history_offsets_.back(),
                        mesh.materials()[mesh.sample_point(i, q).material].history_variables));
                history_offsets_.push_back(point_history_offsets_.back());
            } else
                history_offsets_.push_back(sum(history_offsets_.back(),
                    product(points, mesh.materials()[el.material].history_variables)));
        }
        data_.stress.resize(ip_offsets_.back());
        data_.strain.resize(ip_offsets_.back());
        data_.history.resize(history_offsets_.back());
        auto history = data_.history.write();
        for (Index i = 0; i < e; ++i) {
            for (std::size_t ip = 0; ip < mesh.sample_point_count(i); ++ip) {
                const auto &material = mesh.materials()[mesh.sample_point(i, ip).material];
                if (material.history_schema.empty()) continue;
                auto address = point_history_offsets_.empty()
                    ? sum(history_offsets_[i], product(ip, material.history_variables))
                    : point_history_offsets_.at(ip_offsets_[i] + ip);
                for (const auto &variable : material.history_schema.variables)
                    for (double initial : variable.initial)
                        history(address++) = static_cast<T>(initial);
            }
        }
        if (rotation)
            data_.rotation.emplace(n);
    }
    StateData<T> &data() noexcept { return data_; }
    const StateData<T> &data() const noexcept { return data_; }
    const auto &ip_offsets() const noexcept { return ip_offsets_; }
    const auto &history_offsets() const noexcept { return history_offsets_; }
    const auto &point_history_offsets() const noexcept { return point_history_offsets_; }
    std::size_t history_address(const MeshDB<T> &mesh, Index element, std::size_t ip,
                                const std::string &name, std::size_t component = 0,
                                std::uint32_t layer = 0, std::uint32_t section_point = 0) const {
        if (ip >= mesh.element(element).integration_points) throw std::out_of_range("history integration point");
        for (std::size_t q = 0; q < mesh.sample_point_count(element); ++q) {
            const auto p = mesh.sample_point(element, q);
            if (p.integration_point == ip + 1 && p.layer == layer && p.section_point == section_point)
                return history_address_at(mesh, element, q, name, component);
        }
        throw std::out_of_range("history layer/section point");
    }
    std::size_t history_address_at(const MeshDB<T> &mesh, Index element, std::size_t point,
                                   const std::string &name, std::size_t component = 0) const {
        validate_shape();
        if (mesh.fingerprint() != model_id_)
            throw std::invalid_argument("history address model mismatch");
        const auto sample = mesh.sample_point(element, point);
        const auto &material = mesh.materials()[sample.material];
        auto offset = material.history_schema.offset(name);
        for (const auto &variable : material.history_schema.variables)
            if (variable.field.name == name) {
                if (component >= variable.field.component_count())
                    throw std::out_of_range("history component");
                const auto base = point_history_offsets_.empty()
                    ? sum(history_offsets_.at(element), product(point, material.history_variables))
                    : point_history_offsets_.at(ip_offsets_.at(element) + point);
                return sum(base, sum(offset, component));
            }
        throw std::out_of_range("unknown history variable");
    }
    std::uint64_t model_id() const noexcept { return model_id_; }
    template<class F> F &register_field(FieldDescriptor descriptor, std::size_t count,
            std::string schema_id, std::uint32_t schema_version = 1,
            std::vector<typename F::value_type> initial = {}, std::uint64_t basis_id = 0) {
        if (in_trial()) throw std::logic_error("cannot register state during trial");
        descriptor.validate();
        std::size_t expected;
        if (descriptor.association == Association::Node && descriptor.location == FieldLocation::Entity)
            expected = node_count_;
        else if (descriptor.association == Association::Element) {
            if (descriptor.location == FieldLocation::Entity) expected = ip_offsets_.size()-1;
            else if (descriptor.location == FieldLocation::IntegrationPoint || descriptor.location == FieldLocation::SectionPoint)
                expected = ip_offsets_.back();
            else throw std::invalid_argument("registered element-node addressing unsupported");
        } else if (descriptor.association == Association::Global && descriptor.location == FieldLocation::Entity)
            expected = 1;
        else throw std::invalid_argument("registered state association/location unsupported");
        if (count != expected) throw std::invalid_argument("registered state entity/sample count mismatch");
        if (!data_.registered) {
            RegisteredState next;
            auto &field = next.template add<F>(std::move(descriptor),count,std::move(schema_id),schema_version,std::move(initial),basis_id);
            data_.registered.emplace(std::move(next));
            return field;
        }
        return data_.registered->template add<F>(std::move(descriptor),count,std::move(schema_id),schema_version,std::move(initial),basis_id);
    }
    void freeze_registered_fields() {
        if (in_trial()) throw std::logic_error("cannot freeze state during trial");
        if (data_.registered) data_.registered->freeze();
    }
    // Explicit addresses allow partial samples, element-node, face and contact fields.
    // Topology is checked once here; kernels use the existing flat Field views.
    template<class F> F &register_addressed_field(const MeshDB<T> &mesh, FieldDescriptor descriptor,
            StateLayout layout, std::string schema_id, std::uint32_t schema_version=1,
            std::vector<typename F::value_type> initial={}, std::uint64_t basis_id=0) {
        if(in_trial() || mesh.fingerprint()!=model_id_) throw std::invalid_argument("addressed state model/phase");
        layout.validate(descriptor);
        auto endpoint=[&](const lifecycle::ResultEndpoint &p) {
            if(p.kind==0) { if(p.entity>mesh.nodes()) throw std::out_of_range("state endpoint node"); }
            else {
                if(p.entity>mesh.elements()) throw std::out_of_range("state endpoint element");
                const auto e=static_cast<Index>(p.entity-1);
                if(p.kind==1 && p.local>mesh.connectivity(e).size()) throw std::out_of_range("state local node");
                if(p.kind==2 && p.local>65536) throw std::out_of_range("state local face width");
                if(p.kind==2) mesh.validate_face({mesh.element(e).id,static_cast<std::uint16_t>(p.local-1),static_cast<std::int8_t>(p.side)});
            }
        };
        for(const auto &a:layout.addresses) {
            if(descriptor.association==Association::Node) endpoint({0,a.entity,0,0});
            if(descriptor.association==Association::Element || descriptor.association==Association::Face) {
                if(a.entity>mesh.elements()) throw std::out_of_range("state address element");
                const auto e=static_cast<Index>(a.entity-1);
                if(a.element_node) endpoint({1,a.entity,a.element_node,0});
                if(a.face) endpoint({2,a.entity,a.face,a.side});
                if(a.integration_point) {
                    bool found=false;
                    for(std::size_t q=0;q<mesh.sample_point_count(e);++q) {
                        const auto p=mesh.sample_point(e,q);
                        found |= p.integration_point==a.integration_point && p.layer==a.layer && p.section_point==a.section_point;
                    }
                    if(!found) throw std::out_of_range("state sample not in mesh layout");
                }
            }
            if(descriptor.association==Association::Contact) { endpoint(a.endpoint_a); endpoint(a.endpoint_b); }
        }
        const auto n=layout.addresses.size();
        if(!data_.registered) {
            RegisteredState next;
            auto &field=next.template add<F>(std::move(descriptor),n,std::move(schema_id),schema_version,std::move(initial),basis_id,std::move(layout));
            data_.registered.emplace(std::move(next)); return field;
        }
        return data_.registered->template add<F>(std::move(descriptor),n,std::move(schema_id),schema_version,std::move(initial),basis_id,std::move(layout));
    }
    const RegisteredState *registered_fields() const noexcept {
        return data_.registered ? &*data_.registered : nullptr;
    }
    template<class F> F &registered_field(const std::string &name) {
        if (!data_.registered) throw std::out_of_range("no registered fields");
        return data_.registered->template get<F>(name);
    }
    template<class F> const F &registered_field(const std::string &name) const {
        if (!data_.registered) throw std::out_of_range("no registered fields");
        return data_.registered->template get<F>(name);
    }
    Field<std::uint8_t> &registered_validity(const std::string &name) {
        if (!data_.registered) throw std::out_of_range("no registered fields");
        return data_.registered->find(name).validity_field();
    }
    // Restore creates initialized, independent extension storage before publication.
    void copy_registered_layout(const StateDB &source) {
        if (in_trial()) throw std::logic_error("cannot replace registered layout during trial");
        if (model_id_ != source.model_id_ || node_count_ != source.node_count_)
            throw std::invalid_argument("registered layout model mismatch");
        if (source.data_.registered) data_.registered = source.data_.registered->initialized_copy();
        else data_.registered.reset();
    }
    bool in_trial() const noexcept { return backup_.has_value(); }
    void validate_shape() const {
        if (data_.registered) data_.registered->validate_shape();
        if (data_.displacement.size() != node_count_)
            throw std::logic_error("node field shape changed");
        const auto n = data_.displacement.size(), e = ip_offsets_.size() - 1;
        if (data_.velocity.size() != n || data_.acceleration.size() != n ||
            data_.internal_force.size() != n || data_.external_force.size() != n ||
            data_.contact_force.size() != n || data_.mass.size() != n || data_.inverse_mass.size() != n ||
            data_.node_active.size() != n || data_.stable_dt.size() != e ||
            data_.element_active.size() != e || data_.stress.size() != ip_offsets_.back() ||
            data_.strain.size() != ip_offsets_.back() || data_.history.size() != history_offsets_.back())
            throw std::logic_error("state field shape changed");
        if (data_.rotation) {
            const auto &r = *data_.rotation;
            if (r.angle.size() != n || r.velocity.size() != n || r.acceleration.size() != n ||
                r.inertia.size() != n || r.inverse_inertia.size() != n || r.torque.size() != n)
                throw std::logic_error("rotation shape changed");
        }
    }
    void begin_trial() {
        if (backup_)
            throw std::logic_error("trial already active");
        validate_shape();
        backup_ = data_;
    }
    void commit() {
        if (!backup_)
            throw std::logic_error("no trial");
        backup_.reset();
    }
    void rollback() {
        if (!backup_)
            throw std::logic_error("no trial");
        data_ = std::move(*backup_);
        backup_.reset();
    }
    T current(const MeshDB<T> &mesh, Index node, unsigned c) const {
        if (node >= mesh.nodes() || c >= 3 || mesh.fingerprint() != model_id_)
            throw std::invalid_argument("current coordinate model/index mismatch");
        return mesh.reference()(node, c) + data_.displacement.read()(node, c);
    }
    CurrentCoordinates<T> current_view(const MeshDB<T> &mesh) const {
        validate_shape();
        if (mesh.fingerprint() != model_id_)
            throw std::invalid_argument("current coordinate model mismatch");
        return {mesh.reference(), data_.displacement.read()};
    }
};
} // namespace sdb

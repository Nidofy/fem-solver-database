#pragma once
#include "model.hpp"
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
        for (Index i = 0; i < e; ++i) {
            const auto &el = mesh.element(i);
            ip_offsets_.push_back(sum(ip_offsets_.back(), el.integration_points));
            history_offsets_.push_back(
                sum(history_offsets_.back(),
                    product(el.integration_points, mesh.materials()[el.material].history_variables)));
        }
        data_.stress.resize(ip_offsets_.back());
        data_.strain.resize(ip_offsets_.back());
        data_.history.resize(history_offsets_.back());
        auto history = data_.history.write();
        for (Index i = 0; i < e; ++i) {
            const auto &el = mesh.element(i);
            const auto &material = mesh.materials()[el.material];
            if (material.history_schema.empty()) continue;
            for (std::size_t ip = 0; ip < el.integration_points; ++ip) {
                auto address = sum(history_offsets_[i], product(ip, material.history_variables));
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
    std::size_t history_address(const MeshDB<T> &mesh, Index element, std::size_t ip,
                                const std::string &name, std::size_t component = 0) const {
        validate_shape();
        if (mesh.fingerprint() != model_id_)
            throw std::invalid_argument("history address model mismatch");
        const auto &el = mesh.element(element);
        if (ip >= el.integration_points) throw std::out_of_range("history integration point");
        const auto &material = mesh.materials()[el.material];
        auto offset = material.history_schema.offset(name);
        for (const auto &variable : material.history_schema.variables)
            if (variable.field.name == name) {
                if (component >= variable.field.component_count())
                    throw std::out_of_range("history component");
                return sum(history_offsets_.at(element),
                           sum(product(ip, material.history_variables), sum(offset, component)));
            }
        throw std::out_of_range("unknown history variable");
    }
    std::uint64_t model_id() const noexcept { return model_id_; }
    bool in_trial() const noexcept { return backup_.has_value(); }
    void validate_shape() const {
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

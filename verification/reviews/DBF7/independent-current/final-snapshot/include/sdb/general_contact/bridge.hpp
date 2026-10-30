#pragma once
#include "mechanics.hpp"

namespace sdb::general_contact {
// Canonical state token, computed only at accepted barriers/restart, not per contact point.
template <class T> std::uint64_t state_fingerprint(const StateDB<T> &state) {
    state.validate_shape();
    std::uint64_t hash = 14695981039346656037ull;
    auto scalar = [&]<class X>(X x) {
        if constexpr (std::is_floating_point_v<X>)
            finite(x);
        auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(X)>>(x);
        if constexpr (std::endian::native == std::endian::big)
            std::reverse(bytes.begin(), bytes.end());
        for (auto byte : bytes) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
    };
    scalar(std::uint32_t(1));
    scalar(std::uint32_t(sizeof(T)));
    scalar(state.model_id());
    const auto &data = state.data();
    scalar(data.step);
    scalar(data.time);
    scalar(data.dt);
    scalar(std::uint8_t(data.rotation.has_value()));
    data.fields([&](const auto &field) {
        scalar(std::uint64_t(field.size()));
        scalar(std::uint32_t(std::decay_t<decltype(field)>::components));
        auto v = field.read();
        for (unsigned c = 0; c < std::decay_t<decltype(field)>::components; ++c)
            for (std::size_t i = 0; i < field.size(); ++i)
                scalar(v(i, c));
    });
    return hash;
}
template <class T> Binding bind(const StateDB<T> &state, std::uint64_t configuration) {
    if (state.data().time < 0 || state.data().dt < 0 || (state.data().step && state.data().dt == 0))
        throw std::invalid_argument("invalid contact/mechanical time metadata");
    return {state.model_id(), configuration, state_fingerprint(state), state.data().step, state.data().time};
}
// Single-owner RAII transaction. State/contact must not be independently committed while active.
// The legacy ContactDB is not used or mutated by this independent module.
template <class T> class CoupledTrial {
    StateDB<T> &state_;
    Database<T> &contact_;
    bool active_ = false;

  public:
    CoupledTrial(StateDB<T> &state, Database<T> &contact) : state_(state), contact_(contact) {
        if (state.in_trial() || contact.in_trial() ||
            bind(state, contact.binding().configuration) != contact.binding())
            throw std::logic_error("contact/mechanical transaction origin mismatch");
        state_.begin_trial();
        try {
            contact_.begin_trial();
            active_ = true;
        } catch (...) {
            state_.rollback();
            throw;
        }
    }
    CoupledTrial(const CoupledTrial &) = delete;
    CoupledTrial &operator=(const CoupledTrial &) = delete;
    ~CoupledTrial() {
        if (active_) {
            if (contact_.in_trial())
                contact_.rollback();
            if (state_.in_trial())
                state_.rollback();
        }
    }
    void apply_force(const Frame<T> &frame) {
        if (!active_ || !state_.in_trial() || !contact_.in_trial() || frame.origin != contact_.binding() ||
            frame.state_epoch != contact_.epoch())
            throw std::logic_error("stale contact force frame");
        state_.validate_shape();
        if (frame.nodal_force.size() != state_.data().contact_force.size())
            throw std::invalid_argument("contact force shape");
        for (const auto &force : frame.nodal_force)
            for (T x : force)
                finite(x);
        auto out = state_.data().contact_force.write();
        for (std::size_t i = 0; i < out.count; ++i)
            for (unsigned c = 0; c < 3; ++c)
                out(i, c) = frame.nodal_force[i][c];
    }
    Binding commit() {
        if (!active_ || !state_.in_trial() || !contact_.in_trial())
            throw std::logic_error("contact transaction phases differ");
        auto next = bind(state_, contact_.binding().configuration);
        contact_.validate_commit(next);
        // Both commits release prepared state only; no allocation or numerical work after preflight.
        state_.commit();
        contact_.commit(next);
        active_ = false;
        return next;
    }
    void rollback() {
        if (!active_ || !state_.in_trial() || !contact_.in_trial())
            throw std::logic_error("contact transaction phases differ");
        contact_.rollback();
        state_.rollback();
        active_ = false;
    }
};
} // namespace sdb::general_contact

#pragma once
#include "model.hpp"

namespace sdb {
// Fixed reference geometry and run-wide controls. An empty selection means all elements.
struct ExplicitControls {
    bool refined_tet_dt = false;
    double mass_factor = 1, target_element_dt = 0;
    std::vector<Index> mass_elements;
    double max_added_mass_fraction = std::numeric_limits<double>::max();
    double bulk_linear = 0, bulk_quadratic = 0, max_compression_rate = 0;
    template <class F> void visit(F f) {
        f(refined_tet_dt, mass_factor, target_element_dt, max_added_mass_fraction, mass_elements, bulk_linear,
          bulk_quadratic, max_compression_rate);
    }
    bool operator==(const ExplicitControls &) const = default;
    bool enabled() const {
        return refined_tet_dt || mass_factor != 1 || target_element_dt || bulk_linear || bulk_quadratic;
    }
    void validate(std::size_t elements) const {
        for (auto x : {mass_factor, target_element_dt, max_added_mass_fraction, bulk_linear, bulk_quadratic,
                       max_compression_rate})
            if (!std::isfinite(x) || x < 0)
                throw std::invalid_argument("invalid explicit control value");
        if (mass_factor < 1 || (mass_factor != 1 && target_element_dt) ||
            (bulk_quadratic > 0) != (max_compression_rate > 0))
            throw std::invalid_argument("incompatible mass scaling or quadratic viscosity controls");
        if (max_added_mass_fraction != std::numeric_limits<double>::max() && mass_factor == 1 &&
            !target_element_dt)
            throw std::invalid_argument("added mass budget requires mass scaling");
        if (!mass_elements.empty() &&
            ((mass_factor == 1 && !target_element_dt) || mass_elements.back() >= elements ||
             !std::is_sorted(mass_elements.begin(), mass_elements.end()) ||
             std::adjacent_find(mass_elements.begin(), mass_elements.end()) != mass_elements.end()))
            throw std::invalid_argument("invalid mass scaling element selection");
    }
};

struct ElementControl {
    double physical_mass, undamped_dt, scale, linear_viscosity, quadratic_viscosity;
};

class ExplicitControlPlan {
  public:
    ExplicitControls options;
    std::shared_ptr<const ExplicitControls> identity;
    std::vector<ElementControl> elements;
    double physical_mass = 0, added_mass = 0;
    Index controlling_element = 0;
    ExplicitControlPlan(ExplicitControls controls, std::size_t count) : options(std::move(controls)) {
        options.validate(count);
        if (options.enabled()) {
            identity = std::make_shared<const ExplicitControls>(options);
            elements.resize(count);
        }
    }
    void require_binding(const std::shared_ptr<const ExplicitControls> &bound) const {
        if (bound != identity && (!bound || !identity || *bound != *identity))
            throw std::invalid_argument("state belongs to a different explicit control configuration");
    }

    template <class T> T configure(Index e, T volume, T rho, T lambda, T mu, T &dt) {
        if (elements.empty())
            return T(1);
        double h = dt, V = volume, density = rho;
        if (!(h > 0 && V > 0 && density > 0) || !std::isfinite(h))
            throw std::invalid_argument("invalid reference element for explicit controls");
        double length = std::cbrt(V);
        double c = std::sqrt((double(lambda) + 2 * double(mu)) / density);
        double linear = options.bulk_linear * density * c * length;
        double bL = options.bulk_quadratic * length;
        double quadratic = density * bL * bL;
        // C <= (eta_max / bulk_modulus) K_elastic as quadratic forms.
        // omega <= 2/h. Bound the backward-velocity central difference step.
        if (linear || quadratic) {
            double bulk = double(lambda) + 2 * double(mu) / 3;
            if (!(bulk > 0))
                throw std::invalid_argument("invalid bulk modulus for viscosity");
            double ratio = (linear + 2 * quadratic * options.max_compression_rate) / (bulk * h);
            h /= std::hypot(1., ratio) + ratio;
        }
        bool selected = options.mass_elements.empty() ||
                        std::binary_search(options.mass_elements.begin(), options.mass_elements.end(), e);
        double factor = selected ? options.mass_factor : 1.;
        if (selected && options.target_element_dt > h) {
            double ratio = options.target_element_dt / h;
            factor = ratio * ratio;
        }
        if (!std::isfinite(factor) || factor > std::numeric_limits<T>::max())
            throw std::invalid_argument("mass factor exceeds model precision");
        T applied = T(factor);
        double mass = density * V;
        elements.at(e) = {mass, dt, double(applied), linear, quadratic};
        physical_mass += mass;
        added_mass += mass * (double(applied) - 1);
        double scaled_dt = h * std::sqrt(double(applied));
        if (scaled_dt > std::numeric_limits<T>::max())
            throw std::invalid_argument("element dt overflow");
        dt = T(scaled_dt);
        if (!std::isfinite(physical_mass) || !std::isfinite(added_mass) || !(dt > 0) || !std::isfinite(dt) ||
            !std::isfinite(linear) || !std::isfinite(quadratic))
            throw std::invalid_argument("unrepresentable explicit controls");
        return applied;
    }
};

// Element stiffness row-sum bound for the equally lumped TET4. V cancels against rho V/4.
template <class T> T tet_row_sum_step(std::size_t e, View<T, 12> gradients, T lambda, T mu, T rho) {
    double max_row = 0;
    for (unsigned a = 0; a < 4; ++a)
        for (unsigned i = 0; i < 3; ++i) {
            double row = 0;
            for (unsigned b = 0; b < 4; ++b) {
                double dot = 0;
                for (unsigned k = 0; k < 3; ++k)
                    dot += double(gradients(e, 3 * a + k)) * gradients(e, 3 * b + k);
                for (unsigned j = 0; j < 3; ++j)
                    row += std::abs(double(lambda) * gradients(e, 3 * a + i) * gradients(e, 3 * b + j) +
                                    double(mu) * gradients(e, 3 * a + j) * gradients(e, 3 * b + i) +
                                    (i == j ? mu * dot : 0.));
            }
            max_row = std::max(row, max_row);
        }
    return T(1.9 / std::sqrt(4 * max_row / rho));
}

template <class T, std::size_t N>
double add_bulk_force(const ElementControl &data, double compression_limit, View<const Index, N> nodes,
                    View<const T, 3 * N> grad, View<const T, 3> velocity, View<const std::uint8_t, 3> fixed,
                    View<const std::uint8_t> active_nodes, View<T, 3 * N> local, std::size_t element,
                    std::size_t point, T weight) {
    double rate = 0;
    for (unsigned a = 0; a < N; ++a)
        for (unsigned i = 0; i < 3; ++i)
            if (active_nodes(nodes(element, a)) && !fixed(nodes(element, a), i))
                rate += double(grad(point, 3 * a + i)) * velocity(nodes(element, a), i);
    // Saturate the quadratic coefficient at the requested compression rate. This
    // keeps the force continuous and the tangent bounded for any next velocity.
    double compression = std::clamp(-rate, 0., compression_limit);
    double pressure = rate * (data.linear_viscosity + data.quadratic_viscosity * compression);
    for (unsigned a = 0; a < N; ++a)
        for (unsigned i = 0; i < 3; ++i) {
            auto &force = local(element, 3 * a + i);
            force = T(force + weight * pressure * grad(point, 3 * a + i));
            if (!std::isfinite(force))
                throw std::domain_error("nonfinite viscous force");
        }
    return double(weight)*pressure*rate;
}
} // namespace sdb

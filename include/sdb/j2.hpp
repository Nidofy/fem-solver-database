#pragma once
#include "model.hpp"

namespace sdb {
inline constexpr std::uint32_t j2_model = 0x4a320001;
template <class T> HistorySchema j2_schema() {
    HistorySchema h;
    h.id = "sdb.j2.isotropic";
    HistoryVariable pe, eq;
    pe.field.name = "PE";
    pe.field.scalar = eq.field.scalar = scalar_type<T>();
    pe.field.shape = {6};
    pe.field.components = {"XX", "YY", "ZZ", "XY", "YZ", "ZX"};
    pe.field.shear = ShearConvention::Tensor;
    pe.initial.assign(6, 0);
    eq.field.name = "PEEQ";
    h.variables = {pe, eq};
    return h;
}

// Parameters are rho, E, nu followed by ordered (yield stress, equivalent plastic strain) pairs.
template <class T> void validate_j2(const Material<T> &m) {
    const auto &p = m.parameters;
    if (m.model != j2_model || m.history_schema.template validate<T>() != 7 || p.size() < 5 ||
        p.size() % 2 != 1 || !(p[0] > 0 && p[1] > 0 && p[2] > -1 && p[2] < .5) || p[4] != 0 ||
        m.history_variables != 7 || m.history_schema.id != "sdb.j2.isotropic" ||
        m.history_schema.version != 1 || m.history_schema.variables.size() != 2)
        throw std::invalid_argument("invalid J2 parameters or history layout");
    const auto &pe = m.history_schema.variables[0].field, &eq = m.history_schema.variables[1].field;
    if (pe.name != "PE" || pe.components != std::vector<std::string>{"XX", "YY", "ZZ", "XY", "YZ", "ZX"} ||
        pe.shear != ShearConvention::Tensor || eq.name != "PEEQ" || eq.component_count() != 1)
        throw std::invalid_argument("invalid J2 tensor layout");
    if (!std::isfinite(p[0]) || !std::isfinite(3 * (double(p[1]) / (2 * (1 + double(p[2]))))) ||
        !std::isfinite(double(p[1]) / (3 * (1 - 2 * double(p[2])))))
        throw std::invalid_argument("unrepresentable J2 elastic constants");
    for (std::size_t i = 3; i < p.size(); i += 2)
        if (!std::isfinite(p[i]) || !std::isfinite(p[i + 1]) || !(p[i] > 0) ||
            (i > 3 && (!(p[i + 1] > p[i - 1]) || p[i] < p[i - 2] ||
                       !std::isfinite((double(p[i]) - p[i - 2]) / (double(p[i + 1]) - p[i - 1])))))
            throw std::invalid_argument("invalid isotropic yield table");
}

template <class T> struct J2Trial {
    std::array<T, 6> stress{};
    std::array<T, 7> history{};
    double elastic_energy = 0;
};

// Backward-Euler associative return. The trial is a value and has no side effects.
template <class T> class J2Law {
    std::vector<T> parameters;

  public:
    explicit J2Law(const Material<T> &m) : parameters(m.parameters) { validate_j2(m); }
    J2Trial<T> trial(const std::array<T, 6> &strain, const std::array<T, 7> &committed) const {
        const double E = parameters[1], nu = parameters[2], mu = E / (2 * (1 + nu));
        const double bulk = E / (3 * (1 - 2 * nu));
        const double old_eq = committed[6];
        if (!(old_eq >= 0) || !std::isfinite(old_eq))
            throw std::domain_error("invalid committed PEEQ");
        double ee[6], dev[6];
        for (unsigned c = 0; c < 6; ++c)
            ee[c] = double(strain[c]) - committed[c];
        const double trace = ee[0] + ee[1] + ee[2];
        double norm = 0;
        for (unsigned c = 0; c < 6; ++c) {
            dev[c] = 2 * mu * (ee[c] - (c < 3 ? trace / 3 : 0));
            norm = std::hypot(norm, dev[c] * (c < 3 ? 1. : std::sqrt(2.)));
        }
        const double q = std::sqrt(1.5) * norm;
        // Solve q - 3 mu * delta = yield(old_eq + delta) on its linear segment.
        double delta = std::max(0., (q - parameters[parameters.size() - 2]) / (3 * mu));
        for (std::size_t i = 3; i + 3 < parameters.size(); i += 2) {
            const double start = parameters[i + 1], end = parameters[i + 3];
            if (old_eq >= end || q - 3 * mu * (end - old_eq) > parameters[i + 2])
                continue;
            const double slope = (double(parameters[i + 2]) - parameters[i]) / (end - start);
            delta = std::max(0., (q - parameters[i] - slope * (old_eq - start)) / (3 * mu + slope));
            break;
        }
        if (delta <= 8 * std::numeric_limits<T>::epsilon() * (q / (3 * mu)))
            delta = 0;
        J2Trial<T> out;
        out.history[6] = T(old_eq + delta);
        for (unsigned c = 0; c < 6; ++c) {
            const double dep = delta > 0 ? 1.5 * delta * (dev[c] / q) : 0.;
            out.history[c] = T(committed[c] + dep);
            out.stress[c] = T((c < 3 ? bulk * trace : 0.) + dev[c] - 2 * mu * dep);
            out.elastic_energy += (c < 3 ? .5 : 1.) * out.stress[c] * (ee[c] - dep);
            if (!std::isfinite(out.history[c]) || !std::isfinite(out.stress[c]))
                throw std::domain_error("nonfinite J2 trial");
        }
        if (!std::isfinite(out.history[6]) || !std::isfinite(out.elastic_energy))
            throw std::domain_error("nonfinite J2 history or energy");
        return out;
    }
};
} // namespace sdb

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace sdb::physics {

// Committed deformation gradient (row-major) and spatial Cauchy stress
// (xx, yy, zz, xy, yz, zx). Keep one value per material point.
template <class T> struct GreenNaghdiPoint {
    std::array<T, 9> F{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<T, 6> stress{};
    template <class Visitor> void visit(Visitor f) { f(F, stress); }
};

namespace gn_detail {
using Mat = std::array<double, 9>;
inline Mat transpose(const Mat &a) {
    return {a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8]};
}
inline Mat multiply(const Mat &a, const Mat &b) {
    Mat result{};
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            for (unsigned k = 0; k < 3; ++k)
                result[3 * i + j] += a[3 * i + k] * b[3 * k + j];
    return result;
}
inline double scale(const Mat &a) {
    double s = 0;
    for (auto v : a) {
        if (!std::isfinite(v))
            throw std::domain_error("nonfinite objective-rate tensor");
        s = std::max(s, std::abs(v));
    }
    return s;
}
inline Mat inverse_transpose(Mat a) {
    const auto s = scale(a);
    if (!(s > 0))
        throw std::domain_error("zero deformation/stretch");
    for (auto &v : a)
        v /= s;
    Mat c{};
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            c[3 * i + j] = a[3 * ((i + 1) % 3) + (j + 1) % 3] * a[3 * ((i + 2) % 3) + (j + 2) % 3] -
                           a[3 * ((i + 1) % 3) + (j + 2) % 3] * a[3 * ((i + 2) % 3) + (j + 1) % 3];
    const auto det = a[0] * c[0] + a[1] * c[1] + a[2] * c[2];
    if (!(det > 64 * std::numeric_limits<double>::epsilon()))
        throw std::domain_error("inverted or ill-conditioned deformation/stretch");
    for (auto &v : c)
        v = (v / det) / s;
    scale(c);
    return c;
}
inline Mat rotation(Mat x) {
    const double s = scale(x);
    if (!(s > 0))
        throw std::domain_error("zero deformation gradient");
    for (auto &v : x)
        v /= s;
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        const auto invT = inverse_transpose(x);
        Mat next{};
        double error = 0;
        for (unsigned i = 0; i < 9; ++i) {
            next[i] = .5 * (x[i] + invT[i]);
            error = std::max(error, std::abs(next[i] - x[i]));
        }
        x = next;
        if (error <= 8 * std::numeric_limits<double>::epsilon())
            return x;
    }
    throw std::domain_error("polar rotation did not converge");
}
// Integrate sym(dU U^-1) along the straight right-stretch segment. Rigid
// rotation has dU=0 even if the initial spatial stress is nonzero.
inline Mat integrated_rate(const Mat &start, const Mat &end) {
    Mat delta{};
    for (unsigned k = 0; k < 9; ++k)
        delta[k] = end[k] - start[k];
    if (scale(delta) == 0)
        return {};
    Mat previous{};
    for (unsigned n = 1; n <= 1024; n *= 2) {
        Mat integral{};
        for (unsigned cell = 0; cell < n; ++cell)
            for (double xi : {-.57735026918962576451, .57735026918962576451}) {
                double t = (cell + .5 * (1 + xi)) / n;
                Mat stretch{};
                for (unsigned k = 0; k < 9; ++k)
                    stretch[k] = std::lerp(start[k], end[k], t);
                auto rate = multiply(delta, transpose(inverse_transpose(stretch)));
                for (unsigned i = 0; i < 3; ++i)
                    for (unsigned j = 0; j < 3; ++j)
                        integral[3 * i + j] += (rate[3 * i + j] + rate[3 * j + i]) * (.25 / n);
            }
        double error = 0;
        for (unsigned k = 0; k < 9; ++k)
            error = std::max(error, std::abs(integral[k] - previous[k]));
        if (n > 1 && error <= 2e-13 * std::max(1., scale(integral)))
            return integral;
        previous = integral;
    }
    throw std::domain_error("objective-rate increment requires subdivision");
}
} // namespace gn_detail

template <class T>
GreenNaghdiPoint<T> green_naghdi_trial(const GreenNaghdiPoint<T> &old, const std::array<T, 9> &next_F,
                                       double bulk, double shear) {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
    if (!(bulk > 0 && shear > 0 && std::isfinite(bulk) && std::isfinite(shear)))
        throw std::invalid_argument("invalid objective-rate moduli");
    using namespace gn_detail;
    Mat f0{}, f1{};
    std::copy(old.F.begin(), old.F.end(), f0.begin());
    std::copy(next_F.begin(), next_F.end(), f1.begin());
    const Mat r0 = rotation(f0), r1 = rotation(f1);
    auto de = integrated_rate(multiply(transpose(r0), f0), multiply(transpose(r1), f1));
    const auto &s = old.stress;
    Mat spatial{s[0], s[3], s[5], s[3], s[1], s[4], s[5], s[4], s[2]};
    scale(spatial);
    auto corotated = multiply(multiply(transpose(r0), spatial), r0);
    double trace = de[0] + de[4] + de[8];
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            corotated[3 * i + j] +=
                2 * shear * de[3 * i + j] + (i == j ? (bulk - 2 * shear / 3) * trace : 0.);
    spatial = multiply(multiply(r1, corotated), transpose(r1));
    if (scale(spatial) > std::numeric_limits<T>::max())
        throw std::overflow_error("objective-rate stress overflow");
    return {next_F,
            {T(spatial[0]), T(spatial[4]), T(spatial[8]), T(spatial[1]), T(spatial[5]), T(spatial[6])}};
}
} // namespace sdb::physics

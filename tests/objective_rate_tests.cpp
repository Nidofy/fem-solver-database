#include <sdb/physics/objective_rate.hpp>
#include <iostream>
using namespace sdb::physics;

template <class T> void check_point() {
    using M = std::array<T, 9>;
    const double tol = sizeof(T) == 4 ? 3e-5 : 2e-11;
    auto close = [&](double a, double b) {
        if (!(std::abs(a - b) < tol))
            throw std::runtime_error("objective-rate oracle");
    };
    GreenNaghdiPoint<T> old;
    old.F = {2, 0, 0, 0, 1, 0, 0, 0, 1};
    old.stress = {4, 2, 1, 3, 2, 1};
    // Q is a +90-degree rotation about z. Q F_old has identical right stretch.
    auto rigid = green_naghdi_trial(old, M{0, -1, 0, 2, 0, 0, 0, 0, 1}, 4, 3);
    std::array<double, 6> expected{2, 4, 1, -3, 1, -2};
    for (unsigned i = 0; i < 6; ++i)
        close(rigid.stress[i], expected[i]);

    // U(t)=[[2,b*t],[b*t,1]]. Principal stretch axes turn. Closed integral
    // of sym(dU U^-1) has equal normal components and nonzero shear.
    const T b = T(.4);
    M finish{2, b, 0, b, 1, 0, 0, 0, 1};
    double normal = .5 * std::log1p(-double(b) * b / 2.);
    double xy = 1.5 / std::sqrt(2.) * std::atanh(b / std::sqrt(2.));
    auto trial = green_naghdi_trial(old, finish, 4, 3);
    close(trial.stress[0], 4 + 10 * normal);
    close(trial.stress[1], 2 + 10 * normal);
    close(trial.stress[2], 1 + 4 * normal);
    close(trial.stress[3], 3 + 6 * xy);
    auto mid = green_naghdi_trial(old, M{2, b / 2, 0, b / 2, 1, 0, 0, 0, 1}, 4, 3);
    auto split = green_naghdi_trial(mid, finish, 4, 3);
    auto returned = green_naghdi_trial(trial, old.F, 4, 3);
    for (unsigned i = 0; i < 6; ++i) {
        close(split.stress[i], trial.stress[i]);
        close(returned.stress[i], old.stress[i]);
    }
    // Observer rotates differently at the two endpoints. Q0=90 degrees, Q1=180.
    auto observed = green_naghdi_trial(rigid, M{-2, -b, 0, -b, -1, 0, 0, 0, 1}, 4, 3);
    for (unsigned i = 0; i < 6; ++i)
        close(observed.stress[i], (i == 4 || i == 5 ? -1. : 1.) * trial.stress[i]);
    auto committed = old;
    try {
        green_naghdi_trial(committed, M{-1, 0, 0, 0, 1, 0, 0, 0, 1}, 4, 3);
        throw std::runtime_error("accepted reflection");
    } catch (const std::domain_error &) {
    }
    if (committed.F != old.F || committed.stress != old.stress)
        throw std::runtime_error("rejected trial mutated history");
}
int main() {
    check_point<float>();
    check_point<double>();
    std::cout << "Green-Naghdi transport and split history PASS" << std::endl;
}

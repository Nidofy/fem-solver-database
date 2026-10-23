#include "sdb/general_contact/module.hpp"
#include <iostream>
using namespace sdb;
namespace gc = sdb::general_contact;

// Two Hex8 surface carriers; the lower block is prescribed and the upper block has
// one rigid translational DOF. This isolates contact, not Hex8 continuum mechanics.
MeshDB<double> mesh_pair() {
    ModelBuilder<double> b;
    b.add_material({1, 1, {1, 100, .2}, 0});
    b.add_part(1);
    for (unsigned body = 0; body < 2; ++body) {
        const double z = body ? .1 : -1.;
        std::array<gc::Vec<double>, 8> x{{{0, 0, z},
                                          {1, 0, z},
                                          {1, 1, z},
                                          {0, 1, z},
                                          {0, 0, z + 1},
                                          {1, 0, z + 1},
                                          {1, 1, z + 1},
                                          {0, 1, z + 1}}};
        std::vector<Id> ids;
        for (unsigned j = 0; j < 8; ++j) {
            Id id = 8 * body + j + 1;
            b.add_node(id, x[j]);
            ids.push_back(id);
        }
        b.add_element(body + 1, Topology::Hex8, ids, 1, 1);
    }
    return b.finalize();
}
gc::Definition<double> definition(const MeshDB<double> &mesh) {
    gc::Definition<double> d;
    d.mesh = mesh.fingerprint();
    d.properties = {{1, 1000, 0, 0}};
    d.rules = {{1, 0, 0, 1, 0}};
    d.controls.band = .2;
    return d;
}
void position(StateDB<double> &state, double q) {
    for (Index i = 8; i < 16; ++i)
        state.data().displacement.write()(i, 2) = q;
}
double resultant(const gc::Frame<double> &f) {
    double force = 0;
    for (Index i = 8; i < 16; ++i)
        force += f.nodal_force[i][2];
    return force;
}
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    try {
        const auto folder =
            argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("general-domain-output");
        std::filesystem::create_directories(folder);
        auto mesh = mesh_pair();
        auto def = definition(mesh);
        StateDB<double> implicit(mesh);
        gc::GeneralContact<double> contact(mesh, implicit, def);
        contact.begin_increment();
        implicit.data().dt = 1;
        implicit.data().time = 1;
        implicit.data().step = 1;
        double q = -.11;
        unsigned iterations = 0;
        for (; iterations < 8; ++iterations) {
            position(implicit, q);
            auto result = contact.evaluate(gc::Analysis::Implicit);
            const double residual = resultant(result.frame) - 50;
            if (std::abs(residual) < 1e-9)
                break;
            double tangent = 0;
            for (const auto &e : result.frame.tangent)
                for (Index i = 8; i < 16; ++i)
                    if (e.row == contact.dofs().equation(i, 2))
                        for (Index j = 8; j < 16; ++j)
                            if (e.column == contact.dofs().equation(j, 2))
                                tangent += e.value;
            require(tangent > 0, "singular reduced contact tangent");
            q += residual / tangent;
        }
        require(iterations < 8 && std::abs(q + .15) < 1e-10, "whole-domain implicit force equilibrium");
        contact.commit();
        contact.save(folder / "implicit.sdbcb");
        gc::save_model(folder / "model.sdbcm", contact.model());
        StateDB<double> explicit_state(mesh);
        for (Index i = 0; i < 16; ++i) {
            explicit_state.data().mass.write()(i) = .125;
            explicit_state.data().inverse_mass.write()(i) = 8;
        }
        for (Index i = 8; i < 16; ++i)
            explicit_state.data().velocity.write()(i, 2) = -1;
        gc::GeneralContact<double> dynamic(mesh, explicit_state, def);
        double velocity = -1, acceleration = 0, displacement = 0, penetration = 0;
        const double dt = .0002;
        for (std::uint64_t step = 1; step <= 1700; ++step) {
            dynamic.begin_increment();
            const double half = velocity + .5 * dt * acceleration;
            displacement += dt * half;
            position(explicit_state, displacement);
            explicit_state.data().step = step;
            explicit_state.data().time = step * dt;
            explicit_state.data().dt = dt;
            auto result = dynamic.evaluate(gc::Analysis::Explicit);
            acceleration = resultant(result.frame);
            velocity = half + .5 * dt * acceleration;
            for (Index i = 8; i < 16; ++i) {
                explicit_state.data().velocity.write()(i, 2) = velocity;
                explicit_state.data().acceleration.write()(i, 2) = acceleration;
            }
            penetration = std::max(penetration, result.frame.monitor.maximum_penetration);
            dynamic.commit();
            if (step == 850) {
                dynamic.save(folder / "explicit.sdbcb");
                const auto hash = gc::state_fingerprint(explicit_state);
                dynamic.restore(folder / "explicit.sdbcb");
                require(hash == gc::state_fingerprint(explicit_state), "mid-impact bundle restart");
            }
        }
        require(std::abs(velocity - 1) < 2e-5 && std::abs(penetration - 1 / std::sqrt(1000.)) < 2e-6,
                "whole-domain elastic rebound");
        dynamic.save(folder / "explicit.sdbcb");
        std::cout << "whole-domain implicit: displacement=" << q << ", Newton evaluations=" << iterations + 1
                  << "\nwhole-domain explicit: rebound=" << velocity << ", peak penetration=" << penetration
                  << ", increments=1700, restart=PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

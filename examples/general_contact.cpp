#include "sdb/general_contact/model.hpp"
#include "sdb/general_contact/search.hpp"
#include "sdb/general_contact/bridge.hpp"
#include "sdb/general_contact/results.hpp"
#include <iostream>

namespace gc = sdb::general_contact;
using namespace sdb;
// A fixed Tet4 bottom facet and an independent unit-mass probe below z=0.
// This is a directed flat-patch qualification fixture, not an all-body general-contact driver.
MeshDB<double> model() {
    ModelBuilder<double> b;
    b.add_material({1, 1, {1, 10, .2}, 0});
    b.add_part(1);
    b.add_node(1, {0, 0, 0});
    b.add_node(2, {1, 0, 0});
    b.add_node(3, {0, 1, 0});
    b.add_node(4, {0, 0, 1});
    b.add_node(5, {.2, .2, -.05});
    b.add_element(1, Topology::Tet4, {1, 2, 3, 4}, 1, 1);
    for (Id n = 1; n <= 4; ++n)
        for (unsigned c = 0; c < 3; ++c)
            b.constrain(n, static_cast<std::uint8_t>(c));
    b.constrain(5, 0);
    b.constrain(5, 1);
    return b.finalize();
}
std::vector<gc::Vec<double>> coordinates(const MeshDB<double> &mesh, const StateDB<double> &state) {
    std::vector<gc::Vec<double>> x(mesh.nodes());
    auto view = state.current_view(mesh);
    for (Index i = 0; i < mesh.nodes(); ++i)
        for (unsigned c = 0; c < 3; ++c)
            x[i][c] = view(i, c);
    return x;
}
int main(int argc, char **argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "general-contact-demo";
        std::filesystem::create_directories(output);
        auto mesh = model();
        gc::Definition<double> definition;
        definition.mesh = mesh.fingerprint();
        definition.properties = {{1, 1000, 0, 0}};
        definition.rules = {{1, 0, 0, 1, 0}};
        gc::Model<double> contact_model(mesh, definition);
        gc::save_model(output / "model.sdbcm", contact_model);
        std::vector<gc::Facet> face{contact_model.facets().at(0)};
        std::vector<gc::SearchPoint<double>> point{{4, 1}};
        DofMap dofs(mesh);
        auto search = [&](const StateDB<double> &state, const auto &previous, std::uint64_t epoch) {
            auto current = coordinates(mesh, state);
            return gc::detect_node_triangle<double>(mesh, face, point, current, previous, 1., 1, 1, epoch,
                                                    16);
        };
        StateDB<double> implicit(mesh);
        gc::Database<double> implicit_contact(gc::bind(implicit, contact_model.fingerprint()));
        const auto previous = coordinates(mesh, implicit);
        gc::Frame<double> implicit_frame;
        std::vector<gc::Witness<double>> implicit_witnesses;
        double u = 0;
        unsigned iteration = 0;
        {
            gc::CoupledTrial<double> trial(implicit, implicit_contact);
            for (; iteration < 8; ++iteration) {
                implicit.data().displacement.write()(4, 2) = u;
                auto detected = search(implicit, previous, iteration + 1);
                implicit_witnesses = detected.witnesses;
                implicit_frame = gc::evaluate<double>(implicit_contact, implicit_witnesses,
                                                      definition.properties, mesh.nodes(), iteration + 1,
                                                      gc::Analysis::Implicit, gc::Execution::Serial, &dofs);
                const double residual = 2 * u - 1 + implicit_frame.residual.at(0);
                double stiffness = 2;
                for (const auto &e : implicit_frame.tangent)
                    if (e.row == 0 && e.column == 0)
                        stiffness += e.value;
                if (std::abs(residual) < 1e-12)
                    break;
                u -= residual / stiffness;
            }
            if (iteration == 8 || std::abs(u - 51. / 1002.) > 1e-12)
                throw std::runtime_error("implicit flat-patch reference mismatch");
            trial.apply_force(implicit_frame);
            implicit.data().step = 1;
            implicit.data().time = 1;
            implicit.data().dt = 1;
            trial.commit();
        }
        gc::save_result(output / "implicit.sdbcr",
                        gc::accepted_result<double>(implicit_contact, implicit_frame, implicit_witnesses));
        save_checkpoint(output / "implicit-mechanical.sdbrst", mesh, implicit);
        gc::save(output / "implicit-contact.sdbc", implicit_contact);
        std::cout << "implicit: displacement=" << u << ", Newton evaluations=" << iteration + 1
                  << ", free equations=" << dofs.size() << '\n';
        StateDB<double> explicit_state(mesh);
        explicit_state.data().mass.write()(4) = 1;
        explicit_state.data().inverse_mass.write()(4) = 1;
        explicit_state.data().velocity.write()(4, 2) = 1;
        gc::Database<double> explicit_contact(gc::bind(explicit_state, contact_model.fingerprint()));
        double peak = 0;
        gc::Frame<double> explicit_frame;
        std::vector<gc::Witness<double>> explicit_witnesses;
        const double dt = 1e-4;
        for (unsigned step = 0; step < 1700; ++step) {
            auto old = coordinates(mesh, explicit_state);
            gc::CoupledTrial<double> trial(explicit_state, explicit_contact);
            auto initial = search(explicit_state, old, 2 * step + 1);
            auto first = gc::evaluate<double>(explicit_contact, initial.witnesses, definition.properties,
                                              mesh.nodes(), 2 * step + 1, gc::Analysis::Explicit);
            const double velocity =
                explicit_state.data().velocity.read()(4, 2) + .5 * dt * first.nodal_force[4][2];
            explicit_state.data().displacement.write()(4, 2) += dt * velocity;
            auto final = search(explicit_state, old, 2 * step + 2);
            explicit_witnesses = std::move(final.witnesses);
            explicit_frame = gc::evaluate<double>(explicit_contact, explicit_witnesses, definition.properties,
                                                  mesh.nodes(), 2 * step + 2, gc::Analysis::Explicit);
            explicit_state.data().velocity.write()(4, 2) =
                velocity + .5 * dt * explicit_frame.nodal_force[4][2];
            peak = std::max(peak, explicit_frame.monitor.maximum_penetration);
            trial.apply_force(explicit_frame);
            explicit_state.data().step = step + 1;
            explicit_state.data().time = (step + 1) * dt;
            explicit_state.data().dt = dt;
            trial.commit();
        }
        const double rebound = explicit_state.data().velocity.read()(4, 2);
        if (std::abs(rebound + 1) > 1e-5 || std::abs(peak - 1 / std::sqrt(1000.)) > 1e-5)
            throw std::runtime_error("explicit flat-patch reference mismatch");
        gc::save_result(output / "explicit.sdbcr",
                        gc::accepted_result<double>(explicit_contact, explicit_frame, explicit_witnesses));
        save_checkpoint(output / "explicit-mechanical.sdbrst", mesh, explicit_state);
        gc::save(output / "explicit-contact.sdbc", explicit_contact);
        std::cout << "explicit: rebound velocity=" << rebound << ", peak penetration=" << peak
                  << ", accepted increments=" << explicit_state.data().step << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

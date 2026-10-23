#include <sdb/general_contact/model.hpp>
#include <sdb/general_contact/search.hpp>
#include <sdb/general_contact/bridge.hpp>
#include <sdb/general_contact/results.hpp>
#include <sdb/general_contact/module.hpp>
#include <iostream>
int main() {
    sdb::ModelBuilder<double> builder;
    builder.add_node(10, {0, 0, 0});
    builder.add_node(20, {0, 0, 1});
    auto mesh = builder.finalize();
    sdb::StateDB<double> state(mesh);
    sdb::general_contact::Definition<double> definition;
    definition.mesh = mesh.fingerprint();
    sdb::general_contact::Model<double> model(mesh, definition);
    sdb::general_contact::Database<double> contact(sdb::general_contact::bind(state, model.fingerprint()));
    {
        sdb::general_contact::CoupledTrial<double> trial(state, contact);
        contact.stage({});
        state.data().step = 1;
        state.data().time = state.data().dt = 1;
        trial.commit();
    }
    if (contact.binding() != sdb::general_contact::bind(state, model.fingerprint()))
        return 1;
    sdb::general_contact::GeneralContact<double> module(mesh, state, definition);
    module.begin_increment();
    module.evaluate(sdb::general_contact::Analysis::Implicit);
    module.rollback();
    std::cout << "installed general-contact target and independent headers: PASS\n";
}

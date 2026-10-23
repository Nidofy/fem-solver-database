#include <sdb/database.hpp>
#include <sdb/dynamics.hpp>
#include <sdb/lifecycle.hpp>
int main(int argc, char **argv) {
    sdb::Database<double> database(sdb::oscillator_model<double>());
    sdb::CpuExplicit<double> solver(database.model(), database.state());
    solver.initialize(database.state(), 0.0001);
    solver.step(database.state());
    if (database.state().data().step != 1)
        return 1;
    if (argc > 1) {
        auto ir = sdb::lifecycle::ReaderRegistry{}.read(argv[1]).model;
        auto model = sdb::lifecycle::ModelCompiler{}.compile<double>(std::move(ir));
        sdb::lifecycle::CpuRun<double> run(model);
        while (run.advance()) {
        }
        return run.finished() ? 0 : 1;
    }
    return 0;
}

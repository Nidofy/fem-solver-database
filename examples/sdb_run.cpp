#include <sdb/lifecycle.hpp>
#include <sdb/results/export.hpp>
#include <iostream>
#include <iomanip>
using namespace sdb;
using namespace sdb::lifecycle;
using Args = std::map<std::string, std::string>;
template <class T> int run(const Args &a) {
    auto model = a.contains("--model")
                     ? ModelStore::load<T>(a.at("--model"))
                     : ModelCompiler{}.compile<T>(ReaderRegistry{}.read(a.at("--input")).model);
    if (a.contains("--save-model"))
        ModelStore::save(a.at("--save-model"), model);
    CpuRun<T> cpu(model);
    if (a.contains("--restart"))
        cpu.restore(a.at("--restart"));
    else if (a.contains("--convert-v2"))
        cpu.import_v2(a.at("--convert-v2"));
    else
        cpu.initialize();
    if (a.contains("--convert-v2")) {
        if (!a.contains("--checkpoint"))
            throw std::invalid_argument("conversion requires --checkpoint");
        cpu.save(a.at("--checkpoint"));
        std::cout << "converted v2 state with explicit model/analysis binding\n";
        return 0;
    }
    if (!a.contains("--output"))
        throw std::invalid_argument("--output required");
    auto limit =
        a.contains("--buffer-mib") ? std::stoull(a.at("--buffer-mib")) * 1024 * 1024 : native::default_limit;
    NativeResultStorage storage(a.at("--output"), ResultModel::from(model, cpu.configuration()),
                                a.contains("--append"), a.contains("--recover-tail"), limit);
    ResultWriter writer(storage);
    if (auto last = storage.last(); last && last->time > cpu.state().data().time)
        throw std::runtime_error(
            "result file is ahead of checkpoint; use matching result prefix or new output");
    auto publish = [&] {
        auto last = storage.last();
        if (!last || cpu.state().data().time > last->time)
            writer.write(cpu.snapshot(limit));
    };
    publish();
    auto stop = a.contains("--stop-after") ? std::stoull(a.at("--stop-after"))
                                           : std::numeric_limits<std::uint64_t>::max();
    while (cpu.state().data().step < stop && cpu.advance())
        publish();
    writer.flush();
    if (a.contains("--checkpoint"))
        cpu.save(a.at("--checkpoint"));
    std::cout << std::setprecision(17) << "nodes=" << model.mesh.nodes()
              << " elements=" << model.mesh.elements() << " increments=" << cpu.state().data().step
              << " time=" << cpu.state().data().time << " precision=" << sizeof(T)
              << " completed=" << cpu.finished() << "\n";
    return 0;
}
int main(int argc, char **argv) {
    try {
        Args a;
        const std::set<std::string> flags{"--append", "--recover-tail", "--inspect"};
        const std::set<std::string> value_options{
            "--input",     "--model",      "--save-model", "--backend",    "--precision",
            "--output",    "--checkpoint", "--restart",    "--stop-after", "--buffer-mib",
            "--read",      "--field",      "--region",     "--time",       "--entity",
            "--component", "--vtk",        "--xdmf",       "--convert-v2"};
        for (int i = 1; i < argc; ++i) {
            std::string key = argv[i];
            if (flags.contains(key)) {
                if (!a.emplace(key, "1").second)
                    throw std::invalid_argument("duplicate option");
            } else if (value_options.contains(key) && i + 1 < argc) {
                if (!a.emplace(key, argv[++i]).second)
                    throw std::invalid_argument("duplicate option");
            } else
                throw std::invalid_argument("unknown option/missing value: " + key);
        }
        if (a.contains("--read")) {
            auto limit = a.contains("--buffer-mib") ? std::stoull(a.at("--buffer-mib")) * 1024 * 1024
                                                    : native::default_limit;
            ResultReader r(a.at("--read"), false, limit);
            std::cout << std::setprecision(17);
            if (a.contains("--vtk"))
                ResultExporter::vtk(r, a.at("--vtk"));
            if (a.contains("--xdmf"))
                ResultExporter::xdmf(r, a.at("--xdmf"));
            if (a.contains("--field")) {
                auto region = a.contains("--region") ? a.at("--region") : "";
                if (a.contains("--entity")) {
                    auto values = ResultQuery::history(
                        r, a.at("--field"), std::stoull(a.at("--entity")),
                        a.contains("--component") ? static_cast<unsigned>(std::stoul(a.at("--component")))
                                                  : 0,
                        region);
                    std::cout << "time,value\n";
                    for (auto [t, v] : values)
                        std::cout << t << ',' << v << '\n';
                } else {
                    auto index =
                        a.contains("--time") ? r.by_time(std::stod(a.at("--time"))) : r.index.size() - 1;
                    auto f = r.field(index, a.at("--field"), region);
                    std::cout << "time=" << r.index.at(index).time << " field=" << f.descriptor.name
                              << " entities=" << f.ids.size() << "\n";
                    for (auto v : f.values)
                        std::cout << v << ' ';
                    std::cout << '\n';
                }
            } else
                std::cout << "frames=" << r.index.size() << " nodes=" << r.model.nodes.size()
                          << " elements=" << r.model.elements.size() << "\n";
            return 0;
        }
        if (a.contains("--backend") && a.at("--backend") != "cpu")
            throw std::invalid_argument("lifecycle executable backend is cpu only; CUDA unverified");
        if (a.contains("--inspect")) {
            ReaderOptions o;
            o.mode = ImportMode::MeshOnly;
            auto parsed = ReaderRegistry{}.read(a.at("--input"), o);
            auto m = ModelCompiler{}.compile<double>(std::move(parsed.model));
            std::cout << "Parsed=" << m.report.parsed
                      << " SemanticallyMapped=" << m.report.semantically_mapped
                      << " DatabaseStored=" << m.report.database_stored
                      << " ExecutableCPU=" << m.report.executable_cpu
                      << " ExecutableCUDA=" << m.report.executable_cuda << '\n';
            for (auto &d : m.report.diagnostics)
                std::cout << d.source.str() << ": " << d.message << '\n';
            if (a.contains("--save-model"))
                ModelStore::save(a.at("--save-model"), m);
            return 0;
        }
        auto precision = a.contains("--precision") ? a.at("--precision") : "double";
        if (precision == "float")
            return run<float>(a);
        if (precision == "double")
            return run<double>(a);
        throw std::invalid_argument("precision must be float or double");
    } catch (const std::exception &e) {
        std::cerr << "sdb_run: " << e.what() << '\n';
        return 1;
    }
}

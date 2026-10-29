#include <sdb/lifecycle.hpp>
#include <sdb/results/async_writer.hpp>
#include <sdb/results/checkpoint_link.hpp>
#include <sdb/results/export.hpp>
#include <iostream>
#include <iomanip>
using namespace sdb;
using namespace sdb::lifecycle;
using Args = std::map<std::string, std::string>;
std::uint64_t mib(const std::string &text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("invalid memory budget");
    auto n = std::stoull(text);
    if (!n || n > std::numeric_limits<std::uint64_t>::max() / (1024 * 1024))
        throw std::length_error("memory budget overflow or zero");
    return n * 1024 * 1024;
}
template <class T> int run(const Args &a) {
    auto durability_level = durability::Level::FileSync;
    const auto durability_name = a.contains("--durability") ? a.at("--durability") : "file";
    if (durability_name == "stream") durability_level = durability::Level::StreamFlush;
    else if (durability_name == "publication") durability_level = durability::Level::PublicationSync;
    else if (durability_name != "file") throw std::invalid_argument("--durability requires stream, file or publication");
    durability::require_supported(durability_level);
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
    auto limit = a.contains("--buffer-mib") ? mib(a.at("--buffer-mib")) : native::default_limit;
    if (a.contains("--append") && !a.contains("--restart"))
        throw std::invalid_argument("safe append requires --restart with a result link");
    NativeResultStorage storage(
        a.at("--output"), ResultModel::from(model, cpu.configuration()), a.contains("--append"),
        a.contains("--recover-tail"), limit,
        [&] {
            CheckpointLink::verify(a.at("--output"), a.at("--restart"), cpu, model,
                                   a.contains("--recover-tail"));
        },
        a.contains("--indexed"), a.contains("--typed-indexed") ? ResultFormat::TypedIndexed3 : ResultFormat::Auto);
    ResultWriter writer(storage);
    if (auto last = storage.last(); last && last->time > cpu.state().data().time)
        throw std::runtime_error(
            "result file is ahead of checkpoint; use matching result prefix or new output");
    std::optional<AsyncResultWriter> async;
    if (a.contains("--async-mib"))
        async.emplace(storage, mib(a.at("--async-mib")));
    auto publish = [&] {
        auto last = async ? async->last() : storage.last();
        if (!last || cpu.state().data().time > last->time) {
            auto frame = cpu.snapshot(limit);
            if (a.contains("--typed-indexed"))
                bind_result_frame(frame, ResultModel::from(model, cpu.configuration()));
            if (async)
                async->write(std::move(frame));
            else
                writer.write(frame);
        }
    };
    publish();
    auto stop = a.contains("--stop-after") ? std::stoull(a.at("--stop-after"))
                                           : std::numeric_limits<std::uint64_t>::max();
    while (cpu.state().data().step < stop && cpu.advance())
        publish();
    if (async)
        async->close();
    else
        writer.flush();
    if (a.contains("--checkpoint"))
        CheckpointLink::publish_durable(storage, a.at("--checkpoint"), cpu, model, durability_level);
    else
        storage.barrier(durability_level);
    std::cout << std::setprecision(17) << "nodes=" << model.mesh.nodes()
              << " elements=" << model.mesh.elements() << " increments=" << cpu.state().data().step
              << " time=" << cpu.state().data().time << " precision=" << sizeof(T)
              << " completed=" << cpu.finished() << " durability=" << durability_name << "\n";
    return 0;
}
int main(int argc, char **argv) {
    try {
        Args a;
        const std::set<std::string> flags{"--indexed", "--typed-indexed", "--typed", "--append", "--recover-tail", "--inspect"};
        const std::set<std::string> value_options{
            "--async-mib", "--ip",     "--input",      "--model",   "--save-model", "--backend",
            "--precision", "--output", "--checkpoint", "--restart", "--stop-after", "--buffer-mib",
            "--read",      "--field",  "--region",     "--time",    "--entity",     "--component",
            "--vtk",       "--xdmf",   "--convert-v2", "--durability"};
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
            auto limit = a.contains("--buffer-mib") ? mib(a.at("--buffer-mib")) : native::default_limit;
            ResultReader r(a.at("--read"), false, limit);
            std::cout << std::setprecision(17);
            if (a.contains("--vtk"))
                ResultExporter::vtk(r, a.at("--vtk"));
            if (a.contains("--xdmf"))
                ResultExporter::xdmf(r, a.at("--xdmf"));
            if (a.contains("--typed") && a.contains("--field")) {
                if (a.contains("--region") || a.contains("--component") || a.contains("--ip"))
                    throw std::invalid_argument("typed CLI emits full addresses/components; these legacy filters are unsupported");
                auto index = a.contains("--time") ? r.by_time(std::stod(a.at("--time"))) : r.index.size() - 1;
                auto f = r.typed_field(index, a.at("--field"), {}, a.contains("--entity")
                    ? std::optional<Id>(std::stoull(a.at("--entity"))) : std::nullopt);
                const auto &p = *f.typed;
                std::cout << "time=" << r.index.at(index).time << " scalar=" << unsigned(p.schema.scalar)
                          << " basis=" << p.basis_id << " schema=" << p.schema_id << '\n';
                std::cout << "entity,ip,layer,section_point,element_node,face,side,domain,generation,interaction,contact_point,endpoint_a_kind,endpoint_a_entity,endpoint_a_local,endpoint_a_side,endpoint_b_kind,endpoint_b_entity,endpoint_b_local,endpoint_b_side,component,valid,value\n";
                std::visit([&](const auto &values) {
                    auto width = p.schema.component_count();
                    for (std::size_t row = 0; row < p.addresses.size(); ++row) {
                        const auto &v = p.addresses[row];
                        for (std::size_t c = 0; c < width; ++c) {
                            auto n = row * width + c;
                            std::cout << v.entity << ',' << v.integration_point << ',' << v.layer << ',' << v.section_point << ','
                                << v.element_node << ',' << v.face << ',' << v.side << ',' << v.domain << ',' << v.contact_generation << ','
                                << v.interaction << ',' << v.contact_point << ',' << v.endpoint_a.kind << ',' << v.endpoint_a.entity << ','
                                << v.endpoint_a.local << ',' << v.endpoint_a.side << ',' << v.endpoint_b.kind << ',' << v.endpoint_b.entity << ','
                                << v.endpoint_b.local << ',' << v.endpoint_b.side << ',' << c << ',' << (p.validity.empty() ? 1 : int(p.validity[n]))
                                << ',' << +values[n] << '\n';
                        }
                    }
                }, p.values);
            } else if (a.contains("--field")) {
                auto region = a.contains("--region") ? a.at("--region") : "";
                if (a.contains("--entity")) {
                    auto values = ResultQuery::history(
                        r, a.at("--field"), std::stoull(a.at("--entity")),
                        a.contains("--component") ? static_cast<unsigned>(std::stoul(a.at("--component")))
                                                  : 0,
                        region, false, 0, std::numeric_limits<double>::max(),
                        a.contains("--ip") ? static_cast<unsigned>(std::stoul(a.at("--ip"))) : 1);
                    std::cout << "time,value\n";
                    for (auto [t, v] : values)
                        std::cout << t << ',' << v << '\n';
                } else {
                    auto index =
                        a.contains("--time") ? r.by_time(std::stod(a.at("--time"))) : r.index.size() - 1;
                    auto f =
                        r.field(index, a.at("--field"), region, false, {},
                                a.contains("--ip") ? static_cast<unsigned>(std::stoul(a.at("--ip"))) : 1);
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
    } catch (const LinkPublicationError &e) {
        std::cerr << "sdb_run: " << e.what() << " phase=" << static_cast<unsigned>(e.phase())
                  << " restart_may_have_changed=" << e.restart_may_have_changed()
                  << " link_may_have_changed=" << e.link_may_have_changed() << '\n';
        return 1;
    } catch (const durability::PublicationError &e) {
        std::cerr << "sdb_run: " << e.what() << " published=" << e.published()
                  << " replacement_may_have_happened=" << e.replacement_may_have_happened() << '\n';
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "sdb_run: " << e.what() << '\n';
        return 1;
    }
}

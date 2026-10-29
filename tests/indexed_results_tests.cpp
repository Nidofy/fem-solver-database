#include <sdb/lifecycle.hpp>
#include <iostream>
#include <chrono>
using namespace sdb;
using namespace sdb::lifecycle;
void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "fixture scratch");
        std::filesystem::path dir = argv[2];
        std::filesystem::create_directories(dir);
        auto m = ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model);
        CpuRun<double> run(m);
        auto rm = ResultModel::from(m, run.configuration());
        auto p = dir / "indexed.sdbres";
        std::filesystem::remove(p);
        auto f = run.snapshot();
        {
            NativeResultStorage w(p, rm, false, false, native::default_limit, {}, true);
            w.append(f);
            while (run.advance()) {
                auto next = run.snapshot();
                if (!next.fields.empty())
                    w.append(next);
            }
        }
        ResultReader r(p);
        check(r.index.size() == 21, "indexed count");
        check(r.read_stats().decoded_value_bytes == 0, "normal open decodes no historical values");
        check(native::pack(r.frame(20)) == native::pack(run.snapshot()), "indexed final frame exact");
        check(r.by_time(r.index[7].time) == 7, "indexed time search");
        check(ResultQuery::history(r, "U", 4).size() == 21, "selective history count");
        {
            NativeResultStorage w(p, rm, true);
            rejects([&] { w.append(f); });
        }
        // Corruption is never silently repaired; only an incomplete tail can be discarded.
        auto broken = dir / "broken.sdbres";
        auto original_size = std::filesystem::file_size(p);
        std::filesystem::copy_file(p, broken, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::resize_file(broken, original_size - 1);
        rejects([&] { ResultReader bad(broken); });
        check(ResultReader(broken, true).index.size() == 20, "indexed committed recovery prefix");
        {
            NativeResultStorage repair(broken, rm, true, true);
            repair.append(r.frame(20));
        }
        check(native::pack(ResultReader(broken).frame(20)) == native::pack(r.frame(20)),
              "indexed recovery exact frame");
        std::filesystem::copy_file(p, broken, std::filesystem::copy_options::overwrite_existing);
        {
            std::fstream data(broken, std::ios::in | std::ios::out | std::ios::binary);
            data.seekp(-1, std::ios::end);
            data.put(char(1));
        }
        rejects([&] { ResultReader bad(broken); });
        rejects([&] { ResultReader bad(broken, true); });
        // Even CRC-correct malicious metadata cannot request an unbounded allocation.
        std::filesystem::copy_file(p, broken, std::filesystem::copy_options::overwrite_existing);
        {
            auto offset = original_size - indexed_detail::footer_bytes();
            std::ifstream input(p, std::ios::binary);
            input.seekg(static_cast<std::streamoff>(offset));
            auto ft = native::unpack<indexed_detail::Footer>(native::record(input, 5, 4096), 4096);
            ft.directory.size = std::numeric_limits<std::uint64_t>::max();
            std::fstream data(broken, std::ios::in | std::ios::out | std::ios::binary);
            data.seekp(static_cast<std::streamoff>(offset));
            native::record(data, 5, native::pack(ft));
        }
        rejects([&] { ResultReader bad(broken); });
        // Cut every structural boundary of the final multi-record transaction.
        {
            indexed_detail::File file(p, native::default_limit);
            auto commit = file.latest.ancestors[0] + indexed_detail::footer_bytes();
            std::ifstream in(p, std::ios::binary);
            for (auto cursor = commit; cursor < original_size;) {
                in.seekg(static_cast<std::streamoff>(cursor));
                auto rec = native::header(in);
                for (auto cut : {cursor, cursor + 1, rec.offset, rec.offset + rec.size - 1}) {
                    std::filesystem::copy_file(p, broken, std::filesystem::copy_options::overwrite_existing);
                    std::filesystem::resize_file(broken, cut);
                    check(ResultReader(broken, true).index.size() == 20,
                          "fault injection exposes only committed prefix");
                }
                cursor = rec.offset + rec.size;
            }
        }
        // Large-field query reads one bounded entity block, never the whole field.
        auto big = rm;
        const std::size_t nodes = 100000;
        big.nodes.resize(nodes);
        big.coordinates.resize(nodes);
        for (std::size_t n = 0; n < nodes; ++n) {
            big.nodes[n].id = n + 1;
            big.nodes[n].index = Index(n);
            big.nodes[n].key = {"SYNTHETIC", "LARGE", n + 1};
        }
        ResultFrame large = f;
        large.fields.clear();
        ResultField field;
        field.descriptor.name = "U";
        field.descriptor.components = {"X", "Y", "Z"};
        for (std::size_t n = 0; n < nodes; ++n) {
            field.ids.push_back(n + 1);
            field.values.insert(field.values.end(), {double(n), 1, 2});
        }
        large.fields.push_back(std::move(field));
        auto bp = dir / "large.sdbres";
        std::filesystem::remove(bp);
        {
            NativeResultStorage w(bp, big, false, false, native::default_limit, {}, true);
            w.append(large);
        }
        ResultReader br(bp);
        auto selected = br.entity_field(0, "U", 99999);
        check(selected.values == std::vector<double>({99998, 1, 2}), "entity selection values");
        check(br.read_stats().decoded_value_bytes <= 256 * 3 * 8, "bounded unrelated value decoding");
        // 100k frames with one scalar; frame metadata is lazy, not a resident vector.
        auto many = dir / "many.sdbres";
        std::filesystem::remove(many);
        auto begin = std::chrono::steady_clock::now();
        {
            NativeResultStorage w(many, rm, false, false, native::default_limit, {}, true);
            ResultFrame small = f;
            small.fields.resize(1);
            small.fields[0].ids = {1};
            small.fields[0].values = {0, 0, 0};
            for (std::uint64_t i = 0; i < 100000; ++i) {
                small.info.id = i;
                small.info.increment = i;
                small.info.time = double(i) * .001;
                small.fields[0].values[0] = double(i);
                w.append(small);
            }
        }
        auto written = std::chrono::steady_clock::now();
        ResultReader manyr(many);
        auto opened = std::chrono::steady_clock::now();
        check(manyr.index.size() == 100000, "100k indexed frames");
        check(manyr.read_stats().bytes < 16384, "bounded metadata-only open");
        for (std::size_t i : {0u, 99999u, 56321u, 7u, 33201u})
            check(manyr.entity_field(i, "U", 1).values[0] == double(i), "random indexed query");
        std::cout << "frames=100000 write_ms="
                  << std::chrono::duration<double, std::milli>(written - begin).count()
                  << " open_ms=" << std::chrono::duration<double, std::milli>(opened - written).count()
                  << " query_read_bytes=" << manyr.read_stats().bytes
                  << " value_bytes=" << manyr.read_stats().decoded_value_bytes << '\n';
        std::cout << "indexed results PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

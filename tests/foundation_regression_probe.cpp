// Compiles against both frozen pre-DBF1 headers and current headers.
#include <sdb/lifecycle.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    unsigned failures = 0;
    try {
        (void)native::unpack<ImportMode>(native::Bytes{0, 1, 0, 0});
        std::cout << "FAIL: wire enum 256 aliases Solve=0\n"; ++failures;
    } catch (const std::exception &) { std::cout << "PASS: enum overflow rejected\n"; }
    auto m = ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model);
    StateDB<double> original(m.mesh);
    original.data().dt = .001;
    RestartDB::save(argv[2], m, original, {});
    StateDB<double> target(m.mesh, true);
    target.data().rotation->angle.fill(19);
    bool rejected = false;
    try { RestartDB::load(argv[2], m, target, 0); }
    catch (const std::exception &) { rejected = true; }
    if (!rejected || !target.data().rotation || target.data().rotation->angle.read()(0) != 19) {
        std::cout << "FAIL: restore silently discarded destination rotation\n"; ++failures;
    } else std::cout << "PASS: rotation mismatch rejected without mutation\n";
    return failures ? 1 : 0;
}

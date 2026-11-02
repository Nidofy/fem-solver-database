#include <sdb/input/abaqus.hpp>
#include <sdb/io/native.hpp>
#include <sdb/dynamics.hpp>
#include <cstdlib>
#include <random>

using namespace sdb;
using namespace sdb::lifecycle;
namespace fs = std::filesystem;
constexpr std::size_t limit = 256 * 1024;

struct Scratch {
    fs::path dir;
    Scratch() {
        std::random_device random;
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            dir = fs::temp_directory_path() / ("sdb-fuzz-" + std::to_string(random()));
            if (fs::create_directory(dir)) return;
        }
        throw std::runtime_error("could not allocate private fuzz workspace");
    }
    ~Scratch() { std::error_code ec; fs::remove(dir/"input.bin",ec); fs::remove(dir,ec); }
};
Scratch &scratch() { static Scratch value; return value; }

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    if (auto path = std::getenv("SDB_FUZZ_SEEDS")) {
        fs::path dir(path);
        fs::create_directories(dir);
#if SDB_FUZZ_TARGET == 2
        auto ir = AbaqusInpReader{}.buffer("*Heading\nseed\n", scratch().dir/"seed.inp").model;
        auto bytes = native::pack(ir);
        std::ofstream out(dir/"model",std::ios::binary);
        checkpoint_detail::stream_bytes(out, bytes);
#elif SDB_FUZZ_TARGET == 3
        auto mesh = oscillator_model<double>();
        StateDB<double> state(mesh);
        save_checkpoint(dir/"checkpoint",mesh,state);
#else
        std::ofstream(dir/"node") << "*Node\n1,0,0,0\n";
#endif
    }
    return 0;
}
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    if (size > limit) return 0;
#if SDB_FUZZ_TARGET == 1
    ReaderOptions options;
    options.allowed_root = scratch().dir;
    options.max_depth = 0;
    options.max_bytes = limit;
    options.max_entities = 4096;
#elif SDB_FUZZ_TARGET == 3
    static const auto mesh = oscillator_model<double>();
    StateDB<double> state(mesh);
    auto path = scratch().dir/"input.bin";
    { std::ofstream out(path,std::ios::binary); checkpoint_detail::stream_bytes(out,{data,size}); }
#endif
    try {
#if SDB_FUZZ_TARGET == 1
        AbaqusInpReader{}.buffer({reinterpret_cast<const char*>(data),size}, scratch().dir/"deck.inp", options);
#elif SDB_FUZZ_TARGET == 2
        (void)native::unpack<ModelIR>({data,size},limit);
#elif SDB_FUZZ_TARGET == 3
        load_checkpoint(path,mesh,state,nullptr,limit);
#else
#error Unknown fuzz target
#endif
    } catch (const std::bad_alloc&) { throw; }
      catch (const std::exception&) { /* Malformed inputs may throw. */ }
    return 0;
}

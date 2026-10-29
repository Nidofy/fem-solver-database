#include <sdb/checkpoint.hpp>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;
using namespace sdb;
static int checks = 0;
static void check(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
static std::string read(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static void inject(durability::FaultPoint point, const fs::path &, void *context) {
    if (point == *static_cast<durability::FaultPoint *>(context))
        throw std::logic_error("injected publication failure");
}
int main() {
    fs::path dir;
    try {
        dir = fs::temp_directory_path() / ("sdb-dbf4-atomic-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(dir);
        auto path = dir / "state.bin";
        const std::vector<std::uint8_t> fresh{'n','e','w'};
        std::vector<durability::FaultPoint> points{
            durability::FaultPoint::AfterTempWrite, durability::FaultPoint::BeforeFileSync,
            durability::FaultPoint::AfterFileSync, durability::FaultPoint::BeforeReplace,
            durability::FaultPoint::AfterReplace};
        if (durability::supports_parent_sync) {
            points.push_back(durability::FaultPoint::BeforeParentSync);
            points.push_back(durability::FaultPoint::AfterParentSync);
        }
        for (bool bytes : {false, true}) {
            for (auto point : points) {
                { std::ofstream out(path, std::ios::binary); out << "old"; }
                bool caught = false;
                const bool after = point == durability::FaultPoint::AfterReplace ||
                    point == durability::FaultPoint::BeforeParentSync ||
                    point == durability::FaultPoint::AfterParentSync;
                {
                    durability::ScopedFaultHook hook({inject, &point});
                    try {
                        if (bytes) checkpoint_detail::atomic_write(path, fresh);
                        else checkpoint_detail::atomic_stream_write(path, [](auto &out) { out << "new"; });
                    } catch (const durability::PublicationError &error) {
                        caught = true;
                        check(error.published() == after, "wrong published flag");
                        check(error.replacement_may_have_happened() == after, "wrong uncertain flag");
                        bool cause = false;
                        try { std::rethrow_exception(error.cause()); }
                        catch (const std::logic_error &e) { cause = std::string(e.what()) == "injected publication failure"; }
                        check(cause, "underlying injected exception lost");
                    }
                }
                check(caught, "fault not reached");
                check(read(path) == (after ? "new" : "old"), "visible target disagrees with outcome");
                std::size_t files = 0;
                for (const auto &entry : fs::directory_iterator(dir)) { (void)entry; ++files; }
                check(files == 1, "failed write leaked temporary file");
            }
            // Real replacement failure: the target is an existing directory.
            auto blocked = dir / "directory";
            fs::create_directory(blocked);
            bool caught = false;
            try {
                if (bytes) checkpoint_detail::atomic_write(blocked, fresh);
                else checkpoint_detail::atomic_stream_write(blocked, [](auto &out) { out << "new"; });
            } catch (const durability::PublicationError &error) {
                caught = !error.published() && error.replacement_may_have_happened();
            }
            check(caught && fs::is_directory(blocked), "replacement failure was not classified conservatively");
            fs::remove(blocked);
        }
        // Sync an open append stream, exercising Windows handle-sharing rules.
        { std::ofstream out(path, std::ios::binary | std::ios::app); out << "!"; out.flush(); durability::sync_file(path); }
        check(read(path) == "new!", "append stream synchronization failed");
        bool invalid = false;
        try { durability::require_supported(static_cast<durability::Level>(99)); }
        catch (const std::invalid_argument &) { invalid = true; }
        check(invalid, "invalid durability level accepted");
        if (!durability::supports_parent_sync) {
            bool unsupported = false;
            try { durability::sync_parent(path); }
            catch (const std::runtime_error &) { unsupported = true; }
            check(unsupported, "unsupported directory sync silently downgraded");
        }
        auto point = durability::FaultPoint::BeforeFileSync;
        bool child_ok = false;
        { durability::ScopedFaultHook hook({inject, &point});
          std::thread worker([&] { try { durability::sync_file(path); child_ok = true; } catch (...) {} });
          worker.join(); }
        check(child_ok, "thread-local fault hook leaked into worker");
        durability::sync_file(path); // scoped hook was removed
        fs::remove(path);
        fs::remove(dir);
        std::cout << "DBF4 atomic durability: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "DBF4 atomic failure: " << e.what() << " (evidence directory " << dir << ")\n";
        return 1;
    }
}

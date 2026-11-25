#include <sdb/results/distributed_manifest.hpp>
#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
    using namespace sdb;
    namespace fs = std::filesystem;
    try {
        if (argc != 5) return 2;
        const fs::path reference = argv[1], target = fs::path(argv[2]) / "run.sdbmanifest";
        const auto baseline = DistributedManifest::open(reference);
        auto sources = baseline.indices(reference.parent_path(), false, nullptr, partition_metadata_budget,
                                        PartitionFile::Checkpoint);
        std::vector<OwnedSourceMap> ownership;
        for (const auto &part : sources) ownership.push_back(part.second.header.owned);
        const auto owned = partition_detail::unite(ownership);
        const std::string kind = argv[3], trigger = argv[4];
        if (kind != "results" && kind != "both") return 2;
        using Point = durability::FaultPoint;
        const std::map<std::string, Point> points{{"temp", Point::AfterTempWrite},
            {"before", Point::BeforeReplace}, {"after", Point::AfterReplace}};
        struct Injection { fs::path target; std::optional<Point> point; };
        Injection fault{target, std::nullopt};
        if (trigger != "none") fault.point = points.at(trigger);
        durability::ScopedFaultHook hook({[](Point point, const fs::path &path, void *data) {
            const auto &injection = *static_cast<Injection *>(data);
            if (point == injection.point && path == injection.target) std::_Exit(86);
        }, &fault});
        DistributedManifest::publish(target, baseline.part_count, owned, baseline.epoch,
            kind == "both" ? PartitionFiles::Both : PartitionFiles::Results);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

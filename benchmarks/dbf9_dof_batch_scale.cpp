// Build this same translation unit against the base and candidate with process_memory.cpp.
#include <sdb/consumer_contract.hpp>
#include <sdb/process_memory.hpp>
#include <chrono>
#include <iostream>
#include <numeric>

using namespace sdb;
using namespace sdb::consumer::v1;
using Clock = std::chrono::steady_clock;
namespace pm = process_memory;

int main(int argc, char **argv) {
    const std::size_t nodes = argc == 2 ? std::stoul(argv[1]) : 666667;
    if (nodes < 4) return 2;
    DofSpaceData data;
    data.model = 57;
    data.nodes = nodes;
    data.elements = nodes / 4;
    data.variables = {{1, 3, DofQuantity::Translation, "U", "m", "global"}};
    const auto slots = product(nodes, 3);
    data.bindings.resize(slots);
    data.solver_to_canonical.resize(slots);
    data.external_equations.resize(slots);
    for (std::size_t i = 0; i < slots; ++i) {
        data.bindings[i] = {{DofOwner::Node, i / 3, 0, 1, std::uint32_t(i % 3)}, DofStatus::Free, i, 0, {}};
        data.external_equations[i] = i + 1;
        data.solver_to_canonical[i] = i;
    }
    auto before = pm::stats();
    auto start = Clock::now();
    DofSpace space(std::move(data));
    const auto report = [&](const char *phase, std::uint64_t identity) {
        auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        auto after = pm::stats();
        std::cout << phase << ',' << slots << ',' << elapsed << ',' << after.allocations - before.allocations
                  << ',' << after.peak - before.live << ',' << identity << '\n';
    };
    std::cout << "phase,slots,ms,allocations,peak_above_input_bytes,identity\n";
    report("dofs", space.identity());
    BatchData batch;
    batch.model = 57;
    batch.equation_space = space.identity();
    batch.formulation = "C3D4";
    batch.topology.name = "TET4";
    batch.topology.nodes = 4;
    const auto elements = nodes / 4;
    batch.elements.resize(elements);
    batch.connectivity.resize(elements * 4);
    batch.dof_slots.resize(elements * 12);
    batch.samples.resize(elements);
    batch.materials.resize(elements);
    batch.sections.resize(elements);
    std::iota(batch.elements.begin(), batch.elements.end(), 0ull);
    std::iota(batch.connectivity.begin(), batch.connectivity.end(), 0ull);
    std::iota(batch.dof_slots.begin(), batch.dof_slots.end(), 0ull);
    for (std::size_t i = 0; i < elements; ++i) {
        batch.sample_offsets.push_back(i + 1);
        batch.dof_offsets.push_back((i + 1) * 12);
        batch.samples[i].entity = i + 1;
        batch.samples[i].integration_point = 1;
    }
    lifecycle::MaterialDefinition mat;
    mat.material = "m";
    mat.schema_id = "elastic";
    MaterialCatalog materials({mat});
    SectionCatalog sections({{"s", "solid", 1, {}, {}}});
    // process_memory reports a process-wide high-water mark. Bring live bytes
    // back to that mark before timing the next phase, then subtract this input.
    const auto prior = pm::stats();
    std::vector<char> measurement_floor(prior.peak - prior.live);
    before = pm::stats();
    start = Clock::now();
    Batch result(std::move(batch), std::move(space), std::move(materials), std::move(sections));
    report("batch", result.identity());
    return 0;
}

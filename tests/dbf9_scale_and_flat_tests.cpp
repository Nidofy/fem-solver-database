#include <sdb/consumer_contract.hpp>
#include "dbf9_batch_fixture.hpp"
#include <sdb/process_memory.hpp>
#include <fstream>
#include <iostream>
#include <random>
#include <numeric>

using namespace sdb;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class Exception, class F> void rejects(F fn) {
    try { fn(); } catch (const Exception &) { return; }
    throw std::runtime_error("expected exception");
}
DofSpaceData golden_input() {
    DofSpaceData d;
    d.model = 0x22446688;
    d.nodes = 2; d.elements = 1;
    d.variables = {{3, 1, DofQuantity::Pressure, "P", "Pa", "global"}};
    d.bindings = {{{DofOwner::Node, 1, 0, 3, 0}, DofStatus::Free, 0, 0, {}},
                  {{DofOwner::Node, 0, 0, 3, 0}, DofStatus::Eliminated, no_equation, -2, {{0, .5}}}};
    d.external_equations = {999}; d.solver_to_canonical = {0};
    return d;
}
int main(int argc, char **argv) { try {
    if (argc != 2) throw std::invalid_argument("golden path");
    DofSpace small(golden_input());
    SemanticHasher hasher;
    std::uint64_t bytewise = UINT64_C(14695981039346656037);
    std::mt19937_64 bits(991);
    for (unsigned i = 0; i < 10000; ++i) {
        auto value = bits() >> (i % 64);
        hasher.append(value);
        for (unsigned byte=0; byte<8; ++byte) {
            bytewise = (bytewise ^ (value & 255)) * UINT64_C(1099511628211);
            value >>= 8;
        }
        check(hasher.value() == bytewise, "canonical eight-byte integer hash");
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    check(in.is_open() && bytes == small.encode(), "base SHA golden bytes");
    check(small.identity() == UINT64_C(15329122238504266767), "base SHA golden identity");
    check(DofSpace::decode(bytes).identity() == small.identity(), "golden identity");
    for (unsigned i = 0; i < 7; ++i) {
        auto d = golden_input();
        switch (i) {
        case 0: d.bindings[1].key = d.bindings[0].key; break;
        case 1: d.bindings[0].key.component = 1; break;
        case 2: d.bindings[1].terms[0].slot = 1; break;
        case 3: d.variables.push_back(d.variables[0]); break;
        case 4: d.external_equations.push_back(999); d.solver_to_canonical.push_back(1); break;
        case 5: d.bindings[0].key.entity = 2; break;
        case 6: d.bindings[0].value = std::numeric_limits<double>::infinity(); break;
        }
        rejects<std::invalid_argument>([&] { DofSpace bad(std::move(d)); });
    }
    auto mesh = dbf9_fixture::space();
    auto batch = dbf9_fixture::data(mesh);
    check(dbf9_fixture::batch(mesh,batch).nodes(0)[0] == 3, "preserve connectivity order");
    for (unsigned i = 0; i < 9; ++i) {
        auto b = batch;
        switch (i) {
        case 0: b.connectivity[0] = b.connectivity[1]; break;
        case 1: b.connectivity[0] = mesh.data().nodes; break;
        case 2: b.dof_slots[0] = b.dof_slots[1]; break;
        case 3: b.dof_slots[0] = mesh.slots(); break;
        case 4: b.dof_slots[0] = 10; break;
        case 5: b.samples[0] = b.samples[1]; break;
        case 6: b.samples[0].element_node = 1; break;
        case 7: b.samples[0].layer = 1; break;
        case 8: b.samples[0].entity = 2; break;
        }
        rejects<std::invalid_argument>([&] { dbf9_fixture::batch(mesh,std::move(b)); });
    }
    FieldDescriptor dsc; dsc.name = "history"; dsc.missing = MissingPolicy::ValidityMask;
    StateLayout layout; layout.domain = 2; layout.addresses = batch.samples;
    RegisteredState state;
    state.add<Field<double>>(dsc,2,"test",1,{7},0,layout);
    state.freeze();
    state.get<Field<double>>("history").write()(0) = 0;
    state.find("history").validity_field().write()(0) = 0;
    layout.addresses = {batch.samples[1], {1, 3}, batch.samples[0]}; layout.domain = 3;
    state.remap("history",layout);
    check(state.find("history").validity() == std::vector<std::uint8_t>{1,1,0} &&
          state.get<Field<double>>("history").read()(2) == 0, "remap values and validity by full address");
    constexpr std::size_t rows = 1000000;
    std::vector<std::uint64_t> perm(rows);
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), std::mt19937(912));
    DofSpaceData d;
    d.model = 1; d.nodes = rows;
    d.variables = {{1, 1, DofQuantity::Temperature, "T"}};
    d.bindings.resize(rows);
    for (std::size_t i = 0; i < rows; ++i) d.bindings[i].key = {DofOwner::Node, perm[i], 0, 1, 0};
    const auto before = process_memory::stats();
    DofSpace shuffled(std::move(d));
    const auto after = process_memory::stats();
    for (std::size_t i = 0; i < rows; ++i)
        check(shuffled.slot({DofOwner::Node, perm[i], 0, 1, 0}) == i, "shuffle lookup lost original slot");
    rejects<std::out_of_range>([&] { shuffled.slot({DofOwner::Node, rows, 0, 1, 0}); });
    check(after.allocations - before.allocations < 100, "per-slot heap allocation");
    std::cout << "slots=" << rows << " constructor allocations=" << after.allocations - before.allocations
              << " peak above input=" << after.peak - before.live << '\n';
    return 0;
} catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; } }

#pragma once
#include <sdb/consumer_contract.hpp>

namespace dbf9_fixture {
using namespace sdb;
namespace v1 = consumer::v1;

inline DofSpace space(std::uint64_t model = 17, std::uint64_t revision = 1) {
    DofSpaceData d;
    d.model = model; d.revision = revision; d.nodes = 12; d.elements = 4;
    d.variables = {{2, 1, DofQuantity::Temperature, "T", "K", "global"}};
    for (std::uint64_t i = 0; i < d.nodes; ++i)
        d.bindings.push_back({{DofOwner::Node, i, 0, 2, 0}, DofStatus::Free, i, 0, {}});
    for (std::uint64_t i = 0; i < d.nodes; ++i) {
        d.external_equations.push_back(112-i);
        d.solver_to_canonical.push_back(d.nodes-i-1);
    }
    return DofSpace(std::move(d));
}
inline v1::BatchData data(const DofSpace &space, std::uint64_t element = 0,
                          std::string formulation = "C3D4", std::uint32_t nodes = 4) {
    v1::BatchData b;
    b.model = space.data().model; b.equation_space = space.identity();
    b.formulation = formulation; b.topology.name = formulation; b.topology.nodes = nodes;
    b.elements = {element};
    for (std::uint32_t i = 0; i < nodes; ++i) b.connectivity.push_back(element + nodes-i-1);
    b.dof_slots = b.connectivity;
    b.dof_offsets = {0, nodes}; b.sample_offsets = {0, 2};
    b.samples = {{element+1, 2}, {element+1, 1}};
    b.materials = {0, 0}; b.sections = {0, 0};
    return b;
}
inline v1::Batch batch(const DofSpace &d, v1::BatchData data) {
    lifecycle::MaterialDefinition m;
    m.material = "M"; m.schema_id = "elastic";
    return {std::move(data), d, v1::MaterialCatalog({m}), v1::SectionCatalog({{"S", "section", 1, {}, {}}})};
}
} // namespace dbf9_fixture

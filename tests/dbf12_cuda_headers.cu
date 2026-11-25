#include <sdb/io/native.hpp>
#include <sdb/model_compiler/compiler.hpp>
#include <sdb/registered_state.hpp>
#include <iostream>

// Host-only assertions built by the CUDA front end. No CUDA driver is needed.
int main() {
    using namespace sdb;
    using namespace sdb::lifecycle;
    const NodeId node=NodeId::checked(47);
    native::Writer writer;
    native::encode<const NodeId>(writer,node);
    registered_detail::Bytes registered;
    registered_detail::encode<const NodeId>(registered,node);
    if(writer.bytes!=native::pack(node.value()) || registered!=writer.bytes ||
       native::encoded_size<const NodeId>(node,32)!=writer.bytes.size() ||
       native::unpack<NodeId>(writer.bytes)!=node || semantic_hash(node)!=semantic_hash(node.value()))
        return 1;

    ModelIR model;
    auto &part=model.parts.emplace_back();
    part.name="part";
    part.nodes.push_back({47,{},{"fixture.inp",9,2}});
    auto canonical=canonical_definition(model);
    const auto &saved=canonical.parts.at(0).nodes.at(0);
    if(saved.label!=47 || !saved.source.file.empty() || saved.source.line!=1) return 1;
    std::cout<<"CUDA host frontend: const wire encoding and nested model traversal passed\n";
}

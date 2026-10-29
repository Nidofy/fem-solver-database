#include <sdb/contact.hpp>
#include <sdb/general_contact/database.hpp>
#include <sdb/io/native.hpp>
#include <iostream>
#include <set>
using namespace sdb;
using namespace sdb::lifecycle;
unsigned checks=0;
void check(bool ok) { ++checks; if(!ok) throw std::runtime_error("DBF3 contract check"); }
template<class F> void rejects(F f) { ++checks; try { f(); } catch(const std::exception&) { return; } throw std::runtime_error("expected rejection"); }
static_assert(!std::is_convertible_v<NodeId,ElementId>);
static_assert(!std::is_convertible_v<StepId,FrameId>);
static_assert(!std::is_convertible_v<InstanceId,MaterialId>);
static_assert(sizeof(NodeId)==sizeof(Index) && sizeof(FrameId)==sizeof(std::uint64_t));

template<class T> void topology_model(Topology top) {
    ModelBuilder<T> b;
    const std::array<std::array<T,3>,8> xyz{{{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}}};
    const unsigned n=arity(top);
    for(unsigned i=0;i<n;++i) {
        auto x=xyz[i]; if(top==Topology::Tet4 && i==2) x={0,1,0};
        if(top==Topology::Tet4 && i==3) x={0,0,1};
        b.add_node(100+i,x);
    }
    b.add_material({1,1,{1,100,.2},0}); b.add_part(1);
    std::vector<Id> nodes; for(unsigned i=0;i<n;++i) nodes.push_back(100+i);
    b.add_element(900,top,nodes,1,1,top==Topology::Tet4 ? 1 : 8);
    Surface surf{1,{}};
    for(unsigned f=0;f<face_count(top);++f) surf.faces.push_back({900,static_cast<std::uint16_t>(f),1});
    b.add_surface(surf.id,surf.faces);
    auto mesh=b.finalize(); FaceTable faces(mesh); auto exterior=general_contact::exterior(mesh);
    check(mesh.element(ExternalLabel(900))==ElementId(0));
    check(mesh.external_label(ElementId(0))==ExternalLabel(900));
    check(mesh.connectivity(ElementId(0)).size()==n);
    check(mesh.dofs(NodeId(0))==3);
    check(mesh.sample_point(ElementId(0),0).integration_point==1);
    rejects([&]{mesh.element(ExternalLabel{});}); rejects([&]{mesh.element(ElementId{});});
    for(unsigned f=0;f<face_count(top);++f) {
        const auto kernel=topology_face_nodes(top,f), imported=topology_face_nodes(top,f,FaceOrdering::Abaqus);
        auto p=topology_face_permutation(top,f,FaceOrdering::Abaqus,FaceOrdering::Kernel);
        auto inverse=topology_face_permutation(top,f,FaceOrdering::Kernel,FaceOrdering::Abaqus);
        auto opposite=topology_face_nodes(top,f,FaceOrdering::Kernel,-1);
        for(unsigned j=0;j<kernel.size();++j) {
            check(imported[p[j]]==kernel[j]); check(p[inverse[j]]==j);
            check(opposite[kernel.size()-1-j]==kernel[j]);
            check(faces.nodes[faces.offsets[f]+j]==kernel[j]);
        }
        auto ex=std::find_if(exterior.begin(),exterior.end(),[&](const auto &v){return v.identity.local==f;});
        check(ex!=exterior.end());
        for(unsigned j=0;j<kernel.size();++j) check(ex->nodes[j]==kernel[j]);
    }
}
int main() {
    try {
        for(auto top:{Topology::Point1,Topology::Beam2,Topology::Tri3,Topology::Quad4,Topology::Tet4,Topology::Hex8,Topology::Polygon}) {
            const auto n=arity(top) ? arity(top) : 7;
            TopologyLayout layout{1,top,n}; layout.validate();
            auto round=native::unpack<TopologyLayout>(native::pack(layout)); round.validate();
            check(round.nodes==n && round.topology==top && round.version==1);
            std::set<std::array<unsigned,2>> edges;
            for(unsigned e=0;e<topology_edge_count(top,n);++e) {
                auto v=topology_edge_nodes(top,e,n); check(v[0]<n && v[1]<n && v[0]!=v[1]);
                std::sort(v.begin(),v.end()); check(edges.insert(v).second);
            }
            rejects([&]{topology_edge_nodes(top,topology_edge_count(top,n),n);});
            rejects([&]{topology_face_nodes(top,face_count(top),FaceOrdering::Kernel,1,n);});
            rejects([&]{topology_face_nodes(top,0,FaceOrdering::Kernel,0,n);});
        }
        rejects([]{TopologyLayout{2,Topology::Tet4,4}.validate();});
        rejects([]{TopologyLayout{1,Topology::Tet4,8}.validate();});
        rejects([]{TopologyLayout{1,Topology::Polygon,65536}.validate();});
        rejects([]{topology_face_nodes(static_cast<Topology>(99),0);});
        rejects([]{topology_face_nodes(Topology::Tet4,0,static_cast<FaceOrdering>(99));});
        rejects([]{local_point_index(0,8);}); rejects([]{local_point_index(9,8);});
        rejects([]{local_point_index(1,8,2);}); check(local_point_index(8,8)==7);
        SampleAxisDescriptor axis; axis.units="s"; axis.validate_coordinate(0);
        auto wire=native::pack(axis); auto copy=native::unpack<SampleAxisDescriptor>(wire);
        copy.validate_coordinate(1.25); check(copy.units=="s");
        // Independent LE oracle: uint32 version, uint32 enum, uint64 string size, UTF8.
        check(wire==native::Bytes({1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,'s'}));
        axis.version=2; rejects([&]{axis.validate();}); axis.version=1;
        axis.kind=static_cast<SampleAxisKind>(1); rejects([&]{axis.validate();}); axis.kind=SampleAxisKind::Time;
        rejects([&]{axis.validate_coordinate(-1);}); rejects([&]{axis.validate_coordinate(std::numeric_limits<double>::infinity());});
        rejects([]{NodeId::checked(std::numeric_limits<std::uint32_t>::max());});
        topology_model<float>(Topology::Tet4); topology_model<double>(Topology::Tet4);
        topology_model<float>(Topology::Hex8); topology_model<double>(Topology::Hex8);
        std::cout<<checks<<" DBF3 contract checks passed\n";
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}

#include <sdb/mesh_access.hpp>
#include <iostream>
#include <type_traits>
void check(bool x){if(!x)throw std::runtime_error("mesh access check");}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
template<class T>void test(){
    sdb::ModelBuilder<T> b;b.add_material({71,42,{T(2),T(3)},0});b.add_material({92,42,{T(5)},0});b.add_part(1);
    b.add_node(10,{0,0,0});b.add_node(20,{1,0,0});b.add_node(30,{0,1,0});b.add_node(40,{0,0,1});
    b.add_element(91,sdb::Topology::Tet4,{10,20,30,40},71,1);
    b.add_element(82,sdb::Topology::Tet4,{20,10,40,30},92,1);
    b.add_element(73,sdb::Topology::Tet4,{10,20,30,40},71,1);
    b.add_element(64,sdb::Topology::Tri3,{20,30,40},71,1);
    b.add_element(55,sdb::Topology::Polygon,{10,20,30,40},71,1);
    auto mesh=b.finalize();auto fingerprint=mesh.fingerprint();
    using Read=sdb::FixedBatchRead<T,4>;
    static_assert(!std::is_constructible_v<Read,sdb::MeshDB<T>&&,std::size_t>);
    static_assert(std::is_same_v<decltype(std::declval<Read>().material()),const sdb::Material<T>&>);
    for(std::size_t k=0;k<mesh.batches().size();++k){
        const auto &batch=mesh.batches()[k];
        if(sdb::arity(batch.topology)!=4){rejects([&]{Read view(mesh,k);});continue;}
        auto allocations=sdb::host_memory.allocations.load();Read view(mesh,k);
        check(sdb::host_memory.allocations.load()==allocations);
        check(view.elements().data()==batch.elements.data());
        check(&view.material()==&mesh.materials()[batch.material]);
        check(view.integration_points()==batch.integration_points && view.topology()==batch.topology);
        for(std::size_t i=0;i<view.size();++i)for(std::size_t c=0;c<4;++c){
            auto node=mesh.connectivity(batch.elements[i])[c];check(view.at(i,c)==node && view.column(c)[i]==node);
            check(view.coordinates()(node,0)==mesh.reference()(node,0));
        }
        auto tail=view.subspan(1).subspan(0);check(tail.size()+1==view.size());
        for(std::size_t c=0;c<4;++c){check(tail.column(c).data()==view.column(c).data()+1);
            for(std::size_t i=0;i<tail.size();++i)check(tail.node(i,c)==view.node(i+1,c));}
        auto empty=view.subspan(view.size());check(empty.size()==0 && empty.column(3).empty());
        rejects([&]{view.at(view.size(),0);});rejects([&]{view.column(4);});
        rejects([&]{view.subspan(view.size()+1);});rejects([&]{view.subspan(1,view.size());});
        auto read_sum=[view]{std::size_t s=0;for(auto node:view.column(0))s+=node;return s;};
        auto async=std::async(std::launch::async,read_sum);check(async.get()==read_sum());
    }
    rejects([&]{Read bad(mesh,mesh.batches().size());});check(mesh.fingerprint()==fingerprint);
}
int main(){try{test<float>();test<double>();std::cout<<"mesh access: mixed topology/material, zero-copy, subspan, constness and read concurrency PASS\n";}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

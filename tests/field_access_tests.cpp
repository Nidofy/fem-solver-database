#include <sdb/field_access.hpp>
#include <iostream>
#include <numeric>
#include <type_traits>
void check(bool value){if(!value)throw std::runtime_error("field access check");}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
template<class Layout> void layout_test() {
    sdb::Field<double,3,Layout> f(35);
    auto raw=f.write();
    for(std::size_t i=0;i<35;++i)for(std::size_t c=0;c<3;++c)raw(i,c)=double(3*i+c);
    std::vector<std::size_t> permutation(105);std::iota(permutation.begin(),permutation.end(),0);
    std::reverse(permutation.begin(),permutation.end());
    sdb::DenseAccessPlan<3,Layout> plan(35,permutation);
    auto access=plan.bind(f.write());
    for(std::size_t i=0;i<105;++i){check(access.at(i)==double(permutation[i]));access[i]+=7;}
    auto read=plan.bind(f.read());static_assert(std::is_same_v<decltype(read[0]),const double&>);
    for(std::size_t i=0;i<105;++i)check(read[i]==double(permutation[i]+7));
    rejects([&]{read.at(105);});
    rejects([&]{plan.bind(sdb::View<double,3,Layout>{nullptr,35});});
    rejects([&]{plan.bind(sdb::View<double,3,Layout>{nullptr,0});});
    permutation[0]=permutation[1];rejects([&]{sdb::DenseAccessPlan<3,Layout> bad(35,permutation);});
    permutation[0]=105;rejects([&]{sdb::DenseAccessPlan<3,Layout> bad(35,permutation);});
    permutation.pop_back();rejects([&]{sdb::DenseAccessPlan<3,Layout> bad(35,permutation);});
    sdb::DenseAccessPlan<3,Layout> empty(0,{});check(empty.bind(sdb::View<double,3,Layout>{}).size()==0);
}
int main(){try {
    layout_test<sdb::SoA>();layout_test<sdb::AoSoA<8>>();
    sdb::Field<double,3> f(7);f.fill(2);auto sibling=f.fork_shared();
    auto plane=sdb::component_span(f.write(),1);plane[4]=9;
    check(f.read()(4,1)==9 && sibling.read()(4,1)==2);
    auto const_plane=sdb::component_span(f.read(),1);
    static_assert(std::is_same_v<decltype(const_plane[0]),const double&>);
    check(const_plane.size()==7 && const_plane[4]==9);
    rejects([&]{sdb::component_span(f.read(),3);});
    rejects([]{sdb::component_span(sdb::View<double,3>{nullptr,2},0);});
    check(sdb::component_span(sdb::View<double,3>{},2).empty());
    std::cout<<"field access: SoA/AoSoA permutation, bounds, constness, empty and COW PASS\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

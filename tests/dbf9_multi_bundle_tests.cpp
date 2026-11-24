#include "dbf9_batch_fixture.hpp"
#include <sdb/io/foundation_bundle.hpp>
#include <iostream>

using namespace sdb;
namespace fx = dbf9_fixture;
namespace n = lifecycle::native;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class F> void rejects(F fn) {
    try { fn(); } catch (const std::exception &) { return; }
    throw std::runtime_error("expected bundle rejection");
}
using Records = std::vector<std::pair<std::uint32_t,n::Bytes>>;
static consumer::v1::Batch topology_batch(const DofSpace &d,std::uint64_t element,std::string formulation,
                                           consumer::v1::TopologyContract topology) {
    auto b=fx::data(d,element,std::move(formulation),topology.nodes);
    b.topology=std::move(topology);
    return fx::batch(d,std::move(b));
}
static Records read_records(const std::filesystem::path &file) {
    std::ifstream in(file,std::ios::binary); std::array<std::uint8_t,12> prefix; n::read(in,prefix);
    Records records;
    while (in.peek()!=EOF) {
        auto h=n::header(in); records.emplace_back(h.kind,n::body(in,h));
    }
    return records;
}
// Keep the genuine prefix, regenerate all CRCs so order/identity checks are reached.
static void rewrite(const std::filesystem::path &good,const std::filesystem::path &bad,const Records &records) {
    std::ifstream in(good,std::ios::binary); std::array<std::uint8_t,12> prefix; n::read(in,prefix);
    std::ofstream out(bad,std::ios::binary); n::write(out,prefix);
    for (const auto &[kind,body]:records) n::record(out,kind,body);
}
static RegisteredState make_state(const BatchCatalog &catalog) {
    RegisteredState s;
    s.bind_context(catalog.dofs().data().model,catalog.dofs().identity(),catalog.identity());
    FieldDescriptor d; d.name="stress";
    s.add<Field<double>>(d,2,"voigt-test",1,{1.5},0); s.freeze(); return s;
}
int main(int argc,char **argv) { try {
    if (argc!=3) throw std::invalid_argument("write/read and scratch directory required");
    auto d=fx::space();
    auto tet=topology_batch(d,0,"C3D4",{"TET4",1,4,{0,3,6,9,12},{0,2,1, 0,1,3, 1,2,3, 2,0,3},
        {{0,1},{1,2},{2,0},{0,3},{1,3},{2,3}}});
    auto hex=topology_batch(d,1,"C3D8R",{"HEX8",1,8,{0,4,8,12,16,20,24},
        {0,3,2,1, 4,5,6,7, 0,1,5,4, 1,2,6,5, 2,3,7,6, 3,0,4,7},
        {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}}});
    auto shell=topology_batch(d,2,"S4R",{"QUAD4",1,4,{0,4},{0,1,2,3},{{0,1},{1,2},{2,3},{3,0}}});
    BatchCatalog catalog({tet,hex,shell});
    auto s=make_state(catalog);
    const std::filesystem::path dir=argv[2], good=dir/"multi.sdb", bad=dir/"bad.sdb";
    if (std::string(argv[1])=="read") {
        auto info=MultiBatchBundle::inspect(good);
        check(info.catalog.size()==3 && info.catalog.identity()==catalog.identity() &&
              info.catalog[0].nodes(0).size()==4 && info.catalog[1].nodes(0).size()==8 &&
              info.catalog[2].data().formulation=="S4R" && info.fields.at(0).descriptor.name=="stress", "fresh metadata");
        check(&info.catalog[0].dofs()==&info.catalog[2].dofs(),"common DOF storage");
        for (std::size_t i=0; i<catalog.size(); ++i)
            check(n::pack(info.catalog[i].data().topology)==n::pack(catalog[i].data().topology),"all local faces and edges");
        auto restored=MultiBatchBundle::restore(good,catalog,s);
        check(restored.step==8 && restored.time==0.5 && s.get<Field<double>>("stress").read()(1)==42.5,"fresh process state");
        return 0;
    }
    std::filesystem::create_directories(dir);
    BundleCursor cursor; cursor.step=8; cursor.time=0.5; cursor.consumer_schema="mixed/v1";
    s.get<Field<double>>("stress").write()(1)=42.5;
    MultiBatchBundle::save(good,catalog,s,cursor);
    const auto total=std::filesystem::file_size(good);
    rejects([&]{ MultiBatchBundle::save(good,catalog,s,cursor,total-1); });
    check(std::filesystem::file_size(good)==total,"failed atomic save preserved published file");
    rejects([&]{ MultiBatchBundle::inspect(good,31); });
    rejects([&]{ FoundationBundle::inspect(good); });
    RegisteredState legacy; legacy.bind_context(d.data().model,d.identity(),tet.identity()); legacy.freeze();
    FoundationBundle::save(dir/"single.sdb",tet,legacy,cursor);
    rejects([&]{ MultiBatchBundle::inspect(dir/"single.sdb"); });
    rejects([&]{ MultiBatchBundle::save(bad,catalog,legacy,cursor); });
    rejects([&]{ BatchCatalog empty({}); });
    rejects([&]{ BatchCatalog overlap({tet,fx::batch(d,fx::data(d,0,"shell",4))}); });
    for (auto other:{fx::space(18),fx::space(17,2)})
        rejects([&]{ BatchCatalog wrong({tet,fx::batch(other,fx::data(other,1))}); });
    for (auto wrong:{BatchCatalog({hex,tet,shell}),BatchCatalog({tet,hex})}) {
        auto target=make_state(wrong);
        rejects([&]{ MultiBatchBundle::restore(good,wrong,target); });
    }
    auto target=make_state(catalog);
    auto original=read_records(good);
    auto check_bad=[&](bool metadata) {
        if (metadata) rejects([&]{ MultiBatchBundle::inspect(bad); });
        rejects([&]{ MultiBatchBundle::restore(bad,catalog,target); });
        check(target.get<Field<double>>("stress").read()(1)==1.5,"failed restore changed target");
    };
    for (int fault=0; fault<6; ++fault) {
        auto records=original;
        if (fault==0) std::swap(records.at(2),records.at(3)); // child order
        if (fault==1) records.erase(records.begin()+3);        // child count
        if (fault==2) records.at(0).second.at(20)^=1;         // aggregate identity
        if (fault>=3) {
            auto &body=records.at(fault==3 ? 6 : 2).second;
            const std::string name=fault==3 ? "stress" : (fault==4 ? "C3D4" : "TET4");
            auto i=std::search(body.begin(),body.end(),name.begin(),name.end());
            check(i!=body.end(),"mutation anchor"); *i^=1;
        }
        rewrite(good,bad,records); check_bad(true);
    }
    std::filesystem::copy_file(good,bad,std::filesystem::copy_options::overwrite_existing);
    {
        std::fstream f(bad,std::ios::binary|std::ios::in|std::ios::out);
        f.seekg(-1,std::ios::end); char last; f.get(last); f.seekp(-1,std::ios::end); f.put(last^1);
    }
    check_bad(false);
    auto trailing=original; trailing.push_back(original.back()); rewrite(good,bad,trailing); check_bad(false);
    s.begin_trial_cow(); rejects([&]{ MultiBatchBundle::save(good,catalog,s,cursor); }); s.rollback();
    std::cout << "three topologies, atomic restore, CRC and catalog rejection passed\n";
    return 0;
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }

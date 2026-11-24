#include <sdb/io/partitioned_checkpoint.hpp>
#include <sdb/results/partitioned_result_reader.hpp>
#include <sdb/integration_state.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>

using namespace sdb;
using namespace sdb::lifecycle;
using Path=std::filesystem::path;
using Exact=Field<std::uint64_t>;
using Scalar=Field<double>;
using Tensor=Field<double,3,AoSoA<4>>;
using Clock=std::chrono::steady_clock;
static void check(bool v,const char *s) { if(!v) throw std::runtime_error(s); }
template<class F> static void rejects(F f) {
    try { f(); } catch(const std::exception &) { return; }
    throw std::runtime_error("invalid repartition request accepted");
}
constexpr auto node_base=UINT64_C(0x100000001),element_base=UINT64_C(0x200000001);
static unsigned fixture_scale=1;
static std::vector<OwnedSourceMap> partition(std::size_t n,bool target=false) {
    std::vector<OwnedSourceMap> out(n);
    for(unsigned i=0;i<128*fixture_scale;++i) out[(target?i*7+3:i)%n].node_labels.push_back(node_base+i);
    for(unsigned i=0;i<48*fixture_scale;++i) out[(target?i*5+1:i)%n].element_labels.push_back(element_base+i);
    if(target) for(auto &m:out) { std::ranges::reverse(m.node_labels); std::ranges::reverse(m.element_labels); }
    return out;
}
static PartitionEpoch epoch() { return {37,41,"coupled.plugin/v3",{{1,0,0},{1,1,0.125},{2,0,0.25}}}; }
static std::uint64_t exact(GlobalLabel key,unsigned sample) { return (UINT64_C(1)<<60)+17*(key-node_base)+sample; }
static double value(const ResultAddress &a,unsigned sample) { return 32.0*(a.entity-element_base)+a.integration_point+0.25*a.layer+0.03125*a.section_point+sample; }
static RegisteredState state(const OwnedSourceMap &owned,unsigned sample,bool fill=true,bool wrong_schema=false) {
    RegisteredState s; s.bind_context(37,41,123);
    FieldDescriptor node; node.name="node_tag"; node.scalar=ScalarType::UInt64;
    node.association=Association::Node; node.location=FieldLocation::Entity;
    s.add<Exact>(node,owned.node_labels.size(),"node/v1",1,{7},0);
    FieldDescriptor phase; phase.name="phase"; phase.storage=StorageKind::Ragged;
    FieldDescriptor tensor=phase; tensor.name="tensor_history"; tensor.shape={3}; tensor.components={"xx","yy","xy"};
    tensor.missing=MissingPolicy::ValidityMask;
    std::vector<ElementIntegration> samples,sections;
    for(auto e:owned.element_labels) {
        const auto type=(e-element_base)%3;
        ElementIntegration elem{e,type==1?8u:1u,{}};
        if(type==2) { elem.points_per_layer={2,3}; sections.push_back(elem); }
        samples.push_back(elem);
    }
    const auto mixed=integration_layout(samples),shell=integration_layout(sections);
    HistorySchemaDescriptor custom{"custom.thermo.plastic",wrong_schema?4u:3u,
        {{phase,{5}},{tensor,{1,2,3},HistoryFinitePolicy::MaskMissing}}};
    custom.register_fields<Scalar,Tensor>(s,mixed,11);
    auto layer=phase; layer.name="section_temperature"; layer.location=FieldLocation::SectionPoint;
    s.add<Scalar>(layer,shell.addresses.size(),"layer/v1",1,{6},0,shell);
    s.freeze();
    if(fill) {
        auto nodes=s.get<Exact>("node_tag").write();
        for(std::size_t i=0;i<owned.node_labels.size();++i) nodes(i)=exact(owned.node_labels[i],sample);
        auto ph=s.get<Scalar>("phase").write(); auto hist=s.get<Tensor>("tensor_history").write();
        auto mask=s.find("tensor_history").validity_field().write();
        for(std::size_t i=0;i<mixed.addresses.size();++i) {
            const auto &a=mixed.addresses[i];
            const double v=value(a,sample);
            ph(i)=a.entity==element_base?-0.0:v;
            hist(i,0)=v; hist(i,1)=v+0.125; hist(i,2)=a.layer?0:-v;
            mask(i*3+2)=a.layer?0:1;
        }
        auto st=s.get<Scalar>("section_temperature").write();
        for(std::size_t i=0;i<shell.addresses.size();++i) st(i)=300+value(shell.addresses[i],sample);
    }
    return s;
}
static void write(const Path &dir,unsigned n,unsigned rank,const std::string &fault) {
    auto owned=partition(n)[rank];
    if(fault=="duplicate") owned.node_labels.front()=node_base;
    if(fault=="missing_key") owned.element_labels.pop_back();
    auto clock=epoch(); if(fault=="step") ++clock.samples.back().step;
    std::array<RegisteredState,3> frames;
    std::vector<const RegisteredState*> ptrs;
    for(unsigned i=0;i<frames.size();++i) { frames[i]=state(owned,i,true,fault=="schema"); ptrs.push_back(&frames[i]); }
    PartitionedResultWriter::write(dir,rank,n,clock,owned,ptrs);
    PartitionedCheckpoint::write(dir,rank,n,clock,owned,frames.back());
}
static void queries(const Path &dir) {
    std::vector<PartitionedResultReader> readers;
    for(auto n:{1,2,4}) readers.emplace_back(dir/std::to_string(n)/"run.sdbmanifest");
    auto global=partition(1)[0]; std::uint64_t compared=0;
    for(const auto &field:{"node_tag","phase","tensor_history","section_temperature"}) {
        const bool node=std::string_view(field)=="node_tag";
        for(auto key:node?global.node_labels:global.element_labels) {
            auto reference=readers[0].query_samples(field,key,0,3);
            if(node) check(reference.size()==3 && std::get<std::vector<std::uint64_t>>(reference[2].values)[0]==exact(key,2),"global uint64 result");
            else if(std::string_view(field)=="phase") {
                check(!reference.empty(),"missing element field");
                for(const auto &r:reference) {
                    auto expected=key==element_base?-0.0:value(r.address,static_cast<unsigned>(r.sample));
                    check(std::bit_cast<std::uint64_t>(std::get<std::vector<double>>(r.values).front())==
                          std::bit_cast<std::uint64_t>(expected),"incorrect IP result");
                }
            }
            for(const auto &reader:readers) {
                auto rows=reader.query_samples(field,key,0,3);
                auto timed=reader.query_time(field,key,0.125,0.25);
                auto selected=reader.query_samples(field,key,1,3);
                check(rows.size()==reference.size() && timed.size()==selected.size(),"partition result cardinality");
                for(std::size_t i=0;i<rows.size();++i) {
                    const auto &a=reference[i],&b=rows[i];
                    auto bytes=[](const auto &r) {return std::visit([](const auto &v){return native::pack(v);},r.values);};
                    check(a.address==b.address && a.cursor==b.cursor && a.sample==b.sample && bytes(a)==bytes(b) &&
                          a.validity==b.validity,"result bits changed with partition count");
                    if(a.sample>=1) {
                        const auto j=i-(rows.size()-timed.size());
                        check(timed.at(j).address==b.address && timed[j].sample==b.sample &&
                              bytes(timed[j])==bytes(b) && timed[j].validity==b.validity,"time window value");
                    }
                    ++compared;
                }
            }
        }
    }
    std::cout<<"query_rows_compared="<<compared<<" writers=1,2,4\n";
}
static void restore(const Path &file,unsigned dests,bool one_rank=false,unsigned rank=0) {
    const auto layout=partition(dests,true); auto start=Clock::now();
    std::uint64_t rows=0,payload=0,visited=0;
    for(unsigned r=0;r<dests;++r) {
        if(one_rank && r!=rank) continue;
        auto dest=state(layout[r],0,false),oracle=state(layout[r],2);
        auto stats=PartitionedCheckpoint::restore(file,layout,r,dest,epoch());
        check(stats.cursor==epoch().samples.back() && stats.selected_rows<stats.source_rows,"restore selection/cursor");
        std::uint64_t bytes=0;
        for(std::size_t i=0;i<dest.entries().size();++i) {
            const auto *f=dest.entries()[i]; auto data=f->block(0,f->scalar_count()); bytes+=data.size();
            check(data==oracle.entries()[i]->block(0,f->scalar_count()),"restored row, mask or plugin state differs");
        }
        check(stats.payload_bytes==bytes,"read non-owned payload");
        rows+=stats.selected_rows; payload+=bytes; visited+=stats.source_rows;
    }
    std::cout<<"target_parts="<<dests<<" selected_rows="<<rows<<" directory_rows_examined="<<visited
             <<" selected_payload_bytes="<<payload<<" elapsed_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-start).count()<<"\n";
}
static void target_faults(const Path &dir) {
    auto layout=partition(3,true); const auto file=dir/"2"/"run.sdbmanifest";
    auto target=state(layout[0],0,false),wrong=state(layout[0],0,false,true);
    auto first=[&]{return target.get<Exact>("node_tag").read()(0);};
    rejects([&]{PartitionedCheckpoint::restore(file,layout,0,wrong,epoch());});
    auto invalid=layout; invalid[1].node_labels.push_back(invalid[0].node_labels[0]);
    rejects([&]{PartitionedCheckpoint::restore(file,invalid,0,target,epoch());});
    invalid=layout; invalid[1].element_labels.pop_back();
    rejects([&]{PartitionedCheckpoint::restore(file,invalid,0,target,epoch());});
    invalid=layout; invalid[1].element_labels[0]+=10000;
    rejects([&]{PartitionedCheckpoint::restore(file,invalid,0,target,epoch());});
    auto bad=epoch(); ++bad.samples.back().step;
    rejects([&]{PartitionedCheckpoint::restore(file,layout,0,target,bad);});
    auto extra=state(layout[0],0,false);
    auto extended=*extra.find("phase").addressing();
    auto address=extended.addresses.back(); address.integration_point=999;
    extended.addresses.push_back(address); ++extended.offsets.back(); extra.remap("phase",extended);
    rejects([&]{PartitionedCheckpoint::restore(file,layout,0,extra,epoch());});
    auto missing=*target.find("phase").addressing();
    missing.addresses.pop_back(); --missing.offsets.back();
    target.remap("phase",missing);
    rejects([&]{PartitionedCheckpoint::restore(file,layout,0,target,epoch());});
    check(first()==7 && wrong.get<Exact>("node_tag").read()(0)==7,"failed restore mutated target");
    auto manifest=DistributedManifest::read(file);
    manifest.parts.front().crc64^=1;
    rejects([&]{manifest.indices(file.parent_path(),true);});
    manifest.parts.front().relative_path="../part_0.sdbres";
    rejects([&]{manifest.indices(file.parent_path());});
    auto empty_map=partition(1); empty_map.emplace_back();
    auto empty=state(empty_map.back(),0,false);
    auto stats=PartitionedCheckpoint::restore(file,empty_map,1,empty,epoch());
    check(stats.selected_rows==0 && stats.payload_bytes==0,"empty rank read payload");
}
int main(int argc,char **argv) { try {
    if(argc<3) return 2;
    if(const char *scale=std::getenv("SDB_DBF11_FIXTURE_SCALE")) {
        auto requested=std::stoul(scale);
        if(requested==0 || requested>128) return 2;
        fixture_scale=static_cast<unsigned>(requested);
    }
    std::string op=argv[1]; Path path=argv[2];
    const std::array<std::uint8_t,9> kat{'1','2','3','4','5','6','7','8','9'};
    check(partition_detail::crc64(kat)==UINT64_C(0x6c40df5f0b497347),"CRC64 ECMA-182");
    if(op=="write" && argc>=5) write(path,std::stoul(argv[3]),std::stoul(argv[4]),argc>=6?argv[5]:"");
    else if(op=="publish" && argc==4) DistributedManifest::publish(path/"run.sdbmanifest",std::stoul(argv[3]),partition(1)[0],epoch());
    else if(op=="verify") (void)DistributedManifest::open(path/"run.sdbmanifest");
    else if(op=="query") queries(path);
    else if(op=="restore") { restore(path/"4"/"run.sdbmanifest",2); restore(path/"2"/"run.sdbmanifest",3); }
    else if(op=="target_faults") target_faults(path);
    else if(op=="rank" && argc==5) restore(path/"run.sdbmanifest",std::stoul(argv[3]),true,std::stoul(argv[4]));
    else return 2;
    return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; } }

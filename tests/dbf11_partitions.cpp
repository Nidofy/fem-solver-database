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
static RegisteredState state(const OwnedSourceMap &owned,unsigned sample,bool fill=true,bool wrong_schema=false,std::uint64_t context=700) {
    RegisteredState s; s.bind_context(37,context,123);
    FieldDescriptor global; global.name="SNEXT_CURSOR"; global.association=Association::Global;
    global.location=FieldLocation::Entity; global.scalar=ScalarType::UInt64;
    s.add<Exact>(global,1,"cursor/v1",1,{0},0);
    global.name="LEDGER"; global.scalar=ScalarType::Float64; s.add<Scalar>(global,1,"ledger/v1",1,{0},0);
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
        s.get<Exact>("SNEXT_CURSOR").write()(0)=sample+1;
        s.get<Scalar>("LEDGER").write()(0)=1024.25+sample;
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
    for(unsigned i=0;i<frames.size();++i) { frames[i]=state(owned,i,true,fault=="schema",100+n*10+rank); if(fault=="global") frames[i].get<Scalar>("LEDGER").write()(0)+=1; ptrs.push_back(&frames[i]); }
    PartitionedResultWriter::write(dir,rank,n,clock,owned,ptrs);
    PartitionedCheckpoint::write(dir,rank,n,clock,owned,frames.back());
}
static void queries(const Path &dir) {
    std::vector<PartitionedResultReader> readers;
    for(auto n:{1,2,4}) readers.emplace_back(dir/std::to_string(n)/"run.sdbmanifest");
    for(auto &reader:readers) {
        auto ledger=reader.query_samples("LEDGER",0,0,3);
        auto cursor=reader.query_samples("SNEXT_CURSOR",0,0,3);
        check(ledger.size()==3 && cursor.size()==3 && std::get<std::vector<double>>(ledger[2].values)[0]==1026.25 &&
              std::get<std::vector<std::uint64_t>>(cursor[2].values)[0]==3,"replicated global query");
    }
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
        auto dest=state(layout[r],0,false,false,1000+dests*10+r),oracle=state(layout[r],2);
        auto stats=PartitionedCheckpoint::restore(file,layout,r,dest,epoch());
        check(stats.cursor==epoch().samples.back() && stats.selected_rows<=stats.source_rows,"restore selection/cursor");
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
    static_assert(partition_metadata_budget>lifecycle::native::default_limit);
    auto probe=file.parent_path()/"part_0.sdbrst";
    rejects([&]{partition_detail::index(probe,PartitionFile::Checkpoint,128);});
    rejects([&]{DistributedManifest::open(file,true,128);});
    rejects([&]{PartitionedResultReader tiny(file,128);});
    check(!partition_detail::index(probe,PartitionFile::Checkpoint,4*1024*1024).header.rows.empty(),"configured shard budget");
    auto manifest=DistributedManifest::read(file);
    manifest.parts.front().crc64^=1;
    rejects([&]{manifest.indices(file.parent_path(),true);});
    manifest.parts.front().relative_path="../part_0.sdbres";
    rejects([&]{manifest.indices(file.parent_path());});
    auto empty_map=partition(1); empty_map.emplace_back();
    auto empty=state(empty_map.back(),0,false);
    auto stats=PartitionedCheckpoint::restore(file,empty_map,1,empty,epoch());
    check(stats.payload_shards==1 && stats.selected_rows==2 && stats.payload_bytes==16 && empty.get<Exact>("SNEXT_CURSOR").read()(0)==3,"empty rank read payload");
}
// Opt-in scale regression. Regular CTest also checks the same admission
// boundaries with a small budget; this mode writes an actual >256 MiB header.
static void large_header(const Path &dir) {
    constexpr std::size_t rows=2400000;
    std::filesystem::create_directories(dir);
    OwnedSourceMap map; map.node_labels.resize(rows);
    for(std::size_t i=0;i<rows;++i) map.node_labels[i]=i+1;
    PartitionEpoch clock{7,13,"budget/v1",{{1,0,0.0}}};
    RegisteredState state; state.bind_context(7,13,17);
    FieldDescriptor d; d.name="marker"; d.association=Association::Node; d.location=FieldLocation::Entity;
    d.scalar=ScalarType::UInt8; state.add<Field<std::uint8_t>>(d,rows,"marker",1,{42},0); state.freeze();
    const auto path=dir/"part_0.sdbrst";
    PartitionedCheckpoint::write(dir,0,1,clock,map,state,partition_metadata_budget);
    std::ifstream in(path,std::ios::binary); partition_detail::prefix(in,partition_detail::checkpoint_magic);
    auto h=native::header(in,partition_metadata_budget);
    check(h.size>native::default_limit,"scale regression did not cross legacy header limit");
    rejects([&]{partition_detail::index(path,PartitionFile::Checkpoint,native::default_limit);});
    auto idx=partition_detail::index(path,PartitionFile::Checkpoint,partition_metadata_budget);
    check(idx.header.rows.size()==rows && partition_detail::read_row(in,idx,rows-1)==native::Bytes{42},"large header read");
    std::cout<<"indexed_rows="<<rows<<" header_bytes="<<h.size<<" configured_budget="<<partition_metadata_budget<<"\n";
}
static void legacy_v1(const Path &dir) {
    auto file=dir/"run.sdbmanifest"; auto m=DistributedManifest::open(file);
    for(const auto &part:m.parts) {
        auto idx=partition_detail::index(dir/part.relative_path,part.kind);
        check(idx.header.local_equation_context==13,"v1 equation context");
    }
    PartitionedResultReader reader(file); auto rows=reader.query_samples("legacy",105,0,1);
    check(rows.size()==1 && std::get<std::vector<double>>(rows.front().values).front()==-7.5,"v1 result value");
    OwnedSourceMap map{{105,101},{}}; RegisteredState target; target.bind_context(7,42,17);
    FieldDescriptor d; d.name="legacy"; d.association=Association::Node; d.location=FieldLocation::Entity;
    target.add<Scalar>(d,2,"test",1,{0},0); target.freeze();
    PartitionedCheckpoint::restore(file,{&map,1},0,target,{7,13,"legacy",{{1,0,0.5}}});
    auto v=target.get<Scalar>("legacy").read(); check(v(0)==-7.5 && v(1)==1.25,"v1 reordered restore");
}
static void restart_policies(const Path &dir) {
    auto make=[](const OwnedSourceMap &owned,unsigned rank) {
        RegisteredState s; s.bind_context(37,100+rank,1);
        for(auto policy:{RestartPolicy::Required,RestartPolicy::Omit,RestartPolicy::Reconstruct}) {
            FieldDescriptor d; d.name=std::to_string(static_cast<unsigned>(policy)); d.restart=policy; d.location=FieldLocation::Entity;
            d.association=policy==RestartPolicy::Omit?Association::Global:Association::Node;
            auto count=d.association==Association::Global?1:owned.node_labels.size();
            s.add<Scalar>(d,count,"restart-policy",1,{2.0},0);
        }
        s.freeze();
        for(const auto *e:s.entries()) {
            auto values=s.get<Scalar>(e->descriptor().name).write();
            for(std::size_t i=0;i<e->count();++i) values(i)=9;
        }
        return s;
    };
    auto sources=partition(2);
    for(unsigned r=0;r<2;++r) {
        auto s=make(sources[r],r);
        rejects([&]{PartitionedCheckpoint::write(dir,r,2,epoch(),sources[r],s,64);});
        check(!std::filesystem::exists(dir/partition_detail::filename(r,PartitionFile::Checkpoint)),"failed metadata admission published a shard");
        PartitionedCheckpoint::write(dir,r,2,epoch(),sources[r],s);
        auto idx=partition_detail::index(dir/partition_detail::filename(r,PartitionFile::Checkpoint),PartitionFile::Checkpoint);
        check(idx.header.rows.size()==sources[r].node_labels.size(),"optional checkpoint rows written");
    }
    auto path=dir/"run.sdbmanifest"; auto targets=partition(1);
    DistributedManifest::publish(path,2,targets[0],epoch(),PartitionFiles::Checkpoint);
    auto dest=make(targets[0],9); PartitionedCheckpoint::restore(path,targets,0,dest,epoch());
    for(const auto *e:dest.entries()) {
        auto values=dest.get<Scalar>(e->descriptor().name).read();
        for(std::size_t i=0;i<e->count();++i)
            check(values(i)==(e->descriptor().restart==RestartPolicy::Required?9:2),"optional restart initializer");
    }
}
// Rewrite a private one-rank fixture and regenerate every integrity checksum so
// semantic validation is reached through the public manifest and query APIs.
static void payload_faults(const Path &dir) {
    namespace pd=partition_detail;
    write(dir,1,0,"");
    auto path=dir/pd::filename(0,PartitionFile::Results);
    auto idx=pd::index(path,PartitionFile::Results);
    auto &h=idx.header; std::size_t row=0;
    while(h.fields.at(h.rows.at(row).field).field.name!="tensor_history") ++row;
    std::vector<native::Bytes> blocks;
    { std::ifstream in(path,std::ios::binary);
      for(std::size_t i=0;i<h.rows.size();++i) blocks.push_back(pd::read_row(in,idx,i)); }
    const auto original=blocks.at(row);
    for(auto [v,valid]:std::array<std::pair<double,std::uint8_t>,5>{{
        {std::numeric_limits<double>::infinity(),1},{std::numeric_limits<double>::quiet_NaN(),1},
        {0,2},{1,0},{-0.0,0}}}) {
        auto scalar=native::pack(v); auto &b=blocks[row]; b=original;
        std::copy(scalar.begin(),scalar.end(),b.begin()); b[sizeof(double)]=valid;
        h.rows[row].crc=checkpoint_detail::crc(b);
        native::atomic_stream(path,[&](auto &out){pd::prefix(out,pd::result_magic);
            native::record(out,1,native::pack(h)); for(const auto &block:blocks) native::write(out,block);});
        auto file=dir/"run.sdbmanifest"; DistributedManifest::publish(file,1,partition(1)[0],epoch());
        PartitionedResultReader reader(file); const auto &r=h.rows[row];
        rejects([&]{reader.query_samples("tensor_history",r.address.entity,r.sample,r.sample+1);});
    }
}
int main(int argc,char **argv) { try {
    if(argc<3) return 2;
    if(const char *scale=std::getenv("SDB_DBF11_FIXTURE_SCALE")) {
        auto requested=std::stoul(scale);
        if(requested==0 || requested>256) return 2;
        fixture_scale=static_cast<unsigned>(requested);
    }
    std::string op=argv[1]; Path path=argv[2];
    const std::array<std::uint8_t,9> kat{'1','2','3','4','5','6','7','8','9'};
    check(partition_detail::crc64(kat)==UINT64_C(0x6c40df5f0b497347),"CRC64 ECMA-182");
    if(op=="write" && argc>=5) write(path,std::stoul(argv[3]),std::stoul(argv[4]),argc>=6?argv[5]:"");
    else if(op=="publish" && argc==4) DistributedManifest::publish(path/"run.sdbmanifest",std::stoul(argv[3]),partition(1)[0],epoch());
    else if(op=="large_header") large_header(path);
    else if(op=="legacy") legacy_v1(path);
    else if(op=="restart_policies") restart_policies(path);
    else if(op=="payload_faults") payload_faults(path);
    else if(op=="results_only") {
        PartitionedResultReader reader(path/"run.sdbmanifest");
        check(reader.query_samples("node_tag",node_base,0,1).size()==1,"independent result");
    }
    else if(op=="verify") (void)DistributedManifest::open(path/"run.sdbmanifest");
    else if(op=="query") queries(path);
    else if(op=="restore") { restore(path/"4"/"run.sdbmanifest",2); restore(path/"2"/"run.sdbmanifest",3); }
    else if(op=="bench") {
        fixture_scale=160;
        auto sources=partition(4),targets=partition(2,true);
        std::filesystem::create_directories(path);
        for(unsigned rank=0;rank<4;++rank) {
            auto shard=state(sources[rank],2,true,false,100+rank);
            PartitionedCheckpoint::write(path,rank,4,epoch(),sources[rank],shard);
        }
        auto manifest=path/"bench.sdbmanifest";
        DistributedManifest::publish(manifest,4,partition(1)[0],epoch(),PartitionFiles::Checkpoint);
        std::uint64_t rows=0,payload=0; double elapsed=0;
        for(unsigned rank=0;rank<2;++rank) {
            auto dest=state(targets[rank],0,false,false,200+rank),oracle=state(targets[rank],2);
            auto start=Clock::now();
            auto stats=PartitionedCheckpoint::restore(manifest,targets,rank,dest,epoch());
            elapsed+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
            rows+=stats.selected_rows; payload+=stats.payload_bytes;
            for(std::size_t i=0;i<dest.entries().size();++i) {
                auto *f=dest.entries()[i];
                check(f->block(0,f->scalar_count())==oracle.entries()[i]->block(0,f->scalar_count()),"large restore oracle");
            }
        }
        check(rows>=100000,"undersized flat benchmark");
        std::cout<<"4->2 rows="<<rows<<" payload="<<payload<<" index_order_bytes_per_source_row="<<sizeof(std::size_t)
                 <<" restore_ms="<<elapsed<<"\n";
    }
    else if(op=="target_faults") target_faults(path);
    else if(op=="rank" && argc==5) restore(path/"run.sdbmanifest",std::stoul(argv[3]),true,std::stoul(argv[4]));
    else return 2;
    return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; } }

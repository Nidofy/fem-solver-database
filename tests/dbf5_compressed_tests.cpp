#include <sdb/results/async_writer.hpp>
#include <iostream>
#include <fstream>
using namespace sdb;
using namespace sdb::lifecycle;
namespace fs = std::filesystem;
unsigned checks = 0;
void check(bool value, const char *why) { ++checks; if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F f) { ++checks; try { f(); } catch (const std::exception &) { return; } throw std::runtime_error("expected rejection"); }
native::Bytes bytes(const fs::path &path) { std::ifstream in(path,std::ios::binary); return {std::istreambuf_iterator<char>(in),{}}; }
ResultModel model() {
    ResultModel m; m.mesh=1; m.definition=2; m.configuration=3;
    m.coordinates.resize(1024); m.nodes.resize(1024);
    for (std::size_t i=0;i<1024;++i) { m.nodes[i].id=i+1; m.nodes[i].index=Index(i); m.nodes[i].key={"P","I",i+1}; }
    return m;
}
template<class T> ResultField field(const std::string &name) {
    FieldDescriptor d; d.name=name; d.scalar=scalar_type<T>(); d.association=Association::Node;
    d.location=FieldLocation::Entity; d.missing=MissingPolicy::ValidityMask;
    std::vector<ResultAddress> addresses(1024); std::vector<T> values(1024); std::vector<std::uint8_t> validity(1024,1);
    for (std::size_t i=0;i<1024;++i) {
        addresses[i].entity=i+1;
        if constexpr (std::is_floating_point_v<T>) values[i]=(i%2) ? T(-0.) : T(i)/T(8);
        else values[i]=(i%2) ? (std::numeric_limits<T>::max)() : (std::numeric_limits<T>::min)();
        if(i%17==0) { validity[i]=0; values[i]=T(0); }
    }
    return make_typed_field<T>(std::move(d),std::move(addresses),std::move(values),std::move(validity));
}
ResultFrame frame(const ResultModel &m,std::uint64_t id=0) {
    ResultFrame f; f.info.id=id; f.info.time=double(id); f.info.increment=id;
    f.fields={field<float>("F32"),field<double>("F64"),field<std::uint8_t>("U8"),field<std::int32_t>("I32"),
              field<std::uint32_t>("U32"),field<std::int64_t>("I64"),field<std::uint64_t>("U64")};
    bind_result_frame(f,m); return f;
}
void same(const ResultFrame &a,const ResultFrame &b) {
    check(native::pack(a.info)==native::pack(b.info),"frame metadata changed");
    check(a.fields.size()==b.fields.size(),"field count changed");
    for(std::size_t i=0;i<a.fields.size();++i) {
        check(native::pack(*a.fields[i].typed)==native::pack(*b.fields[i].typed),"typed values/address/schema/bits changed");
        check(native::pack(a.fields[i].descriptor)==native::pack(b.fields[i].descriptor),"descriptor changed");
    }
}
int main(int argc,char **argv) {
    try {
        if(argc!=2) throw std::invalid_argument("scratch required");
        const fs::path root=argv[1]; fs::create_directories(root);
        const auto m=model(); const auto f=frame(m);
        CompressionSettings policy;
        if(compression::available(compression::Codec::Zstd)) policy.defaults.codec=compression::Codec::Zstd;
        policy.fields["U64"]={}; // exercise mixed raw/compressed blocks in the same frame
        const auto compressed=root/"typed4.sdbres", legacy=root/"typed3.sdbres";
        fs::remove(compressed); fs::remove(legacy);
        { NativeResultStorage out(legacy,m,false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3); out.append(f); }
        { NativeResultStorage out(compressed,m,false,false,native::default_limit,{},false,ResultFormat::CompressedIndexed4,policy);
          check(out.admission_overhead()>=compression::hard_workspace_limit,"codec scratch not charged");
          const auto charge=AsyncResultWriter::required_bytes(f);
          { AsyncResultWriter too_small(out,charge); auto owned=f; rejects([&]{too_small.write(std::move(owned));}); check(!owned.fields.empty(),"rejected admission consumed source"); too_small.close(); }
          AsyncResultWriter async(out,charge+out.admission_overhead()+1024);
          auto owned=f; async.write(std::move(owned));
          auto receipt=async.barrier(durability::Level::FileSync);
          check(receipt.last && receipt.last->id==0 && receipt.bytes==fs::file_size(compressed),"bad durable receipt");
          async.close(); }
        ResultReader reader(compressed); check(reader.format()==ResultFormat::CompressedIndexed4,"format detection");
        same(ResultReader(legacy).frame(0),reader.frame(0));
        const auto before=reader.read_stats();
        const auto selected=reader.typed_field(0,"U64",{},Id(513));
        check(typed_values<std::uint64_t>(selected).size()==1 && typed_values<std::uint64_t>(selected)[0]==0,"selected exact integer lane");
        check(reader.read_stats().raw_block_bytes-before.raw_block_bytes<fs::file_size(legacy),"selective query decoded full frame");
        if(compression::available(compression::Codec::Zstd)) {
            check(reader.read_stats().compressed_blocks>0,"no compressed blocks read");
            check(fs::file_size(compressed)<fs::file_size(legacy),"compression did not reduce fixture");
        }
        { NativeResultStorage append(compressed,m,true); append.append(frame(m,1)); append.barrier(durability::Level::FileSync); }
        check(ResultReader(compressed).index.size()==2,"append policy adoption");
        const auto complete=bytes(compressed);
        { std::ofstream tail(compressed,std::ios::binary|std::ios::app); tail<<"partial"; }
        auto mismatched=policy; mismatched.defaults.min_bytes++;
        rejects([&]{NativeResultStorage out(compressed,m,true,true,native::default_limit,{},false,ResultFormat::Auto,mismatched);});
        check(fs::file_size(compressed)==complete.size()+7,"policy mismatch truncated tail");
        rejects([&]{ResultReader bad(compressed);});
        { NativeResultStorage out(compressed,m,true,true); out.barrier(durability::Level::FileSync); }
        check(bytes(compressed)==complete,"tail recovery changed committed bytes");
        ResultReader recovered(compressed); same(frame(m,1),recovered.frame(1));
        rejects([&]{ResultReader small(compressed,false,1024); (void)small.frame(0);});
        auto corruption=complete;
        std::size_t cursor=12;
        while(cursor+16<=corruption.size()) {
            native::Reader header{std::span<const std::uint8_t>(corruption).subspan(cursor,16)};
            const auto kind=header.integer<std::uint32_t>(); const auto size=header.integer<std::uint64_t>();
            if(kind==2) { corruption.at(cursor+16+compressed_detail::envelope_bytes)^=0x80; break; }
            cursor+=16+static_cast<std::size_t>(size);
        }
        const auto damaged=root/"damaged.sdbres";
        { std::ofstream out(damaged,std::ios::binary|std::ios::trunc); native::write(out,corruption); }
        rejects([&]{ResultReader bad(damaged); (void)bad.frame(0);});
        rejects([&]{ResultReader bad(damaged,true);});
        check(bytes(damaged)==corruption,"reader modified corruption");
        // A 256-row block exceeds the codec cap: split rows without flattening
        // components or changing their schema. This exercises the adaptive path.
        FieldDescriptor wide; wide.name="WIDE"; wide.scalar=ScalarType::Float64;
        wide.association=Association::Node; wide.location=FieldLocation::Entity;
        wide.shape={4096}; wide.components.clear();
        for (unsigned c=0;c<4096;++c) wide.components.push_back("C"+std::to_string(c));
        std::vector<ResultAddress> wide_addresses(256);
        for(std::size_t i=0;i<wide_addresses.size();++i) wide_addresses[i].entity=i+1;
        ResultFrame wide_frame; wide_frame.fields.push_back(make_typed_field<double>(wide,std::move(wide_addresses),std::vector<double>(256*4096,2.5)));
        bind_result_frame(wide_frame,m);
        const auto wide_path=root/"wide.sdbres"; fs::remove(wide_path);
        { NativeResultStorage out(wide_path,m,false,false,native::default_limit,{},false,ResultFormat::CompressedIndexed4,policy); out.append(wide_frame); }
        same(wide_frame,ResultReader(wide_path).frame(0));
        CompressionSettings unavailable; unavailable.defaults.codec=compression::Codec::Zstd;
        if(!compression::available(compression::Codec::Zstd)) {
            const auto path=root/"unavailable.sdbres"; fs::remove(path);
            rejects([&]{NativeResultStorage out(path,m,false,false,native::default_limit,{},false,ResultFormat::CompressedIndexed4,unavailable);});
            check(!fs::exists(path),"unavailable codec created output");
        }
        std::cout<<"DBF5 compressed integration: "<<checks<<" checks PASS zstd="<<compression::available(compression::Codec::Zstd)<<'\n';
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}

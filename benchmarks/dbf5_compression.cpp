#include <sdb/results/async_writer.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
using namespace sdb;
using namespace sdb::lifecycle;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
template<class T> ResultField lane(std::size_t rows, bool random) {
    FieldDescriptor schema; schema.scalar=scalar_type<T>(); schema.name="LANE"+std::to_string(unsigned(schema.scalar));
    schema.association=Association::Node; schema.location=FieldLocation::Entity; schema.missing=MissingPolicy::ValidityMask;
    std::vector<ResultAddress> addresses(rows);
    std::vector<T> values(rows);
    std::vector<std::uint8_t> validity(rows,1);
    std::mt19937_64 generator(0xDBF5 + unsigned(schema.scalar));
    for(std::size_t i=0;i<rows;++i) {
        addresses[i].entity=i+1;
        if(i%13==0) {validity[i]=0; values[i]=0; continue;}
        if constexpr(std::is_floating_point_v<T>) values[i]=random ? T(std::int64_t(generator()%2000001)-1000000)/T(8) : T(1.25);
        else if constexpr(std::is_signed_v<T>) values[i]=random ? T(std::bit_cast<std::int64_t>(generator())) : (std::numeric_limits<T>::min)()+T(7);
        else values[i]=random ? T(generator()) : (std::numeric_limits<T>::max)()-T(7);
    }
    return make_typed_field<T>(schema,std::move(addresses),std::move(values),std::move(validity));
}
int main(int argc,char **argv) {
    try {
        if(argc<2 || argc>3) throw std::invalid_argument("dbf5_compression DIRECTORY [ROWS]");
        if(!compression::available(compression::Codec::Zstd)) throw std::runtime_error("benchmark requires real zstd");
        const std::filesystem::path directory=argv[1]; std::filesystem::create_directories(directory);
        const std::size_t rows=argc==3 ? std::stoull(argv[2]) : 10000;
        if(!rows || rows>1000000) throw std::invalid_argument("rows must be 1..1000000");
        std::cout<<"case,format,rows,fields,frames,file_bytes,write_ms,open_ms,query_ms,query_read_bytes,raw_block_bytes,stored_block_bytes,compressed_blocks,decoded_value_bytes,frame_admission_bytes,codec_admission_reserve,reader_budget,source_unchanged,returned_rows\n"<<std::setprecision(9);
        for(bool random : {false,true}) {
            const std::string name=random?"random":"repeated";
            ResultModel model; model.mesh=1; model.definition=2; model.configuration=3;
            model.coordinates.resize(rows); model.nodes.resize(rows);
            for(std::size_t i=0;i<rows;++i) {model.nodes[i].id=i+1; model.nodes[i].index=Index(i); model.nodes[i].key={"SYNTHETIC","DBF5",i+1};}
            ResultFrame frame;
            frame.fields={lane<float>(rows,random),lane<double>(rows,random),lane<std::int32_t>(rows,random),
                lane<std::uint32_t>(rows,random),lane<std::int64_t>(rows,random),lane<std::uint64_t>(rows,random),lane<std::uint8_t>(rows,random)};
            bind_result_frame(frame,model);
            std::vector<native::Bytes> original; for(const auto &field:frame.fields) original.push_back(native::pack(*field.typed));
            for(auto format : {ResultFormat::TypedIndexed3,ResultFormat::CompressedIndexed4}) {
                const bool compressed=format==ResultFormat::CompressedIndexed4;
                auto path=directory/(name+(compressed?"-compressed.sdbres":"-native.sdbres"));
                std::filesystem::remove(path);
                CompressionSettings settings{{compression::Codec::Zstd,3,0,true},{}};
                // Explicit None demonstrates a persisted per-field override.
                settings.fields.emplace("LANE6",compression::Options{});
                auto start=Clock::now(); std::uint64_t reserve=0;
                {
                    NativeResultStorage writer(path,model,false,false,native::default_limit,{},false,format,
                        compressed?std::optional<CompressionSettings>{settings}:std::nullopt);
                    reserve=writer.admission_overhead();
                    for(unsigned k=0;k<3;++k) {frame.info.id=k;frame.info.increment=k;frame.info.time=k;bind_result_frame(frame,model);writer.append(frame);}
                }
                auto write_ms=ms(start);
                bool unchanged=true; for(std::size_t i=0;i<original.size();++i) unchanged &= native::pack(*frame.fields[i].typed)==original[i];
                if(!unchanged) throw std::runtime_error("source changed");
                start=Clock::now(); ResultReader reader(path); auto open_ms=ms(start);
                if(reader.read_stats().decoded_value_bytes) throw std::runtime_error("open decoded values");
                // Full exact type and validity comparison before timing the selective read.
                auto restored=reader.frame(2);
                for(std::size_t i=0;i<original.size();++i) if(native::pack(*restored.fields[i].typed)!=original[i]) throw std::runtime_error("full lane mismatch");
                auto before=reader.read_stats(); ResultAddress selected;selected.entity=rows/2+1;
                start=Clock::now();auto found=reader.typed_field(2,"LANE5",selected);auto query_ms=ms(start);
                const auto &expected=std::get<std::vector<std::uint64_t>>(frame.fields[5].typed->values);
                if(found.typed->addresses.size()!=1 || typed_values<std::uint64_t>(found)[0]!=expected.at(selected.entity-1) ||
                   found.typed->validity[0]!=frame.fields[5].typed->validity[selected.entity-1]) throw std::runtime_error("selective exact lane mismatch");
                const auto after=reader.read_stats();
                std::cout<<name<<','<<(compressed?"compressed":"native")<<','<<rows<<",7,3,"<<std::filesystem::file_size(path)<<','
                    <<write_ms<<','<<open_ms<<','<<query_ms<<','<<after.bytes-before.bytes<<','<<after.raw_block_bytes-before.raw_block_bytes<<','
                    <<after.stored_block_bytes-before.stored_block_bytes<<','<<after.compressed_blocks-before.compressed_blocks<<','
                    <<after.decoded_value_bytes-before.decoded_value_bytes<<','<<AsyncResultWriter::required_bytes(frame)<<','<<reserve<<','
                    <<native::default_limit<<','<<unchanged<<','<<found.typed->addresses.size()<<'\n';
            }
        }
    } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}

#include <sdb/results/async_writer.hpp>
#include <chrono>
#include <iostream>
#include <iomanip>
using namespace sdb;
using namespace sdb::lifecycle;
using Clock = std::chrono::steady_clock;
static double milliseconds(Clock::time_point begin) {
    return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
}
// Storage-only benchmark: no numerical kernel and no fixed speed expectation.
// Separate dimensions expose metadata/frame overhead versus scalar volume.
int main(int argc,char **argv) {
    try {
        if (argc != 2) throw std::invalid_argument("typed_results_scaling SCRATCH_DIRECTORY");
        const std::filesystem::path directory=argv[1];
        std::filesystem::create_directories(directory);
        std::cout<<"entities,fields,frames,file_bytes,write_ms,open_ms,query_ms,query_read_bytes,query_value_bytes,frame_admission_bytes,source_unchanged,returned_rows\n";
        std::cout<<std::setprecision(9);
        for (std::size_t entities : {100,1000,10000})
            for (unsigned field_count : {1,4})
                for (unsigned frames : {1,4}) {
                    ResultModel model;
                    model.mesh=1; model.definition=2; model.configuration=3;
                    model.coordinates.resize(entities); model.nodes.resize(entities);
                    for (std::size_t i=0;i<entities;++i) model.nodes[i].id=i+1;
                    ResultFrame frame;
                    for (unsigned j=0;j<field_count;++j) {
                        FieldDescriptor schema;
                        schema.name="COUNTER"+std::to_string(j); schema.scalar=ScalarType::UInt64;
                        schema.association=Association::Node; schema.location=FieldLocation::Entity;
                        std::vector<ResultAddress> addresses; std::vector<std::uint64_t> values;
                        addresses.reserve(entities); values.reserve(entities);
                        for (std::size_t i=entities;i>0;--i) {
                            ResultAddress a; a.entity=i; addresses.push_back(a);
                            values.push_back(UINT64_MAX-i-j);
                        }
                        frame.fields.push_back(make_typed_field<std::uint64_t>(schema,std::move(addresses),std::move(values)));
                    }
                    bind_result_frame(frame,model);
                    auto before=native::hash(native::pack(*frame.fields.front().typed));
                    auto admission=AsyncResultWriter::required_bytes(frame);
                    auto path=directory/(std::to_string(entities)+"-"+std::to_string(field_count)+"-"+std::to_string(frames)+".sdbres");
                    std::filesystem::remove(path);
                    auto start=Clock::now();
                    {
                        NativeResultStorage writer(path,model,false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3);
                        for (unsigned k=0;k<frames;++k) {
                            frame.info.id=k; frame.info.increment=k; frame.info.time=k;
                            bind_result_frame(frame,model); writer.append(frame);
                        }
                    }
                    const auto write_ms=milliseconds(start);
                    const bool unchanged=before==native::hash(native::pack(*frame.fields.front().typed));
                    if (!unchanged) throw std::runtime_error("writer mutated source snapshot");
                    start=Clock::now(); ResultReader reader(path); const auto open_ms=milliseconds(start);
                    if (reader.read_stats().decoded_value_bytes) throw std::runtime_error("open decoded values");
                    const auto stats=reader.read_stats();
                    ResultAddress selected; selected.entity=entities/2;
                    start=Clock::now(); auto found=reader.typed_field(frames-1,"COUNTER0",selected);
                    const auto query_ms=milliseconds(start);
                    if (found.typed->addresses.size()!=1 || typed_values<std::uint64_t>(found)[0]!=UINT64_MAX-selected.entity)
                        throw std::runtime_error("incorrect selective scalar");
                    std::cout<<entities<<','<<field_count<<','<<frames<<','<<std::filesystem::file_size(path)<<','
                        <<write_ms<<','<<open_ms<<','<<query_ms<<','<<reader.read_stats().bytes-stats.bytes<<','
                        <<reader.read_stats().decoded_value_bytes-stats.decoded_value_bytes<<','<<admission<<','
                        <<unchanged<<','<<found.typed->addresses.size()<<'\n';
                }
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}

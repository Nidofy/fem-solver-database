#define main upstream_result_main
#include "../../../../build-dbf3-review/fixed47/tests/dbf3_result_tests.cpp"
#undef main
int main(){try{
 std::filesystem::path dir="verification/reviews/DBF3/independent-astra/result";std::filesystem::create_directories(dir);
 auto ir=fixture();ir.instances[1].rotation={0,0,0,1,0,0,90};auto m=ModelCompiler{}.compile<double>(ir);StateDB<double>s(m.mesh);
 FieldDescriptor d;d.name="U32";d.scalar=ScalarType::UInt32;d.association=Association::Node;d.location=FieldLocation::Entity;
 s.register_field<Field<std::uint32_t>>(d,m.mesh.nodes(),"exact.u32",1,{UINT32_MAX});
 d.name="I32";d.scalar=ScalarType::Int32;s.register_field<Field<std::int32_t>>(d,m.mesh.nodes(),"exact.i32",1,{INT32_MIN});
 d.name="U8";d.scalar=ScalarType::UInt8;s.register_field<Field<std::uint8_t>>(d,m.mesh.nodes(),"exact.u8",1,{255});s.freeze_registered_fields();
 auto frame=ResultPublisher::snapshot_history(m,s);ResultPublisher::append_registered(frame,m,s);auto rm=ResultModel::from(m,0);validate_frame(frame,rm,{});
 auto file=dir/"noncommuting.sdbres";std::filesystem::remove(file);
 {NativeResultStorage store(file,rm,false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3);auto charge=AsyncResultWriter::required_bytes(frame);AsyncResultWriter writer(store,charge);
 writer.write(std::move(frame));s.registered_field<Field<std::uint32_t>>("U32").fill(0);s.data().history.fill(99);writer.close();check(writer.stats().peak_bytes<=charge,"independent async admission cap");}
 ResultReader reader(file);check(typed_values<std::uint32_t>(reader.typed_field(0,"U32"))[0]==UINT32_MAX,"u32 exact independent");check(typed_values<std::int32_t>(reader.typed_field(0,"I32"))[0]==INT32_MIN,"i32 exact independent");check(typed_values<std::uint8_t>(reader.typed_field(0,"U8"))[0]==255,"u8 exact independent");
 const std::array<Rotation,2> expected{{{0,-1,0,0,0,-1,1,0,0},{-1,0,0,0,0,-1,0,-1,0}}};
 for(unsigned layer=1;layer<=2;++layer){auto v=reader.typed_field(0,"HV",address(2,layer),{},"material.local-history");check(typed_values<double>(v)==std::vector<double>({1,2,3}),"snapshot owns original components");auto b=std::find_if(reader.model.bases.begin(),reader.model.bases.end(),[&](auto&b){return b.id==v.typed->basis_id;});check(b!=reader.model.bases.end(),"basis found");for(unsigned i=0;i<9;++i)check(std::abs(b->rotation[i]-expected[layer-1][i])<1e-12,"noncommuting Rx Rz layer oracle");}
 auto before=ResultPublisher::snapshot_registered(m,s);auto count=before.fields.size();rejects([&]{ResultPublisher::append_history(before,m,s,{},1);});check(before.fields.size()==count,"append budget rejection preserves destination");
 auto denied=ResultPublisher::snapshot_registered(m,s);{NativeResultStorage store(dir/"denied.sdbres",rm,false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3);AsyncResultWriter writer(store,AsyncResultWriter::required_bytes(denied)-1);rejects([&]{writer.write(std::move(denied));});check(denied.fields.size()==3,"denied move retains owning values");writer.close();}
 std::cout<<"Independent result checks="<<checks<<" PASS\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

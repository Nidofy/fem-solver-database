#define main upstream_state_main
#include "../../../../build-dbf3-review/fixed47/tests/dbf3_state_tests.cpp"
#undef main
int main(){try{
 auto m=ModelCompiler{}.compile<double>(ReaderRegistry{}.read("tests/fixtures/abaqus/forced_tet.inp").model);StateDB<double>s(m.mesh),dst(m.mesh);register_fields(s);register_fields(dst);s.data().dt=.01;RestartInfo info;info.configuration=123;
 std::filesystem::path dir="verification/reviews/DBF3/independent-astra/scratch",path=dir/"wire-clean.sdbrst",bad=dir/"wire-bad.sdbrst";RestartDB::save(path,m,s,info);
 struct R{unsigned id;native::Bytes b;};std::vector<R> records;{std::ifstream in(path,std::ios::binary);native::prefix(in,restart_magic);while(in.peek()!=EOF){auto h=native::header(in);records.push_back({h.kind,native::body(in,h)});}}
 for(unsigned scenario=0;scenario<6;++scenario){auto altered=records;auto manifest=std::find_if(altered.begin(),altered.end(),[](auto&r){return r.id==200;});auto block=std::find_if(altered.begin(),altered.end(),[](auto&r){return r.id==201;});
 if(scenario==0)manifest->b[0]=2;
 if(scenario==1){std::string from="synthetic-storage-fixture.state";auto at=std::search(manifest->b.begin(),manifest->b.end(),from.begin(),from.end());check(at!=manifest->b.end(),"manifest schema located");*(at+from.size()-1)='z';}
 if(scenario==2)block->id=202;
 if(scenario==3)std::fill(block->b.begin()+16,block->b.begin()+24,255);
 if(scenario==4)block->b[8]=1;
 if(scenario==5)altered.erase(block);
 native::atomic_stream(bad,[&](auto&out){native::prefix(out,restart_magic);for(auto&r:altered)native::record(out,r.id,r.b);});
 dst.data().displacement.fill(63);dst.registered_field<Field<std::uint64_t>>("exact").fill(17);auto core=dst.data().displacement.checked_read();auto ext=dst.registered_field<Field<std::uint64_t>>("exact").checked_read();
 rejects([&]{RestartDB::load(bad,m,dst,123);});check(core.at(0)==63 && ext.at(0)==17,"malformed required blocks preserve all views");
 }
 // Oversized manifest body is refused from its header, before a body allocation.
 native::atomic_stream(bad,[&](auto&out){native::prefix(out,restart_magic);for(auto&r:records){if(r.id==200){native::Writer w;w.integer<std::uint32_t>(200);w.integer<std::uint64_t>(UINT64_MAX);w.integer<std::uint32_t>(0);native::write(out,w.bytes);break;}native::record(out,r.id,r.b);}});
 rejects([&]{RestartDB::load(bad,m,dst,123);});
 std::cout<<"Independent malformed registered wire checks="<<checks<<" PASS\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}


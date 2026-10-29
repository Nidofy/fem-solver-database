#define main upstream_state_main
#include "../../../../build-dbf3-review/fixed47/tests/dbf3_state_tests.cpp"
#undef main
int main(int argc,char**argv) {
 try {
  std::filesystem::path fixture="tests/fixtures/abaqus/forced_tet.inp", dir="verification/reviews/DBF3/independent-astra/scratch"; std::filesystem::create_directories(dir);
  contracts<float>(fixture,dir);contracts<double>(fixture,dir);cpu_run_contracts<float>(fixture,dir);cpu_run_contracts<double>(fixture,dir);
  std::cout<<"Reexecuted upstream state checks: "<<checks<<"\n";
  auto m=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(fixture).model);
  StateDB<double> src(m.mesh,true),dst(m.mesh,true);register_fields(src);register_fields(dst);src.data().dt=.01;
  src.data().rotation->inertia.fill(2);src.data().rotation->inverse_inertia.fill(.5);src.data().rotation->angle.fill(8);
  auto chk=dir/"rotation.chk",rst=dir/"rotation.sdbrst";RestartInfo info;info.configuration=77;
  save_checkpoint(chk,m.mesh,src);RestartDB::save(rst,m,src,info);load_checkpoint(chk,m.mesh,dst);RestartDB::load(rst,m,dst,77);
  check(dst.data().rotation->angle.read()(0)==8,"rotation exact");
  src.data().rotation->inertia.write()(0,0)=-2;
  save_checkpoint(chk,m.mesh,src);RestartDB::save(rst,m,src,info);
  bool chk_reject=false,rst_reject=false;
  try {load_checkpoint(chk,m.mesh,dst);}catch(const std::exception&){chk_reject=true;}
  try {RestartDB::load(rst,m,dst,77);}catch(const std::exception&){rst_reject=true;}
  std::cout<<"PROBE negative rotation inertia checkpoint rejected="<<chk_reject<<" restart rejected="<<rst_reject<<" published inertia="<<dst.data().rotation->inertia.read()(0,0)<<"\n";
  // Same-length schema drift must reject before touching borrowed core and extension views.
  StateDB<double> alternate(m.mesh,true);register_fields(alternate,"synthetic-storage-fixture.statz");
  auto stable=alternate.data().rotation->angle.checked_read();alternate.data().rotation->angle.fill(37);
  rejects([&]{load_checkpoint(chk,m.mesh,alternate);});check(stable.at(0)==37,"manifest drift preserves rotation view");
  // Late semantic corruption of required registered scalar (invalid validity) with a correct CRC.
  src.data().rotation->inertia.fill(2);save_checkpoint(chk,m.mesh,src);auto bytes=read_file(chk);
  checkpoint_detail::Reader reader{bytes};auto magic=reader.integer<std::uint64_t>();checkpoint_detail::Writer rebuilt;rebuilt.integer(magic);
  bool changed=false;
  while(reader.pos<bytes.size()) {
   auto id=reader.integer<std::uint32_t>();auto count=reader.integer<std::uint64_t>();reader.integer<std::uint32_t>();
   checkpoint_detail::Writer block;block.bytes.assign(bytes.begin()+reader.pos,bytes.begin()+reader.pos+count);reader.pos+=count;
   if(id==201){std::size_t p=0;auto index=registered_detail::scalar<std::uint64_t>(block.bytes,p);if(index==4){block.bytes[32]=2;changed=true;}}
   rebuilt.chunk(id,block);
  }
  check(changed,"located validity record");write_file(chk,rebuilt.bytes);
  dst.data().rotation->angle.fill(43);auto rv=dst.data().rotation->angle.checked_read();auto ev=dst.registered_field<Field<std::uint64_t>>("exact").checked_read();auto old=ev.at(0);
  rejects([&]{load_checkpoint(chk,m.mesh,dst);});check(rv.at(0)==43 && ev.at(0)==old,"late extension semantic failure transactional rotation/core/extensions");
  std::cout<<"Independent state checks total: "<<checks<<" PASS (negative-inertia behavior separately reported)\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}


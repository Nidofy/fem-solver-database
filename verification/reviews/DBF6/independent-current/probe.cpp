#define main dbf6_original_main
#include "snapshot/dbf6_contract_tests.cpp"
#undef main
int main(int argc,char **argv) {
  try {
    if(argc!=2) return 2;
    std::filesystem::path dir=argv[1]; std::filesystem::create_directories(dir);
    DofSpace d(definition()); auto bytes=d.encode();
    for(std::size_t i=0;i<bytes.size();++i) rejects([&]{DofSpace::decode(std::span(bytes).first(i));});
    auto trail=bytes; trail.push_back(0); rejects([&]{DofSpace::decode(trail);});
    auto s=registry(d); FoundationStore::save(dir/"good.sdbf",d,s);
    std::ifstream in(dir/"good.sdbf",std::ios::binary); n::Bytes file((std::istreambuf_iterator<char>(in)),{});
    auto target=registry(d); target.get<Field<double>>("temperature").fill(777);
    auto live=target.get<Field<double>>("temperature").checked_read();
    for(std::size_t i=0;i<file.size();i+=17) {
      {std::ofstream out(dir/"truncated.sdbf",std::ios::binary); out.write(reinterpret_cast<const char*>(file.data()),i);}
      rejects([&]{FoundationStore::restore(dir/"truncated.sdbf",d,target);});
      check(live.at(0)==777,"truncation cannot publish or invalidate target");
    }
    for(std::uint64_t cap : {0ull,16ull,256ull}) {
      rejects([&]{FoundationStore::save(dir/"good.sdbf",d,s,cap);});
      std::ifstream same(dir/"good.sdbf",std::ios::binary); n::Bytes unchanged((std::istreambuf_iterator<char>(same)),{});
      check(unchanged==file,"failed budget save preserves published bytes");
    }
    s.begin_trial(); rejects([&]{FoundationStore::save(dir/"trial.sdbf",d,s);});
    rejects([&]{RegisteredState copy(s);}); s.rollback();
    auto bad=node_layout(); bad.addresses[0].face=1; rejects([&]{bad.validate(descriptor("x"));});
    auto before=s.get<Field<double>>("temperature").checked_read();
    rejects([&]{s.remap("temperature",bad);}); check(before.at(0)==300,"bad remap leaves owner live");
    std::cout<<"independent adversarial checks="<<checks<<" PASS\n"; return 0;
  } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}

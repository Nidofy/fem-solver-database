#include <sdb/lifecycle.hpp>
#include <iostream>
using namespace sdb;using namespace sdb::lifecycle;
template<class T> native::Bytes image(const StateDB<T>&s){native::Writer w;w.scalar(s.data().time);w.scalar(s.data().dt);w.integer(s.data().step);s.data().fields([&](const auto&f){auto v=f.read();for(unsigned c=0;c<std::decay_t<decltype(f)>::components;++c)for(std::size_t n=0;n<f.size();++n)w.scalar(v(n,c));});return w.bytes;}
int main(int argc,char**argv){try{auto dir=std::filesystem::path(argv[1]),out=std::filesystem::path(argv[2]);
 auto m=ModelStore::load<double>(dir/"double.sdbmodel");StateDB<double> s(m.mesh,true);s.data().fields([](auto&f){f.fill(typename std::decay_t<decltype(f)>::value_type(1));});s.data().time=9;s.data().dt=.2;s.data().step=91;auto before=image(s);
 std::ifstream in(dir/"double.sdbrst",std::ios::binary);native::prefix(in,restart_magic);std::vector<std::pair<unsigned,native::Bytes>> records;
 while(in.peek()!=std::char_traits<char>::eof()){auto h=native::header(in);records.push_back({h.kind,native::body(in,h)});}
 unsigned checks=0;
 for(unsigned mode=0;mode<4;++mode){auto changed=records;
  if(mode==0){auto &last=changed.at(changed.size()-2);if(last.first!=20)throw std::runtime_error("expected torque last");double nan=std::numeric_limits<double>::quiet_NaN();auto bits=std::bit_cast<std::uint64_t>(nan);for(unsigned k=0;k<8;++k)last.second[29+k]=static_cast<std::uint8_t>(bits>>(8*k));}
  if(mode==1)changed.pop_back();
  if(mode==2)changed.push_back({900,{}});
  if(mode==3){auto info=native::unpack<RestartInfo>(changed[0].second);info.state_schema=99;changed[0].second=native::pack(info);}
  auto path=out/("atomic-"+std::to_string(mode)+".sdbrst");{std::ofstream f(path,std::ios::binary);native::prefix(f,restart_magic);for(auto &[kind,b]:changed)native::record(f,kind,b);}
  bool rejected=false;try{RestartDB::load(path,m,s,77);}catch(const std::exception&e){rejected=true;std::cout<<"fault "<<mode<<": "<<e.what()<<'\n';}
  if(!rejected||image(s)!=before)throw std::runtime_error("non-atomic restore");++checks;
 }
 auto rm=ResultModel::from(m,77);rm.nodes[0].index=999;rm.nodes[0].id=999;rm.nodes[1].key=rm.nodes[0].key;
 auto path=out/"bad-source.sdbres";std::filesystem::remove(path);
 bool accepted=true;try{{NativeResultStorage w(path,rm,false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3);}ResultReader r(path);}catch(...){accepted=false;}
 std::cout<<checks<<" full-state restore atomicity checks PASS; inconsistent source-map accepted="<<accepted<<'\n';return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

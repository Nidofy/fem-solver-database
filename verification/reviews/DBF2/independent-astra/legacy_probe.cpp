#include <sdb/results/results.hpp>
#include <iostream>
using namespace sdb;using namespace sdb::lifecycle;
native::Bytes hex(const std::filesystem::path&p){std::ifstream in(p);std::string all((std::istreambuf_iterator<char>(in)),{}),s;for(unsigned char c:all)if(!std::isspace(c))s+=char(c);native::Bytes b;for(std::size_t i=0;i<s.size();i+=2)b.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(i,2),nullptr,16)));return b;}
native::Bytes bytes(const std::filesystem::path&p){std::ifstream in(p,std::ios::binary);return native::Bytes(std::istreambuf_iterator<char>(in),{});}
int main(int argc,char**argv){try{std::filesystem::path root=argv[1],out=argv[2];unsigned checks=0;
 for(auto name:{"foundation/legacy.sdbmodel","foundation3/schema2.sdbmodel"}){auto b=hex(root/(std::string(name)+".hex"));auto p=out/std::filesystem::path(name).filename();{std::ofstream f(p,std::ios::binary);native::write(f,b);}auto m=ModelStore::load<double>(p);auto q=p;q+=".resaved";ModelStore::save(q,m);if(bytes(q)!=b)throw std::runtime_error("old model byte drift");std::cout<<"model schema "<<m.definition.schema<<" exact-byte resave PASS\n";++checks;}
 for(auto name:{"native1.sdbres","indexed2.sdbres"}){auto b=hex(root/"foundation3"/(std::string(name)+".hex"));auto p=out/name;{std::ofstream f(p,std::ios::binary);native::write(f,b);}ResultReader r(p);auto f=r.field(0,"U");if(r.index.size()!=1||f.values.size()!=15)throw std::runtime_error("legacy frame shape");for(auto x:f.values)if(x!=0)throw std::runtime_error("legacy displacement oracle");std::cout<<name<<" decoded initial 5-node displacement oracle PASS\n";++checks;}
 std::cout<<checks<<" independent frozen legacy checks PASS\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

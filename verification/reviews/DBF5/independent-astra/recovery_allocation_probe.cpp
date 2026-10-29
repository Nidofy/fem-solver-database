#include <sdb/results/results.hpp>
#include <cstdlib>
#include <iostream>
#include <new>
static bool tracking=false;
static std::size_t largest=0;
void* operator new(std::size_t n) { if(tracking && n>largest)largest=n; if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
int main(int argc,char**argv){
    using namespace sdb;using namespace sdb::lifecycle;
    if(argc!=2)return 2;
    const std::filesystem::path path=argv[1];
    std::filesystem::remove(path);
    ResultModel m;m.mesh=1;m.definition=2;m.configuration=3;
    m.nodes.resize(1);m.nodes[0].id=1;m.nodes[0].index=0;m.nodes[0].key={"P","I",1};m.coordinates.resize(1);
    {NativeResultStorage out(path,m,false,false,native::default_limit,{},false,ResultFormat::CompressedIndexed4);}
    const auto initial=std::filesystem::file_size(path);
    {std::ofstream out(path,std::ios::binary|std::ios::app);native::Bytes huge(8*1024*1024);native::record(out,2,huge);}
    bool accepted=false;std::string error;
    tracking=true;
    try{ResultReader recovered(path,true);accepted=recovered.valid_end==initial&&recovered.incomplete_tail;}
    catch(const std::exception&e){error=e.what();}
    tracking=false;
    std::cout<<"recover_accepted="<<accepted<<" largest_new="<<largest<<" stored_cap="<<compressed_detail::max_stored+compressed_detail::envelope_bytes<<" error="<<error<<'\n';
    return accepted&&largest>compressed_detail::max_stored+compressed_detail::envelope_bytes?0:1;
}

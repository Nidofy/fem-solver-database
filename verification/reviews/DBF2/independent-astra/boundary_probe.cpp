#include <sdb/implicit.hpp>
#include <sdb/results/results.hpp>
#include <iostream>
using namespace sdb;using namespace sdb::lifecycle;
int main(){
 MatrixData<double> d;d.block_rows=1;d.block_size=1024;d.offsets={0,1};d.columns={0};
 auto bytes=native::pack(d);auto baseline=host_memory.live.load();host_memory.peak=baseline;
 std::string error;try{auto x=SparseMatrix<double,1024>::decode(bytes,1024);}catch(const std::exception&e){error=e.what();}
 std::cout<<"matrix bytes="<<bytes.size()<<" decode budget=1024 allocated aligned peak="<<host_memory.peak.load()-baseline<<" error="<<error<<'\n';
 ModelBuilder<double> b;b.add_node(UINT64_MAX,{0,0,0});auto m=b.finalize();
 std::cout<<"accepted max node label="<<m.node_id(0)<<" typed external_label valid="<<m.external_label(NodeId(0)).valid()<<'\n';
}

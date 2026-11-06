#include <sdb/mesh_access.hpp>
#include <chrono>
#include <iostream>
int main(){
    constexpr std::size_t count=120000,iterations=30;
    constexpr std::size_t nodes=16384;
    sdb::ModelBuilder<double>b;b.reserve(nodes,count);b.add_material({1,42,{2},0});b.add_part(1);
    for(std::size_t i=0;i<nodes;++i)b.add_node(i+1,{double(i),0,0});
    std::uint64_t expected=0;
    for(std::size_t i=0;i<count;++i){std::vector<sdb::Id> ids;expected+=1;
        for(std::size_t c=0;c<4;++c){auto n=(i*17+c)%nodes;ids.push_back(n+1);expected+=n;}
        b.add_element(i+1,sdb::Topology::Quad4,std::move(ids),1,1);}
    expected*=iterations;
    auto mesh=b.finalize();sdb::FixedBatchRead<double,4> batch(mesh,0);
    std::vector<double> times[2];std::uint64_t observed=0;
    for(int repeat=0;repeat<8;++repeat)for(int order=0;order<2;++order){
        int method=(repeat+order)%2;std::uint64_t sum=0;
        auto start=std::chrono::steady_clock::now();
        for(std::size_t pass=0;pass<iterations;++pass) {
            if(method==0)for(auto id:mesh.batches()[0].elements){
                const auto &el=mesh.element(id);sum+=mesh.materials()[el.material].id;
                for(auto node:mesh.connectivity(id))sum+=node;
            }else{
                const auto material=batch.material().id;
                for(std::size_t i=0;i<batch.size();++i){sum+=material;for(std::size_t c=0;c<4;++c)sum+=batch.node(i,c);}
            }
        }
        double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        if(sum!=expected)return 1;observed=sum;if(repeat)times[method].push_back(elapsed);
    }
    for(auto &t:times)std::sort(t.begin(),t.end());
    std::cout<<"{\"rows\":"<<count<<",\"iterations\":"<<iterations<<",\"repeats\":7,\"additional_packed_bytes\":0,\"view_bytes\":"<<sizeof(batch)
             <<",\"query_median_seconds\":"<<times[0][3]<<",\"batch_median_seconds\":"<<times[1][3]<<",\"checksum\":"<<observed<<"}\n";
}

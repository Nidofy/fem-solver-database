#include <sdb/field_access.hpp>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
int main(){
    constexpr std::size_t rows=160000,components=3,iterations=50;
    sdb::Field<double,components> a(rows),b(rows);
    std::vector<std::size_t> permutation(rows*components);std::iota(permutation.begin(),permutation.end(),0);
    std::mt19937 random(1729);std::shuffle(permutation.begin(),permutation.end(),random);
    auto begin=std::chrono::steady_clock::now();sdb::DenseAccessPlan<components> plan(rows,permutation);
    double setup=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    std::vector<double> times[2];
    for(int repeat=0;repeat<8;++repeat)for(int order=0;order<2;++order){
        int method=(repeat+order)%2;a.fill(1);b.fill(2);
        auto av=a.write();auto bv=b.read();auto ap=plan.bind(av);auto bp=plan.bind(bv);
        begin=std::chrono::steady_clock::now();
        for(std::size_t k=0;k<iterations;++k) {
            if(method==0)for(auto dense:permutation)av(dense/components,dense%components)+=0.25*bv(dense/components,dense%components);
            else for(std::size_t i=0;i<ap.size();++i)ap[i]+=0.25*bp[i];
        }
        auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        for(std::size_t c=0;c<components;++c)for(double v:sdb::component_span(a.read(),c))if(v!=26)return 1;
        if(repeat)times[method].push_back(elapsed);
    }
    for(auto &t:times)std::sort(t.begin(),t.end());
    std::cout<<"{\"rows\":"<<rows<<",\"iterations\":"<<iterations<<",\"repeats\":7,\"plan_bytes\":"<<plan.bytes()
             <<",\"setup_seconds\":"<<setup<<",\"dynamic_median_seconds\":"<<times[0][3]<<",\"planned_median_seconds\":"<<times[1][3]<<",\"values_verified\":true}\n";
}

#include <sdb/general_contact/search.hpp>
#include <chrono>
#include <iostream>
using namespace sdb;
namespace gc=sdb::general_contact;
int main() {
    using Box=gc::detail::Box<double>;
    std::vector<Box> boxes(32768);
    for(std::size_t i=0;i<boxes.size();++i) {
        boxes[i].add({double(i%256),double(i/256),0});
        boxes[i].add({double(i%256)+.1,double(i/256)+.1,.1});
    }
    gc::detail::BVH<double> cached(boxes);
    using clock=std::chrono::steady_clock;
    double refit=0,rebuild=0;
    for(unsigned step=0;step<12;++step) {
        for(std::size_t i=step;i<boxes.size();i+=100) {boxes[i].lower[2]+=.05;boxes[i].upper[2]+=.05;}
        auto t0=clock::now();
        cached.refit(boxes);
        auto t1=clock::now();
        gc::detail::BVH<double> fresh(boxes);
        auto t2=clock::now();
        refit+=std::chrono::duration<double,std::milli>(t1-t0).count();
        rebuild+=std::chrono::duration<double,std::milli>(t2-t1).count();
        for(std::size_t i=0;i<boxes.size();i+=71) {
            std::vector<Index> a,b;
            cached.query(boxes[i].upper,.01,[&](auto f){a.push_back(f);});
            fresh.query(boxes[i].upper,.01,[&](auto f){b.push_back(f);});
            if(a!=b || a!=std::vector<Index>{static_cast<Index>(i)}) return 1;
        }
    }
    std::cout<<"faces="<<boxes.size()<<" steps=12 refit_ms="<<refit<<" rebuild_ms="<<rebuild<<'\n';
}

#include <sdb/foundation.hpp>
#include <chrono>
#include <iostream>
int main(){
    using namespace sdb;
    constexpr std::size_t rows=1000000;
    RegisteredState state;FieldDescriptor d;d.association=Association::Node;d.location=FieldLocation::Entity;
    for(unsigned i=0;i<4;++i){d.name="v"+std::to_string(i);state.add<Field<double>>(d,rows,"benchmark",1,{7},0);}
    state.freeze();
    std::cout<<"policy,rows,fields,begin_ms,trial_aligned_bytes,write_aligned_bytes,detached_value_bytes\n";
    for(auto policy:{TrialPolicy::DeepCopy,TrialPolicy::CopyOnWrite}){
        StateSession session(state);auto baseline=host_memory.live.load();
        auto start=std::chrono::steady_clock::now();session.begin_trial(policy);
        auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        auto trial=host_memory.live.load()-baseline;
        {auto lease=session.write();lease.field<Field<double>>("v0").at(rows-1)=9;}
        auto written=host_memory.live.load()-baseline;auto stats=session.memory();
        session.rollback();auto read=session.read();
        if(read.field<Field<double>>("v0").at(rows-1)!=7 || host_memory.live!=baseline)return 1;
        std::cout<<(policy==TrialPolicy::DeepCopy?"deep":"cow")<<','<<rows<<",4,"<<ms<<','<<trial<<','<<written<<','<<stats.detached_value_bytes<<'\n';
    }
}

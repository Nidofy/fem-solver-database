#include <sdb/foundation.hpp>
#include <chrono>
#include <iostream>
using namespace sdb;
int main(int argc,char **argv){try{
    auto rows=argc>1?std::stoull(argv[1]):1000000;
    FieldDescriptor d;d.name="velocity";d.shape={3};d.components={"X","Y","Z"};d.association=Association::Node;d.location=FieldLocation::Entity;
    RegisteredState state;state.bind_context(1,1);state.add<Field<double,3>>(d,rows,"benchmark",1,{1,2,3},0);state.freeze();
    auto bytes=state_payload_bytes(state);auto begin=std::chrono::steady_clock::now();StateSession session(state,product(bytes,2));
    auto cloned=std::chrono::steady_clock::now();session.begin_trial();auto trial=std::chrono::steady_clock::now();
    {auto w=session.write();if(rows)w.field<Field<double,3>>("velocity").at(rows-1,2)=9;}
    session.rollback();auto rolled=std::chrono::steady_clock::now();
    {auto r=session.read();if(rows && r.field<Field<double,3>>("velocity").at(rows-1,2)!=3)throw std::runtime_error("rollback");}
    auto stats=session.memory();
    std::cout<<"rows,resident_payload,high_water_payload,copied_payload,clone_ms,trial_ms,rollback_ms\n"
             <<rows<<','<<stats.resident_payload<<','<<stats.high_water_payload<<','<<stats.copied_payload<<','
             <<std::chrono::duration<double,std::milli>(cloned-begin).count()<<','
             <<std::chrono::duration<double,std::milli>(trial-cloned).count()<<','
             <<std::chrono::duration<double,std::milli>(rolled-trial).count()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}

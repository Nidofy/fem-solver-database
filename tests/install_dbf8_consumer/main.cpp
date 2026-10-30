#include <sdb/foundation.hpp>
#include <sdb/process_memory.hpp>
#include <iostream>
#ifdef EXPECT_INDEX_BITS
static_assert(sdb::index_bits==EXPECT_INDEX_BITS);
#endif
int main(){
    using namespace sdb;
    FieldDescriptor d;d.name="temperature";d.association=Association::Node;d.location=FieldLocation::Entity;
    RegisteredState state;state.add<Field<double>>(d,2,"thermal",1,{300},0);state.freeze();
    StateSession owner(state);owner.begin_trial(TrialPolicy::CopyOnWrite);
    {auto lease=owner.write();lease.field<Field<double>>("temperature").at(0)=310;}
    owner.rollback();auto lease=owner.read();if(lease.field<Field<double>>("temperature").at(0)!=300)return 1;
    auto stats=process_memory::stats();if(!stats.live || !stats.allocations)return 2;
    std::cout<<"installed standalone SDK index_bits="<<index_bits<<" COW + process memory PASS\n";
}

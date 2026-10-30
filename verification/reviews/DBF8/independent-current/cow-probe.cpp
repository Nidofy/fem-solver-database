#include <sdb/foundation.hpp>
#include <sdb/process_memory.hpp>
#include <iostream>
using namespace sdb;
int main(){
 for(std::size_t extra=0;extra<12000;extra+=32){
  RegisteredState state;FieldDescriptor d;d.name="v";d.association=Association::Node;d.location=FieldLocation::Entity;
  StateLayout layout;layout.domain=64;layout.addresses.resize(64);for(unsigned i=0;i<64;++i)layout.addresses[i].entity=i+1;
  state.add<Field<double>>(d,64,"test",1,{7},0,layout);d.name="w";state.add<Field<double>>(d,64,"test",1,{8},0,layout);state.freeze();
  auto view=state.get<Field<double>>("v").checked_read();auto baseline=process_memory::stats();process_memory::set_limit(baseline.live+extra);
  bool failed=false;try{state.begin_trial_cow();}catch(const std::bad_alloc&){failed=true;}
  process_memory::set_limit(std::numeric_limits<std::size_t>::max());
  if(failed){bool stale=false;try{(void)view.at(0);}catch(const std::logic_error&){stale=true;}
   if(stale){std::cout<<"OOM after allowance="<<extra<<" invalidated live borrow while in_trial="<<state.in_trial()<<" value="<<state.get<Field<double>>("v").read()(0)<<'\n';return 0;}}
 }
 std::cout<<"no partial borrow invalidation found\n";return 0;
}

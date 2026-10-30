#include <sdb/state_session.hpp>
#include <iostream>
int main(){using namespace sdb;auto max=std::numeric_limits<std::size_t>::max();if(state_counter_add(max-3,4)!=max || state_counter_add(2,3)!=5)return 1;Field<int> f(1);f.fill(7);auto view=f.checked_read();auto shared=f.fork_shared();shared.write()(0)=9;if(f.read()(0)!=7 || shared.read()(0)!=9)return 2;try{view.at(0);return 3;}catch(const std::logic_error&){}std::cout<<"closure counter/COW smoke PASS\n";}

#include <cstdlib>
#include <new>
#include <cstddef>
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0) throw std::bad_alloc();if(fail_after>0)--fail_after;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p) noexcept{std::free(p);}
void operator delete(void*p,std::size_t) noexcept{std::free(p);}
#define main upstream_state_main
#include "../../../../build-dbf3-review/fixed47/tests/dbf3_state_tests.cpp"
#undef main
int main(){
 auto m=ModelCompiler{}.compile<double>(ReaderRegistry{}.read("tests/fixtures/abaqus/forced_tet.inp").model);
 CpuRun<double> src(m);register_fields(src.state());src.advance();src.save("verification/reviews/DBF3/independent-astra/scratch/alloc.sdbrst");
 for(long n=0;n<2000;++n){
  CpuRun<double> dst(m);register_fields(dst.state());auto view=dst.state().registered_field<Field<std::uint64_t>>("exact").checked_read();auto cursor=dst.increment();
  bool failed=false;fail_after=n;
  try{dst.restore("verification/reviews/DBF3/independent-astra/scratch/alloc.sdbrst");}catch(const std::bad_alloc&){failed=true;}catch(...){fail_after=-1;throw;}
  fail_after=-1;
  if(!failed){std::cout<<"No mutation on allocation failure through "<<n<<" allocations\n";break;}
  bool invalidated=false;try{view.at(0);}catch(...){invalidated=true;}
  if(invalidated || dst.increment()!=cursor){std::cout<<"PROBE bad_alloc AFTER PUBLICATION: failure allocation="<<n<<" old view invalidated="<<invalidated<<" cursor before="<<cursor<<" after="<<dst.increment()<<"\n";return 0;}
 }
}


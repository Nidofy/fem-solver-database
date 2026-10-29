#include <exception>
#include <cstdio>
#include <cstring>
static int destroyed=0;
struct Owned : std::exception {char message[64]="owned diagnostic"; Owned()=default; Owned(const Owned&)=default; ~Owned(){++destroyed;} const char *what()const noexcept override{return message;}};
const char *diagnostic(std::exception_ptr p) noexcept {try {std::rethrow_exception(p);}catch(const std::exception &e){return e.what();}catch(...){return "fallback";}}
int main(){std::exception_ptr p;try{throw Owned();}catch(...){p=std::current_exception();}int before=destroyed; const char *result=diagnostic(p);int after=destroyed;std::printf("exception destructors before what=%d after what=%d; returned address=%p\n",before,after,(const void*)result);if(after>before){std::puts("CONFIRMED: rethrow copy destroyed before diagnostic pointer is consumed");return 0;}std::puts("runtime reused retained exception object");return 0;}

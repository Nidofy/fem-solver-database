#include <sdb/io/durability.hpp>
#include <cstdio>
#include <cstring>
#include <stdexcept>
static int destroyed=0;
struct Owned : std::exception {char message[64]="owned diagnostic"; Owned()=default; Owned(const Owned&)=default; ~Owned(){++destroyed;} const char *what()const noexcept override{return message;}};
int main(){sdb::durability::PublicationError error;try{throw Owned();}catch(...){error.capture(true,true);}int before=destroyed; const char *result=error.what();int after=destroyed;if(after!=before||std::strcmp(result,"owned diagnostic")){std::puts("FAIL owned diagnostic");return 1;}auto copied=error;if(copied.what()==error.what()||std::strcmp(copied.what(),"owned diagnostic")||!copied.published()){std::puts("FAIL copied wrapper diagnostic");return 2;}sdb::durability::PublicationError long_error;try{throw std::runtime_error(std::string(3000,'X'));}catch(...){long_error.capture(false,false);}if(std::strlen(long_error.what())!=1023){std::puts("FAIL bounded long message");return 3;}std::printf("PASS owned diagnostic: destructor count before what=%d after what=%d; independent copied buffer; 1023-byte truncation\n",before,after);}

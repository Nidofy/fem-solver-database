#include <cstdlib>
#include <cstdio>
#include <cstring>
int main(){
    // Deliberately bypass C++ new. The OS launcher must still bound this memory.
    void *blocks[512]{};std::size_t count=0;
    for(;count<512;++count){blocks[count]=std::malloc(1024*1024);if(!blocks[count])break;std::memset(blocks[count],1,1024*1024);}
    for(std::size_t i=0;i<count;++i)std::free(blocks[i]);
    std::printf("malloc worker admitted_mib=%zu denied=%d\n",count,count<512);
    return count>0 && count<128?0:1;
}

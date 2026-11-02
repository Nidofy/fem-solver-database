#include <sdb/io/native.hpp>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        std::string payload(32768,argv[2][0]);
        sdb::lifecycle::native::atomic_stream(argv[1],[&](auto &out){out.write(payload.data(),payload.size());});
    } catch (const sdb::durability::PublicationError& e) {
        if(e.published() || e.replacement_may_have_happened()) return 3;
        std::cerr << e.what() << '\n'; return 17;
    }
}

#include <sdb/results/async_writer.hpp>
#include <atomic>
#include <cstdlib>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
// Count actual ordinary heap allocations only inside the admission operation.
// This catches a validation tree allocated before a tiny queue budget rejects.
static std::atomic<bool> measuring{false};
static std::atomic<std::size_t> allocated{0};
void *operator new(std::size_t n) {
    if (measuring.load()) allocated.fetch_add(n);
    if (auto p=std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p,std::size_t) noexcept { std::free(p); }
struct Sink : IResultStorage {
    void append(const ResultFrame &) override {}
    void flush() override {}
    std::optional<FrameInfo> last() const override { return {}; }
};
int main() {
    try {
        FieldDescriptor schema;schema.name="COUNT";schema.scalar=ScalarType::UInt64;
        schema.association=Association::Node;schema.location=FieldLocation::Entity;
        std::vector<ResultAddress> addresses(100000);
        for(std::size_t i=0;i<addresses.size();++i) addresses[i].entity=i+1;
        ResultFrame frame;
        frame.fields.push_back(make_typed_field<std::uint64_t>(schema,std::move(addresses),std::vector<std::uint64_t>(100000)));
        Sink sink;AsyncResultWriter writer(sink,1024);
        allocated=0;measuring=true;
        auto charge=AsyncResultWriter::required_bytes(frame);
        measuring=false;
        if(allocated.load()!=0 || charge<=1024) throw std::runtime_error("admission accounting allocates scratch");
        allocated=0;measuring=true;bool rejected=false;
        try { writer.write(std::move(frame)); } catch(const std::length_error &) { rejected=true; }
        measuring=false;
        const auto rejection_bytes=allocated.load();
        writer.close();
        if(!rejected || rejection_bytes>4096 || frame.fields.at(0).typed->addresses.size()!=100000)
            throw std::runtime_error("rejected admission allocated unbounded scratch or consumed source");
        auto malformed=frame;malformed.fields[0].typed->addresses[1]=malformed.fields[0].typed->addresses[0];
        AsyncResultWriter admitted(sink,AsyncResultWriter::required_bytes(malformed));
        admitted.write(std::move(malformed));
        rejected=false;try { admitted.close(); } catch(const std::invalid_argument &) { rejected=true; }
        if(!rejected || !admitted.stats().failed) throw std::runtime_error("worker skipped admitted payload validation");
        std::cout<<"allocation-free admission; tiny-budget rejection bytes="<<rejection_bytes
                 <<"; post-admission validation PASS\n";
    } catch(const std::exception &e) { measuring=false;std::cerr<<e.what()<<'\n';return 1; }
}

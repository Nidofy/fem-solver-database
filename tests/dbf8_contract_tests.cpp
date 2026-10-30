#include <sdb/foundation.hpp>
#include <sdb/process_memory.hpp>
#include <sdb/implicit.hpp>
#include <iostream>
#include <thread>
#include <atomic>
using namespace sdb;
namespace pm=sdb::process_memory;
static int checks=0;
void check(bool x,const char *message){++checks;if(!x)throw std::runtime_error(message);}
template<class F> void rejects(F f){++checks;try{f();}catch(const std::exception&){return;}throw std::runtime_error("expected failure");}
int main(){try{
    constexpr std::size_t rows=100000;
    Field<double> plain(rows);plain.fill(7);auto old=plain.checked_read();
    auto shared=plain.fork_shared();rejects([&]{old.at(0);});check(shared.read().data==plain.read().data,"explicit sharing");
    auto deep=plain;check(deep.read().data!=plain.read().data,"ordinary copy remains deep");
    shared.write()(0)=8;check(plain.read()(0)==7 && shared.read()(0)==8,"detach before write");
    FieldDescriptor d;d.name="v";d.association=Association::Node;d.location=FieldLocation::Entity;
    RegisteredState state;state.bind_context(1,1);state.add<Field<double>>(d,rows,"cow-test",1,{3},0);state.freeze();
    StateSession session(state);auto before=host_memory.live.load();auto allocation_count=host_memory.allocations.load();
    session.begin_trial(TrialPolicy::CopyOnWrite);
    check(host_memory.live==before && host_memory.allocations==allocation_count,"COW trial allocates no aligned value buffers");
    auto prior=pm::stats();pm::set_limit(prior.live+32768);
    bool denied=false;
    {auto write=session.write();try{write.field<Field<double>>("v").at(0)=9;}catch(const std::bad_alloc&){denied=true;}}
    pm::set_limit(std::numeric_limits<std::size_t>::max());check(denied,"COW detach global admission failure");
    {auto read=session.read();check(read.field<Field<double>>("v").at(0)==3,"failed detach unchanged");}
    {auto write=session.write();write.field<Field<double>>("v").at(rows-1)=9;}
    check(host_memory.live==before+rows*sizeof(double),"only touched field detached");
    session.rollback();{auto read=session.read();check(read.field<Field<double>>("v").at(rows-1)==3,"COW rollback");}
    check(host_memory.live==before,"rollback releases detached buffer");
    session.begin_trial(TrialPolicy::CopyOnWrite);{auto write=session.write();write.field<Field<double>>("v").at(0)=11;}session.commit();
    {auto read=session.read();check(read.field<Field<double>>("v").at(0)==11,"COW commit");}
    // Sweep metadata staging OOM: failed COW begin preserves existing checked borrows.
    unsigned failed_stages=0,successful_stages=0;
    for(std::size_t allowance=0;allowance<20000;allowance+=32) {
        RegisteredState staged;FieldDescriptor md=d;md.missing=MissingPolicy::ValidityMask;
        StateLayout layout;layout.domain=64;layout.addresses.resize(64);
        for(unsigned i=0;i<64;++i)layout.addresses[i].entity=i+1;
        staged.add<Field<double>>(md,64,"oom",1,{7},0,layout);md.name="w";
        staged.add<Field<double>>(md,64,"oom",1,{8},0,layout);staged.freeze();
        auto view=staged.get<Field<double>>("v").checked_read();
        auto mask=staged.find("v").validity_field().checked_read();
        auto baseline=pm::stats();pm::set_limit(baseline.live+allowance);
        bool failed=false;try{staged.begin_trial_cow();}catch(const std::bad_alloc&){failed=true;}
        pm::set_limit(std::numeric_limits<std::size_t>::max());
        if(failed){++failed_stages;check(!staged.in_trial() && view.at(0)==7 && mask.at(0)==1,"atomic COW metadata OOM");}
        else {++successful_stages;rejects([&]{view.at(0);});rejects([&]{mask.at(0);});
            staged.find("v").validity_field().write()(0)=0;staged.rollback();
            check(staged.find("v").validity_field().read()(0)==1,"validity mask COW rollback");}
    }
    check(failed_stages && successful_stages,"COW OOM sweep crossed admission boundary");
    // STL memory, metadata and aligned Fields share the process C++ envelope.
    auto baseline=pm::stats();pm::set_limit(baseline.live+4096);
    bool vector_denied=false;try{std::vector<char> big(100000);}catch(const std::bad_alloc&){vector_denied=true;}
    auto *aligned=::operator new(128,std::align_val_t(256),std::nothrow);check(aligned && reinterpret_cast<std::uintptr_t>(aligned)%256==0,"overaligned guarded allocation");
    ::operator delete(aligned,std::align_val_t(256));pm::set_limit(std::numeric_limits<std::size_t>::max());
    check(vector_denied && pm::stats().live==baseline.live,"STL refusal and allocation release");
    std::atomic<int> workers=0;std::thread a([&]{for(int i=0;i<100;++i){std::vector<int> v(256);v[0]=i;}++workers;});
    std::thread b([&]{for(int i=0;i<100;++i){Field<double> v(256);v.fill(i);}++workers;});a.join();b.join();check(workers==2,"parallel allocation accounting");
    if constexpr(sizeof(Index)==8) {
        auto high=NodeId::checked(UINT64_C(4294967297));check(high.value()==UINT64_C(4294967297),"global NodeId64");
        auto bytes=lifecycle::native::pack(std::vector<Index>{high.value()});
        check(lifecycle::native::unpack<std::vector<Index>>(bytes)[0]==high.value(),"Index64 wire vector");
        lifecycle::ResultSample sample;sample.material=high.value();
        check(lifecycle::native::unpack<lifecycle::ResultSample>(lifecycle::native::pack(sample)).material==high.value(),"material result index64");
    } else rejects([]{NodeId::checked(UINT64_C(4294967297));});
    auto info=pm::stats();std::cout<<"DBF8 checks="<<checks<<" index_bits="<<index_bits<<" guarded_peak="<<info.peak<<" rejected="<<info.rejected<<" PASS\n";return 0;
}catch(const std::exception &e){pm::set_limit(std::numeric_limits<std::size_t>::max());std::cerr<<e.what()<<'\n';return 1;}}

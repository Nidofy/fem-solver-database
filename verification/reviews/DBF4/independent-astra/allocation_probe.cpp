#include <cstdlib>
#include <new>
#include <cstddef>
static thread_local bool deny_alloc = false;
void *operator new(std::size_t n) { if (deny_alloc) throw std::bad_alloc(); if (auto p=std::malloc(n?n:1)) return p; throw std::bad_alloc(); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p,std::size_t) noexcept { std::free(p); }
#include <sdb/results/checkpoint_link.hpp>
#include <iostream>
namespace fs=std::filesystem;
using namespace sdb;
using namespace sdb::lifecycle;
struct Injection { fs::path target; bool hit=false; };
void inject(durability::FaultPoint p,const fs::path &path,void *v) {
    auto &i=*static_cast<Injection*>(v);
    if(p==durability::FaultPoint::AfterReplace && path==i.target) {i.hit=true; deny_alloc=true; throw std::bad_alloc();}
}
void check(bool b,const char *s) {if(!b) throw std::runtime_error(s);}
int main(int argc,char **argv) {
    try {
        check(argc==3,"args"); const auto dir=fs::absolute(argv[2]); fs::create_directories(dir);
        auto p=dir/"atomic.bin"; std::vector<std::uint8_t> bytes{'n','e','w'};
        for(bool raw:{false,true}) {
            {std::ofstream out(p);out<<"old";} Injection in{p}; bool caught=false;
            try {durability::ScopedFaultHook hook({inject,&in}); if(raw) checkpoint_detail::atomic_write(p,bytes); else checkpoint_detail::atomic_stream_write(p,[](auto &out){out<<"new";});}
            catch(const durability::PublicationError &e) {const bool diagnostic=std::string_view(e.what())==std::bad_alloc{}.what();deny_alloc=false; caught=diagnostic&&e.published()&&e.replacement_may_have_happened(); try {std::rethrow_exception(e.cause());}catch(const std::bad_alloc&){} catch(...){caught=false;}}
            deny_alloc=false; check(in.hit&&caught,"atomic post-replace classification"); std::ifstream input(p);std::string value;input>>value;check(value=="new","atomic target");
        }
        bool original_diagnostic=false; try {checkpoint_detail::atomic_stream_write(p,[](auto &out){out<<"partial";throw std::runtime_error("independent original diagnostic");});} catch(const durability::PublicationError &e){original_diagnostic=std::string_view(e.what())=="independent original diagnostic" && !e.published() && !e.replacement_may_have_happened();} check(original_diagnostic,"original callback diagnostic");
        bool fallback=false; try {checkpoint_detail::atomic_stream_write(p,[](auto &){throw 7;});}catch(const durability::PublicationError &e){fallback=std::string_view(e.what()).find("atomic publication failed")!=std::string_view::npos;} check(fallback,"non-standard cause fallback");
        auto model=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model);
        for(bool at_link:{false,true}) {
            auto result=dir/(at_link?"link.sdbres":"restart.sdbres");auto restart=dir/(at_link?"link.sdbrst":"restart.sdbrst");
            fs::remove(result); CpuRun<double> run(model);run.initialize();
            NativeResultStorage storage(result,ResultModel::from(model,run.configuration()));storage.append(run.snapshot());
            CheckpointLink::publish_durable(storage,restart,run,model);run.advance();
            Injection in{at_link?CheckpointLink::path(restart):restart};bool caught=false;
            try {durability::ScopedFaultHook hook({inject,&in}); CheckpointLink::publish_durable(storage,restart,run,model);}
            catch(const LinkPublicationError &e) {deny_alloc=false;caught=e.restart_may_have_changed() && e.link_may_have_changed()==at_link && e.phase()==(at_link?LinkPublicationPhase::LinkPublication:LinkPublicationPhase::RestartPublication);}
            deny_alloc=false;check(in.hit&&caught,"link post-replace classification");
            CpuRun<double> restored(model);restored.restore(restart);check(restored.state().data().step==1,"restart visible");
            bool accepted=true;try {CheckpointLink::verify(result,restart,restored,model);}catch(...){accepted=false;}check(accepted==at_link,"mismatch refusal");
        }
        std::cout<<"independent allocation-denial probe PASS: both atomic helpers and restart/link post-replace failure classifications\n";
    }catch(const std::exception &e){deny_alloc=false;std::cerr<<e.what()<<'\n';return 1;}
}


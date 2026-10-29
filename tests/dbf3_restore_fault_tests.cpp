#include <cstdlib>
#include <new>
#include <cstddef>
// Inject ordinary producer-thread allocations only. Aligned Field allocations
// and platform I/O allocations are not claimed to be exhaustively instrumented.
static thread_local long fail_after=-1;
void *operator new(std::size_t size) {
    if(fail_after==0) throw std::bad_alloc();
    if(fail_after>0) --fail_after;
    if(void *p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept {std::free(p);}
void operator delete(void *p,std::size_t) noexcept {std::free(p);}
#include <sdb/lifecycle.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
template<class T> void register_counter(StateDB<T> &state,bool required) {
    FieldDescriptor d;d.name="counter";d.scalar=ScalarType::UInt64;
    d.association=Association::Node;d.location=FieldLocation::Entity;
    d.restart=required?RestartPolicy::Required:RestartPolicy::Reconstruct;
    state.template register_field<Field<std::uint64_t>>(d,state.data().displacement.size(),"fault-counter",1,{9007199254740993ull});
    state.freeze_registered_fields();
}
template<class T> void campaign(const std::filesystem::path &input,const std::filesystem::path &dir) {
    auto model=ModelCompiler{}.compile<T>(ReaderRegistry{}.read(input).model);
    CpuRun<T> source(model),legacy(model);register_counter(source.state(),true);
    source.advance();legacy.advance();
    auto restart=dir/(sizeof(T)==4?"fault-f.sdbrst":"fault-d.sdbrst");
    auto checkpoint=dir/(sizeof(T)==4?"fault-f.chk":"fault-d.chk");
    source.save(restart);save_checkpoint(checkpoint,model.mesh,legacy.state());
    for(bool import:{false,true}) {
        bool success=false;long failures=0;
        for(long n=0;n<2000;++n) {
            CpuRun<T> target(model);register_counter(target.state(),!import);
            target.state().data().displacement.fill(T(37));
            auto core=target.state().data().displacement.checked_read();
            auto field=target.state().template registered_field<Field<std::uint64_t>>("counter").checked_read();
            bool failed=false;fail_after=n;
            try {if(import) target.import_v2(checkpoint);else target.restore(restart);}
            catch(const std::bad_alloc&) {failed=true;}
            catch(...) {fail_after=-1;throw;}
            fail_after=-1;
            if(!failed) {
                if(target.increment()!=1) throw std::runtime_error("successful restore cursor");
                success=true;break;
            }
            ++failures;
            if(target.increment()!=0 || target.step_index()!=0 || core.at(0)!=T(37) || field.at(0)!=9007199254740993ull)
                throw std::runtime_error("allocation failure published partial restore");
        }
        if(!success || !failures) throw std::runtime_error("fault campaign did not reach success");
        std::cout<<(sizeof(T)==4?"float":"double")<<(import?" import":" restore")<<": "<<failures<<" failure points atomic, success reached\n";
    }
}
int main(int argc,char **argv) {
    try {
        if(argc!=3) throw std::invalid_argument("input scratch expected");
        std::filesystem::create_directories(argv[2]);
        campaign<float>(argv[1],argv[2]);campaign<double>(argv[1],argv[2]);
    }catch(const std::exception &e){fail_after=-1;std::cerr<<e.what()<<'\n';return 1;}
}

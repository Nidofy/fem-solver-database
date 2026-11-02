#include <sdb/lifecycle.hpp>
#include <iostream>
using namespace sdb;using namespace sdb::lifecycle;
template<class T> void run(const std::filesystem::path& fixture,const std::filesystem::path& tmp) {
    auto m=ModelCompiler{}.compile<T>(ReaderRegistry{}.read(fixture).model);
    if(!m.report.executable_cpu || m.definition.schema!=7) throw std::runtime_error("DLOAD execution unavailable");
    CpuRun<T> cpu(m);cpu.initialize();
    auto verify=[&](double f) {
        double total[3]{};
        for(Index n=0;n<m.mesh.nodes();++n) for(unsigned d=0;d<3;++d) total[d]+=cpu.state().data().external_force.read()(n,d);
        if(std::abs(total[0]-f*2)>1e-6 || std::abs(total[1]+f*6)>1e-6 || std::abs(total[2]-f*3)>1e-6)
            throw std::runtime_error("distributed resultant or amplitude mismatch");
    };
    verify(0);for(unsigned i=0;i<500;++i) cpu.advance();verify(.5);
    cpu.save(tmp/"load.rst");ModelStore::save(tmp/"load.model",m);
    auto mm=ModelStore::load<T>(tmp/"load.model");CpuRun<T> split(mm);split.restore(tmp/"load.rst");
    while(cpu.advance()) if(!split.advance()) throw std::runtime_error("split cursor");
    verify(1);if(native::pack(cpu.snapshot())!=native::pack(split.snapshot())) throw std::runtime_error("split mismatch");
}
int main(int argc,char**argv) {
    if(argc!=3) return 2;std::filesystem::create_directories(argv[2]);
    run<float>(argv[1],argv[2]);run<double>(argv[1],argv[2]);std::cout<<"DLOAD PASS\\n";
}

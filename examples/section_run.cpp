#include <sdb/physics/section_run.hpp>
#include <iostream>
using namespace sdb::lifecycle;
int main(int argc,char**argv){
    try{
        if(argc!=3)throw std::invalid_argument("usage: sdb_section_run model.inp new_result.sdbres");
        auto m=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model);
        sdb::physics::SectionRun<double> run(m);
        NativeResultStorage storage(argv[2],ResultModel::from(m,run.configuration()));
        ResultWriter writer(storage);run.initialize();writer.write(run.snapshot());
        while(run.advance()){}writer.write(run.snapshot());
        std::cout<<"section increments "<<run.state().data().step<<"\\n";
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}

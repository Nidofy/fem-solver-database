#include <sdb/physics/section_run.hpp>
#include <iostream>
#include "penalty_option.hpp"
using namespace sdb::lifecycle;
int main(int argc,char**argv){
    try{
        if(argc!=3 && !(argc==5 && std::string(argv[3])=="--penalty-stiffness"))
            throw std::invalid_argument("usage: sdb_section_run model.inp new_result.sdbres [--penalty-stiffness K]");
        auto ir=ReaderRegistry{}.read(argv[1]).model;
        if(argc==5) apply_penalty_option(ir,argv[4]);
        auto m=ModelCompiler{}.compile<double>(std::move(ir));
        sdb::physics::SectionRun<double> run(m);
        NativeResultStorage storage(argv[2],run.result_model(),false,false,native::default_limit,{},false,
                                    run.recovery_enabled()?ResultFormat::TypedIndexed3:ResultFormat::Native1);
        ResultWriter writer(storage);run.initialize();writer.write(run.snapshot());
        while(run.advance()) if(!m.outputs[run.step_index()].empty() || run.end_of_step()) writer.write(run.snapshot());
        std::cout<<"section increments "<<run.state().data().step<<"\n";
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}

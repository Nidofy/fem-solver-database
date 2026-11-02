#include <sdb/lifecycle.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
int main(int argc,char**argv) {
    if(argc!=3) return 2;
    auto ir=ReaderRegistry{}.read(argv[1]).model;
    ir.schema=3;
    IREquation eq; eq.id=ConstraintId(1);
    eq.terms={{{"","",10},Dof::X,1},{{"","",40},Dof::X,-1}};
    ir.foundation.equations={eq};
    auto model=ModelCompiler{}.compile<double>(ir);
    physics::PenaltyConstraints plan(model,100);
    StateDB<double> state(model.mesh);state.data().mass.fill(2);
    state.data().displacement.write()(model.sources.node_id({"","",40}).value(),0)=.2;
    auto out=plan.evaluate(state);
    if(plan.rows()!=1 || std::abs(out.potential-2)>1e-12 || out.frequency_squared_bound!=100 ||
       out.restoring_force.read()(model.sources.node_id({"","",10}).value(),0)!=20 ||
       out.restoring_force.read()(model.sources.node_id({"","",40}).value(),0)!=-20) return 1;
    ir.schema=6; ir.penalty_stiffness=100;
    auto executable=ModelCompiler{}.compile<double>(ir);
    CpuRun<double> run(executable);
    for(unsigned i=0;i<200;++i) run.advance();
    auto scratch=std::filesystem::path(argv[2]); std::filesystem::create_directories(scratch);
    ModelStore::save(scratch/"penalty.sdbmodel",executable);
    run.save(scratch/"penalty.sdbrst");
    auto reloaded=ModelStore::load<double>(scratch/"penalty.sdbmodel");
    CpuRun<double> split(reloaded); split.restore(scratch/"penalty.sdbrst");
    while(run.advance()) if(!split.advance()) return 1;
    if(native::pack(run.snapshot())!=native::pack(split.snapshot())) return 1;
    // Free x oscillator with one grounded penalty spring in parallel with E/6.
    double k=100+100./6, u=(1-std::cos(std::sqrt(k*24)*.2))/k;
    if(std::abs(run.state().data().displacement.read()(model.sources.node_id({"","",40}).value(),0)-u)>1e-6) return 1;
    std::cout<<"penalty gap, work, transfer and modal bound passed\\n";
}

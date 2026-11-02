#include <sdb/physics/penalty_constraints.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
int main(int argc,char**argv) {
    if(argc!=2) return 2;
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
    std::cout<<"penalty gap, work, transfer and modal bound passed\\n";
}

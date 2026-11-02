#include <sdb/physics/section_run.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
using namespace sdb::physics;

void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
template<class F>void rejects(F fn){try{fn();}catch(const std::exception&){return;}throw std::runtime_error("expected rejection");}

template<class T>void verify(const std::string& input,const std::filesystem::path& scratch){
    auto compile=[&](std::string text){
        auto ir=AbaqusInpReader{}.buffer(text,"coupling.inp").model;
        ir.schema=6;ir.penalty_stiffness=10;return ModelCompiler{}.compile<T>(ir);
    };
    auto model=compile(input);auto id=ConstraintId(0);
    const auto& coupling=model.foundation.couplings.at(0);
    const double expected[]={1./12,1./4,1./4,1./12,1./6,1./6};
    check(coupling.nodes.size()==6 && model.foundation.extensions.couplings.at(0).dof_mask==63,"default available DOFs");
    for(unsigned i=0;i<6;++i)check(std::abs(coupling.weights.at(i)-expected[coupling.nodes.at(i).value()])<1e-12,"tributary area and shared nodes");
    SectionRun<T> full(model);
    for(unsigned i=0;i<20;++i)check(full.advance(),"advance");
    full.save(scratch/"surface.rst");ModelStore::save(scratch/"surface.model",model);
    auto copy=ModelStore::load<T>(scratch/"surface.model");SectionRun<T> split(copy);split.restore(scratch/"surface.rst");
    while(full.advance())check(split.advance(),"split cursor");
    check(native::pack(full.snapshot())==native::pack(split.snapshot()),"surface coupling exact restart");
    StateDB<T> state(model.mesh,true);state.data().mass.fill(1);state.data().rotation->inertia.fill(1);
    state.data().rotation->angle.write()(6,2)=T(.2);
    PenaltyConstraints plan(model,10);auto r=plan.evaluate(state);
    double Fx=0,Fy=0,Mz=0;auto x=model.mesh.reference();
    for(unsigned n=0;n<8;++n){auto f=r.restoring_force.read();Fx+=f(n,0);Fy+=f(n,1);
        Mz+=r.restoring_moment.read()(n,2)+x(n,0)*f(n,1)-x(n,1)*f(n,0);}
    double tol=sizeof(T)==4?1e-6:1e-12;
    check(std::abs(Fx)+std::abs(Fy)+std::abs(Mz)<tol && std::abs(r.restoring_moment.read()(6,2)+2)<tol,"force and moment equilibrium");

    const std::string card="*Distributing,weighting method=UNIFORM,coupling=CONTINUUM";
    auto replace=[&](std::string a,std::string b){auto s=input;s.replace(s.find(a),a.size(),b);return s;};
    auto kinematic=compile(replace(card,"*Kinematic\n4,6"));SectionRun<T> krun(kinematic);check(krun.advance(),"native rotational kinematic rows");
    rejects([&]{compile(replace(card,"*Distributing,weighting method=CUBIC"));});
    rejects([&]{compile(replace("PLATES,SPOS","PLATES,SPOS\nPLATES,SNEG"));});
}
int main(int argc,char**argv){
    if(argc!=3)return 2;std::ifstream file(argv[1]);std::string text{std::istreambuf_iterator<char>(file),{}};
    std::filesystem::create_directories(argv[2]);verify<float>(text,argv[2]);verify<double>(text,argv[2]);
    std::cout<<"Native surface coupling area weights, six-DOF transfer and restart PASS"<<std::endl;
}

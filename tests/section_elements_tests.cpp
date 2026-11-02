#include <sdb/physics/section_run.hpp>
#include <iostream>
using namespace sdb;using namespace sdb::lifecycle;using namespace sdb::physics;
template<class T>void test(const std::filesystem::path& dir,const std::filesystem::path& scratch,const char*file){
    auto model=ModelCompiler{}.compile<T>(ReaderRegistry{}.read(dir/file).model);
    SectionRun<T> run(model);
    // Infinitesimal rigid rotation about arbitrary axis plus translation: all six forces vanish.
    auto& el=run.element(0);auto x=model.mesh.reference();double q[24]{};
    for(unsigned a=0;a<el.count;++a) {
        auto n=el.nodes[a]; q[6*a]=2+2*x(n,2)-3*x(n,1);q[6*a+1]=3+3*x(n,0)-x(n,2);
        q[6*a+2]=4+x(n,1)-2*x(n,0);q[6*a+3]=1;q[6*a+4]=2;q[6*a+5]=3;
    }
    for(unsigned i=0;i<6*el.count;++i){double f=0;for(unsigned j=0;j<6*el.count;++j)f+=el.K[i][j]*q[j];
        if(std::abs(f)>1e-9)throw std::runtime_error("rigid section force");}
    auto energy=[&] {
        double U=0;for(unsigned i=0;i<6*el.count;++i)for(unsigned j=0;j<6*el.count;++j)
            U+=.5*q[i]*el.K[i][j]*q[j];
        return U;
    };
    auto oracle=[&](double expected) {if(std::abs(energy()-expected)>1e-8*expected+1e-12)
        throw std::runtime_error("section bending/shear/torsion energy oracle");};
    std::fill(q,q+24,0.);
    // Unit constant curvature, with transverse displacement cancelling centered shear.
    for(unsigned a=0;a<el.count;++a){
        double X=x(el.nodes[a],0);
        if(el.count==2){q[6*a+1]=.5*X*X;q[6*a+5]=X;}
        else {q[6*a+2]=-.5*X*X;q[6*a+4]=X;}
    }
    if(el.count==2){
        oracle(.5*100*.1*std::pow(.2,3)/12);
        std::fill(q,q+24,0.);q[6]=0;q[9]=1;
        // Saint-Venant Fourier-series reference for a 2:1 rectangle; the
        // inexpensive section approximation is accurate to 1% here.
        double series=0;for(unsigned odd=1;odd<=39;odd+=2) series+=std::tanh(odd*std::numbers::pi)/std::pow(double(odd),5);
        double J=.2*std::pow(.1,3)/3*(1-192*.1/(std::pow(std::numbers::pi,5)*.2)*series);
        if(std::abs(energy()/(.5*50*J)-1)>.01)throw std::runtime_error("rectangular torsion");
    }else{
        oracle(.5*100*std::pow(.1,3)/12);
        std::fill(q,q+24,0.);for(unsigned a=0;a<4;++a)q[6*a+2]=x(el.nodes[a],0);
        oracle(.5*(5./6)*50*.1);
        // theta_x=x-.5, theta_y=y-.5 has zero curvature, zero center shear and
        // zero checkerboard amplitude. Edge shear must resist this nonrigid mode.
        std::fill(q,q+24,0.);
        for(unsigned a=0;a<4;++a){q[6*a+3]=x(el.nodes[a],0)-.5;q[6*a+4]=x(el.nodes[a],1)-.5;}
        oracle(.5*(5./6)*50*.1/6);
    }
    for(unsigned i=0;i<330;++i)run.advance();run.save(scratch/"section.rst");ModelStore::save(scratch/"section.model",model);
    auto m=ModelStore::load<T>(scratch/"section.model");SectionRun<T> split(m);split.restore(scratch/"section.rst");
    while(run.advance())if(!split.advance())throw std::runtime_error("split cursor");
    double u=.001*(1-std::cos(std::sqrt(200.)*.1));
    if(std::abs(run.state().data().displacement.read()(1,0)-u)>3e-8)throw std::runtime_error("section axial oscillator");
    if(native::pack(run.snapshot())!=native::pack(split.snapshot()))throw std::runtime_error("section restart mismatch");
}
void moments(const std::filesystem::path& dir) {
    auto ir=ReaderRegistry{}.read(dir/"section_shell.inp").model; ir.schema=6;ir.penalty_stiffness=100;
    ir.sets.push_back({"","CLOUD","",true,{"2","3","4"},{}});
    IRCoupling c;c.id=ConstraintId(1);c.name="D";c.reference={"","",1};c.nodes="CLOUD";c.kind=CouplingKind::Distributing;c.dof_mask=32;
    for(unsigned i=2;i<=4;++i)c.weights.push_back({{"","",i},double(i)});
    ir.extensions.couplings={c};
    for(bool distributing:{true,false}){
        if(!distributing){ir.extensions.couplings[0].kind=CouplingKind::Kinematic;
            ir.extensions.couplings[0].weights.clear();ir.extensions.couplings[0].dof_mask=2;}
        auto m=ModelCompiler{}.compile<double>(ir);
        SectionRun<double> integration(m);integration.advance();
        PenaltyConstraints constraints(m,100);StateDB<double> state(m.mesh,true);
        state.data().mass.fill(1);state.data().rotation->inertia.fill(1);state.data().rotation->angle.write()(0,2)=.1;
        auto v=constraints.evaluate(state);auto f=v.restoring_force.read(),M=v.restoring_moment.read(),x=m.mesh.reference();
        double net=0,torque=0;
        for(unsigned n=0;n<4;++n){net+=f(n,1);torque+=M(n,2)+x(n,0)*f(n,1)-x(n,1)*f(n,0);}
        if(std::abs(net)>1e-10 || std::abs(torque)>1e-10 || !(M(0,2)<-9.9))
            throw std::runtime_error("constraint force/couple balance");
        for(unsigned n=0;n<4;++n){state.data().displacement.write()(n,0)=-.1*x(n,1);state.data().displacement.write()(n,1)=.1*x(n,0);}
        if(constraints.evaluate(state).potential>1e-20)throw std::runtime_error("rigid motion creates constraint energy");
    }
}
int main(int argc,char**argv){if(argc!=3)return 2;std::filesystem::create_directories(argv[2]);
    moments(argv[1]);
    for(auto* file:{"section_beam.inp","section_shell.inp"}){test<double>(argv[1],argv[2],file);test<float>(argv[1],argv[2],file);}
    std::cout<<"section beam/shell FP32/64 rigid modes, analytic oscillator, exact restart PASS\\n";}

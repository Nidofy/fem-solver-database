#include <sdb/physics/section_run.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace sdb;
using namespace sdb::lifecycle;

// K - omega^2 M is positive definite precisely below the first frequency.
// Dense Cholesky on the small constrained bending subsystem avoids an eigensolver dependency.
bool below(const std::vector<double>& stiffness, const std::vector<double>& mass, double omega) {
    auto L=stiffness;
    const auto n=mass.size();
    for(std::size_t i=0;i<n;++i) {
        L[i*n+i]-=omega*omega*mass[i];
        for(std::size_t j=0;j<=i;++j) {
            double a=L[i*n+j];
            for(std::size_t k=0;k<j;++k)a-=L[i*n+k]*L[j*n+k];
            if(i==j) {
                if(!(a>0))return false;
                L[i*n+j]=std::sqrt(a);
            } else L[i*n+j]=a/L[j*n+j];
        }
    }
    return true;
}
template<class T> double frequency(unsigned cells, double reference) {
    std::ostringstream deck;
    deck<<std::setprecision(17)<<"*Heading\nUniform B31 cantilever, first transverse mode\n*Node\n";
    for(unsigned i=0;i<=cells;++i)deck<<i+1<<','<<double(i)/cells<<",0,0\n";
    deck<<"*Element,type=B31,elset=BEAM\n";
    for(unsigned i=0;i<cells;++i)deck<<i+1<<','<<i+1<<','<<i+2<<'\n';
    deck<<"*Material,name=M\n*Density\n1\n*Elastic\n100,0\n*Beam Section,material=M,elset=BEAM,section=RECT\n"
          ".01,.01\n0,1,0\n*Boundary\n1,1,6\n*Step,name=FREE,nlgeom=NO\n*Dynamic,Explicit\n,.001\n*End Step\n";
    auto m=ModelCompiler{}.compile<T>(AbaqusInpReader{}.buffer(deck.str(),"cantilever.inp").model);
    physics::SectionRun<T> run(m);
    std::size_t n=cells*2;
    std::vector<double> stiffness(n*n),mass(n);
    for(unsigned e=0;e<cells;++e) {
        const auto& element=run.element(e);
        for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b) {
            auto left=element.nodes[a],right=element.nodes[b];
            if(!left || !right)continue; // clamped root, both bending DOFs eliminated
            const unsigned dof[]={1,5}; // global y displacement and z rotation
            for(unsigned i=0;i<2;++i)for(unsigned j=0;j<2;++j)
                stiffness[(2*(left-1)+i)*n+2*(right-1)+j]+=element.K[6*a+dof[i]][6*b+dof[j]];
        }
    }
    for(Index i=1;i<m.mesh.nodes();++i) {
        mass[2*(i-1)]=run.state().data().mass.read()(i);
        mass[2*(i-1)+1]=run.state().data().rotation->inertia.read()(i,2);
    }
    double lo=0,hi=2*reference;
    if(!below(stiffness,mass,lo) || below(stiffness,mass,hi))throw std::runtime_error("frequency bracket");
    for(unsigned i=0;i<48;++i) {
        double mid=(lo+hi)/2;
        if(below(stiffness,mass,mid))lo=mid;else hi=mid;
    }
    return (lo+hi)/2;
}
template<class T> void compare() {
    const double beta=1.875104068711961;
    const double reference=beta*beta*std::sqrt(100.*std::pow(.01,4)/(12*.01*.01));
    double previous=1;
    for(unsigned n:{4u,8u,16u,32u}) {
        auto omega=frequency<T>(n,reference);
        double error=std::abs(omega-reference)/reference;
        double limit=.4/(n*n)+.00015;
        bool passed=error<limit && error<previous;
        std::cout<<std::setprecision(12)<<"{\"case\":\"b31_cantilever_bending\",\"precision\":"<<sizeof(T)*8
            <<",\"elements\":"<<n<<",\"omega\":"<<omega<<",\"reference_omega\":"<<reference
            <<",\"normalized_linf\":"<<error<<",\"limit\":"<<limit<<",\"passed\":"<<(passed?"true":"false")<<"}\n";
        if(!passed)throw std::runtime_error("cantilever convergence/error envelope");
        previous=error;
    }
}
int main() {
    try{compare<float>();compare<double>();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

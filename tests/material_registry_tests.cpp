#include <sdb/physics/material_registry.hpp>
using namespace sdb::physics;
extern "C" int32_t trial(const void*p,const void*x,const void*old,void*s,void*h,void*K) {
    const double k=*static_cast<const double*>(p);
    for(unsigned i=0;i<6;++i) static_cast<double*>(s)[i]=k*static_cast<const double*>(x)[i];
    *static_cast<double*>(h)=*static_cast<const double*>(old)+1;
    if(K) for(unsigned i=0;i<36;++i) static_cast<double*>(K)[i]=i%7?0:k;
    return k>0?0:1;
}
int main() {
    const char* pn[]={"stiffness"},*hn[]={"count"};
    sdb_material_plugin_v1 p{1,sizeof(p),17,2,"spring",SDB_MAT_F64,1,1,SDB_MAT_SYMMETRIC,pn,hn,trial};
    MaterialRegistry registry;registry.add(p);
    double k[]={3},h[]={5};std::array<double,6> strain{2,1};
    auto out=registry.trial<double>(17,"spring",2,k,strain,h,true);
    if(out.history.at(0)!=6 || h[0]!=5 || out.stress[0]!=6 || out.tangent[7]!=3) return 1;
    k[0]=-1;try {registry.trial<double>(17,"spring",2,k,strain,h);}catch(const std::runtime_error&){return h[0]!=5;}
    return 1;
}

#include <sdb/foundation.hpp>
#include <iostream>

// An installed-package client: no lifecycle.hpp, CpuExplicit, production headers or model compiler.
int main(int argc,char **argv) {
    using namespace sdb;
    namespace v1=sdb::consumer::v1;
    try {
        if(argc!=2) return 2;
        DofSpaceData data; data.model=123; data.nodes=2;
        data.variables={{0,1,DofQuantity::Temperature,"temperature","K","scalar"}};
        data.bindings={{{DofOwner::Node,0,0,0,0},DofStatus::Free,0,0,{}},
                       {{DofOwner::Node,1,0,0,0},DofStatus::Prescribed,no_equation,310,{}}};
        data.external_equations={9001}; data.solver_to_canonical={0};
        DofSpace space(data); RegisteredState state; state.bind_context(data.model,space.identity());
        FieldDescriptor d; d.name="temperature"; d.units="K"; d.basis="scalar";
        d.association=Association::Node; d.location=FieldLocation::Entity;
        StateLayout l; l.domain=2; lifecycle::ResultAddress a; a.entity=1; l.addresses.push_back(a); a.entity=2; l.addresses.push_back(a);
        state.add<Field<double>>(d,2,"thermal-state",1,{300},0,l); state.freeze();
        v1::FieldRequirement requirement{d,"thermal-state",1,0,2,l};
        auto output=v1::bind_write<Field<double>>(state,space,requirement);
        v1::AccessSet access; access.write(output); access.validate();
        auto values=space.gather(std::vector<double>{320});
        auto view=output.resolve(); for(std::size_t i=0;i<values.size();++i) view(i)=values[i];
        FoundationStore::save(argv[1],space,state);
        state.get<Field<double>>("temperature").fill(0); FoundationStore::restore(argv[1],space,state);
        auto snapshot=v1::snapshot(state,space,requirement);
        if(std::get<std::vector<double>>(snapshot.values)!=values) return 3;
        bool stale=false; try { output.resolve(); } catch(const std::logic_error &) { stale=true; }
        if(!stale) return 4;
        std::cout<<"Installed standalone foundation consumer PASS\n"; return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}

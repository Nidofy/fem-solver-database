#include <sdb/foundation.hpp>
int main(){
    using namespace sdb;
    DofSpaceData data;data.model=123;data.nodes=1;
    data.variables={{1,1,DofQuantity::Temperature,"T","K","scalar"}};
    data.bindings={{{DofOwner::Node,0,0,1,0},DofStatus::Free,0,0,{}}};
    data.external_equations={9001};data.solver_to_canonical={0};DofSpace dofs(data);
    FieldDescriptor d;d.name="T";d.association=Association::Node;d.location=FieldLocation::Entity;d.units="K";
    RegisteredState state;state.bind_context(123,dofs.identity());state.add<Field<double>>(d,1,"thermal",1,{300},0);state.freeze();
    StateSession owner(state);owner.begin_trial();{auto w=owner.write();w.field<Field<double>>("T").at(0)=500;}owner.rollback();
    auto r=owner.read();if(r.field<Field<double>>("T").at(0)!=300)return 1;
    EquationMatrix<> matrix;matrix.rows=matrix.columns=1;matrix.offsets={0,1};matrix.indices={0};matrix.values={1};
    matrix.row_space=matrix.column_space=dofs.identity();matrix.require(dofs,dofs);
    return EquationMatrix<>::decode(matrix.encode()).values[0]==1?0:2;
}

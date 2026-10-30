#pragma once
#include "dof_space.hpp"
#include "units.hpp"

namespace sdb {
enum class MatrixRole : std::uint8_t { Stiffness, Mass, Damping, GeometricStiffness, Conductivity, Capacity, Coupling, User };
enum class EquationStage : std::uint8_t { Full, Reduced };
enum class EquationOrdering : std::uint8_t { Canonical, Solver };
// Additive portable CSR contract. Independent of the existing 32-bit MeshDB/CSR.
// Index64 does not imply that an allocation of that size was physically tested.
template<class T=double,class I=std::uint64_t> struct EquationMatrix {
    static_assert((std::is_same_v<T,float> || std::is_same_v<T,double>) &&
                  (std::is_same_v<I,std::uint32_t> || std::is_same_v<I,std::uint64_t>));
    std::uint32_t version=1,index_bits=sizeof(I)*8;
    ScalarType scalar=scalar_type<T>();
    MatrixRole role=MatrixRole::Stiffness;
    EquationStage stage=EquationStage::Reduced;
    EquationOrdering row_order=EquationOrdering::Canonical,column_order=EquationOrdering::Canonical;
    std::string user_role;
    std::uint64_t row_space=0,column_space=0,structure_revision=1,value_revision=1,rows=0,columns=0;
    bool symmetric=false;
    Unit unit;
    std::vector<std::uint64_t> offsets;
    std::vector<I> indices;
    std::vector<T> values;
    template<class F> void visit(F f) { f(version,index_bits,scalar,role,stage,row_order,column_order,user_role,row_space,column_space,
        structure_revision,value_revision,rows,columns,symmetric,unit,offsets,indices,values); }
    void validate() const {
        unit.validate();
        if(version!=1 || index_bits!=sizeof(I)*8 || scalar!=scalar_type<T>() || !row_space || !column_space ||
           !structure_revision || !value_revision || !enum_at_most(role,MatrixRole::User) ||
           !enum_at_most(stage,EquationStage::Reduced) || !enum_at_most(row_order,EquationOrdering::Solver) ||
           !enum_at_most(column_order,EquationOrdering::Solver) ||
           (stage==EquationStage::Full && (row_order!=EquationOrdering::Canonical || column_order!=EquationOrdering::Canonical)) ||
           (role==MatrixRole::User && user_role.empty()) ||
           rows==UINT64_MAX || rows+1!=offsets.size() || offsets.empty() || offsets.front()!=0 ||
           offsets.back()!=indices.size() || indices.size()!=values.size()) throw std::invalid_argument("matrix contract/shape");
        if(columns && columns-1>std::numeric_limits<I>::max()) throw std::length_error("matrix index width");
        if(symmetric && (rows!=columns || row_space!=column_space || row_order!=column_order)) throw std::invalid_argument("symmetric equation spaces");
        for(std::size_t r=0;r<rows;++r) {
            if(offsets[r]>offsets[r+1] || offsets[r+1]>indices.size()) throw std::invalid_argument("matrix row offsets");
            for(auto k=offsets[r];k<offsets[r+1];++k) {
                if(indices[k]>=columns || !std::isfinite(values[k]) || (k>offsets[r] && indices[k-1]>=indices[k]))
                    throw std::invalid_argument("matrix column/value");
            }
        }
        if(symmetric) for(std::size_t r=0;r<rows;++r) for(auto k=offsets[r];k<offsets[r+1];++k) {
            auto c=indices[k]; auto first=indices.begin()+offsets[c],last=indices.begin()+offsets[c+1];
            auto it=std::lower_bound(first,last,static_cast<I>(r));
            if(it==last || *it!=r || values[it-indices.begin()]!=values[k]) throw std::invalid_argument("matrix symmetry");
        }
    }
    void require(const DofSpace &row,const DofSpace &column) const {
        validate();
        const auto nr=stage==EquationStage::Reduced?row.data().external_equations.size():row.data().bindings.size();
        const auto nc=stage==EquationStage::Reduced?column.data().external_equations.size():column.data().bindings.size();
        if(row.identity()!=row_space || column.identity()!=column_space || rows!=nr || columns!=nc)
            throw std::invalid_argument("matrix DOF identity/stage mismatch");
    }
    auto encode(std::uint64_t budget=lifecycle::native::default_limit) const { validate(); return lifecycle::native::pack(*this,budget); }
    static EquationMatrix decode(std::span<const std::uint8_t> bytes,std::uint64_t budget=lifecycle::native::default_limit) {
        auto out=lifecycle::native::unpack<EquationMatrix>(bytes,budget); out.validate(); return out;
    }
};
} // namespace sdb

#pragma once
#include "io/native.hpp"
#include <numeric>
#include <set>

namespace sdb {
// Independent of MeshDB's legacy 3/6 mechanical slots. IDs are semantic, not pointers.
enum class DofOwner : std::uint8_t { Node, Element, Global };
enum class DofQuantity : std::uint8_t { Translation, Rotation, Temperature, Pressure, Multiplier, User };
enum class DofStatus : std::uint8_t { Free, Prescribed, Inactive, Eliminated };
inline constexpr std::uint64_t no_equation = UINT64_MAX;
struct DofVariable {
    std::uint32_t id = 0, components = 1;
    DofQuantity quantity = DofQuantity::User;
    std::string name, units = "UNSPECIFIED", basis = "UNSPECIFIED";
    template<class F> void visit(F f) { f(id,components,quantity,name,units,basis); }
};
struct DofKey {
    DofOwner owner = DofOwner::Node;
    std::uint64_t entity = 0; // zero-based entity in the declared model; Global requires zero
    std::uint32_t local = 0, variable = 0, component = 0;
    auto operator<=>(const DofKey &) const = default;
    template<class F> void visit(F f) { f(owner,entity,local,variable,component); }
};
struct DofTerm {
    std::uint64_t slot = 0;
    double coefficient = 0;
    template<class F> void visit(F f) { f(slot,coefficient); }
};
struct DofBinding {
    DofKey key;
    DofStatus status = DofStatus::Inactive;
    std::uint64_t equation = no_equation; // canonical, zero based
    double value = 0; // prescribed/inactive value or eliminated affine offset
    std::vector<DofTerm> terms; // externally flattened relation to Free slots only
    template<class F> void visit(F f) { f(key,status,equation,value,terms); }
};
struct DofSpaceData {
    std::uint32_t schema = 1;
    std::uint64_t model = 0, nodes = 0, elements = 0, revision = 1;
    std::vector<DofVariable> variables;
    std::vector<DofBinding> bindings;
    std::vector<std::uint64_t> external_equations; // opaque unique original labels, no base inference
    std::vector<std::uint64_t> solver_to_canonical; // complete permutation
    template<class F> void visit(F f) {
        f(schema,model,nodes,elements,revision,variables,bindings,external_equations,solver_to_canonical);
    }
};
// Frozen input contract. It validates a supplied numbering/transform; it does not generate MPCs.
class DofSpace {
    DofSpaceData data_;
    std::vector<std::size_t> canonical_to_solver_;
    std::map<DofKey,std::size_t> slots_;
    std::uint64_t identity_ = 0;
    void validate() {
        if(data_.schema!=1 || !data_.model || !data_.revision) throw std::invalid_argument("DOF identity/schema");
        std::map<std::uint32_t,const DofVariable*> variables;
        std::set<std::string> names;
        for(const auto &v:data_.variables) {
            if(!v.components || v.name.empty() || v.units.empty() || v.basis.empty() ||
               !enum_at_most(v.quantity,DofQuantity::User) || !variables.emplace(v.id,&v).second || !names.insert(v.name).second)
                throw std::invalid_argument("DOF variable contract");
            if(((v.quantity==DofQuantity::Translation || v.quantity==DofQuantity::Rotation) && v.components>3) ||
               ((v.quantity==DofQuantity::Temperature || v.quantity==DofQuantity::Pressure) && v.components!=1))
                throw std::invalid_argument("DOF quantity/component contract");
        }
        const auto n=data_.external_equations.size();
        if(data_.solver_to_canonical.size()!=n) throw std::invalid_argument("DOF permutation size");
        std::set<std::uint64_t> labels;
        canonical_to_solver_.assign(n,n);
        for(std::size_t i=0;i<n;++i) {
            const auto c=data_.solver_to_canonical[i];
            if(c>=n || canonical_to_solver_[c]!=n || !labels.insert(data_.external_equations[i]).second)
                throw std::invalid_argument("DOF permutation/label duplicate");
            canonical_to_solver_[c]=i;
        }
        std::vector<bool> used(n,false);
        for(std::size_t i=0;i<data_.bindings.size();++i) {
            const auto &b=data_.bindings[i]; const auto &k=b.key;
            auto v=variables.find(k.variable);
            if(!enum_at_most(k.owner,DofOwner::Global) || !enum_at_most(b.status,DofStatus::Eliminated) ||
               v==variables.end() || k.component>=v->second->components || !std::isfinite(b.value) ||
               !slots_.emplace(k,i).second || (k.owner==DofOwner::Node && (k.entity>=data_.nodes || k.local)) ||
               (k.owner==DofOwner::Element && k.entity>=data_.elements) || (k.owner==DofOwner::Global && k.entity))
                throw std::invalid_argument("DOF binding/key");
            if(b.status==DofStatus::Free) {
                if(b.equation>=n || used[b.equation] || b.value!=0 || !b.terms.empty())
                    throw std::invalid_argument("DOF free equation");
                used[b.equation]=true;
            } else {
                if(b.equation!=no_equation || (b.status!=DofStatus::Eliminated && !b.terms.empty()))
                    throw std::invalid_argument("DOF inactive equation/terms");
                if(b.status==DofStatus::Eliminated && b.terms.empty()) throw std::invalid_argument("DOF empty relation");
            }
            std::uint64_t previous=0; bool first=true;
            for(const auto &t:b.terms) {
                if(t.slot>=data_.bindings.size() || data_.bindings[t.slot].status!=DofStatus::Free ||
                   !std::isfinite(t.coefficient) || t.coefficient==0 || (!first && t.slot<=previous))
                    throw std::invalid_argument("DOF relation must be sorted, unique and flattened to free slots");
                previous=t.slot; first=false;
            }
        }
        if(std::find(used.begin(),used.end(),false)!=used.end()) throw std::invalid_argument("unowned DOF equation");
        identity_=semantic_hash(data_);
    }
  public:
    explicit DofSpace(DofSpaceData data):data_(std::move(data)) { validate(); }
    const DofSpaceData &data() const noexcept { return data_; }
    std::uint64_t identity() const noexcept { return identity_; }
    std::size_t equations() const noexcept { return canonical_to_solver_.size(); }
    std::size_t slots() const noexcept { return data_.bindings.size(); }
    std::size_t slot(const DofKey &key) const { return slots_.at(key); }
    std::size_t solver_equation(std::size_t slot) const {
        const auto &b=data_.bindings.at(slot);
        if(b.status!=DofStatus::Free) throw std::invalid_argument("DOF is not free");
        return canonical_to_solver_.at(b.equation);
    }
    auto encode(std::uint64_t budget=lifecycle::native::default_limit) const { return lifecycle::native::pack(data_,budget); }
    static DofSpace decode(std::span<const std::uint8_t> bytes,std::uint64_t budget=lifecycle::native::default_limit) {
        return DofSpace(lifecycle::native::unpack<DofSpaceData>(bytes,budget));
    }
    // Owned outputs make aliasing and arithmetic failure transactional. Kernels can cache the map instead.
    std::vector<double> gather(std::span<const double> equation_values) const {
        if(equation_values.size()!=equations()) throw std::invalid_argument("DOF gather shape");
        for(auto v:equation_values) if(!std::isfinite(v)) throw std::invalid_argument("DOF nonfinite input");
        std::vector<double> out; out.reserve(slots());
        for(std::size_t i=0;i<slots();++i) {
            const auto &b=data_.bindings[i]; double v=b.value;
            if(b.status==DofStatus::Free) v=equation_values[solver_equation(i)];
            else for(const auto &t:b.terms) v+=t.coefficient*equation_values[solver_equation(t.slot)];
            if(!std::isfinite(v)) throw std::overflow_error("DOF gather overflow");
            out.push_back(v);
        }
        return out;
    }
    // Transpose of gather's linear part. Prescribed/inactive reactions remain in caller slot space.
    std::vector<double> scatter(std::span<const double> slot_values) const {
        if(slot_values.size()!=slots()) throw std::invalid_argument("DOF scatter shape");
        for(auto v:slot_values) if(!std::isfinite(v)) throw std::invalid_argument("DOF nonfinite input");
        std::vector<double> out(equations(),0);
        for(std::size_t i=0;i<slots();++i) {
            const auto &b=data_.bindings[i];
            if(b.status==DofStatus::Free) out[solver_equation(i)]+=slot_values[i];
            else for(const auto &t:b.terms) out[solver_equation(t.slot)]+=t.coefficient*slot_values[i];
        }
        for(auto v:out) if(!std::isfinite(v)) throw std::overflow_error("DOF scatter overflow");
        return out;
    }
};
} // namespace sdb

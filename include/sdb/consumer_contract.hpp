#pragma once
#include "dof_space.hpp"
#include "model_ir/foundation_validate.hpp"
#include <functional>
#include <string_view>

namespace sdb::consumer::v1 {
inline constexpr std::uint32_t contract_version=1;
// Opaque formulation names are interpreted by the consumer, never dispatched by the database.
struct TopologyContract {
    std::string name;
    std::uint32_t version=1, nodes=0;
    std::vector<std::uint32_t> face_offsets{0}, face_nodes;
    std::vector<std::array<std::uint32_t,2>> edges;
    template<class F> void visit(F f) { f(name,version,nodes,face_offsets,face_nodes,edges); }
    void validate() const {
        if(name.empty() || !version || !nodes || face_offsets.empty() || face_offsets.front()!=0 ||
           face_offsets.back()!=face_nodes.size() || !std::is_sorted(face_offsets.begin(),face_offsets.end()))
            throw std::invalid_argument("consumer topology contract");
        for(std::size_t i=1;i<face_offsets.size();++i) {
            std::set<std::uint32_t> seen;
            if(face_offsets[i]==face_offsets[i-1]) throw std::invalid_argument("consumer empty face");
            for(auto j=face_offsets[i-1];j<face_offsets[i];++j)
                if(face_nodes[j]>=nodes || !seen.insert(face_nodes[j]).second) throw std::invalid_argument("consumer face node");
        }
        for(const auto &e:edges) if(e[0]>=nodes || e[1]>=nodes || e[0]==e[1]) throw std::invalid_argument("consumer edge");
    }
};
struct BatchData {
    std::uint32_t version=1, formulation_version=1;
    std::uint64_t model=0, equation_space=0;
    std::string formulation;
    TopologyContract topology;
    std::vector<std::uint64_t> elements, connectivity, sample_offsets{0}, dof_offsets{0}, dof_slots;
    // One explicit material and section per sample; sample numbering has no inferred quadrature.
    std::vector<lifecycle::ResultAddress> samples;
    std::vector<std::uint32_t> materials, sections;
    template<class F> void visit(F f) {
        f(version,formulation_version,model,equation_space,formulation,topology,elements,connectivity,
          sample_offsets,dof_offsets,dof_slots,samples,materials,sections);
    }
};
class MaterialCatalog {
    std::shared_ptr<const std::vector<lifecycle::MaterialDefinition>> definitions_;
  public:
    explicit MaterialCatalog(std::vector<lifecycle::MaterialDefinition> definitions) {
        lifecycle::ModelFoundation f; f.material_definitions=std::move(definitions);
        (void)lifecycle::validate_foundation<double>(f);
        definitions_=std::make_shared<const std::vector<lifecycle::MaterialDefinition>>(std::move(f.material_definitions));
    }
    const auto &operator[](std::size_t i) const { return definitions_->at(i); }
    std::size_t size() const noexcept { return definitions_->size(); }
    std::uint64_t identity() const { return semantic_hash(*definitions_); }
};
// Section schemas reuse typed parameter/table payloads. The consumer interprets the named schema.
struct SectionDefinition {
    std::string name, schema;
    std::uint32_t version=1;
    std::vector<lifecycle::MaterialParameter> parameters;
    std::vector<lifecycle::MaterialTable> tables;
    template<class F> void visit(F f) { f(name,schema,version,parameters,tables); }
};
class SectionCatalog {
    std::shared_ptr<const std::vector<SectionDefinition>> definitions_;
  public:
    explicit SectionCatalog(std::vector<SectionDefinition> definitions) {
        lifecycle::ModelFoundation validation;
        for(const auto &s:definitions) {
            lifecycle::MaterialDefinition typed;
            typed.material=s.name; typed.schema_id=s.schema; typed.version=s.version;
            typed.parameters=s.parameters; typed.tables=s.tables; validation.material_definitions.push_back(std::move(typed));
        }
        (void)lifecycle::validate_foundation<double>(validation);
        definitions_=std::make_shared<const std::vector<SectionDefinition>>(std::move(definitions));
    }
    const auto &operator[](std::size_t i) const { return definitions_->at(i); }
    std::size_t size() const noexcept { return definitions_->size(); }
    std::uint64_t identity() const { return semantic_hash(*definitions_); }
};
struct BatchSnapshot {
    std::uint32_t version=1;
    BatchData batch;
    DofSpaceData dofs;
    std::vector<lifecycle::MaterialDefinition> materials;
    std::vector<SectionDefinition> sections;
    template<class F> void visit(F f) { f(version,batch,dofs,materials,sections); }
};
// Immutable batch metadata is shared by views; values/outputs are separate generation-checked borrows.
class Batch {
    std::shared_ptr<const BatchData> data_;
    std::shared_ptr<const DofSpace> dofs_;
    MaterialCatalog materials_;
    SectionCatalog sections_;
    std::uint64_t identity_=0;
  public:
    Batch(BatchData data, DofSpace dofs, MaterialCatalog materials,SectionCatalog sections)
        :dofs_(std::make_shared<const DofSpace>(std::move(dofs))),materials_(std::move(materials)),sections_(std::move(sections)) {
        data.topology.validate(); const auto n=data.elements.size(), q=data.samples.size();
        if(data.version!=contract_version || data.formulation.empty() || !data.formulation_version ||
           data.model!=dofs_->data().model || data.equation_space!=dofs_->identity() ||
           data.connectivity.size()!=product(n,data.topology.nodes) || data.sample_offsets.size()!=sum(n,1) ||
           data.dof_offsets.size()!=sum(n,1) || data.sample_offsets.front()!=0 || data.dof_offsets.front()!=0 ||
           data.sample_offsets.back()!=q || data.dof_offsets.back()!=data.dof_slots.size() ||
           !std::is_sorted(data.sample_offsets.begin(),data.sample_offsets.end()) ||
           !std::is_sorted(data.dof_offsets.begin(),data.dof_offsets.end()) || data.materials.size()!=q || data.sections.size()!=q)
            throw std::invalid_argument("consumer batch identity/layout");
        std::set<std::uint64_t> elements;
        std::set<lifecycle::ResultAddress> samples;
        for(std::size_t e=0;e<n;++e) {
            if(data.elements[e]>=dofs_->data().elements || !elements.insert(data.elements[e]).second)
                throw std::invalid_argument("consumer element identity");
            std::set<std::uint64_t> nodes,slots;
            for(std::size_t j=0;j<data.topology.nodes;++j) {
                auto node=data.connectivity[e*data.topology.nodes+j];
                if(node>=dofs_->data().nodes || !nodes.insert(node).second) throw std::invalid_argument("consumer connectivity");
            }
            for(auto j=data.dof_offsets[e];j<data.dof_offsets[e+1];++j) {
                auto slot=data.dof_slots[j];
                if(slot>=dofs_->slots() || !slots.insert(slot).second) throw std::invalid_argument("consumer DOF slot");
                const auto &k=dofs_->data().bindings[slot].key;
                if((k.owner==DofOwner::Node && !nodes.contains(k.entity)) || (k.owner==DofOwner::Element && k.entity!=data.elements[e]))
                    throw std::invalid_argument("consumer DOF belongs to another element");
            }
            for(auto j=data.sample_offsets[e];j<data.sample_offsets[e+1];++j) {
                const auto &a=data.samples[j];
                if(a.entity!=data.elements[e]+1 || !a.integration_point || data.materials[j]>=materials_.size() ||
                   data.sections[j]>=sections_.size() || !samples.insert(a).second)
                    throw std::invalid_argument("consumer sample/material identity");
                FieldDescriptor d; d.name="batch sample";
                StateLayout layout; layout.domain=1; layout.addresses={a}; layout.validate(d);
            }
        }
        data_=std::make_shared<const BatchData>(std::move(data));
        identity_=semantic_hash(std::array<std::uint64_t,4>{semantic_hash(*data_),dofs_->identity(),materials_.identity(),sections_.identity()});
    }
    const BatchData &data() const noexcept { return *data_; }
    const DofSpace &dofs() const noexcept { return *dofs_; }
    const MaterialCatalog &materials() const noexcept { return materials_; }
    const SectionCatalog &sections() const noexcept { return sections_; }
    std::uint64_t identity() const noexcept { return identity_; }
    auto encode(std::uint64_t budget=lifecycle::native::default_limit) const {
        BatchSnapshot snapshot; snapshot.batch=*data_; snapshot.dofs=dofs_->data();
        for(std::size_t i=0;i<materials_.size();++i) snapshot.materials.push_back(materials_[i]);
        for(std::size_t i=0;i<sections_.size();++i) snapshot.sections.push_back(sections_[i]);
        return lifecycle::native::pack(snapshot,budget);
    }
    static Batch decode(std::span<const std::uint8_t> bytes,std::uint64_t budget=lifecycle::native::default_limit) {
        auto snapshot=lifecycle::native::unpack<BatchSnapshot>(bytes,budget);
        if(snapshot.version!=1) throw std::invalid_argument("consumer snapshot version");
        return Batch(std::move(snapshot.batch),DofSpace(std::move(snapshot.dofs)),
            MaterialCatalog(std::move(snapshot.materials)),SectionCatalog(std::move(snapshot.sections)));
    }
    void require_material(std::size_t index,std::string_view schema,std::uint32_t version) const {
        const auto &m=materials_[index];
        if(m.schema_id!=schema || m.version!=version) throw std::invalid_argument("consumer material schema mismatch");
    }
    void require_section(std::size_t index,std::string_view schema,std::uint32_t version) const {
        const auto &s=sections_[index];
        if(s.schema!=schema || s.version!=version) throw std::invalid_argument("consumer section schema mismatch");
    }
    std::span<const std::uint64_t> nodes(std::size_t e) const {
        if(e>=data_->elements.size()) throw std::out_of_range("consumer element");
        return std::span(data_->connectivity).subspan(e*data_->topology.nodes,data_->topology.nodes);
    }
    std::span<const std::uint64_t> slots(std::size_t e) const {
        if(e>=data_->elements.size()) throw std::out_of_range("consumer element");
        return std::span(data_->dof_slots).subspan(data_->dof_offsets[e],data_->dof_offsets[e+1]-data_->dof_offsets[e]);
    }
    void require(std::string_view formulation,std::uint32_t version,const TopologyContract &topology) const {
        if(formulation!=data_->formulation || version!=data_->formulation_version ||
           lifecycle::native::pack(topology)!=lifecycle::native::pack(data_->topology))
            throw std::invalid_argument("consumer capability mismatch");
    }
};
struct FieldRequirement {
    FieldDescriptor descriptor;
    std::string schema;
    std::uint32_t version=1;
    std::uint64_t basis=0;
    std::size_t rows=0;
    std::optional<StateLayout> addressing;
};
struct MemoryRange { std::uintptr_t begin=0,end=0; };
inline bool overlaps(MemoryRange a,MemoryRange b) { return a.begin<a.end && b.begin<b.end && a.begin<b.end && b.begin<a.end; }
template<class F,bool Writable> class FieldBinding {
    using V=typename F::value_type;
    using L=typename F::layout_type;
    using Q=std::conditional_t<Writable,V,const V>;
    CheckedView<Q,F::components,L> view_;
    std::size_t bytes_;
  public:
    FieldBinding(CheckedView<Q,F::components,L> view,std::size_t bytes):view_(std::move(view)),bytes_(bytes) {}
    auto resolve() const { return view_.raw(); }
    MemoryRange range() const {
        auto v=resolve(); auto begin=reinterpret_cast<std::uintptr_t>(v.data);
        if(bytes_>UINTPTR_MAX-begin) throw std::overflow_error("consumer pointer range");
        return {begin,begin+bytes_};
    }
};
inline void check_field(const RegisteredState &state,const DofSpace &space,const FieldRequirement &r,std::uint64_t consumer=0) {
    if(!state.frozen() || state.model_context()!=space.data().model || state.equation_context()!=space.identity() ||
       state.consumer_context()!=consumer)
        throw std::invalid_argument("consumer state context/phase");
    const auto &e=state.find(r.descriptor.name);
    if(lifecycle::native::pack(e.descriptor())!=lifecycle::native::pack(r.descriptor) || e.schema_id()!=r.schema ||
       e.schema_version()!=r.version || e.basis_id()!=r.basis || e.count()!=r.rows || e.addressing().has_value()!=r.addressing.has_value())
        throw std::invalid_argument("consumer field contract mismatch");
    if(r.addressing && lifecycle::native::pack(*r.addressing)!=lifecycle::native::pack(*e.addressing()))
        throw std::invalid_argument("consumer field addressing mismatch");
    e.validate_shape();
}
template<class F> FieldBinding<F,false> bind_read(const RegisteredState &state,const DofSpace &space,const FieldRequirement &r) {
    check_field(state,space,r); const auto &f=state.template get<F>(r.descriptor.name);
    return {f.checked_read(),f.bytes()};
}
template<class F> FieldBinding<F,true> bind_write(RegisteredState &state,const DofSpace &space,const FieldRequirement &r) {
    check_field(state,space,r); auto &f=state.template get<F>(r.descriptor.name);
    return {f.checked_write(),f.bytes()};
}
template<class F> FieldBinding<F,false> bind_read(const RegisteredState &state,const Batch &batch,const FieldRequirement &r) {
    check_field(state,batch.dofs(),r,batch.identity()); const auto &f=state.template get<F>(r.descriptor.name);
    return {f.checked_read(),f.bytes()};
}
template<class F> FieldBinding<F,true> bind_write(RegisteredState &state,const Batch &batch,const FieldRequirement &r) {
    check_field(state,batch.dofs(),r,batch.identity()); auto &f=state.template get<F>(r.descriptor.name);
    return {f.checked_write(),f.bytes()};
}
// Validate at each batch entry, before any write. Caller joins owners and prevents mutation of layouts.
class AccessSet {
    std::vector<std::function<MemoryRange()>> reads_,writes_;
  public:
    template<class F> void read(FieldBinding<F,false> b) { reads_.push_back([b]{return b.range();}); }
    template<class F> void write(FieldBinding<F,true> b) { writes_.push_back([b]{return b.range();}); }
    void validate() const {
        for(const auto &r:reads_) (void)r();
        for(std::size_t i=0;i<writes_.size();++i) {
            auto w=writes_[i]();
            for(const auto &r:reads_) if(overlaps(w,r())) throw std::invalid_argument("consumer read/write alias");
            for(std::size_t j=0;j<i;++j) if(overlaps(w,writes_[j]())) throw std::invalid_argument("consumer write/write alias");
        }
    }
};
inline lifecycle::TypedFieldPayload snapshot(const RegisteredState &state,const DofSpace &space,const FieldRequirement &r,
                                             std::uint64_t budget=lifecycle::native::default_limit,std::uint64_t consumer=0) {
    check_field(state,space,r,consumer);
    if(state.in_trial() || !r.addressing) throw std::invalid_argument("snapshot needs committed explicit addresses");
    const auto &e=state.find(r.descriptor.name);
    const auto scalar_bytes=std::visit([](const auto &v){return sizeof(typename std::decay_t<decltype(v)>::value_type);},
                                      lifecycle::empty_typed_values(e.descriptor().scalar));
    const auto bytes=sum(lifecycle::native::encoded_size(e.descriptor(),budget),sum(e.schema_id().size()+64,
        product(e.count(),sum(sizeof(lifecycle::ResultAddress),product(e.descriptor().component_count(),scalar_bytes+1)))));
    if(bytes>budget) throw std::length_error("consumer snapshot ownership budget");
    lifecycle::TypedFieldPayload out;
    out.schema=e.descriptor(); out.addresses=e.addressing()->addresses; out.values=e.values();
    out.validity=e.validity(); out.schema_id=e.schema_id(); out.schema_version=e.schema_version(); out.basis_id=e.basis_id();
    lifecycle::validate_typed_payload(out); return out;
}
inline lifecycle::TypedFieldPayload snapshot(const RegisteredState &state,const Batch &batch,const FieldRequirement &r,
                                             std::uint64_t budget=lifecycle::native::default_limit) {
    return snapshot(state,batch.dofs(),r,budget,batch.identity());
}
} // namespace sdb::consumer::v1

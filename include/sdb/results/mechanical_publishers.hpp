#pragma once
#include "results.hpp"
#include "../general_contact/database.hpp"

namespace sdb::lifecycle {
// Compact sample-major recovery input. Only actual section points consume
// values, so a section-bearing element may share a model with ordinary solids.
template<class T, class V>
ResultField section_point_field(const CompiledModel<T>& model,FieldDescriptor descriptor,
                                std::span<const V> values, std::uint64_t budget=native::default_limit) {
    if(descriptor.association!=Association::Element || descriptor.location!=FieldLocation::SectionPoint)
        throw std::invalid_argument("section output requires Element/SectionPoint schema");
    descriptor.validate();
    const auto width=descriptor.component_count();
    if(values.size()%width || sum(product(values.size()/width,sizeof(ResultAddress)),
       product(values.size(),sizeof(V)))>budget) throw std::length_error("section output shape/budget");
    std::vector<ResultAddress> addresses;
    addresses.reserve(values.size()/width);
    for(Index e=0;e<model.mesh.elements();++e)
        for(std::size_t q=0;q<model.mesh.sample_point_count(e);++q) {
            const auto p=model.mesh.sample_point(e,q);
            if(!p.section_point) continue;
            if(addresses.size()==values.size()/width) throw std::invalid_argument("section output missing sample");
            ResultAddress a; a.entity=Id(e)+1; a.integration_point=p.integration_point;
            a.layer=p.layer; a.section_point=p.section_point; addresses.push_back(a);
        }
    if(addresses.size()!=values.size()/width) throw std::invalid_argument("section output excess samples");
    return make_typed_field<V>(std::move(descriptor),std::move(addresses),{values.begin(),values.end()});
}

// Publish one recovered local section in its catalogued basis.
template<class T>
ResultField section_point_field(const ResultModel& model,Index e,FieldDescriptor d,std::vector<T> values,
                                std::uint64_t basis,const std::string& region,bool history) {
    std::vector<ResultAddress> addresses;
    for(const auto &point:model.samples.at(e)) {
        ResultAddress a; a.entity=Id(e)+1; a.integration_point=point.integration_point;
        a.section_point=point.section_point; a.layer=point.layer;
        validate_result_address(a,d,model); addresses.push_back(a);
    }
    auto f=make_typed_field<T>(std::move(d),std::move(addresses),std::move(values),{},region,history);
    f.typed->basis_id=basis;
    return f;
}
// Domain generation stays stable across time increments. Contact's internal
// transaction/borrow epoch is not a persistent result identity.
template<class T>
void append_contact_history(ResultFrame& frame,const ResultModel& model,
                            const general_contact::Database<T>& contact,
                            const general_contact::Binding& expected,std::uint64_t generation,
                            std::uint64_t budget=native::default_limit) {
    using namespace general_contact;
    if(!generation || contact.in_trial() || contact.binding()!=expected || expected.mesh!=model.mesh ||
       expected.configuration!=model.configuration || expected.step!=frame.info.id || expected.time!=frame.info.time ||
       !frame.binding || *frame.binding!=ResultBinding{model.mesh,model.definition,model.configuration,
           frame.info.step,frame.info.increment,frame.info.time})
        throw std::invalid_argument("contact output binding/phase mismatch");
    const auto &table=contact.committed();
    constexpr std::size_t row_bytes=3*sizeof(ResultAddress)+sizeof(std::uint32_t)+2*sizeof(T);
    if(product(table.size(),row_bytes)>budget) throw std::length_error("contact result budget");
    auto descriptor=[](const std::string& name,ScalarType type) {
        FieldDescriptor d; d.name=name; d.scalar=type; d.association=Association::Contact;
        d.location=FieldLocation::Entity; return d;
    };
    auto endpoint=[&](const Feature& f) {
        ResultEndpoint e;
        if(f.kind==FeatureKind::Node) {
            if(f.local || f.side!=1) throw std::invalid_argument("noncanonical node endpoint");
            e.entity=f.entity;
        }
        else if(f.kind==FeatureKind::Face) {
            if(f.local==std::numeric_limits<std::uint32_t>::max()) throw std::out_of_range("face local overflow");
            e.kind=2; e.entity=f.entity; e.local=f.local+1; e.side=f.side;
        } else throw std::invalid_argument("contact output supports mesh node/face features");
        return e;
    };
    std::vector<ResultAddress> addresses;
    std::vector<std::uint32_t> status;
    std::vector<T> slip,work;
    addresses.reserve(table.size());status.reserve(table.size());slip.reserve(table.size());work.reserve(table.size());
    auto sd=descriptor("CSTATUS",ScalarType::UInt32);
    for(std::size_t i=0;i<table.size();++i) {
        const auto &k=table.keys()[i]; const auto h=table.at(i);
        if(k.point==std::numeric_limits<std::uint32_t>::max()) throw std::out_of_range("contact point overflow");
        ResultAddress a; a.domain=1; a.interaction=k.interaction; a.contact_point=k.point+1;
        a.contact_generation=generation; a.endpoint_a=endpoint(k.a); a.endpoint_b=endpoint(k.b);
        validate_result_address(a,sd,model);
        addresses.push_back(a); status.push_back(static_cast<std::uint32_t>(h.status));
        slip.push_back(h.accumulated_slip); work.push_back(h.friction_work);
    }
    std::array<ResultField,3> fields{
        make_typed_field<std::uint32_t>(sd,addresses,std::move(status)),
        make_typed_field<T>(descriptor("CSLIP_ACCUM",scalar_type<T>()),addresses,std::move(slip)),
        make_typed_field<T>(descriptor("CFRICTION_WORK",scalar_type<T>()),std::move(addresses),std::move(work))};
    for(auto &f:fields) {
        f.typed->schema_id="sdb.contact.committed";
        for(const auto &old:frame.fields) if(old.descriptor.name==f.descriptor.name &&
            old.descriptor.location==Location::Contact) throw std::invalid_argument("duplicate contact output");
    }
    frame.fields.reserve(sum(frame.fields.size(),fields.size()));
    for(auto &f:fields) frame.fields.push_back(std::move(f));
}
}

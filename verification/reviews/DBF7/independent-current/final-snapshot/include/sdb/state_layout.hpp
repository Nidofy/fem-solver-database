#pragma once
#include "results/typed.hpp"

namespace sdb {
// Reuse result identities so persistent state and output address the same samples.
// Physical values still live in the existing typed Field, with flat packed rows.
struct StateLayout {
    std::uint32_t version = 1;
    std::uint64_t domain = 0;
    std::vector<std::uint64_t> offsets, indices;
    std::vector<lifecycle::ResultAddress> addresses;
    template<class F> void visit(F f) { f(version,domain,offsets,indices,addresses); }
    void validate(const FieldDescriptor &d) const {
        d.validate();
        const auto n=addresses.size();
        if(version!=1) throw std::invalid_argument("state addressing version");
        if(d.storage==StorageKind::Dense) {
            if(domain!=n || !offsets.empty() || !indices.empty()) throw std::invalid_argument("dense state layout");
        } else if(d.storage==StorageKind::Ragged) {
            if(domain==UINT64_MAX || offsets.size()!=domain+1 || offsets.empty() || offsets.front()!=0 ||
               offsets.back()!=n || !std::is_sorted(offsets.begin(),offsets.end()) || !indices.empty())
                throw std::invalid_argument("ragged state layout");
        } else {
            if(!offsets.empty() || indices.size()!=n) throw std::invalid_argument("sparse state layout");
            for(std::size_t i=0;i<n;++i) if(indices[i]>=domain || (i && indices[i]<=indices[i-1]))
                throw std::invalid_argument("sparse state index");
        }
        std::set<lifecycle::ResultAddress> seen;
        for(const auto &a:addresses) {
            if(!seen.insert(a).second) throw std::invalid_argument("duplicate state sample identity");
            lifecycle::ResultAddress allowed;
            allowed.entity=a.entity;
            if(!a.entity && d.association!=Association::Global && d.association!=Association::Contact)
                throw std::invalid_argument("state entity is one-based");
            switch(d.association) {
            case Association::Node: break;
            case Association::Global:
                if(a.entity!=0) throw std::invalid_argument("global state entity");
                break;
            case Association::Element:
                if(d.location==FieldLocation::ElementNode) {
                    if(!a.element_node) throw std::invalid_argument("state local node");
                    allowed.element_node=a.element_node;
                } else if(d.location==FieldLocation::IntegrationPoint || d.location==FieldLocation::SectionPoint) {
                    if(!a.integration_point || bool(a.layer)!=bool(a.section_point) ||
                       (d.location==FieldLocation::SectionPoint && !a.section_point))
                        throw std::invalid_argument("state sample point");
                    allowed.integration_point=a.integration_point; allowed.layer=a.layer; allowed.section_point=a.section_point;
                }
                break;
            case Association::Face:
                if(!a.face || (a.side!=1 && a.side!=-1)) throw std::invalid_argument("state face identity");
                allowed.face=a.face; allowed.side=a.side;
                break;
            case Association::Contact: {
                auto endpoint=[](const lifecycle::ResultEndpoint &e) {
                    return e.kind<=2 && e.entity &&
                        (e.kind==0 ? e.local==0 && e.side==0 :
                         e.kind==1 ? e.local>0 && e.side==0 : e.local>0 && (e.side==1 || e.side==-1));
                };
                if(a.entity || a.domain!=1 || !a.contact_point || !a.contact_generation || !a.interaction ||
                   !endpoint(a.endpoint_a) || !endpoint(a.endpoint_b) || a.endpoint_a==a.endpoint_b)
                    throw std::invalid_argument("state contact identity/generation");
                allowed.contact_generation=a.contact_generation; allowed.interaction=a.interaction;
                allowed.endpoint_a=a.endpoint_a; allowed.endpoint_b=a.endpoint_b;
                allowed.contact_point=a.contact_point; allowed.domain=a.domain;
                break;
            }
            }
            if(a!=allowed) throw std::invalid_argument("inactive state address lanes must be zero");
        }
    }
};
} // namespace sdb

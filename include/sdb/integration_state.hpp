#pragma once
#include <array>
#include "registered_state.hpp"
#include <tuple>

namespace sdb {
// Positive global element labels and one-based local sample numbers. An empty
// layer list denotes a continuum. For beams, each fiber is a layer with one point.
struct ElementIntegration {
    std::uint64_t element=0;
    std::uint32_t integration_points=0;
    std::vector<std::uint32_t> points_per_layer;
};
inline StateLayout integration_layout(std::vector<ElementIntegration> elements,
                                      std::size_t max_rows=std::numeric_limits<std::size_t>::max()) {
    std::ranges::sort(elements,{},&ElementIntegration::element);
    StateLayout result;
    result.domain=elements.size();
    result.offsets.push_back(0);
    std::uint64_t previous=0;
    for(const auto &e:elements) {
        if(e.element<=previous || !e.integration_points || e.points_per_layer.size()>UINT32_MAX)
            throw std::invalid_argument("invalid element integration descriptor");
        previous=e.element;
        std::size_t section_points=e.points_per_layer.empty()?1:0;
        for(auto count:e.points_per_layer) {
            if(!count) throw std::invalid_argument("empty integration layer");
            section_points=sum(section_points,count);
        }
        const auto rows=product(e.integration_points,section_points);
        if(rows>max_rows-result.addresses.size()) throw std::length_error("integration layout row budget");
        for(std::uint64_t ip=1;ip<=e.integration_points;++ip) {
            lifecycle::ResultAddress a; a.entity=e.element; a.integration_point=static_cast<std::uint32_t>(ip);
            if(e.points_per_layer.empty()) result.addresses.push_back(a);
            for(std::size_t layer=0;layer<e.points_per_layer.size();++layer) {
                a.layer=static_cast<std::uint32_t>(layer+1);
                for(std::uint64_t point=1;point<=e.points_per_layer[layer];++point) {
                    a.section_point=static_cast<std::uint32_t>(point);
                    result.addresses.push_back(a);
                }
            }
        }
        result.offsets.push_back(result.addresses.size());
    }
    return result;
}
inline std::size_t integration_row(const StateLayout &layout,const lifecycle::ResultAddress &address) {
    const auto it=std::ranges::lower_bound(layout.addresses,address);
    if(it==layout.addresses.end() || *it!=address) throw std::out_of_range("unknown integration sample");
    return static_cast<std::size_t>(it-layout.addresses.begin());
}
inline std::uint64_t integration_layout_identity(const StateLayout &layout) { return semantic_hash(layout); }

enum class HistoryFinitePolicy : std::uint8_t { RequireFinite, MaskMissing };
struct HistorySlotDescriptor {
    FieldDescriptor field;
    std::vector<double> initial;
    HistoryFinitePolicy finite=HistoryFinitePolicy::RequireFinite;
    template<class F> void visit(F f) { f(field,initial,finite); }
};
struct HistorySchemaDescriptor {
    std::string id;
    std::uint32_t version=1;
    std::vector<HistorySlotDescriptor> slots;
    template<class F> void visit(F f) { f(id,version,slots); }
    void validate() const {
        if(id.empty() || !version || slots.empty()) throw std::invalid_argument("invalid material history schema");
        std::set<std::string> names;
        for(const auto &slot:slots) {
            const auto &d=slot.field; d.validate();
            if(!names.insert(d.name).second || d.association!=Association::Element ||
               (d.location!=FieldLocation::SectionPoint && d.location!=FieldLocation::IntegrationPoint) ||
               d.persistence!=Persistence::Persistent || d.restart!=RestartPolicy::Required ||
               d.storage!=StorageKind::Ragged || slot.initial.size()!=d.component_count() ||
               slot.finite>HistoryFinitePolicy::MaskMissing ||
               (slot.finite==HistoryFinitePolicy::MaskMissing)!=(d.missing==MissingPolicy::ValidityMask))
                throw std::invalid_argument("invalid history slot policy or shape");
            for(double value:slot.initial) if(!std::isfinite(value)) throw std::invalid_argument("nonfinite history initializer");
        }
    }
    // Each plugin binds its own scalar type and compile-time component count.
    // Existing fields and the destination survive a failed late slot admission.
    template<class... Fields> void register_fields(RegisteredState &state,const StateLayout &layout,
                                                    std::uint64_t basis=0) const {
        validate();
        if(state.frozen() || state.in_trial() || slots.size()!=sizeof...(Fields))
            throw std::invalid_argument("history registration phase or slot count");
        RegisteredState staged(state);
        std::size_t index=0;
        auto add=[&]<class FieldType>() {
            using V=typename FieldType::value_type;
            const auto &slot=slots[index++];
            if(slot.field.scalar!=scalar_type<V>() || slot.field.component_count()!=FieldType::components)
                throw std::invalid_argument("history field type/shape mismatch");
            std::vector<V> defaults;
            for(auto value:slot.initial) {
                if constexpr(std::is_integral_v<V>) {
                    // Compare half-open powers of two before converting a binary64 integer.
                    const double bound=std::ldexp(1.0,std::numeric_limits<V>::digits);
                    if(std::trunc(value)!=value || value<(std::is_signed_v<V>?-bound:0.0) || value>=bound)
                        throw std::invalid_argument("history integer initializer");
                } else if(std::abs(value)>std::numeric_limits<V>::max()) throw std::invalid_argument("history float initializer");
                defaults.push_back(static_cast<V>(value));
            }
            staged.add<FieldType>(slot.field,layout.addresses.size(),id,version,std::move(defaults),basis,layout);
        };
        (add.template operator()<Fields>(),...);
        state=std::move(staged);
    }
};
} // namespace sdb

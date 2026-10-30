#pragma once
#include "dynamics.hpp"
#include <optional>

namespace sdb::consumer {
inline constexpr std::uint32_t contract_version = 0;
inline constexpr Index invalid_element = std::numeric_limits<Index>::max();
// Checked at each batch entry. No ownership, copies or hot-loop lookups.
template <class T, std::size_t C = 1> class BorrowedField {
    CheckedView<const T, C, SoA> checked_;
  public:
    explicit BorrowedField(const Field<T, C> &field) : checked_(field.checked_read()) {}
    View<const T, C> resolve() const { return checked_.raw(); }
    std::size_t size() const { return checked_.size(); }
    const T &at(std::size_t i, std::size_t c = 0) const { return checked_.at(i, c); }
};
template <class T> struct NodeStateView {
    BorrowedField<T, 3> displacement, velocity, acceleration;
    BorrowedField<T> mass, inverse_mass;
    BorrowedField<std::uint8_t> node_active, element_active;
    std::uint64_t model_id;
    static NodeStateView capture(const StateDB<T> &state) {
        state.validate_shape();
        const auto &s = state.data();
        return {BorrowedField<T, 3>(s.displacement), BorrowedField<T, 3>(s.velocity),
                BorrowedField<T, 3>(s.acceleration), BorrowedField<T>(s.mass),
                BorrowedField<T>(s.inverse_mass), BorrowedField<std::uint8_t>(s.node_active),
                BorrowedField<std::uint8_t>(s.element_active), state.model_id()};
    }
    void validate() const {
        element_active.resolve();
        const auto n = displacement.size();
        if (velocity.size() != n || acceleration.size() != n || mass.size() != n ||
            inverse_mass.size() != n || node_active.size() != n)
            throw std::logic_error("node view dimensions changed");
    }
};
template <std::size_t N> struct ElementBatchView {
    BorrowedField<Index, N> nodes;
    BorrowedField<Index> global_elements;
    BorrowedField<std::size_t> integration_points;
    BorrowedField<std::uint8_t> active;
};
template <class T> struct MaterialView { BorrowedField<T> lambda, mu; };
template <class T, std::size_t N> struct ShapeTableView {
    BorrowedField<T, 3 * N> gradients;
    BorrowedField<T> weights;
};
// Outputs are borrowed separately and are never reachable from input views.
template <class T> class ResultSink {
    CheckedView<T, 3, SoA> force_;
    CheckedView<T, 6, SoA> stress_, strain_;
  public:
    struct Resolved { View<T, 3> force; View<T, 6> stress, strain; };
    ResultSink(Field<T, 3> &force, Field<T, 6> &stress, Field<T, 6> &strain)
        : force_(force.checked_write()), stress_(stress.checked_write()), strain_(strain.checked_write()) {}
    Resolved resolve() const { return {force_.raw(), stress_.raw(), strain_.raw()}; }
};
// Informational value snapshot. Only CheckpointLink::verify authorizes append.
struct RestartLink {
    std::uint64_t definition, configuration, total_increment, result_bytes, result_hash, restart_bytes, restart_hash;
    template <class Link> static RestartLink capture(const Link &link) {
        if (link.schema != 1) throw std::invalid_argument("unsupported restart link schema");
        return {link.definition, link.configuration, link.total_increment, link.result_bytes, link.result_hash,
                link.restart_bytes, link.restart_hash};
    }
    void require_identity(std::uint64_t d, std::uint64_t c, std::uint64_t increment) const {
        if (definition != d || configuration != c || total_increment != increment)
            throw std::invalid_argument("restart consumer identity mismatch");
    }
};
// Adapter-only one-based numbering. Legacy kernels may mutate only this owned scratch.
template <std::size_t N>
std::vector<Index> private_dof_scratch(const BorrowedField<Index, N> &nodes, std::size_t element,
                                     std::uint32_t components = 3) {
    if (!components) throw std::invalid_argument("zero DOF components");
    std::vector<Index> scratch(product(N, components));
    for (std::size_t a = 0; a < N; ++a)
        for (std::uint32_t c = 0; c < components; ++c) {
            auto node=nodes.at(element,a);
            if (node > (std::numeric_limits<Index>::max()-c-1)/components)
                throw std::overflow_error("DOF scratch overflow");
            auto value = node * components + c + 1;
            scratch[a * components + c] = static_cast<Index>(value);
        }
    return scratch;
}
// Reference-only deterministic serial consumer. Geometry/material arrays remain plan-owned;
// the adapter owns only element/IP maps and force scratch, prepared once outside evaluation.
template <class T> class ReferenceConsumer {
    Field<Index> tet_elements_, hex_elements_;
    Field<std::size_t> tet_ips_, hex_ips_;
    Field<T, 12> tet_local_;
    Field<T, 24> hex_local_;
    ElementBatchView<4> tet_;
    MaterialView<T> tet_material_;
    ShapeTableView<T, 4> tet_shape_;
    std::optional<ElementBatchView<8>> hex_;
    std::optional<MaterialView<T>> hex_material_;
    std::optional<ShapeTableView<T, 8>> hex_shape_;
    std::uint64_t model_id_;
    std::size_t nodes_, points_;
  public:
    ReferenceConsumer(const TetPlan<T> &plan, const StateDB<T> &state)
        : tet_elements_(plan.is_tet.size()), tet_ips_(plan.ip_offsets.size() - 1),
          tet_local_(plan.nodes.size()),
          tet_{BorrowedField<Index, 4>(plan.nodes), BorrowedField<Index>(tet_elements_),
               BorrowedField<std::size_t>(tet_ips_), BorrowedField<std::uint8_t>(state.data().element_active)},
          tet_material_{BorrowedField<T>(plan.lambda), BorrowedField<T>(plan.mu)},
          tet_shape_{BorrowedField<T, 12>(plan.gradients), BorrowedField<T>(plan.volume)},
          model_id_(plan.model_id), nodes_(plan.fixed.size()), points_(state.data().stress.size()) {
        if (state.model_id() != model_id_) throw std::invalid_argument("consumer model mismatch");
        state.validate_shape();
        auto es = tet_elements_.write(); auto ips = tet_ips_.write();
        for (std::size_t e = 0; e < plan.is_tet.size(); ++e) {
            es(e) = plan.is_tet[e] ? static_cast<Index>(e) : invalid_element;
            ips(e) = plan.ip_offsets[e];
        }
        if (plan.hex) {
            const auto &h = *plan.hex;
            hex_elements_.resize(h.elements.size()); hex_ips_.resize(h.ip_offsets.size());
            hex_local_.resize(h.elements.size());
            for (std::size_t e = 0; e < h.elements.size(); ++e) {
                hex_elements_.write()(e) = h.elements[e]; hex_ips_.write()(e) = h.ip_offsets[e];
            }
            hex_.emplace(ElementBatchView<8>{BorrowedField<Index, 8>(h.nodes), BorrowedField<Index>(hex_elements_),
                BorrowedField<std::size_t>(hex_ips_), BorrowedField<std::uint8_t>(state.data().element_active)});
            hex_material_.emplace(MaterialView<T>{BorrowedField<T>(h.lambda), BorrowedField<T>(h.mu)});
            hex_shape_.emplace(ShapeTableView<T, 8>{BorrowedField<T, 24>(h.gradients), BorrowedField<T>(h.weights)});
        }
    }
    ReferenceConsumer(const ReferenceConsumer &) = delete;
    ReferenceConsumer &operator=(const ReferenceConsumer &) = delete;
    const ElementBatchView<4> &tet_batch() const { return tet_; }
    const ShapeTableView<T, 4> &tet_shapes() const { return tet_shape_; }
    void evaluate(const NodeStateView<T> &node, const ResultSink<T> &sink) {
        node.validate();
        if (node.model_id != model_id_ || node.displacement.size() != nodes_)
            throw std::invalid_argument("consumer node identity/size mismatch");
        const auto u = node.displacement.resolve();
        const auto conn = tet_.nodes.resolve(); const auto es = tet_.global_elements.resolve();
        const auto ips = tet_.integration_points.resolve(); const auto active = tet_.active.resolve();
        TetViews<T> t{conn, tet_shape_.gradients.resolve(), tet_shape_.weights.resolve(),
                      tet_material_.lambda.resolve(), tet_material_.mu.resolve()};
        std::optional<HexViews<T>> h;
        View<const Index> hes{}; View<const std::uint8_t> hactive{};
        if (hex_) {
            hes = hex_->global_elements.resolve(); hactive = hex_->active.resolve();
            h.emplace(HexViews<T>{hex_->nodes.resolve(), hex_shape_->gradients.resolve(),
                hex_shape_->weights.resolve(), hex_material_->lambda.resolve(), hex_material_->mu.resolve(),
                hex_->integration_points.resolve()});
        }
        if (node.element_active.resolve().data != active.data)
            throw std::invalid_argument("consumer bound to another state activity field");
        auto out = sink.resolve();
        if (out.force.count != nodes_ || out.stress.count != points_ || out.strain.count != points_)
            throw std::invalid_argument("consumer output dimensions mismatch");
        // Reject aliasing of displacement into force; inputs and outputs require disjoint storage.
        if ((out.force.count && (out.force.data == u.data || out.force.data == node.velocity.resolve().data ||
             out.force.data == node.acceleration.resolve().data)) ||
            (out.stress.count && out.stress.data == out.strain.data))
            throw std::invalid_argument("consumer input/output alias");
        auto local = tet_local_.write(); auto hl = hex_local_.write();
        for (std::size_t e = 0; e < conn.count; ++e) {
            if (es(e) == invalid_element) continue;
            if (active(es(e))) tet_force(e, t, u, local, out.stress, out.strain, ips(e));
            else {
                for (unsigned c = 0; c < 12; ++c) local(e, c) = 0;
                for (unsigned c = 0; c < 6; ++c) out.stress(ips(e), c) = out.strain(ips(e), c) = 0;
            }
        }
        if (h) for (std::size_t e = 0; e < hes.count; ++e)
            hex_force(e, *h, u, hl, out.stress, out.strain, hactive(hes(e)) != 0);
        for (std::size_t n = 0; n < nodes_; ++n)
            for (unsigned c = 0; c < 3; ++c) out.force(n, c) = 0;
        // Ascending element/local-node traversal matches each node's deterministic adjacency order.
        for (std::size_t e = 0; e < conn.count; ++e) if (es(e) != invalid_element)
            for (unsigned a = 0; a < 4; ++a)
                for (unsigned c = 0; c < 3; ++c) out.force(conn(e, a), c) += local(e, 3 * a + c);
        if (h) for (std::size_t e = 0; e < hes.count; ++e)
            for (unsigned a = 0; a < 8; ++a)
                for (unsigned c = 0; c < 3; ++c) out.force(h->nodes(e, a), c) += hl(e, 3 * a + c);
    }
};
} // namespace sdb::consumer

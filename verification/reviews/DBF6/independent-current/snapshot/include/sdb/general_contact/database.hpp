#pragma once
#include "../model.hpp"
#include <set>
#include <optional>

// Independent extension: does not change MeshDB, StateDB or the legacy contact/checkpoint ABI.
namespace sdb::general_contact {
template <class T> using Vec = std::array<T, 3>;
template <class T> T dot(const Vec<T> &a, const Vec<T> &b) {
    T r{};
    for (unsigned i = 0; i < 3; ++i)
        r += a[i] * b[i];
    return r;
}
template <class T> void finite(T x) {
    if (!std::isfinite(x))
        throw std::invalid_argument("nonfinite general contact value");
}
enum class FeatureKind : std::uint32_t { Node, Edge, Face, RigidSurface };
struct Feature {
    FeatureKind kind = FeatureKind::Node;
    Id entity = 0; // mesh node ID, element ID (edge/face), or rigid surface ID
    std::uint32_t local = 0;
    std::int32_t side = 1;
    auto operator<=>(const Feature &) const = default;
    template <class F> void visit(F f) { f(kind, entity, local, side); }
};
struct Key {
    Id interaction = 0;
    Feature a, b;            // oriented: never swap without transforming normal, history and stencil
    std::uint32_t point = 0; // persistent integration point, not transient candidate ordinal
    auto operator<=>(const Key &) const = default;
    template <class F> void visit(F f) { f(interaction, a, b, point); }
};
inline void validate(const Key &k) {
    if (!k.interaction || k.a == k.b)
        throw std::invalid_argument("invalid contact identity");
    for (const auto &f : {k.a, k.b})
        if (f.kind > FeatureKind::RigidSurface || (f.side != 1 && f.side != -1))
            throw std::invalid_argument("invalid contact feature");
}
struct Facet {
    FaceId identity;
    std::array<Index, 4> nodes{};
    std::uint32_t count = 0;
};
// Solid exterior compiler. Unsupported topologies fail instead of disappearing from ALL EXTERIOR.
template <class T> std::vector<Facet> exterior(const MeshDB<T> &mesh) {
    constexpr unsigned tet[4][3] = {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}};
    constexpr unsigned hex[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                                    {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    std::map<std::vector<Index>, std::vector<Facet>> incidence;
    for (Index e = 0; e < mesh.elements(); ++e) {
        auto top = mesh.element(e).topology;
        if (top != Topology::Tet4 && top != Topology::Hex8)
            throw std::invalid_argument("solid exterior supports Tet4/Hex8 only");
        auto conn = mesh.connectivity(e);
        for (unsigned f = 0; f < face_count(top); ++f) {
            Facet face{
                {mesh.element(e).id, static_cast<std::uint16_t>(f), 1}, {}, top == Topology::Tet4 ? 3u : 4u};
            for (unsigned j = 0; j < face.count; ++j)
                face.nodes[j] = conn[top == Topology::Tet4 ? tet[f][j] : hex[f][j]];
            std::vector<Index> key(face.nodes.begin(), face.nodes.begin() + face.count);
            std::sort(key.begin(), key.end());
            if (std::adjacent_find(key.begin(), key.end()) != key.end())
                throw std::invalid_argument("repeated facet node");
            incidence[key].push_back(face);
        }
    }
    std::vector<Facet> result;
    for (const auto &[key, faces] : incidence) {
        (void)key;
        if (faces.size() > 2)
            throw std::invalid_argument("nonmanifold solid facet");
        if (faces.size() == 1)
            result.push_back(faces[0]);
        else {
            const auto &a = faces[0];
            const auto &b = faces[1];
            bool opposite = false;
            for (unsigned shift = 0; shift < a.count; ++shift) {
                bool match = true;
                for (unsigned j = 0; j < a.count; ++j)
                    match = match && a.nodes[j] == b.nodes[(shift + a.count - j) % a.count];
                opposite = opposite || match;
            }
            if (!opposite)
                throw std::invalid_argument("inconsistent shared facet orientation");
        }
    }
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.identity < b.identity; });
    return result;
}
template <class T> struct Property {
    Id id = 1;
    T normal_penalty = 1, tangential_penalty = 0, friction = 0; // traction / length; dimensionless mu
    template <class F> void visit(F f) { f(id, normal_penalty, tangential_penalty, friction); }
    void validate() const {
        finite(normal_penalty);
        finite(tangential_penalty);
        finite(friction);
        if (!id || normal_penalty <= 0 || tangential_penalty < 0 || friction < 0 ||
            (friction > 0 && tangential_penalty == 0))
            throw std::invalid_argument("invalid penalty property");
    }
};
struct Binding {
    std::uint64_t mesh = 0, configuration = 0, mechanical_state = 0, step = 0;
    double time = 0;
    auto operator<=>(const Binding &) const = default;
    template <class F> void visit(F f) { f(mesh, configuration, mechanical_state, step, time); }
    void validate() const {
        finite(time);
        if (time < 0)
            throw std::invalid_argument("negative contact time");
    }
};
enum class Status : std::uint32_t { Open, Stick, Slip };
template <class T> struct History {
    Vec<T> elastic_slip{}, normal{};
    T accumulated_slip = 0, friction_work = 0;
    Status status = Status::Open;
    template <class F> void visit(F f) { f(elastic_slip, normal, accumulated_slip, friction_work, status); }
    void validate() const {
        for (T x : elastic_slip)
            finite(x);
        for (T x : normal)
            finite(x);
        finite(accumulated_slip);
        finite(friction_work);
        if (accumulated_slip < 0 || friction_work < 0 || status > Status::Slip)
            throw std::invalid_argument("invalid contact history");
        const T tol = T(256) * std::numeric_limits<T>::epsilon();
        if (status != Status::Open &&
            (std::abs(dot(normal, normal) - T(1)) > tol ||
             std::abs(dot(elastic_slip, normal)) > tol * (1 + std::sqrt(dot(elastic_slip, elastic_slip)))))
            throw std::invalid_argument("invalid contact history frame");
        if (status == Status::Open && dot(elastic_slip, elastic_slip) != 0)
            throw std::invalid_argument("open contact retains elastic traction");
    }
};
template <class T> struct Record {
    Key key;
    History<T> history;
    template <class F> void visit(F f) { f(key, history); }
};
// Hot history fields are SoA. All view acquisition and topology changes occur at phase barriers.
template <class T> class HistoryTable {
    std::vector<Key> keys_;
    Field<T, 3> elastic_, normal_;
    Field<T> slip_, work_;
    std::vector<Status> status_;

  public:
    HistoryTable() = default;
    explicit HistoryTable(std::vector<Record<T>> records)
        : elastic_(records.size()), normal_(records.size()), slip_(records.size()), work_(records.size()) {
        std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) { return a.key < b.key; });
        auto e = elastic_.write(), n = normal_.write();
        auto s = slip_.write(), w = work_.write();
        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto &r = records[i];
            validate(r.key);
            r.history.validate();
            if (i && records[i - 1].key == r.key)
                throw std::invalid_argument("duplicate contact history key");
            keys_.push_back(r.key);
            status_.push_back(r.history.status);
            for (unsigned c = 0; c < 3; ++c) {
                e(i, c) = r.history.elastic_slip[c];
                n(i, c) = r.history.normal[c];
            }
            s(i) = r.history.accumulated_slip;
            w(i) = r.history.friction_work;
        }
    }
    std::size_t size() const { return keys_.size(); }
    const std::vector<Key> &keys() const { return keys_; }
    History<T> at(std::size_t i) const {
        if (i >= size())
            throw std::out_of_range("contact history row");
        History<T> h;
        for (unsigned c = 0; c < 3; ++c) {
            h.elastic_slip[c] = elastic_.read()(i, c);
            h.normal[c] = normal_.read()(i, c);
        }
        h.accumulated_slip = slip_.read()(i);
        h.friction_work = work_.read()(i);
        h.status = status_[i];
        return h;
    }
    History<T> find(const Key &key) const {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key);
        return it == keys_.end() || *it != key ? History<T>{}
                                               : at(static_cast<std::size_t>(it - keys_.begin()));
    }
    std::vector<Record<T>> records() const {
        std::vector<Record<T>> r;
        r.reserve(size());
        for (std::size_t i = 0; i < size(); ++i)
            r.push_back({keys_[i], at(i)});
        return r;
    }
};
template <class T> class Database {
    Binding binding_;
    HistoryTable<T> committed_;
    std::optional<HistoryTable<T>> trial_;
    bool active_ = false;
    std::uint64_t epoch_ = 0;
    T friction_dissipation_ = 0, trial_dissipation_ = 0;

  public:
    explicit Database(Binding b) : binding_(b) { b.validate(); }
    const Binding &binding() const { return binding_; }
    const HistoryTable<T> &committed() const { return committed_; }
    std::uint64_t epoch() const { return epoch_; }
    T friction_dissipation() const { return friction_dissipation_; }
    // Publish a completely validated replacement without copying buffers; preserve local epoch monotonicity.
    void adopt(Database &&next) {
        if (active_ || next.active_ || binding_.mesh != next.binding_.mesh ||
            binding_.configuration != next.binding_.configuration)
            throw std::logic_error("incompatible contact database replacement");
        committed_ = std::move(next.committed_);
        binding_ = next.binding_;
        friction_dissipation_ = next.friction_dissipation_;
        trial_.reset();
        ++epoch_;
    }
    bool in_trial() const { return active_; }
    void invalidate_trial() {
        if (!active_)
            throw std::logic_error("contact invalidate outside trial");
        trial_.reset();
        ++epoch_;
    }
    void begin_trial() {
        if (active_)
            throw std::logic_error("nested contact trial");
        active_ = true;
        trial_.reset();
        ++epoch_;
    }
    // Every nonlinear iteration reads committed(), never a previous Newton trial.
    void stage(std::vector<Record<T>> records) {
        if (!active_)
            throw std::logic_error("contact stage outside trial");
        invalidate_trial();
        std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) { return a.key < b.key; });
        T dissipation = friction_dissipation_;
        for (const auto &r : records) {
            auto old = committed_.find(r.key);
            if (r.history.friction_work < old.friction_work ||
                r.history.accumulated_slip < old.accumulated_slip)
                throw std::invalid_argument("contact irreversible history decreased");
            dissipation += r.history.friction_work - old.friction_work;
        }
        finite(dissipation);
        HistoryTable<T> next(std::move(records));
        trial_ = std::move(next);
        trial_dissipation_ = dissipation;
        ++epoch_;
    }
    const HistoryTable<T> &trial() const {
        if (!active_ || !trial_)
            throw std::logic_error("contact trial not evaluated");
        return *trial_;
    }
    void validate_commit(const Binding &next) const {
        next.validate();
        if (!active_ || !trial_ || next.mesh != binding_.mesh ||
            next.configuration != binding_.configuration || next.step <= binding_.step ||
            next.time < binding_.time)
            throw std::logic_error("invalid contact commit binding");
    }
    void commit(const Binding &next) {
        validate_commit(next);
        committed_ = std::move(*trial_);
        friction_dissipation_ = trial_dissipation_;
        binding_ = next;
        trial_.reset();
        active_ = false;
        ++epoch_;
    }
    void rollback() {
        if (!active_)
            throw std::logic_error("contact rollback outside trial");
        trial_.reset();
        active_ = false;
        ++epoch_;
    }
    void restore(const Binding &expected, std::vector<Record<T>> records, T total_dissipation) {
        if (active_ || expected != binding_)
            throw std::logic_error("contact restart binding mismatch");
        finite(total_dissipation);
        T retained_work = 0;
        for (const auto &r : records)
            retained_work += r.history.friction_work;
        finite(retained_work);
        if (total_dissipation < 0 ||
            retained_work > total_dissipation + T(256) * std::numeric_limits<T>::epsilon() *
                                                    std::max(T(1), total_dissipation))
            throw std::invalid_argument("inconsistent contact dissipation ledger");
        HistoryTable<T> next(std::move(records));
        committed_ = std::move(next);
        friction_dissipation_ = total_dissipation;
        ++epoch_;
    }
};
} // namespace sdb::general_contact

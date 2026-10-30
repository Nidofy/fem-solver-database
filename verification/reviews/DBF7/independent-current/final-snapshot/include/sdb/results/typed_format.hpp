#pragma once
// Version-three extensions reuse the indexed file's commit and recovery machinery.
namespace sdb::lifecycle::typed_detail {
inline constexpr std::uint64_t magic = 0x33534552424453ull;
struct ModelWire {
    ResultModel model;
    template<class F> void visit(F f) { f(model, model.samples, model.bases); }
};
struct ModelView {
    const ResultModel &model;
    template<class F> void visit(F f) const { f(model, model.samples, model.bases); }
};
inline native::Bytes pack_model(const ResultModel &m, std::uint64_t limit, bool typed) {
    return typed ? native::pack(ModelView{m}, limit) : native::pack(m, limit);
}
inline ResultModel unpack_model(const native::Bytes &bytes, std::uint64_t limit, bool typed) {
    return typed ? native::unpack<ModelWire>(bytes, limit).model : native::unpack<ResultModel>(bytes, limit);
}
inline TypedFieldPayload metadata(const TypedFieldPayload &p) {
    TypedFieldPayload out;
    out.schema = p.schema; out.schema_id = p.schema_id; out.schema_version = p.schema_version; out.basis_id = p.basis_id;
    out.values = empty_typed_values(p.schema.scalar);
    return out;
}
inline void append_row(TypedFieldPayload &to, const TypedFieldPayload &from, std::size_t row) {
    auto width = from.schema.component_count();
    to.addresses.push_back(from.addresses.at(row));
    std::visit([&](const auto &src) {
        auto &dst = std::get<std::decay_t<decltype(src)>>(to.values);
        dst.insert(dst.end(), src.begin() + row * width, src.begin() + (row + 1) * width);
    }, from.values);
    if (!from.validity.empty())
        to.validity.insert(to.validity.end(), from.validity.begin() + row * width,
            from.validity.begin() + (row + 1) * width);
}
inline std::size_t scalar_bytes(ScalarType type) {
    return std::visit([](const auto &v) { return sizeof(typename std::decay_t<decltype(v)>::value_type); },
        empty_typed_values(type));
}
inline void validate_model(const ResultModel &m, std::uint64_t budget = native::default_limit) {
    if ((m.precision != 4 && m.precision != 8) || m.coordinates.size() != m.nodes.size() ||
        m.connectivity.size() != m.elements.size() || m.descriptors.size() != m.elements.size())
        throw std::runtime_error("result mesh shape/precision invalid");
    index_of(m.nodes.size());index_of(m.elements.size());
    auto sources = [&](const std::vector<SourceEntry> &entries) {
        if (product(entries.size(), sizeof(const SourceKey *)) > budget)
            throw std::length_error("result source validation budget");
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].index != i || entries[i].id != i + 1 || !entries[i].key.label)
                throw std::runtime_error("result source ID/index/label invalid");
        std::vector<const SourceKey *> keys;
        keys.reserve(entries.size());
        for (const auto &entry : entries) keys.push_back(&entry.key);
        std::sort(keys.begin(), keys.end(), [](auto a, auto b) { return *a < *b; });
        for (std::size_t i = 1; i < keys.size(); ++i)
            if (*keys[i-1] == *keys[i]) throw std::runtime_error("duplicate result scoped source identity");
    };
    sources(m.nodes);sources(m.elements);
    for (const auto &xyz : m.coordinates)
        for (double x : xyz) if (!std::isfinite(x)) throw std::runtime_error("nonfinite result coordinate");
    for (const auto &nodes : m.connectivity)
        for (auto n : nodes) if (n >= m.nodes.size()) throw std::runtime_error("result connectivity out of bounds");
    std::set<std::uint64_t> ids;
    for (const auto &b : m.bases) {
        if (!b.id || !ids.insert(b.id).second) throw std::runtime_error("invalid/duplicate result basis");
        for (double x : b.rotation) if (!std::isfinite(x)) throw std::runtime_error("nonfinite basis");
        for (unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) {
            double dot=0; for(unsigned k=0;k<3;++k) dot+=b.rotation[3*i+k]*b.rotation[3*j+k];
            if (std::abs(dot-(i==j ? 1.0 : 0.0))>1e-10) throw std::runtime_error("nonorthonormal result basis");
        }
        const auto &r=b.rotation;
        double det=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
        if (std::abs(det-1)>1e-10) throw std::runtime_error("improper result basis");
    }
    if (!m.samples.empty()) {
        if (m.samples.size() != m.elements.size()) throw std::runtime_error("typed model sample shape");
        for (std::size_t e = 0; e < m.samples.size(); ++e) {
            std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> seen;
            if (m.samples[e].empty()) throw std::runtime_error("empty element sample layout");
            for (const auto &p : m.samples[e])
                if (!p.integration_point || p.integration_point > m.descriptors.at(e).points ||
                    bool(p.layer) != bool(p.section_point) ||
                    !seen.emplace(p.integration_point, p.layer, p.section_point).second)
                    throw std::runtime_error("typed model sample identity");
        }
    }
}
} // namespace sdb::lifecycle::typed_detail

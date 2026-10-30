#pragma once
#include "mechanics.hpp"
#include "io.hpp"

namespace sdb::general_contact {
template <class T> struct ResultRow {
    Key key;
    T gap = 0, area = 0;
    Vec<T> normal{}, traction{};
    History<T> history;
    template <class F> void visit(F f) { f(key, gap, area, normal, traction, history); }
};
template <class T> struct ResultSnapshot {
    std::uint32_t scalar_bytes = sizeof(T), schema = 1;
    Binding binding;
    std::uint64_t geometry_epoch = 0;
    T friction_dissipation = 0;
    std::vector<ResultRow<T>> rows;
    template <class F> void visit(F f) {
        f(scalar_bytes, schema, binding, geometry_epoch, friction_dissipation, rows);
    }
    void validate() const {
        binding.validate();
        finite(friction_dissipation);
        if (friction_dissipation < 0)
            throw std::invalid_argument("negative contact dissipation result");
        if (scalar_bytes != sizeof(T) || schema != 1)
            throw std::invalid_argument("contact result schema/precision");
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto &r = rows[i];
            general_contact::validate(r.key);
            finite(r.gap);
            finite(r.area);
            r.history.validate();
            for (T x : r.normal)
                finite(x);
            for (T x : r.traction)
                finite(x);
            if (r.area <= 0 ||
                std::abs(dot(r.normal, r.normal) - 1) > T(256) * std::numeric_limits<T>::epsilon() ||
                (i && !(rows[i - 1].key < r.key)))
                throw std::invalid_argument("invalid contact result row");
        }
    }
};
template <class T>
ResultSnapshot<T> accepted_result(const Database<T> &db, const Frame<T> &frame,
                                  std::vector<Witness<T>> witnesses) {
    if (db.in_trial() || db.epoch() != frame.state_epoch + 1 || frame.origin.mesh != db.binding().mesh ||
        frame.origin.configuration != db.binding().configuration || db.binding().step <= frame.origin.step ||
        frame.keys != db.committed().keys() || frame.responses.size() != frame.keys.size() ||
        witnesses.size() != frame.keys.size())
        throw std::logic_error("result is not the most recently committed contact frame");
    std::sort(witnesses.begin(), witnesses.end(), [](const auto &a, const auto &b) { return a.key < b.key; });
    ResultSnapshot<T> result;
    result.binding = db.binding();
    result.geometry_epoch = frame.geometry_epoch;
    result.friction_dissipation = db.friction_dissipation();
    for (std::size_t i = 0; i < witnesses.size(); ++i) {
        const auto &w = witnesses[i];
        const auto &r = frame.responses[i];
        if (w.key != frame.keys[i] || w.geometry_epoch != frame.geometry_epoch)
            throw std::invalid_argument("contact result witness mismatch");
        result.rows.push_back({w.key, w.gap, w.area, w.normal, r.traction, db.committed().at(i)});
    }
    result.validate();
    return result;
}
inline constexpr std::uint64_t result_magic = 0x3152434342445355ull;
template <class T>
void save_result(const std::filesystem::path &path, const ResultSnapshot<T> &result,
                 std::uint64_t budget = lifecycle::native::default_limit) {
    result.validate();
    auto bytes = lifecycle::native::pack(result, budget);
    lifecycle::native::atomic_stream(path, [&](std::ostream &out) {
        lifecycle::native::prefix(out, result_magic);
        lifecycle::native::record(out, 1, bytes);
    });
}
template <class T>
ResultSnapshot<T> load_result(const std::filesystem::path &path, const Binding &expected,
                              std::uint64_t budget = lifecycle::native::default_limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("contact result open failed");
    lifecycle::native::prefix(in, result_magic);
    auto bytes = lifecycle::native::record(in, 1, budget);
    lifecycle::native::end(in);
    auto result = lifecycle::native::unpack<ResultSnapshot<T>>(bytes, budget);
    result.validate();
    if (result.binding != expected)
        throw std::runtime_error("contact result binding mismatch");
    return result;
}
} // namespace sdb::general_contact

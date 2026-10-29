#include <sdb/storage.hpp>
#include <iostream>
using namespace sdb;
static unsigned checks = 0;
void check(bool condition) {
    ++checks;
    if (!condition) throw std::runtime_error("storage check failed");
}
template <class F> void rejects(F f) {
    ++checks;
    try { f(); } catch (const std::exception &) { return; }
    throw std::runtime_error("expected storage rejection");
}
template <class T> void floating_roundtrip() {
    RaggedData<T, 2> r;
    r.offsets = {0, 1, 1, 2}; r.values = {{{T(.125), T(-7.5)}}, {{T(0), T(1.25)}}};
    auto rf = RaggedField<T, 2, AoSoA<4>>::from_data(r);
    check(rf.encoded_bytes() == rf.encode().size());
    check(decltype(rf)::decode(rf.encode()).data().values == r.values);
    rejects([&] { rf.encode(rf.encoded_bytes() - 1); });
    auto forged = rf.encode(); forged[5] = 1; // uint32 wire enum = 256 + valid scalar.
    rejects([&] { decltype(rf)::decode(forged); });
    SparseData<T, 2> s;
    s.domain = 4; s.indices = {0, 3}; s.values = r.values; s.validity = {0, 1};
    auto sf = SparseField<T, 2, AoSoA<4>>::from_data(s);
    check(sf.encoded_bytes() == sf.encode().size());
    check(decltype(sf)::decode(sf.encode()).data().values == s.values);
    rejects([&] { sf.encode(sf.encoded_bytes() - 1); });
    forged = sf.encode(); forged[5] = 1;
    rejects([&] { decltype(sf)::decode(forged); });
}
void scaling() {
    for (std::uint32_t n : {0u, 16u, 1024u, 16384u}) {
        RaggedData<double, 3> r;
        r.offsets = std::vector<std::uint64_t>(std::size_t(n) + 1); r.values.resize(n);
        SparseData<double, 3> s;
        s.domain = n; s.indices.resize(n); s.validity.assign(n, 1); s.values.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            r.offsets[i] = i; s.indices[i] = i;
            r.values[i] = s.values[i] = {double(i), 0, -double(i)};
        }
        r.offsets[n] = n;
        auto before = host_memory.live.load();
        {
            auto rf = RaggedField<double, 3, AoSoA<4>>::from_data(r);
            auto sf = SparseField<double, 3, AoSoA<4>>::from_data(s);
            auto delta = host_memory.live.load() - before;
            check(rf.encoded_bytes() == rf.encode().size());
            check(sf.encoded_bytes() == sf.encode().size());
            check(decltype(rf)::decode(rf.encode()).data().values == r.values);
            check(decltype(sf)::decode(sf.encode()).data().values == s.values);
            std::cout << "storage n=" << n << " field_host_live_delta=" << delta
                      << " ragged_encoded_bytes=" << rf.encoded_bytes()
                      << " sparse_encoded_bytes=" << sf.encoded_bytes() << '\n';
        }
        check(host_memory.live.load() == before);
    }
}
int main() {
    try {
        floating_roundtrip<float>(); floating_roundtrip<double>(); scaling();
        using R = RaggedField<std::int64_t, 2, AoSoA<4>>;
        RaggedData<std::int64_t, 2> d;
        constexpr std::int64_t exact = (std::int64_t(1) << 53) + 1;
        d.offsets = {0, 0, 2, 2}; d.values = {{{exact, -exact}}, {{0, 7}}};
        auto r = R::from_data(d);
        check(r.segments() == 3 && r.segment(0).size() == 0 && r.segment(2).size() == 0);
        check(r.segment(1).at(0, 0) == exact && r.segment(1).at(0, 1) == -exact);
        rejects([&] { r.segment(0).at(0); });
        rejects([&] { r.segment(3); });
        rejects([&] { r.segment(1).at(0, 2); });
        auto copy = R::decode(r.encode());
        check(r.encoded_bytes() == r.encode().size());
        check(copy.data().values == d.values && copy.data().offsets == d.offsets);
        auto view = r.checked_read(); auto segment = r.segment(1);
        auto moved = std::move(r);
        rejects([&] { view.at(0); }); rejects([&] { segment.at(0); });
        check(moved.checked_read().at(0) == exact);
        auto old = copy.checked_read(); copy = std::move(moved);
        rejects([&] { old.at(0); });
        check(copy.checked_read().at(0) == exact);
        auto destroyed = [&] { auto local = R::from_data(d); return local.checked_read(); }();
        rejects([&] { destroyed.at(0); });
        for (auto offsets : std::vector<std::vector<std::uint64_t>>{{}, {1, 2}, {0, 2, 1, 2}, {0, 3},
                         {0, std::numeric_limits<std::uint64_t>::max(), 2}}) {
            auto bad = d; bad.offsets = offsets; rejects([&] { R::from_data(bad); });
        }
        auto wrong = d; wrong.schema = 2; rejects([&] { R::from_data(wrong); });
        wrong = d; wrong.scalar = ScalarType::Float64; rejects([&] { R::from_data(wrong); });
        wrong = d; wrong.components = 1; rejects([&] { R::from_data(wrong); });
        check(R::decode(R{}.encode()).segments() == 0);
        rejects([&] { R::decode(copy.encode(), 1); });
        auto truncated = copy.encode(); truncated.pop_back(); rejects([&] { R::decode(truncated); });
        wrong = d; wrong.offsets = {0, 7};
        rejects([&] { R::decode(lifecycle::native::pack(wrong)); });

        using S = SparseField<std::int64_t, 1, AoSoA<4>>;
        SparseData<std::int64_t> s;
        s.domain = 10; s.indices = {1, 4, 8}; s.values = {{{0}}, {{0}}, {{exact}}}; s.validity = {1, 0, 1};
        auto sparse = S::from_data(s);
        check(sparse.present(1) && !sparse.present(4) && !sparse.present(2));
        check(sparse.checked_read().at(0) == 0 && sparse.validity()[0] == 1);
        auto scopy = S::decode(sparse.encode());
        check(sparse.encoded_bytes() == sparse.encode().size());
        check(scopy.checked_read().at(2) == exact && scopy.data().validity == s.validity);
        scopy.checked_write().at(2) = -exact;
        check(S::decode(scopy.encode()).checked_read().at(2) == -exact);
        rejects([&] { sparse.present(10); });
        for (auto indices : std::vector<std::vector<std::uint32_t>>{{1, 1, 8}, {4, 1, 8}, {1, 4, 10}, {1}}) {
            auto bad = s; bad.indices = indices; rejects([&] { S::from_data(bad); });
        }
        auto bad = s; bad.validity = {1, 2, 1}; rejects([&] { S::from_data(bad); });
        bad = s; bad.validity.pop_back(); rejects([&] { S::from_data(bad); });
        bad = s; bad.domain = std::uint64_t(std::numeric_limits<std::uint32_t>::max()) + 1;
        rejects([&] { S::from_data(bad); });
        bad = s; bad.domain = 0; rejects([&] { S::from_data(bad); });
        bad = s; bad.components = 2; rejects([&] { S::from_data(bad); });
        check(S::decode(S{}.encode()).domain_count() == 0);
        SparseData<std::int64_t> maxdomain; maxdomain.domain = std::numeric_limits<std::uint32_t>::max();
        check(S::from_data(maxdomain).domain_count() == maxdomain.domain);
        auto sv = sparse.checked_read(); auto sm = std::move(sparse);
        rejects([&] { sv.at(0); }); check(sm.checked_read().at(2) == exact);
        auto dead = [&] { auto local = S::from_data(s); return local.checked_read(); }();
        rejects([&] { dead.at(0); });
        SparseData<double> nonfinite; nonfinite.domain = 1; nonfinite.indices = {0};
        nonfinite.validity = {0}; nonfinite.values = {{{std::numeric_limits<double>::quiet_NaN()}}};
        rejects([&] { SparseField<double>::from_data(nonfinite); });
        std::cout << "storage checks=" << checks << '\n';
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

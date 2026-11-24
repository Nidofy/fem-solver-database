#include <sdb/state_session.hpp>
#include <chrono>
#include <iostream>

using namespace sdb;
using Scalar = Field<double>;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class E, class F> void rejects(F fn) {
    try { fn(); } catch (const E &) { return; }
    throw std::runtime_error("expected validation failure");
}
int main(int argc,char **argv) { try {
    if (argc!=2) throw std::invalid_argument("checkpoint path required");
    [[maybe_unused]] void (RegisteredState::*legacy_commit)()=&RegisteredState::commit;
    [[maybe_unused]] void (StateSession::*legacy_begin)(TrialPolicy)=&StateSession::begin_trial;
    DofSpaceData data; data.model=7;
    DofSpace space(std::move(data));
    FieldDescriptor d; d.name = "x"; d.missing = MissingPolicy::ValidityMask;
    RegisteredState state;
    state.bind_context(space.data().model,space.identity());
    state.add<Scalar>(d, 2, "test", 1, {0}, 0);
    state.freeze();
    constexpr auto fast = CommitValidation::FastShapeOnly;
    for (auto mode : {CommitValidation::FullFiniteScan, fast}) {
        state.begin_trial_cow(mode);
        state.get<Scalar>("x").write()(0) = 9;
        state.rollback();
        check(state.get<Scalar>("x").read()(0) == 0, "COW rollback value");
        state.begin_trial_cow(mode);
        state.get<Scalar>("x").write()(1) = 8;
        state.commit(mode);
        check(state.get<Scalar>("x").read()(1) == 8, "COW commit value");
    }
    for (double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        state.begin_trial_cow();
        state.get<Scalar>("x").write()(0) = value;
        rejects<std::invalid_argument>([&] { state.commit(); });
        check(state.in_trial(), "failed commit must allow rollback");
        state.rollback();
    }
    for (int fault = 0; fault < 3; ++fault) {
        state.begin_trial_cow();
        state.find("x").validity_field().write()(0) = fault == 0 ? 2 : 0;
        state.get<Scalar>("x").write()(0) = fault == 1 ? 3 : -0.0;
        rejects<std::invalid_argument>([&] { state.commit(CommitValidation::FullFiniteScan); });
        state.rollback();
    }
    state.begin_trial_cow();
    rejects<std::invalid_argument>([&] { state.commit(static_cast<CommitValidation>(9)); });
    state.get<Scalar>("x").resize(3);
    rejects<std::logic_error>([&] { state.commit(fast); });
    state.rollback();
    StateSession persisted(state);
    persisted.save(argv[1],space);
    persisted.begin_trial(TrialPolicy::CopyOnWrite,fast);
    { auto w=persisted.write(); w.field<Scalar>("x").at(1)=std::numeric_limits<double>::infinity(); }
    persisted.commit(fast);
    rejects<std::invalid_argument>([&]{ persisted.save(argv[1],space); });
    persisted.restore(argv[1],space);
    check(persisted.read().field<Scalar>("x").at(1)==8,"full checkpoint audit preserved previous file");
    state.begin_trial_cow();
    state.get<Scalar>("x").write()(0) = std::numeric_limits<double>::quiet_NaN();
    state.commit(fast);
    rejects<std::invalid_argument>([&] { state.begin_trial_cow(); });
    rejects<std::invalid_argument>([&] { checkpoint_detail::write_registered(state, [](auto,auto &) {}); });

    constexpr std::size_t rows = 1000000;
    StateLayout addresses;
    addresses.domain = rows; addresses.addresses.resize(rows);
    for (std::size_t i = 0; i < rows; ++i) addresses.addresses[i] = {i+1, 1};
    RegisteredState large;
    large.add<Scalar>(d,rows,"bench",1,{0},0,std::move(addresses)); large.freeze();
    StateSession session(large);
    auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 1000; ++i) {
        session.begin_trial(TrialPolicy::CopyOnWrite,fast);
        session.commit(fast);
    }
    double ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    check(session.memory().detached_value_bytes == 0, "untouched COW payload detached");
    for (auto mode : {CommitValidation::FullFiniteScan, fast}) {
        session.begin_trial(TrialPolicy::CopyOnWrite,mode);
        { auto w=session.write(); w.field<Scalar>("x").at(rows-1)=13; }
        session.rollback();
        check(session.read().field<Scalar>("x").at(rows-1)==0, "leased rollback");
        session.begin_trial(TrialPolicy::CopyOnWrite,mode);
        { auto w=session.write(); w.field<Scalar>("x").at(0)=14; }
        session.commit(mode);
        check(session.read().field<Scalar>("x").at(0)==14, "leased commit");
    }
    std::cout << "addressed_rows=" << rows << " cow_steps=1000 begin_plus_fast_commit_ms=" << ms << '\n';
    return 0;
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }

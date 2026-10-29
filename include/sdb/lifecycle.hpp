#pragma once
#include "results/results.hpp"
#include "io/restart.hpp"

namespace sdb::lifecycle {
struct StepPlan {
    std::uint64_t increments = 0;
    double dt = 0, start = 0;
    template <class F> void visit(F f) { f(increments, dt, start); }
};
template <class T> class CpuRun {
    static const CompiledModel<T> &validated(const CompiledModel<T> &m) {
        m.require_cpu();
        return m;
    }
    const CompiledModel<T> &model_;
    StateDB<T> state_;
    CpuExplicit<T> cpu_;
    std::vector<StepPlan> plans_;
    std::uint64_t config_ = 0, step_ = 0, increment_ = 0;
    bool initialized_ = false;
    void mask(std::size_t step) {
        std::vector<Constraint> cs;
        for (auto &a : model_.analysis.steps.at(step).boundaries)
            cs.push_back({a.node, a.component, a.value});
        cpu_.set_constraints(cs, state_);
    }
    void loads() {
        LoadEvaluator::apply(model_.analysis, step_, double(increment_) * plans_[step_].dt,
                             state_.data().time, state_);
    }
    // Prepare every throwing operation on the candidate. set_constraints checks
    // all inputs before its nonallocating mask/velocity/acceleration writes.
    // After that point state/cursor publication is a no-throw commit.
    void publish_restored(StateDB<T> &candidate, std::uint64_t step, std::uint64_t increment) {
        std::vector<Constraint> constraints;
        for (const auto &a : model_.analysis.steps.at(step).boundaries)
            constraints.push_back({a.node, a.component, a.value});
        LoadEvaluator::apply(model_.analysis, step, double(increment) * plans_.at(step).dt,
                             candidate.data().time, candidate);
        cpu_.set_constraints(constraints, candidate);
        static_assert(std::is_nothrow_move_assignable_v<StateData<T>>);
        state_.data() = std::move(candidate.data());
        step_ = step;
        increment_ = increment;
        initialized_ = true;
    }
    void next_step() {
        // Rephase half-step velocity around the same physical time when dt/load/mask changes.
        cpu_.forces(state_);
        auto &s = state_.data();
        auto oldfixed = cpu_.plan().fixed.read();
        auto v = s.velocity.write();
        for (Index n = 0; n < model_.mesh.nodes(); ++n)
            for (unsigned c = 0; c < 3; ++c)
                if (!oldfixed(n, c))
                    v(n, c) += static_cast<T>(.5 * s.dt) *
                               (s.external_force.read()(n, c) + s.contact_force.read()(n, c) -
                                s.internal_force.read()(n, c)) *
                               s.inverse_mass.read()(n);
        ++step_;
        increment_ = 0;
        mask(step_);
        loads();
        cpu_.forces(state_);
        s.dt = plans_[step_].dt;
        auto fixed = cpu_.plan().fixed.read();
        for (Index n = 0; n < model_.mesh.nodes(); ++n)
            for (unsigned c = 0; c < 3; ++c)
                if (!fixed(n, c))
                    v(n, c) -= static_cast<T>(.5 * s.dt) *
                               (s.external_force.read()(n, c) + s.contact_force.read()(n, c) -
                                s.internal_force.read()(n, c)) *
                               s.inverse_mass.read()(n);
    }

  public:
    explicit CpuRun(const CompiledModel<T> &model)
        : model_(model), state_(validated(model).mesh), cpu_(model.mesh, state_) {
        model.require_cpu();
        double start = 0;
        for (auto &s : model.analysis.steps) {
            double bound = double(cpu_.plan().stable_step) * .8;
            double requested = s.increment > 0 ? s.increment : bound;
            if (s.direct && requested > double(cpu_.plan().stable_step))
                fail(s.source, "direct increment exceeds CPU stability bound");
            double target = s.direct ? requested : std::min(requested, bound);
            double count = std::ceil(s.period / target - 1e-12);
            if (!std::isfinite(count) || count < 1 || count > 100000000)
                fail(s.source, "step increment count outside supported budget");
            auto n = static_cast<std::uint64_t>(count);
            double dt = s.period / double(n);
            if (s.direct && std::abs(dt - requested) > requested * 1e-7)
                fail(s.source, "DIRECT USER CONTROL requires period to be an integer multiple of increment");
            dt = double(static_cast<T>(dt));
            plans_.push_back({n, dt, start});
            start += double(n) * dt;
        }
        native::Writer w;
        bool hex = false;
        for (const auto &d : model.descriptors)
            hex = hex || d.shape == Topology::Hex8;
        native::encode(w, std::string(hex ? "cpu/tet4-hex8-full/linear/cd-v1/serial/deterministic"
                                          : "cpu/tet4/linear/cd-v1/serial/deterministic"));
        native::encode(w, plans_);
        native::encode(w, std::uint8_t(sizeof(T)));
        // Only runs affected by the corrected propagated step-time amplitude
        // semantics change configuration identity. Their old checkpoints must
        // not silently resume under a different loading history.
        bool propagated_step_amplitude = false;
        for (std::size_t step = 0; step < model.analysis.steps.size(); ++step)
            for (const auto &a : model.analysis.steps[step].loads)
                if (a.origin_step < step && !a.amplitude.empty() &&
                    !model.analysis.amplitudes.at(a.amplitude).total_time)
                    propagated_step_amplitude = true;
        if (propagated_step_amplitude)
            native::encode(w, std::string("propagated-step-amplitude/hold-origin-end-v2"));
        config_ = native::hash(w.bytes);
    }
    StateDB<T> &state() { return state_; }
    const StateDB<T> &state() const { return state_; }
    const auto &plans() const { return plans_; }
    std::uint64_t configuration() const { return config_; }
    std::uint64_t step_index() const { return step_; }
    std::uint64_t increment() const { return increment_; }
    std::uint64_t force_evaluations() const { return cpu_.force_evaluations(); }
    std::uint64_t output_force_evaluations() const { return cpu_.output_force_evaluations(); }
    void initialize() {
        if (initialized_)
            throw std::logic_error("run already initialized");
        mask(0);
        loads();
        cpu_.initialize(state_, static_cast<T>(plans_[0].dt));
        initialized_ = true;
    }
    bool finished() const {
        return initialized_ && step_ + 1 == plans_.size() && increment_ == plans_.back().increments;
    }
    bool advance() {
        if (!initialized_)
            initialize();
        if (finished())
            return false;
        if (increment_ == plans_[step_].increments)
            next_step();
        loads();
        cpu_.step(state_);
        ++increment_;
        state_.data().time = plans_[step_].start + double(increment_) * plans_[step_].dt;
        loads();
        return true;
    }
    ResultFrame snapshot(std::uint64_t budget = native::default_limit) {
        if (!initialized_)
            initialize();
        FrameInfo info{state_.data().step, step_,
                       increment_,         model_.mesh.fingerprint(),
                       state_.data().time, double(increment_) * plans_[step_].dt,
                       sizeof(T),          8};
        return ResultPublisher::publish(model_, state_, cpu_, info, model_.outputs.at(step_),
                                        plans_[step_].increments, budget);
    }
    void save(const std::filesystem::path &path) {
        if (!initialized_)
            initialize();
        // Lazy publication must not leave a stale force field in the restart:
        // persist a displacement-consistent evaluation like the eager path did.
        cpu_.forces(state_);
        RestartInfo info;
        info.configuration = config_;
        info.step = step_;
        info.increment = increment_;
        RestartDB::save(path, model_, state_, info);
    }
    void restore(const std::filesystem::path &path) {
        if (state_.in_trial()) throw std::logic_error("restore requires committed state");
        StateDB<T> restored(model_.mesh, state_.data().rotation.has_value());
        restored.copy_registered_layout(state_);
        auto info = RestartDB::load(path, model_, restored, config_);
        if (info.step >= plans_.size() || info.increment > plans_[info.step].increments)
            throw std::runtime_error("restart cursor out of range");
        std::uint64_t total = info.increment;
        for (std::size_t i = 0; i < info.step; ++i)
            total += plans_[i].increments;
        if (total != info.total_increment || info.dt != plans_[info.step].dt ||
            std::abs(info.time - (plans_[info.step].start + double(info.increment) * info.dt)) > 1e-12)
            throw std::runtime_error("restart cursor/time/config inconsistency");
        for (auto &a : model_.analysis.steps[info.step].boundaries)
            if (restored.data().displacement.read()(a.node, a.component) != T(0))
                throw std::runtime_error("restart violates homogeneous boundary");
        publish_restored(restored, info.step, info.increment);
    }
    // Explicit v2 conversion is tied to the chosen compiled model; v2 itself had no analysis identity.
    void import_v2(const std::filesystem::path &path) {
        if (state_.in_trial()) throw std::logic_error("import requires committed state");
        if (const auto *registered = state_.registered_fields())
            for (const auto &entry : registered->entries())
                if (entry->descriptor().restart == RestartPolicy::Required)
                    throw std::runtime_error("v2 import requires explicit migration for registered required state");
        StateDB<T> candidate(model_.mesh);
        load_checkpoint(path, model_.mesh, candidate);
        candidate.copy_registered_layout(state_);
        auto &s = candidate.data();
        std::uint64_t remain = s.step;
        std::uint64_t candidate_step = 0;
        for (; candidate_step < plans_.size(); ++candidate_step) {
            if (remain <= plans_[candidate_step].increments)
                break;
            remain -= plans_[candidate_step].increments;
        }
        if (candidate_step == plans_.size() || s.dt != plans_[candidate_step].dt ||
            std::abs(s.time - (plans_[candidate_step].start + double(remain) * s.dt)) > 1e-9)
            throw std::runtime_error("v2 checkpoint does not match chosen analysis plan");
        for (auto &a : model_.analysis.steps[candidate_step].boundaries)
            if (s.displacement.read()(a.node, a.component) != T(0))
                throw std::runtime_error("v2 checkpoint violates boundary");
        publish_restored(candidate, candidate_step, remain);
    }
};
} // namespace sdb::lifecycle

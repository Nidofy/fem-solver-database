#pragma once
#include "results/results.hpp"
#include "io/restart.hpp"
#include "physics/penalty_constraints.hpp"

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
    std::optional<physics::PenaltyConstraints> penalty_;
    std::vector<StepPlan> plans_;
    std::uint64_t config_ = 0, step_ = 0, increment_ = 0;
    bool initialized_ = false;
    std::optional<Field<T,3>> applied_without_penalty_;
    void mask(std::size_t step) {
        std::vector<Constraint> cs;
        for (auto &a : model_.analysis.steps.at(step).boundaries)
            cs.push_back({a.node, a.component, a.value});
        cpu_.set_constraints(cs, state_);
    }
    void follower_loads(StateDB<T>& state,std::size_t step,double local,double total) const {
        if(!model_.definition.finite_geometry) return;
        auto f=state.data().external_force.write(); auto x=state.current_view(model_.mesh);
        for(const auto& value:model_.effective_load_values(StepId::checked(step),local,total)) {
            if(value.kind!=LoadValueKind::Pressure) continue;
            const auto& resolved=model_.foundation.load(value.definition.id,value.definition.step);
            for(auto face:model_.mesh.surfaces().at(resolved.surface.value()).faces) {
                Index e=model_.mesh.element_index(face.element); auto ns=model_.mesh.connectivity(e);
                auto faceids=topology_face_nodes(Topology::Tet4,face.local);
                distributed::Vec a{},b{};for(unsigned d=0;d<3;++d) {
                    a[d]=double(x(ns[faceids[1]],d))-x(ns[faceids[0]],d);
                    b[d]=double(x(ns[faceids[2]],d))-x(ns[faceids[0]],d);
                }
                auto area=distributed::cross(a,b);
                for(auto i:faceids) for(unsigned d=0;d<3;++d) {
                    auto v=double(f(ns[i],d))-value.scalar*area[d]/6;
                    if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<T>::max()) throw std::overflow_error("current pressure overflow");
                    f(ns[i],d)=T(v);
                }
            }
        }
    }
    void apply_penalty(StateDB<T>& state) const {
        if (!penalty_) return;
        auto reaction=penalty_->evaluate(state).restoring_force;
        auto f=state.data().external_force.write();
        for (std::size_t n=0;n<f.count;++n) for(unsigned d=0;d<3;++d) {
            double total=double(f(n,d))+reaction.read()(n,d);
            if (!std::isfinite(total) || std::abs(total)>std::numeric_limits<T>::max())
                throw std::overflow_error("constraint force accumulation overflow");
            f(n,d)=T(total);
        }
    }
    // Center the lumped Coriolis force at the integer-time velocity. On free DOFs
    // (I-cross(d))*w=v_half + dt*a_other/2, d=dt*k/(2*m); antisymmetry gives zero work.
    void coriolis_loads(StateDB<T>& state,std::size_t step,double local,bool full_velocity) {
        const auto& actions=model_.analysis.steps.at(step).coriolis;
        if(actions.empty())return;
        std::map<Index,distributed::Vec> coefficients;
        for(const auto& a:actions)
            coefficients[a.node][a.component]+=a.value*model_.load_amplitude(a.amplitude,StepId::checked(a.origin_step),
                StepId::checked(step),local,state.data().time);
        const auto& s=state.data();
        const bool center=!full_velocity && s.dt!=0;
        View<const T,3> internal{};
        if(center)internal=cpu_.output_forces(state).internal_force.read();
        std::map<Index,std::array<bool,3>> fixed;
        for(const auto& a:model_.analysis.steps[step].boundaries)fixed[a.node][a.component]=true;
        auto f=state.data().external_force.write();
        for(const auto& [n,k]:coefficients) {
            if(!s.node_active.read()(n))continue;
            auto constrained=fixed[n];
            distributed::Vec rhs{},d{},w{};
            for(unsigned j=0;j<3;++j) {
                if(!constrained[j]) {
                    rhs[j]=s.velocity.read()(n,j);
                    if(center)rhs[j]+=.5*s.dt*s.inverse_mass.read()(n)*(double(f(n,j))+s.contact_force.read()(n,j)-internal(n,j));
                }
                if(center && !constrained[(j+1)%3] && !constrained[(j+2)%3])
                    d[j]=.5*s.dt*s.inverse_mass.read()(n)*k[j];
            }
            auto skew=distributed::cross(d,rhs);
            double denom=1+distributed::dot(d,d),proj=distributed::dot(d,rhs);
            for(unsigned j=0;j<3;++j)w[j]=(rhs[j]+skew[j]+d[j]*proj)/denom;
            auto force=distributed::cross(k,w);
            for(unsigned j=0;j<3;++j) {
                double total=double(f(n,j))+force[j];
                if(!std::isfinite(total) || std::abs(total)>std::numeric_limits<T>::max())throw std::overflow_error("Coriolis load overflow");
                f(n,j)=T(total);
            }
        }
    }
    void loads(bool full_velocity=false) {
        LoadEvaluator::apply(model_.analysis, step_, double(increment_) * plans_[step_].dt,
                             state_.data().time, state_);
        follower_loads(state_,step_,double(increment_)*plans_[step_].dt,state_.data().time);
        // Penalty forces store recoverable energy. A centered gyroscopic force
        // does zero work; using it against end-half-step velocity would add spurious ALLWK.
        if (state_.data().external_work && (penalty_ || !model_.analysis.steps[step_].coriolis.empty()))
            applied_without_penalty_=state_.data().external_force;
        else applied_without_penalty_.reset();
        apply_penalty(state_);
        coriolis_loads(state_,step_,double(increment_)*plans_[step_].dt,full_velocity);
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
        follower_loads(candidate,step,double(increment)*plans_[step].dt,candidate.data().time);
        apply_penalty(candidate);
        coriolis_loads(candidate,step,double(increment)*plans_[step].dt,false);
        cpu_.set_constraints(constraints, candidate);
        static_assert(std::is_nothrow_move_assignable_v<StateData<T>>);
        state_.data() = std::move(candidate.data());
        step_ = step;
        increment_ = increment;
        initialized_ = true;
    }
    void next_step() {
        if (!(controls().options.bulk_linear || controls().options.bulk_quadratic) &&
            model_.analysis.steps[step_].coriolis.empty() && model_.analysis.steps[step_+1].coriolis.empty()) {
            rephase();
            return;
        }
        // A velocity-dependent evaluation can fail on either side of a mask
        // change. Preserve the last accepted state including its old half-step.
        const auto previous_step = step_, previous_increment = increment_;
        auto previous_state = state_.data();
        std::vector<Constraint> constraints;
        for (const auto &a : model_.analysis.steps[step_].boundaries)
            constraints.push_back({a.node,a.component,a.value});
        try {
            rephase();
        } catch (...) {
            state_.data() = std::move(previous_state);
            step_ = previous_step;
            increment_ = previous_increment;
            cpu_.set_constraints(constraints,state_);
            throw;
        }
    }
    void rephase() {
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
        loads(true);
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
    explicit CpuRun(const CompiledModel<T> &model, ExplicitControls controls = {}, CpuPolicy policy = CpuPolicy::Serial, TetKernel kernel = TetKernel::Scalar)
        : model_(model), state_(validated(model).mesh), cpu_(model.mesh, state_, policy, Assembly::Deterministic, controls,kernel,model.definition.finite_geometry) {
        model.require_cpu();
        if(controls.bulk_linear || controls.bulk_quadratic)
            for(const auto& step:model.analysis.steps) if(!step.coriolis.empty())
                throw std::invalid_argument("coupled Coriolis and bulk viscosity requires a coupled velocity solve");
        double stable = cpu_.plan().stable_step;
        if (model.definition.penalty_stiffness > 0) {
            penalty_.emplace(model, model.definition.penalty_stiffness);
            auto bound = penalty_->evaluate(state_).frequency_squared_bound;
            stable = 2/(2/stable + std::sqrt(bound));
        }
        double start = 0;
        for (auto &s : model.analysis.steps) {
            double bound = stable * .8;
            double requested = s.increment > 0 ? s.increment : bound;
            if (s.direct && requested > stable)
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
        for (const auto &step : model.outputs) for (const auto &request : step)
            for (const auto &name : request.variables) {
                if (name=="ALLWK" || name=="ETOTAL" || name=="ALLVD") state_.data().external_work=0.;
            }
        native::Writer w;
        if(model.definition.finite_geometry) native::encode(w,std::string("finite-tet4/neo-hookean-v1"));
        if (penalty_) native::encode(w, std::string("explicit-penalty-v1"));
        bool hex = false;
        for (const auto &d : model.descriptors)
            hex = hex || d.shape == Topology::Hex8;
        native::encode(w, std::string(hex ? "cpu/tet4-hex8-full/linear/cd-v1/serial/deterministic"
                                          : "cpu/tet4/linear/cd-v1/serial/deterministic"));
        if (cpu_.plan().hex && cpu_.plan().hex->hourglass_stiffness.size())
            native::encode(w, std::string("c3d8r/uniform-strain/elastic-hourglass-v1"));
        if (!cpu_.plan().plastic_points.empty()) native::encode(w, std::string("j2/tet4/radial-return-v1"));
        if (kernel == TetKernel::Mixed8) native::encode(w,std::string("tet4/mixed8-v1"));
        if (kernel == TetKernel::Tiled8) native::encode(w,std::string("tet4/tiled8-v1"));
        if (kernel == TetKernel::Batch8) native::encode(w,std::string("tet4/aosoa8-v1"));
        if (policy == CpuPolicy::OpenMP) native::encode(w,std::string("openmp/deterministic-v1"));
        if (controls.enabled()) {
            native::encode(w,std::string("fixed-mass/bulk/element-bound-v1"));
            native::encode(w,controls);
        }
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
    const CompiledModel<T> &model() const { return model_; }
    double mixed_peak_error() const noexcept {return cpu_.mixed_peak_error();}
    const ExplicitControlPlan& controls() const { return cpu_.plan().controls; }
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
        if (state_.data().external_work) {
            // The integrator used these start-of-increment forces. Updated
            // half-step velocity times dt is the accepted displacement increment.
            auto v=state_.data().velocity.read(),
                 f=applied_without_penalty_ ? applied_without_penalty_->read() : state_.data().external_force.read(),
                 contact=state_.data().contact_force.read();
            double work=0;
            for (std::size_t n=0;n<v.count;++n) for(unsigned c=0;c<3;++c)
                work += (double(f(n,c))+contact(n,c))*v(n,c)*state_.data().dt;
            *state_.data().external_work += work;
            state_.data().bulk_dissipation += cpu_.bulk_power()*state_.data().dt;
        }
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
        std::optional<double> penalty_energy;
        if (penalty_) {
            for (const auto &r:model_.outputs.at(step_))
                if(OutputScheduler::due(r,increment_,plans_[step_].increments) &&
                   (std::find(r.variables.begin(),r.variables.end(),"ALLIE")!=r.variables.end() ||
                    std::find(r.variables.begin(),r.variables.end(),"ETOTAL")!=r.variables.end())) {
                    penalty_energy=penalty_->evaluate(state_).potential;
                    break;
                }
        }
        return ResultPublisher::publish(model_, state_, cpu_, info, model_.outputs.at(step_),
                                        plans_[step_].increments, budget, penalty_energy);
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
        restored.data().external_work=state_.data().external_work;
        restored.data().explicit_controls = controls().identity;
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
        if (controls().identity) {
            const auto &expected = state_.data();
            const auto &saved = restored.data();
            for (Index n=0; n<model_.mesh.nodes(); ++n)
                if (saved.mass.read()(n)!=expected.mass.read()(n) ||
                    saved.inverse_mass.read()(n)!=expected.inverse_mass.read()(n))
                    throw std::runtime_error("restart mass differs from configured mass");
            for (Index e=0; e<model_.mesh.elements(); ++e)
                if (saved.stable_dt.read()(e)!=expected.stable_dt.read()(e))
                    throw std::runtime_error("restart stability bound differs from configured bound");
        }
        publish_restored(restored, info.step, info.increment);
    }
    // Explicit v2 conversion is tied to the chosen compiled model; v2 itself had no analysis identity.
    void import_v2(const std::filesystem::path &path) {
        if (controls().options.enabled() || state_.data().external_work) throw std::logic_error("v2 lacks explicit controls/work identity");
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

#pragma once
#include "../lifecycle.hpp"
#include "section_elements.hpp"

namespace sdb::physics {
template<class T> class SectionRun {
    const lifecycle::CompiledModel<T>& model_;
    StateDB<T> state_;
    std::vector<SectionElement> elements_;
    Field<T,6> forces_;
    Field<std::uint8_t,6> fixed_;
    std::optional<PenaltyConstraints> penalty_;
    std::uint64_t increments_=0,configuration_=0;
    double dt_=0;
    void evaluate(){
        const auto& step=model_.analysis.steps[0];auto &s=state_.data();auto& r=*s.rotation;
        forces_.fill(0);s.internal_force.fill(0);s.external_force.fill(0);r.torque.fill(0);
        auto f=forces_.write(); auto u=s.displacement.read(),a=r.angle.read();
        for(Index e=0;e<elements_.size();++e) if(s.element_active.read()(e)){
            const auto& el=elements_[e];for(unsigned i=0;i<6*el.count;++i){
                double fi=0;for(unsigned j=0;j<6*el.count;++j){
                    auto n=el.nodes[j/6],d=j%6;fi+=el.K[i][j]*(d<3?u(n,d):a(n,d-3));
                }
                auto n=el.nodes[i/6],d=i%6;f(n,d)-=T(fi);
                if(d<3)s.internal_force.write()(n,d)+=T(fi);else r.torque.write()(n,d-3)+=T(fi);
            }
        }
        if(penalty_) {
            auto reactions=penalty_->evaluate(state_);
            for(Index n=0;n<model_.mesh.nodes();++n)for(unsigned d=0;d<3;++d){
                f(n,d)+=reactions.restoring_force.read()(n,d);
                f(n,d+3)+=reactions.restoring_moment.read()(n,d);
            }
        }
        for(auto& l:step.loads) {
            double val=l.value*model_.load_amplitude(l.amplitude,StepId(0),StepId(0),s.time,s.time);
            f(l.node,l.component)+=T(val);if(l.component<3)s.external_force.write()(l.node,l.component)+=T(val);
        }
        for(Index n=0;n<model_.mesh.nodes();++n)for(unsigned d=0;d<6;++d){
            T inv=d<3?s.inverse_mass.read()(n):r.inverse_inertia.read()(n,d-3);
            T av=fixed_.read()(n,d)?T(0):T(f(n,d)*inv);
            if(!std::isfinite(av))throw std::overflow_error("nonfinite sectional acceleration");
            if(d<3)s.acceleration.write()(n,d)=av;else r.acceleration.write()(n,d-3)=av;
        }
    }
  public:
    explicit SectionRun(const lifecycle::CompiledModel<T>& model):model_(model),state_(model.mesh,true),
        forces_(model.mesh.nodes()),fixed_(model.mesh.nodes()){
        for (const auto &step : model.outputs)
            for (const auto &request : step)
                for (const auto &name : request.variables)
                    if(name!="U" && name!="UR")
                        throw std::invalid_argument("section backend publishes U/UR only; requested output unavailable: "+name);
        const auto& def=model.definition;
        auto other=def.foundation;auto extensions=def.extensions;
        if(def.penalty_stiffness>0){other.equations.clear();other.rigids.clear();extensions.couplings.clear();
            penalty_.emplace(model,def.penalty_stiffness);}
        if(def.mode!=lifecycle::ImportMode::Solve || model.analysis.steps.size()!=1 || !def.opaque.empty() ||
           !other.empty() || !extensions.empty() || def.finite_geometry || !def.plastic.empty() ||
           model.sections.size()!=model.mesh.elements())
            throw std::invalid_argument("section backend requires an isolated single-step elastic section model");
        const auto& step=model.analysis.steps[0];
        for(const auto& b:step.boundaries)fixed_.write()(b.node,b.component)=1;
        std::vector<const lifecycle::SectionRecord*> sections(model.mesh.elements());
        for(const auto& section:model.sections) sections.at(section.element)=&section;
        auto &s=state_.data();auto& r=*s.rotation;double bound=0;
        for(Index e=0;e<model.mesh.elements();++e){
            if(!sections[e])throw std::invalid_argument("unassigned sectional element");
            elements_.emplace_back(model,e,*sections[e]);const auto& el=elements_.back();
            auto omega2=el.squared_frequency_bound();s.stable_dt.write()(e)=T(1.9/std::sqrt(omega2));bound=std::max(bound,omega2);
            for(unsigned a=0;a<el.count;++a){
                auto n=el.nodes[a];s.mass.write()(n)+=T(el.nodal_mass);
                for(unsigned d=0;d<3;++d) r.inertia.write()(n,d)+=T(el.rotary_mass);
            }
        }
        for(Index n=0;n<model.mesh.nodes();++n){
            if(!(s.mass.read()(n)>0 && std::isfinite(s.mass.read()(n))))throw std::domain_error("section node without mass");
            s.inverse_mass.write()(n)=1/s.mass.read()(n);
            for(unsigned d=0;d<3;++d)r.inverse_inertia.write()(n,d)=1/r.inertia.read()(n,d);
        }
        if(penalty_)bound+=penalty_->evaluate(state_).frequency_squared_bound;
        double h=1.9/std::sqrt(bound);
        if(step.direct && step.increment>h)throw std::domain_error("section increment exceeds stability bound");
        double requested=step.direct?step.increment:std::min(step.increment>0?step.increment:h,.8*h);
        double count=std::ceil(step.period/requested-1e-12);
        if(!(count>=1 && count<=1e8))throw std::domain_error("section increment budget");
        increments_=std::uint64_t(count);dt_=double(T(step.period/count));
        if(!(dt_>0) || (step.direct && std::abs(step.period/count-step.increment)>step.increment*1e-7))
            throw std::invalid_argument("invalid section direct increment");
        configuration_=semantic_hash(std::make_pair(std::string("section/small-strain/v1"),dt_));
    }
    std::uint64_t configuration() const noexcept {return configuration_;}
    const StateDB<T>& state() const{return state_;}
    const SectionElement& element(Index e) const{return elements_.at(e);}
    void initialize(){
        auto& s=state_.data();if(s.dt!=0)throw std::logic_error("section run already initialized");
        evaluate();s.dt=dt_;
        for(Index n=0;n<model_.mesh.nodes();++n)for(unsigned d=0;d<3;++d){
            s.velocity.write()(n,d)-=T(dt_/2)*s.acceleration.read()(n,d);
            s.rotation->velocity.write()(n,d)-=T(dt_/2)*s.rotation->acceleration.read()(n,d);
        }
    }
    bool advance(){
        auto& s=state_.data();if(s.dt==0)initialize();if(s.step>=increments_)return false;
        evaluate();auto& r=*s.rotation;
        for(Index n=0;n<model_.mesh.nodes();++n)for(unsigned d=0;d<3;++d){
            s.velocity.write()(n,d)+=T(dt_)*s.acceleration.read()(n,d);s.displacement.write()(n,d)+=T(dt_)*s.velocity.read()(n,d);
            r.velocity.write()(n,d)+=T(dt_)*r.acceleration.read()(n,d);r.angle.write()(n,d)+=T(dt_)*r.velocity.read()(n,d);
        }
        ++s.step;s.time=dt_*s.step;return true;
    }
    lifecycle::ResultFrame snapshot() const{
        const auto& s=state_.data();lifecycle::ResultFrame frame;
        frame.info={s.step,0,s.step,model_.mesh.fingerprint(),s.time,s.time,sizeof(T),8};
        for(bool rotational:{false,true}){
            lifecycle::ResultField field;field.descriptor.name=rotational?"UR":"U";
            field.descriptor.components={"X","Y","Z"};field.descriptor.provenance="section linear six-DOF";
            auto v=rotational?s.rotation->angle.read():s.displacement.read();
            for(Index n=0;n<model_.mesh.nodes();++n){field.ids.push_back(Id(n)+1);for(unsigned d=0;d<3;++d)field.values.push_back(v(n,d));}
            frame.fields.push_back(std::move(field));
        }
        return frame;
    }
    void save(const std::filesystem::path& path){
        if(state_.data().dt==0)initialize();evaluate();lifecycle::RestartInfo info;
        info.configuration=configuration_;info.increment=state_.data().step;
        lifecycle::RestartDB::save(path,model_,state_,info);
    }
    void restore(const std::filesystem::path& path){
        StateDB<T> next(model_.mesh,true);
        auto info=lifecycle::RestartDB::load(path,model_,next,configuration_);
        if(info.step || info.increment!=info.total_increment || info.increment>increments_ || info.dt!=dt_ ||
           info.time!=dt_*info.increment) throw std::invalid_argument("section restart cursor mismatch");
        for(Index n=0;n<model_.mesh.nodes();++n) {
            if(next.data().mass.read()(n)!=state_.data().mass.read()(n)) throw std::invalid_argument("section mass mismatch");
            for(unsigned d=0;d<3;++d)if(next.data().rotation->inertia.read()(n,d)!=state_.data().rotation->inertia.read()(n,d))
                throw std::invalid_argument("section rotary inertia mismatch");
        }
        state_.data()=std::move(next.data());
    }
};
}

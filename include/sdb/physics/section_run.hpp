#pragma once
#include "../lifecycle.hpp"
#include "section_elements.hpp"
#include "../results/mechanical_publishers.hpp"

namespace sdb::physics {
template<class T> class SectionRun {
    const lifecycle::CompiledModel<T>& model_;
    StateDB<T> state_;
    std::vector<SectionElement> elements_;
    Field<T,6> forces_;
    Field<std::uint8_t,6> fixed_;
    std::optional<PenaltyConstraints> penalty_;
    std::uint64_t increments_=0,configuration_=0;
    struct Segment { std::uint64_t first, count; double start, dt; };
    std::vector<Segment> segments_;
    std::size_t step_id_=0;
    std::vector<std::vector<lifecycle::Action>> loads_;
    double dt_=0;
    std::optional<lifecycle::ResultModel> recovery_model_;
    static std::uint64_t basis_id(Index e) {return (std::uint64_t(1)<<63)+e;}
    void prepare_loads() {
        using namespace lifecycle;
        const auto& foundation=model_.foundation;
        for(std::size_t step=0;step<segments_.size();++step) {
            auto& actions=loads_.emplace_back(model_.analysis.steps[step].loads);
            for(const auto& live:foundation.effective_loads(StepId::checked(step))) for(auto origin:live.contributions) {
                auto v=foundation.load_contribution(live.id,origin);
                const auto& r=foundation.load(live.id,origin);
                if(v.definition.coordinate.value()!=0)throw std::invalid_argument("section loads require global coordinates");
                auto add=[&](const SectionElement& el,Vec3 total,double scale){
                    for(unsigned a=0;a<el.count;++a) for(std::uint8_t d=0;d<3;++d){
                        double value=total[d]*scale;
                        if(!std::isfinite(value))throw std::overflow_error("section load exceeds finite range");
                        if(value!=0) actions.push_back({el.nodes[a],d,value,v.definition.amplitude,origin.value()});
                    }
                };
                if(v.kind==LoadValueKind::Pressure) {
                    for(auto face:model_.mesh.surfaces().at(r.surface.value()).faces) {
                        const auto& el=elements_.at(model_.mesh.element_index(face.element));
                        if(el.count!=4 || face.local!=0)throw std::invalid_argument("section pressure requires S4R face");
                        add(el,el.axis(2),-face.side*v.scalar*el.load_measure()/4);
                    }
                    continue;
                }
                for(auto id:r.elements) {
                    auto e=id.value();const auto& el=elements_.at(e);
                    if(v.kind==LoadValueKind::Acceleration) add(el,v.vector,el.nodal_mass);
                    else if(v.kind==LoadValueKind::ForcePerVolume) {
                        auto rho=model_.mesh.materials()[model_.mesh.element(e).material].parameters.at(0);
                        add(el,v.vector,el.nodal_mass/rho);
                    } else if(v.kind==LoadValueKind::GlobalLine || v.kind==LoadValueKind::LocalLine) {
                        if(el.count!=2)throw std::invalid_argument("line load requires B31");
                        Vec3 global=v.vector;
                        if(v.kind==LoadValueKind::LocalLine) {
                            global={};
                            for(unsigned i=0;i<3;++i)for(unsigned d=0;d<3;++d)global[d]+=v.vector[i]*el.axis(i)[d];
                        }
                        add(el,global,el.load_measure()/2);
                    } else throw std::invalid_argument("unsupported sectional body load");
                }
                if(r.elements.empty())throw std::invalid_argument("unsupported sectional load target");
            }
        }
    }
    void evaluate(){auto &s=state_.data();auto& r=*s.rotation;
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
        for(const auto& l:loads_.at(step_id_)) {
            auto current=StepId::checked(step_id_);
            auto origin=l.origin_step==UINT64_MAX?current:StepId::checked(l.origin_step);
            double val=l.value*model_.load_amplitude(l.amplitude,origin,current,
                (s.step-segments_[step_id_].first)*dt_,s.time);
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
                    if(name=="S" || name=="E") recovery_model_.emplace();
                    else if(name!="U" && name!="UR")
                        throw std::invalid_argument("section backend supports U/UR and section S/E only: "+name);
        const auto& def=model.definition;
        auto other=def.foundation;auto extensions=def.extensions;
        if(def.penalty_stiffness>0){other.equations.clear();other.rigids.clear();extensions.couplings.clear();
            penalty_.emplace(model,def.penalty_stiffness);}
        other.face_loads.clear();other.body_loads.clear();
        if(def.mode!=lifecycle::ImportMode::Solve || model.analysis.steps.empty() || !def.opaque.empty() ||
           !other.empty() || !extensions.empty() || def.finite_geometry || !def.plastic.empty() ||
           model.sections.size()!=model.mesh.elements())
            throw std::invalid_argument("section backend requires an isolated elastic section model");
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
        double start=0;
        for(const auto& step:model.analysis.steps) {
        // Multi-step runs share the established homogeneous six-DOF mask.
        if(step.boundaries.size()!=model.analysis.steps.front().boundaries.size())throw std::invalid_argument("changing section boundary mask unsupported");
        for(auto& b:step.boundaries)if(!fixed_.read()(b.node,b.component))throw std::invalid_argument("changing section boundary mask unsupported");
        if(step.direct && step.increment>h)throw std::domain_error("section increment exceeds stability bound");
        double requested=step.direct?step.increment:std::min(step.increment>0?step.increment:h,.8*h);
        double count=std::ceil(step.period/requested-1e-12);
        if(!(count>=1 && count<=1e8))throw std::domain_error("section increment budget");
        double segment_dt=double(T(step.period/count));
        if(!(segment_dt>0) || (step.direct && std::abs(step.period/count-step.increment)>step.increment*1e-7))
            throw std::invalid_argument("invalid section direct increment");
        segments_.push_back({increments_,std::uint64_t(count),start,segment_dt});
        increments_+=std::uint64_t(count); start+=count*segment_dt;
        }
        dt_=segments_[0].dt;
        prepare_loads();
        configuration_=semantic_hash(std::make_pair(std::string("section/small-strain/v1"),dt_));
        if(segments_.size()>1) {
            std::vector<std::pair<std::uint64_t,double>> signature;
            for(auto& seg:segments_)signature.emplace_back(seg.count,seg.dt);
            configuration_=semantic_hash(std::make_pair(std::string("section/steps/v1"),signature));
        }
        if(recovery_model_) {
            auto &out=*recovery_model_;out=lifecycle::ResultModel::from(model_,configuration_);
            out.samples.assign(elements_.size(),{});
            for(Index e=0;e<elements_.size();++e) {
                out.bases.push_back({basis_id(e),elements_[e].recovery_basis()});
                for(unsigned q=1;q<=elements_[e].sample_count();++q)
                    out.samples[e].push_back({1,1,q,model.mesh.element(e).material});
            }
        }
    }
    std::uint64_t increments() const noexcept { return increments_; }
    std::size_t step_index() const noexcept {return step_id_;}
    bool end_of_step() const noexcept {return state_.data().step==segments_[step_id_].first+segments_[step_id_].count;}
    bool recovery_enabled() const noexcept {return recovery_model_.has_value();}
    lifecycle::ResultModel result_model() const {
        return recovery_model_ ? *recovery_model_ : lifecycle::ResultModel::from(model_,configuration_);
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
        if(s.step==segments_[step_id_].first+segments_[step_id_].count) {
            // Recover integer-time velocities with the old loads, then initialize
            // the new half step with the new loads. All boundaries are unchanged.
            evaluate();
            auto shift=[&](double h) {
                for(Index n=0;n<model_.mesh.nodes();++n)for(unsigned d=0;d<3;++d) {
                    s.velocity.write()(n,d)+=T(h)*s.acceleration.read()(n,d);
                    s.rotation->velocity.write()(n,d)+=T(h)*s.rotation->acceleration.read()(n,d);
                }
            };
            shift(dt_/2);++step_id_;dt_=segments_[step_id_].dt;s.dt=dt_;evaluate();shift(-dt_/2);
        }
        evaluate();auto& r=*s.rotation;
        for(Index n=0;n<model_.mesh.nodes();++n)for(unsigned d=0;d<3;++d){
            s.velocity.write()(n,d)+=T(dt_)*s.acceleration.read()(n,d);s.displacement.write()(n,d)+=T(dt_)*s.velocity.read()(n,d);
            r.velocity.write()(n,d)+=T(dt_)*r.acceleration.read()(n,d);r.angle.write()(n,d)+=T(dt_)*r.velocity.read()(n,d);
        }
        ++s.step;s.time=segments_[step_id_].start+dt_*(s.step-segments_[step_id_].first);return true;
    }
    lifecycle::ResultFrame snapshot() const{
        const auto& s=state_.data();lifecycle::ResultFrame frame;
        auto local=s.step-segments_[step_id_].first;
        frame.info={s.step,step_id_,local,model_.mesh.fingerprint(),s.time,dt_*local,sizeof(T),8};
        if(recovery_model_) lifecycle::bind_result_frame(frame,*recovery_model_);
        const auto &requests=model_.outputs.at(step_id_);
        auto publish=[&](const std::string& name,const std::string& region,bool history) {
            const bool nodal=name=="U" || name=="UR";
            std::vector<Index> all;
            if(region.empty()) {all.resize(nodal?model_.mesh.nodes():elements_.size());std::iota(all.begin(),all.end(),Index(0));}
            const auto &ids=region.empty()?all:model_.region(nodal,region);
            if(nodal) {
                lifecycle::ResultField f; f.descriptor.name=name; f.descriptor.region=region; f.descriptor.history=history;
                f.descriptor.components={"X","Y","Z"};f.descriptor.provenance="section linear six-DOF";
                auto v=name=="UR"?s.rotation->angle.read():s.displacement.read();
                for(auto n:ids){f.ids.push_back(Id(n)+1);for(unsigned d=0;d<3;++d)f.values.push_back(v(n,d));}
                frame.fields.push_back(std::move(f));return;
            }
            for(auto e:ids) {
                if(!s.element_active.read()(e)) continue;
                const auto &el=elements_[e];
                FieldDescriptor d;d.name=name;d.scalar=scalar_type<T>();d.location=FieldLocation::SectionPoint;
                d.basis="SECTION LOCAL"; d.components=el.count==2?std::vector<std::string>{"11"}:
                                         std::vector<std::string>{"11","22","12"};
                d.shape={static_cast<std::uint32_t>(d.components.size())};
                if(name=="E" && el.count==4)d.shear=ShearConvention::Tensor;
                auto f=lifecycle::section_point_field(*recovery_model_,e,d,el.recover(state_,name=="S"),
                                                     basis_id(e),region,history);
                f.typed->schema_id=el.count==2?"sdb.b31.rect.axial":"sdb.s4r.inplane";
                f.descriptor.provenance=el.count==2?"RECT corner axial fiber":"Centre IP plane-stress layer recovery";
                frame.fields.push_back(std::move(f));
            }
        };
        if(requests.empty()) {publish("U","",false);publish("UR","",false);}
        std::set<std::tuple<std::string,std::string,bool>> emitted;
        for(const auto& r:requests) if(lifecycle::OutputScheduler::due(r,local,segments_[step_id_].count))
            for(const auto& name:r.variables) if(emitted.emplace(name,r.region,r.history).second)
                publish(name,r.region,r.history);
        return frame;
    }
    void save(const std::filesystem::path& path){
        if(state_.data().dt==0)initialize();evaluate();lifecycle::RestartInfo info;
        info.configuration=configuration_;info.step=step_id_;info.increment=state_.data().step-segments_[step_id_].first;
        lifecycle::RestartDB::save(path,model_,state_,info);
    }
    void restore(const std::filesystem::path& path){
        StateDB<T> next(model_.mesh,true);
        auto info=lifecycle::RestartDB::load(path,model_,next,configuration_);
        if(info.step>=segments_.size())throw std::invalid_argument("section restart step mismatch");
        auto seg=segments_[info.step];
        if(info.increment>seg.count || info.total_increment!=seg.first+info.increment || info.dt!=seg.dt ||
           info.time!=seg.start+seg.dt*info.increment) throw std::invalid_argument("section restart cursor mismatch");
        for(Index n=0;n<model_.mesh.nodes();++n) {
            if(next.data().mass.read()(n)!=state_.data().mass.read()(n)) throw std::invalid_argument("section mass mismatch");
            for(unsigned d=0;d<3;++d)if(next.data().rotation->inertia.read()(n,d)!=state_.data().rotation->inertia.read()(n,d))
                throw std::invalid_argument("section rotary inertia mismatch");
        }
        state_.data()=std::move(next.data());step_id_=info.step;dt_=seg.dt;
    }
};
}

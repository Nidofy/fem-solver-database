#include <sdb/consumer_views.hpp>
#include <sdb/results/checkpoint_link.hpp>
#include <iostream>
#include <type_traits>
using namespace sdb;
using namespace sdb::consumer;
std::size_t checks = 0;
void check(bool ok, const char *name) { ++checks; if (!ok) throw std::runtime_error(name); }
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::exception &) { rejected = true; }
    check(rejected, "expected rejection");
}
static_assert(std::is_same_v<decltype(std::declval<BorrowedField<double, 3>>().at(0, 0)), const double &>);
static_assert(std::is_same_v<decltype(std::declval<BorrowedField<double, 3>>().resolve()(0, 0)), const double &>);
static_assert(!std::is_assignable_v<decltype(std::declval<BorrowedField<Index, 4>>().resolve()(0, 0)), Index>);
static_assert(!std::is_convertible_v<View<const double, 3>, View<double, 3>>);
static_assert(!std::is_copy_constructible_v<ReferenceConsumer<double>>);
constexpr int corners[8][3] = {{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
template<class T> MeshDB<T> mixed_model(bool hex_only = false) {
    ModelBuilder<T> b;
    b.add_material({1,1,{T(2),T(100),T(.25)},0}); b.add_part(1);
    for (unsigned a=0;a<8;++a) {
        T x = corners[a][0], y = corners[a][1], z = corners[a][2];
        b.add_node(a+1,{x+T(.2)*y+T(.1)*z,T(.1)*x+T(1.2)*y,z+T(.05)*y});
    }
    b.add_element(100,Topology::Hex8,{1,2,3,4,5,6,7,8},1,1,8);
    if (!hex_only) {
        b.add_node(9,{T(-1),0,0});
        b.add_element(200,Topology::Tet4,{1,4,9,5},1,1,1);
    }
    for (Id n : {1,4,5,8}) for (unsigned c=0;c<3;++c) b.constrain(n,static_cast<std::uint8_t>(c));
    return b.finalize();
}
template<class T, std::size_t C> void equal_fields(const Field<T,C> &a, const Field<T,C> &b, double tol = 0) {
    check(a.size()==b.size(),"same field extent");
    for(std::size_t i=0;i<a.size();++i) for(std::size_t c=0;c<C;++c)
        if(tol==0) check(std::bit_cast<std::array<std::byte,sizeof(T)>>(a.read()(i,c))==
                        std::bit_cast<std::array<std::byte,sizeof(T)>>(b.read()(i,c)),"bitwise numerical parity");
        else check(std::abs(double(a.read()(i,c))-double(b.read()(i,c))) <= tol, "numerical parity");
}
template<class T> void parity(MeshDB<T> mesh, CpuPolicy policy = CpuPolicy::Serial) {
    StateDB<T> baseline(mesh);
    CpuExplicit<T> cpu(mesh,baseline,policy);
    auto u = baseline.data().displacement.write();
    auto v = baseline.data().velocity.write();
    for(std::size_t n=0;n<mesh.nodes();++n) for(unsigned c=0;c<3;++c) {
        u(n,c)=T(.0002*std::sin(double(3*n+c+1)));
        v(n,c)=T(.0001*std::cos(double(n+c+1)));
    }
    cpu.initialize(baseline,cpu.plan().stable_step*T(.1));
    StateDB<T> candidate = baseline;
    ReferenceConsumer<T> consumer(cpu.plan(),candidate);
    auto nodes = NodeStateView<T>::capture(candidate);
    auto fixed = BorrowedField<std::uint8_t,3>(cpu.plan().fixed);
    ResultSink<T> sink(candidate.data().internal_force,candidate.data().stress,candidate.data().strain);
    check(nodes.displacement.resolve().data == candidate.data().displacement.read().data,"zero-copy node input");
    check(consumer.tet_shapes().gradients.resolve().data == cpu.plan().gradients.read().data,"zero-copy shape input");
    check(consumer.tet_batch().nodes.resolve().data == cpu.plan().nodes.read().data,"zero-copy connectivity");
    // Each iteration evaluates through only contract fields and outputs, then uses the same central difference rule.
    for(unsigned step=0;step<80;++step) {
        if(step==20) { baseline.data().element_active.write()(0)=0; candidate.data().element_active.write()(0)=0; }
        if(step==30) { baseline.data().element_active.write()(0)=1; candidate.data().element_active.write()(0)=1; }
        consumer.evaluate(nodes,sink);
        cpu.step(baseline);
        equal_fields(candidate.data().internal_force,baseline.data().internal_force);
        equal_fields(candidate.data().stress,baseline.data().stress);
        equal_fields(candidate.data().strain,baseline.data().strain);
        auto mask=fixed.resolve(); auto inv=nodes.inverse_mass.resolve(); auto live=nodes.node_active.resolve();
        auto force=candidate.data().internal_force.read(); auto ext=candidate.data().external_force.read();
        auto contact=candidate.data().contact_force.read();
        auto cu=candidate.data().displacement.write(), cv=candidate.data().velocity.write(), ca=candidate.data().acceleration.write();
        T dt=static_cast<T>(candidate.data().dt);
        for(std::size_t n=0;n<mesh.nodes();++n) for(unsigned c=0;c<3;++c) {
            if(mask(n,c)||!live(n)) { cv(n,c)=ca(n,c)=0; continue; }
            ca(n,c)=(ext(n,c)+contact(n,c)-force(n,c))*inv(n);
            cv(n,c)+=dt*ca(n,c); cu(n,c)+=dt*cv(n,c);
        }
        ++candidate.data().step; candidate.data().time+=candidate.data().dt;
        equal_fields(candidate.data().displacement,baseline.data().displacement);
        equal_fields(candidate.data().velocity,baseline.data().velocity);
        equal_fields(candidate.data().acceleration,baseline.data().acceleration);
    }
}
void lifetimes_and_adversary() {
    auto mesh=oscillator_model<double>(); StateDB<double> state(mesh); CpuExplicit<double> cpu(mesh,state);
    ReferenceConsumer<double> consumer(cpu.plan(),state);
    auto borrowed=consumer.tet_batch().nodes;
    std::array<Index,4> original{};
    for(unsigned a=0;a<4;++a) original[a]=borrowed.at(0,a);
    auto scratch=private_dof_scratch(borrowed,0);
    for(auto &dof:scratch) --dof; // Exact old eecontact-style mutation, confined to private scratch.
    for(unsigned a=0;a<4;++a) {
        check(original[a]==borrowed.at(0,a),"scratch mutation preserves canonical connectivity");
        for(unsigned c=0;c<3;++c) check(scratch[3*a+c]==3*original[a]+c,"owned scratch has zero-based DOFs");
    }
    rejects([&]{ private_dof_scratch(borrowed,0,0); });
    rejects([&]{ borrowed.at(borrowed.size(),0); });
    rejects([&]{ borrowed.at(0,4); });
    Field<Index,4> overflow(1); overflow.fill(std::numeric_limits<Index>::max());
    rejects([&]{ private_dof_scratch(BorrowedField<Index,4>(overflow),0); });
    auto nodes=NodeStateView<double>::capture(state);
    ForceSnapshot<double> output(mesh.nodes(),state.data().stress.size());
    ResultSink<double> sink(output.internal_force,output.stress,output.strain);
    consumer.evaluate(nodes,sink);
    StateDB<double> other(mesh);
    rejects([&]{ consumer.evaluate(NodeStateView<double>::capture(other),sink); });
    ResultSink<double> alias(state.data().displacement,output.stress,output.strain);
    rejects([&]{ consumer.evaluate(nodes,alias); });
    ResultSink<double> alias_v(state.data().velocity,output.stress,output.strain);
    rejects([&]{ consumer.evaluate(nodes,alias_v); });
    ResultSink<double> alias_s(output.internal_force,output.stress,output.stress);
    rejects([&]{ consumer.evaluate(nodes,alias_s); });
    output.stress.resize(output.stress.size()+1);
    rejects([&]{ consumer.evaluate(nodes,sink); });
    state.data().displacement.resize(mesh.nodes()); // Same extent, generation must still expire.
    rejects([&]{ nodes.displacement.resolve(); });
    Field<double> field(0); auto empty=BorrowedField<double>(field); field.resize(0);
    rejects([&]{ empty.resolve(); });
    std::optional<BorrowedField<double>> destroyed;
    { Field<double> local(1); destroyed.emplace(local); }
    rejects([&]{ destroyed->resolve(); });
    Field<double> moved(2); auto stale=BorrowedField<double>(moved); Field<double> destination(std::move(moved));
    rejects([&]{ stale.resolve(); });
    Field<double> left(1),right(1); auto swapped=BorrowedField<double>(left); left.swap(right);
    rejects([&]{ swapped.resolve(); });
    Field<double> assigned(1); auto copied=BorrowedField<double>(assigned); assigned=destination;
    rejects([&]{ copied.resolve(); });
    std::optional<MaterialView<double>> dead_material;
    std::optional<ShapeTableView<double,4>> dead_shape;
    std::unique_ptr<ReferenceConsumer<double>> dead_owner;
    {
        CpuExplicit<double> temporary(mesh,state);
        dead_material.emplace(MaterialView<double>{BorrowedField<double>(temporary.plan().lambda), BorrowedField<double>(temporary.plan().mu)});
        dead_shape.emplace(ShapeTableView<double,4>{BorrowedField<double,12>(temporary.plan().gradients), BorrowedField<double>(temporary.plan().volume)});
        dead_owner=std::make_unique<ReferenceConsumer<double>>(temporary.plan(),state);
    }
    rejects([&]{ dead_material->lambda.resolve(); });
    rejects([&]{ dead_shape->gradients.resolve(); });
    ForceSnapshot<double> fresh_output(mesh.nodes(),state.data().stress.size());
    rejects([&]{ dead_owner->evaluate(NodeStateView<double>::capture(state),
        ResultSink<double>(fresh_output.internal_force,fresh_output.stress,fresh_output.strain)); });
    auto shape=consumer.tet_shapes().gradients;
    // Plan owner outlives the view here, but reallocation must still invalidate it.
    auto &plan=const_cast<TetPlan<double>&>(cpu.plan());
    plan.gradients.resize(plan.gradients.size());
    rejects([&]{ shape.resolve(); });
    lifecycle::CheckpointLink link; link.definition=1;link.configuration=2;link.total_increment=3;
    auto restart=RestartLink::capture(link); restart.require_identity(1,2,3); ++checks;
    rejects([&]{ restart.require_identity(1,2,4); });
    link.schema=2; rejects([&]{ RestartLink::capture(link); });
}

void publication_integration(const std::filesystem::path &fixture, const std::filesystem::path &scratch) {
    using namespace sdb::lifecycle;
    auto model=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(fixture).model);
    CpuRun<double> run(model);
    auto register_outputs=[](StateDB<double> &state) {
        FieldDescriptor d; d.name="CONSUMER_S"; d.shape={6}; d.components={"XX","YY","ZZ","XY","YZ","ZX"};
        d.basis="GLOBAL"; d.shear=ShearConvention::Tensor;
        state.register_field<Field<double,6>>(d,state.ip_offsets().back(),"consumer.stress",1,{0,0,0,0,0,0});
        d.name="CONSUMER_E";
        state.register_field<Field<double,6>>(d,state.ip_offsets().back(),"consumer.strain",1,{0,0,0,0,0,0});
        state.freeze_registered_fields();
    };
    register_outputs(run.state()); run.initialize();
    for(unsigned i=0;i<100;++i) check(run.advance(),"fixture advancement");
    CpuExplicit<double> geometry(model.mesh,run.state());
    ReferenceConsumer<double> consumer(geometry.plan(),run.state());
    auto &stress=run.state().registered_field<Field<double,6>>("CONSUMER_S");
    auto &strain=run.state().registered_field<Field<double,6>>("CONSUMER_E");
    consumer.evaluate(NodeStateView<double>::capture(run.state()),
        ResultSink<double>(run.state().data().internal_force,stress,strain));
    geometry.forces(run.state()); equal_fields(stress,run.state().data().stress); equal_fields(strain,run.state().data().strain);
    auto frame=ResultPublisher::snapshot_registered(model,run.state(),{}, {}, native::default_limit,run.configuration());
    check(frame.fields.size()==2,"existing publisher snapshots contract outputs");
    std::filesystem::create_directories(scratch);
    auto result=scratch/"consumer.sdbres", restart=scratch/"consumer.sdbrst";
    std::filesystem::remove(result); std::filesystem::remove(restart); std::filesystem::remove(CheckpointLink::path(restart));
    {
        NativeResultStorage storage(result,ResultModel::from(model,run.configuration()),false,false,
            native::default_limit,{},false,ResultFormat::TypedIndexed3);
        storage.append(frame);
        auto publication=CheckpointLink::publish_durable(storage,restart,run,model);
        check(publication.result.bytes==std::filesystem::file_size(result),"consumer linked durable prefix");
        check(publication.restart_published&&publication.link_published,"consumer restart/link publication");
        CheckpointLink::verify(result,restart,run,model); ++checks;
    }
    ResultReader reader(result);
    auto published=reader.typed_field(FrameId::checked(run.state().data().step),"CONSUMER_S");
    check(typed_values<double>(published)==typed_values<double>(frame.fields[0]),"typed publication retains consumer values");
    CpuRun<double> restored(model); register_outputs(restored.state()); restored.restore(restart);
    CheckpointLink::verify(result,restart,restored,model); ++checks;
    equal_fields(restored.state().registered_field<Field<double,6>>("CONSUMER_S"),stress);
    std::ifstream in(CheckpointLink::path(restart),std::ios::binary);
    native::prefix(in,0x314b4e494c424453ull);
    auto link=native::unpack<CheckpointLink>(native::record(in,0,1024),1024); native::end(in);
    auto contract_link=RestartLink::capture(link);
    contract_link.require_identity(ModelStore::identity(model),run.configuration(),run.state().data().step); ++checks;
    check(contract_link.result_bytes==std::filesystem::file_size(result),"RestartLink carries verified prefix");
}

int main(int argc, char **argv) {
    try {
        parity<float>(oscillator_model<float>()); parity<double>(oscillator_model<double>());
        parity<float>(mixed_model<float>(true)); parity<double>(mixed_model<double>(true));
        parity<float>(mixed_model<float>()); parity<double>(mixed_model<double>());
#ifdef _OPENMP
        parity<double>(oscillator_model<double>(),CpuPolicy::OpenMP);
        parity<double>(mixed_model<double>(true),CpuPolicy::OpenMP);
        parity<double>(mixed_model<double>(),CpuPolicy::OpenMP);
#endif
        lifetimes_and_adversary();
        if(argc==3) publication_integration(argv[1],argv[2]);
        else if(argc!=1) throw std::invalid_argument("optional fixture scratch arguments");
        std::cout<<"DBF5 consumer checks "<<checks<<" passed; FP32/FP64 TET4/HEX8/mixed 80-step trajectories exactly equal\n";
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}


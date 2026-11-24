#include <sdb/device_state_session.hpp>
#include <sdb/io/foundation_bundle.hpp>
#include "dbf9_batch_fixture.hpp"
#include <bit>
#include <iostream>

using namespace sdb;
using Pair = Field<double,2,AoSoA<4>>;
using Scalar = Field<std::int32_t>;
static void check(bool ok,const char *why) { if (!ok) throw std::runtime_error(why); }
template<class E=std::logic_error,class F> void rejects(F fn) {
    try { fn(); } catch (const E &) { return; }
    throw std::runtime_error("device access unexpectedly succeeded");
}
class FailingBackend final : public DeviceStateBackend {
    HostDeviceSimulator sim_;
  public:
    std::size_t device_allocations=0, host_allocations=0;
    bool fail_copy=false, fail_wait=false;
    std::unique_ptr<Buffer> allocate_host(std::size_t n) override { ++host_allocations; return sim_.allocate_host(n); }
    std::unique_ptr<Buffer> allocate_device(std::size_t n) override { ++device_allocations; return sim_.allocate_device(n); }
    void copy(DeviceCopy d,Buffer &a,Buffer &b,std::size_t n) override {
        if (std::exchange(fail_copy,false)) throw std::runtime_error("injected submission error");
        sim_.copy(d,a,b,n);
    }
    void wait() override { if (std::exchange(fail_wait,false)) throw std::runtime_error("injected wait error"); }
    bool host_accessible_device() const noexcept override { return true; }
};
static void device_restart(const std::filesystem::path &path) {
    using Value=Field<double>;
    namespace fx=dbf9_fixture;
    auto dofs=fx::space();
    BatchCatalog catalog({fx::batch(dofs,fx::data(dofs)),fx::batch(dofs,fx::data(dofs,1,"C3D8R",8))});
    RegisteredState target;
    target.bind_context(dofs.data().model,dofs.identity(),catalog.identity());
    FieldDescriptor d; d.name="energy";
    target.add<Value>(d,1,"energy/v1",1,{0},0); target.freeze();
    consumer::v1::FieldRequirement field{d,"energy/v1",1,0,1,{}};
    consumer::v1::bind_write<Value>(target,catalog,field).resolve()(0)=2;
    DeviceStateSession device(target,std::make_shared<HostDeviceSimulator>());
    device.sync_to_device({"energy"}); device.begin_trial_device();
    device.device_write<Value>("energy").at(0)=17; device.commit_device();
    device.sync_to_host({"energy"});
    BundleCursor cursor; cursor.step=9; cursor.consumer_schema="device/mixed";
    MultiBatchBundle::save(path,catalog,device.snapshot(),cursor);
    auto read=MultiBatchBundle::restore(path,catalog,target);
    check(read.step==9 && consumer::v1::bind_read<Value>(target,catalog,field).resolve()(0)==17,"device-to-bundle recovery");
    BatchCatalog reordered({catalog[1],catalog[0]});
    rejects<std::invalid_argument>([&]{ consumer::v1::bind_read<Value>(target,reordered,field); });
    RegisteredState other;
    other.bind_context(dofs.data().model,dofs.identity(),catalog.identity());
    d.name="different"; other.add<Value>(d,1,"energy/v1",1,{8},0); other.freeze();
    rejects<std::invalid_argument>([&]{ MultiBatchBundle::restore(path,catalog,other); });
    check(other.get<Value>("different").read()(0)==8,"mismatched registration must preserve target");
}
int main(int argc,char **argv) { try {
    if (argc==2) device_restart(argv[1]);
    auto backend=std::make_shared<FailingBackend>();
    RegisteredState input;
    input.bind_context(101,102,103);
    FieldDescriptor p; p.name="x"; p.shape={2}; p.components={"X","Y"}; p.missing=MissingPolicy::ValidityMask;
    input.add<Pair>(p,3,"position",1,{0,2},0);
    FieldDescriptor q; q.name="count"; q.scalar=ScalarType::Int32;
    input.add<Scalar>(q,1,"count",1,{6},0);
    q.name="empty"; input.add<Scalar>(q,0,"empty",1,{0},0);
    input.freeze(); input.get<Pair>("x").write()(0,0)=-0.0;
    StateSession owner(input);
    { auto writer=owner.write(); rejects([&]{ DeviceStateSession busy(owner,backend); }); }
    DeviceStateSession s(owner,backend);
    const std::size_t x_bytes=8*sizeof(double)+6, count_bytes=sizeof(std::int32_t);
    auto host=s.host_read<Pair>("x");
    check(s.residency("x")==MirrorResidency::HostLatest && reinterpret_cast<std::uintptr_t>(host.raw().data)%64==0,"aligned host");
    rejects([&]{ s.device_read<Pair>("x"); }); rejects([&]{ s.device_write<Pair>("x"); });
    rejects<std::invalid_argument>([&]{ s.host_read<Scalar>("x"); });
    rejects<std::out_of_range>([&]{ s.sync_to_device({"x","unknown"}); });
    check(s.transfers().h2d_bytes==0,"subset preflight");
    s.begin_upload("x");
    auto output=input;
    rejects([&]{ s.export_host_subset(output,{"x"}); });
    check(s.residency("x")==MirrorResidency::Uploading,"upload pending");
    rejects([&]{ host.at(0); }); rejects([&]{ s.host_write<Pair>("x"); }); rejects([&]{ s.device_read<Pair>("x"); });
    rejects([&]{ s.begin_trial_device(); }); rejects([&]{ s.begin_upload("x"); });
    backend->fail_wait=true; rejects<std::runtime_error>([&]{ s.finish_transfer("x"); });
    check(s.residency("x")==MirrorResidency::Uploading && s.transfers().h2d_bytes==0,"failed wait published upload");
    s.finish_transfer("x");
    s.sync_to_device({"count","empty","x","x"});
    check(backend->host_allocations==2,"one staging allocation per nonempty field");
    auto committed=s.device_read<Pair>("x");
    auto original=committed.raw();
    check(reinterpret_cast<std::uintptr_t>(original.data)%64==0 && original.data!=s.host_read<Pair>("x").raw().data,"isolated aligned device");
    std::array<std::byte,8*sizeof(double)> bits{};
    std::memcpy(bits.data(),original.data,bits.size());
    check(s.transfers().h2d_bytes==x_bytes+count_bytes,"upload byte accounting");
    s.begin_trial_device();
    rejects([&]{ s.export_host_subset(output,{"x"}); });
    rejects([&]{ committed.at(0); }); rejects([&]{ s.begin_trial_device(); }); rejects([&]{ s.host_write<Pair>("x"); });
    backend->fail_copy=true; rejects<std::runtime_error>([&]{ s.device_write<Pair>("x"); });
    check(s.device_read<Pair>("x").raw().data==original.data && s.transfers().d2d_bytes==0,"failed clone preserved original");
    auto trial=s.device_write<Pair>("x");
    check(trial.raw().data!=original.data && trial.validity_at(2,1)==1,"isolated trial starts with previous data/mask");
    trial.at(0,0)=1234; trial.at(2,1)=0; trial.validity_at(2,1)=0;
    rejects([&]{ s.host_read<Pair>("x"); }); rejects([&]{ s.sync_to_host({"x"}); });
    s.device_write<Pair>("x").at(0,0)=9876;
    backend->fail_wait=true; rejects<std::runtime_error>([&]{ s.commit_device(); });
    check(s.in_trial(),"failed join must retain rollback");
    s.rollback_device(); rejects([&]{ trial.at(0); });
    auto restored=s.device_read<Pair>("x");
    check(restored.raw().data==original.data && std::memcmp(restored.raw().data,bits.data(),bits.size())==0 &&
          restored.validity_at(2,1)==1 && s.residency("x")==MirrorResidency::Synced,"bitwise rollback incl padding/mask");
    const auto previous_allocations=backend->device_allocations;
    for (int step=1; step<=3; ++step) {
        s.begin_trial_device();
        s.device_write<Pair>("x").at(0,0)=step;
        s.device_write<Scalar>("count").at(0)=10+step;
        s.commit_device();
    }
    check(backend->device_allocations==previous_allocations+1,"resident trial buffers must be reused");
    check(s.transfers().d2h_bytes==0 && s.transfers().d2d_bytes==4*x_bytes+3*count_bytes,"device steps transferred to host");
    auto device_before=s.device_read<Pair>("x").raw().data;
    std::array<std::byte,8*sizeof(double)+6> resident_bits{};
    std::memcpy(resident_bits.data(),device_before,resident_bits.size());
    s.begin_trial_device();
    auto poison=s.device_write<Pair>("x");
    poison.at(0)=std::bit_cast<double>(UINT64_C(0x7ff8000000000042));
    poison.validity_at(1,1)=253;
    s.rollback_device();
    check(s.residency("x")==MirrorResidency::DeviceLatest && s.device_read<Pair>("x").raw().data==device_before &&
          std::memcmp(device_before,resident_bits.data(),resident_bits.size())==0 && s.transfers().d2h_bytes==0,
          "resident rollback changed payload, padding, validity or ownership");
    rejects([&]{ s.snapshot(); }); rejects([&]{ s.host_read<Scalar>("count"); });
    s.begin_download("x");
    check(s.residency("x")==MirrorResidency::Downloading,"download pending");
    rejects([&]{ s.device_read<Pair>("x"); }); rejects([&]{ s.host_read<Pair>("x"); });
    s.finish_transfer("x"); s.sync_to_host({"x"});
    check(s.host_read<Pair>("x").at(0)==3 && s.transfers().d2h_bytes==x_bytes,"only output field downloaded");
    check(s.residency("count")==MirrorResidency::DeviceLatest,"unselected field resident");
    rejects([&]{ s.export_host_subset(output,{"x","count"}); });
    check(output.get<Pair>("x").read()(0)==0,"failed export partially published");
    s.host_write<Pair>("x").at(2,1)=0;
    s.host_write<Pair>("x").validity_at(2,1)=0;
    s.sync_to_device({"x"});
    std::array<std::string_view,1> selected{"x"};
    s.export_host_subset(output,selected);
    check(output.get<Pair>("x").read()(0)==3 && output.find("x").validity().at(5)==0 &&
          output.get<Scalar>("count").read()(0)==6 && s.residency("count")==MirrorResidency::DeviceLatest &&
          s.transfers().d2h_bytes==x_bytes,"subset values, mask or resident field changed");
    for (auto context: {std::array<std::uint64_t,3>{1,102,103},{101,2,103},{101,102,3}}) {
        RegisteredState wrong; wrong.bind_context(context[0],context[1],context[2]); wrong.freeze();
        rejects<std::invalid_argument>([&]{ s.export_host_subset(wrong,{}); });
    }
    s.sync_to_host({"count"});
    auto snap=s.snapshot(); check(snap.get<Scalar>("count").read()(0)==13 && owner.read().field<Scalar>("count").at(0)==6,"snapshot ownership");
    auto read=s.device_read<Scalar>("count"); s.host_write<Scalar>("count").at(0)=19;
    rejects([&]{ read.at(0); }); rejects([&]{ s.device_write<Scalar>("count"); });
    s.sync_to_device({"count"}); check(s.device_read<Scalar>("count").at(0)==19,"host edit and reupload");
    s.begin_trial_device();
    auto empty=s.device_write<Scalar>("empty"); check(empty.raw().count==0,"empty field");
    s.rollback_device(); rejects([&]{ s.rollback_device(); });
    s.device_write<Pair>("x").at(0)=std::numeric_limits<double>::infinity();
    s.sync_to_host({"x"});
    rejects<std::invalid_argument>([&]{ s.snapshot(); });
    rejects<std::invalid_argument>([&]{ s.export_host_subset(output,{"x"}); });
    s.export_host_subset(output,{"count"});
    s.export_host_subset(output,{"x"},CommitValidation::FastShapeOnly);
    check(std::isinf(output.get<Pair>("x").read()(0)) && backend->host_allocations==2,"subset mode or staging reuse");
    std::cout << "field_bytes=" << x_bytes << " output_step_d2h=" << x_bytes << " resident_steps=3 PASS\n";
    return 0;
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }

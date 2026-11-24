#include <sdb/device_state_session.hpp>
#include <iostream>

__global__ void set_first(sdb::View<double,1,sdb::SoA> field,double value) { field(0)=value; }
int main() { try {
    using F=sdb::Field<double>;
    sdb::FieldDescriptor d; d.name="x";
    sdb::RegisteredState state; state.add<F>(d,1,"gpu-test",1,{7},0); state.freeze();
    auto ctx=std::make_shared<sdb::cuda::Context>();
    sdb::DeviceStateSession session(state,std::make_shared<sdb::CudaStateBackend>(ctx));
    session.sync_to_device({"x"});
    for (bool commit:{false,true}) {
        session.begin_trial_device();
        set_first<<<1,1,0,ctx->stream()>>>(session.device_write<F>("x").raw(),9);
        sdb::cuda::check(cudaGetLastError(),"set_first");
        if (commit) session.commit_device(); else session.rollback_device();
        session.sync_to_host({"x"});
        if (session.host_read<F>("x").at(0)!=(commit?9:7)) return 1;
    }
    return session.transfers().d2h_bytes==sizeof(double) ? 0 : 1;
} catch (const std::exception &e) { std::cerr << e.what()<<'\n'; return 1; } }

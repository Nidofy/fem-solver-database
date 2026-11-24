#include <sdb/device_state_session.hpp>
#include <iostream>

__global__ void set_first(sdb::View<double,1,sdb::SoA> field,double value) { field(0)=value; }
__global__ void read_first(sdb::View<const double,1,sdb::SoA> src,double *dst) { *dst=src(0); }
int main() { try {
    using F=sdb::Field<double>;
    sdb::FieldDescriptor d; d.name="x";
    sdb::RegisteredState state; state.add<F>(d,1,"gpu-test",1,{7},0); state.freeze();
    auto ctx=std::make_shared<sdb::cuda::Context>();
    sdb::DeviceStateSession session(state,std::make_shared<sdb::CudaStateBackend>(ctx));
    session.sync_to_device({"x"});
    sdb::cuda::DeviceBuffer<double> probe(ctx,1);
    sdb::cuda::PinnedBuffer<double> observed(1);
    for (bool commit:{false,true}) {
        session.begin_trial_device();
        set_first<<<1,1,0,ctx->stream()>>>(session.device_write<F>("x").raw(),9);
        sdb::cuda::check(cudaGetLastError(),"set_first");
        if (commit) session.commit_device(); else session.rollback_device();
        read_first<<<1,1,0,ctx->stream()>>>(session.device_read<F>("x").raw(),probe.data());
        sdb::cuda::check(cudaGetLastError(),"read_first");
        sdb::cuda::check(cudaMemcpyAsync(observed.data(),probe.data(),sizeof(double),cudaMemcpyDeviceToHost,
                                         ctx->stream()),"read committed GPU probe");
        ctx->wait();
        if (*observed.data()!=(commit?9:7)) return 1;
        session.sync_to_host({"x"});
        if (session.host_read<F>("x").at(0)!=(commit?9:7)) return 1;
    }
    return session.transfers().d2h_bytes==sizeof(double) ? 0 : 1;
} catch (const std::exception &e) { std::cerr << e.what()<<'\n'; return 1; } }

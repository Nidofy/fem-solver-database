#include "sdb/cuda_contact.hpp"
#include <cstddef>
#include <iostream>
using namespace sdb;
static_assert(detail::has_visit<std::remove_cvref_t<const NodeId &>>);
static_assert(!detail::has_visit<std::uint32_t>);
__global__ void append_candidates(cuda::CandidateView view) {
    auto i = blockIdx.x * blockDim.x + threadIdx.x;
    view.append({{100, 0, 1}, {200, 0, -1}, i, 0});
}
__global__ void update_manifold(cuda::ManifoldView<double> view) {
    auto i = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i < view.count)
        view[i].gap = -.5;
}
int main() {
    try {
        auto ctx = std::make_shared<cuda::Context>();
        cuda::Candidates candidates(ctx, 100);
        append_candidates<<<1, 128, 0, ctx->stream()>>>(candidates.view());
        cuda::check(cudaGetLastError(), "append test launch");
        std::size_t dropped = 0;
        auto keys = candidates.download(dropped);
        if (keys.size() != 100 || dropped != 28)
            throw std::runtime_error("CUDA bounded append");
        // Inspect device bytes before any host-side ContactKey copies can change padding.
        std::vector<unsigned char> bytes(keys.size() * sizeof(ContactKey));
        cuda::check(cudaMemcpy(bytes.data(), candidates.view().entries, bytes.size(), cudaMemcpyDeviceToHost),
                    "read candidate object representations");
        for (std::size_t i = 0; i < keys.size(); ++i) {
            const auto &key = keys[i];
            if (key.a != FaceId{100, 0, 1} || key.b != FaceId{200, 0, -1} ||
                key.feature_a >= 128 || key.feature_b != 0)
                throw std::runtime_error("CUDA candidate identity");
            for (auto face : {offsetof(ContactKey, a), offsetof(ContactKey, b)})
                for (auto pad = offsetof(FaceId, side) + sizeof(FaceId::side); pad < sizeof(FaceId); ++pad)
                    if (bytes[i * sizeof(ContactKey) + face + pad] != 0)
                        throw std::runtime_error("CUDA candidate padding");
        }
        ContactDB<double> db;
        auto handle = db.touch({{100, 0, 1}, {200, 0, -1}, 0, 0}, 1);
        cuda::Manifolds<double> manifolds(ctx, db);
        update_manifold<<<1, 32, 0, ctx->stream()>>>(manifolds.view());
        cuda::check(cudaGetLastError(), "manifold test launch");
        manifolds.download(db);
        if (db.get(handle).gap != -.5)
            throw std::runtime_error("CUDA manifold history");
        // Destruction while a transfer is pending must retain the pinned source until completion.
        {
            cuda::Mirror<double> pending(ctx, 100000);
            pending.host_write()(0) = 7;
            pending.upload();
        }
        ctx->wait();
        if (cuda::cleanup_errors.load())
            throw std::runtime_error("CUDA cleanup error");
        std::cout << "PASS CUDA candidates, manifolds, async teardown\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

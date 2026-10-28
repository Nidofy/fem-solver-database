#pragma once
#include "../lifecycle.hpp"

namespace sdb::lifecycle {
// Optional companion manifest, not a replacement ModelStore/ResultDB/Restart.
// Legacy checkpoints still load; safe append requires a published link. A new
// output file can branch from any valid legacy checkpoint.
struct CheckpointLink {
    std::uint32_t schema = 1;
    std::uint64_t definition = 0, configuration = 0, total_increment = 0;
    std::uint64_t result_bytes = 0, result_hash = 0, restart_bytes = 0, restart_hash = 0;
    template<class F> void visit(F f) {
        f(schema,definition,configuration,total_increment,result_bytes,result_hash,restart_bytes,restart_hash);
    }
    static std::filesystem::path path(const std::filesystem::path &restart) {
        auto out=restart; out += ".result-link"; return out;
    }
    // Constant-memory FNV-1a fingerprint for accidental mismatch/corruption,
    // NOT authentication. Bound exact file bytes, including header and records.
    static std::uint64_t fingerprint(const std::filesystem::path &file, std::uint64_t count) {
        if (count > 64ull*1024*1024*1024 || count > std::filesystem::file_size(file))
            throw std::length_error("linked file size outside budget");
        std::ifstream in(file,std::ios::binary);
        std::array<std::uint8_t,65536> buffer{};
        std::uint64_t h=14695981039346656037ull;
        while (count) {
            auto n=static_cast<std::size_t>(std::min<std::uint64_t>(count,buffer.size()));
            native::read(in,std::span<std::uint8_t>(buffer.data(),n));
            for (std::size_t i=0;i<n;++i) { h^=buffer[i]; h*=1099511628211ull; }
            count-=n;
        }
        return h;
    }
    // Caller must hold the result writer lease and first drain/flush its writer.
    // Publish order: result prefix -> atomic restart -> atomic link. Interrupted
    // publication is refused on append; it never authorizes a mismatched pair.
    template<class T>
    static void publish(const std::filesystem::path &result, const std::filesystem::path &restart,
                        CpuRun<T> &run, const CompiledModel<T> &model) {
        auto same = [](const auto &a, const auto &b) {
            return std::filesystem::weakly_canonical(a)==std::filesystem::weakly_canonical(b) ||
                   (std::filesystem::exists(a) && std::filesystem::exists(b) && std::filesystem::equivalent(a,b));
        };
        if (same(result,restart) || same(result,path(restart)))
            throw std::invalid_argument("checkpoint and result destinations must be distinct");
        CheckpointLink link;
        link.definition=ModelStore::identity(model);
        link.configuration=run.configuration();
        link.total_increment=run.state().data().step;
        link.result_bytes=std::filesystem::file_size(result);
        link.result_hash=fingerprint(result,link.result_bytes);
        run.save(restart);
        link.restart_bytes=std::filesystem::file_size(restart);
        link.restart_hash=fingerprint(restart,link.restart_bytes);
        native::atomic_stream(path(restart),[&](auto &out) {
            native::prefix(out,0x314b4e494c424453ull);
            native::record(out,0,native::pack(link));
        });
    }
    // Called inside NativeResultStorage's lease, BEFORE append/recovery mutation.
    template<class T>
    static void verify(const std::filesystem::path &result, const std::filesystem::path &restart,
                       const CpuRun<T> &run, const CompiledModel<T> &model, bool recover_tail=false) {
        if (!std::filesystem::exists(path(restart)))
            throw std::runtime_error("restart has no result link; branch to a new output file");
        std::ifstream in(path(restart),std::ios::binary);
        native::prefix(in,0x314b4e494c424453ull);
        auto link=native::unpack<CheckpointLink>(native::record(in,0,1024),1024);
        native::end(in);
        if (link.schema!=1 || link.definition!=ModelStore::identity(model) ||
            link.configuration!=run.configuration() || link.total_increment!=run.state().data().step ||
            link.restart_bytes!=std::filesystem::file_size(restart) ||
            link.restart_hash!=fingerprint(restart,link.restart_bytes))
            throw std::runtime_error("checkpoint/link identity mismatch; branch to a new output file");
        if (!std::filesystem::exists(result))
            throw std::runtime_error("linked result file missing; branch to a new output file");
        auto size=std::filesystem::file_size(result);
        if (size!=link.result_bytes) {
            if (recover_tail && size>link.result_bytes) {
                ResultReader prefix(result,true);
                if (!prefix.incomplete_tail || prefix.valid_end!=link.result_bytes)
                    throw std::runtime_error("result ahead of checkpoint or linked prefix mismatch");
            } else
                throw std::runtime_error(size>link.result_bytes ? "result ahead of checkpoint linked prefix"
                                                               : "checkpoint ahead of result linked prefix");
        }
        if (link.result_hash!=fingerprint(result,link.result_bytes))
            throw std::runtime_error("result/checkpoint prefix content mismatch");
    }
};
} // namespace sdb::lifecycle

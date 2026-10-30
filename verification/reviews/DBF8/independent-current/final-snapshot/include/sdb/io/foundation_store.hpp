#pragma once
#include "../dof_space.hpp"

namespace sdb {
// One standalone envelope, reusing the native CRC and registered-field checkpoint protocol.
// Caller registers the expected exact schemas/layout first; no runtime type guessing on restore.
class FoundationStore {
    static constexpr std::uint64_t magic=0x31444e554f464453ull;
  public:
    static void save(const std::filesystem::path &path,const DofSpace &space,const RegisteredState &state,
                     std::uint64_t budget=lifecycle::native::default_limit) {
        namespace n=lifecycle::native;
        if(state.in_trial()) throw std::logic_error("cannot save trial foundation");
        if(state.model_context()!=space.data().model || state.equation_context()!=space.identity())
            throw std::invalid_argument("foundation state/DOF context mismatch");
        state.validate();
        auto dofs=space.encode(budget);
        // Budget is encoded file bytes, excluding the fixed 12-byte prefix. Preflight before publication.
        std::uint64_t used=sum(dofs.size(),16);
        auto charge=[&](std::uint32_t,std::span<const std::uint8_t> b) {
            if(used>budget || b.size()>budget-used || 16>budget-used-b.size()) throw std::length_error("foundation file budget");
            used+=b.size()+16;
        };
        checkpoint_detail::write_registered(state,charge);
        n::atomic_stream(path,[&](auto &out) {
            n::prefix(out,magic); n::record(out,1,dofs);
            checkpoint_detail::write_registered(state,[&](auto kind,const auto &b){ n::record(out,kind,b); });
        });
    }
    static DofSpace inspect_space(const std::filesystem::path &path,std::uint64_t budget=lifecycle::native::default_limit) {
        namespace n=lifecycle::native;
        std::ifstream in(path,std::ios::binary); n::prefix(in,magic);
        return DofSpace::decode(n::record(in,1,budget),budget); // metadata only; does not authorize restart
    }
    static void restore(const std::filesystem::path &path,const DofSpace &expected,RegisteredState &state,
                        std::uint64_t budget=lifecycle::native::default_limit) {
        namespace n=lifecycle::native;
        if(state.in_trial()) throw std::logic_error("cannot restore active trial");
        if(state.model_context()!=expected.data().model || state.equation_context()!=expected.identity())
            throw std::invalid_argument("foundation target context mismatch");
        std::ifstream in(path,std::ios::binary); n::prefix(in,magic);
        std::uint64_t remaining=budget;
        auto read=[&](std::uint32_t kind,std::uint64_t limit) {
            if(remaining<16) throw std::length_error("foundation file budget");
            auto bytes=n::record(in,kind,std::min(limit,remaining-16)); remaining-=bytes.size()+16; return bytes;
        };
        auto bytes=read(1,budget); auto actual=DofSpace::decode(bytes,budget);
        if(actual.encode(budget)!=expected.encode(budget)) throw std::invalid_argument("foundation DOF/model identity mismatch");
        auto staged=state.initialized_copy();
        checkpoint_detail::read_registered(staged,read); n::end(in);
        state=std::move(staged); // all throwing checks complete before publication
    }
};
} // namespace sdb

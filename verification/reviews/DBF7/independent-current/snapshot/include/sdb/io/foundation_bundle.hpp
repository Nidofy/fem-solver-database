#pragma once
#include "../consumer_contract.hpp"

namespace sdb {
struct StateFieldInfo {
    FieldDescriptor descriptor;
    std::uint64_t rows=0,basis=0;
    std::string schema;
    std::uint32_t schema_version=1,layout_kind=0,layout_width=0;
    bool addressed=false;
    StateLayout addressing;
    lifecycle::native::Bytes initial_scalars; // canonical little-endian, descriptor scalar type
    template<class F> void visit(F f) { f(descriptor,rows,basis,schema,schema_version,layout_kind,layout_width,addressed,addressing,initial_scalars); }
};
struct BundleCursor {
    std::uint32_t version=1;
    std::uint64_t step=0,increment=0;
    double time=0;
    std::string consumer_schema;
    template<class F> void visit(F f) { f(version,step,increment,time,consumer_schema); }
    void validate() const {
        if(version!=1 || !std::isfinite(time) || time<0 || consumer_schema.empty()) throw std::invalid_argument("bundle cursor");
    }
};
struct BundleInspection {
    consumer::v1::Batch batch;
    BundleCursor cursor;
    std::vector<StateFieldInfo> fields;
};
class FoundationBundle {
    static constexpr std::uint64_t magic=0x314c444e55424453ull;
    static std::vector<StateFieldInfo> catalog(const RegisteredState &state) {
        std::vector<StateFieldInfo> out;
        for(auto e:state.entries()) {
            StateFieldInfo v;
            v.descriptor=e->descriptor(); v.rows=e->count(); v.basis=e->basis_id(); v.schema=e->schema_id();
            v.schema_version=e->schema_version(); v.layout_kind=e->layout_kind(); v.layout_width=e->layout_width();
            v.initial_scalars=e->initialization(); v.addressed=e->addressing().has_value();
            if(v.addressed) v.addressing=*e->addressing();
            out.push_back(std::move(v));
        }
        return out;
    }
    static void context(const consumer::v1::Batch &batch,const RegisteredState &state) {
        if(state.in_trial() || state.model_context()!=batch.dofs().data().model ||
           state.equation_context()!=batch.dofs().identity() || state.consumer_context()!=batch.identity())
            throw std::invalid_argument("bundle state/batch context or active trial");
        state.validate();
    }
  public:
    static void save(const std::filesystem::path &path,const consumer::v1::Batch &batch,const RegisteredState &state,
                     const BundleCursor &cursor,std::uint64_t budget=lifecycle::native::default_limit) {
        namespace n=lifecycle::native;
        context(batch,state); cursor.validate();
        auto b=batch.encode(budget), c=n::pack(cursor,budget), fields=n::pack(catalog(state),budget);
        std::uint64_t used=0;
        auto charge=[&](std::uint32_t,std::span<const std::uint8_t> bytes) {
            if(used>budget || budget-used<16 || bytes.size()>budget-used-16) throw std::length_error("bundle file budget");
            used+=bytes.size()+16;
        };
        charge(1,b); charge(2,c); charge(3,fields); checkpoint_detail::write_registered(state,charge);
        n::atomic_stream(path,[&](auto &out) {
            n::prefix(out,magic); n::record(out,1,b); n::record(out,2,c); n::record(out,3,fields);
            checkpoint_detail::write_registered(state,[&](auto kind,const auto &bytes){n::record(out,kind,bytes);});
        });
    }
    // Metadata inspection only. Does not certify state blocks or authorize restart.
    static BundleInspection inspect(const std::filesystem::path &path,std::uint64_t budget=lifecycle::native::default_limit) {
        namespace n=lifecycle::native;
        std::ifstream in(path,std::ios::binary); n::prefix(in,magic);
        auto read=[&](std::uint32_t kind) {
            if(budget<16) throw std::length_error("bundle file budget");
            auto b=n::record(in,kind,budget-16); budget-=b.size()+16; return b;
        };
        const auto decode_budget=budget;
        auto batch=consumer::v1::Batch::decode(read(1),decode_budget);
        auto cursor=n::unpack<BundleCursor>(read(2),decode_budget); cursor.validate();
        auto fields=n::unpack<std::vector<StateFieldInfo>>(read(3),decode_budget);
        std::set<std::string> names;
        for(auto &f:fields) {
            f.descriptor.validate();
            if(f.schema.empty() || !f.schema_version || !names.insert(f.descriptor.name).second || f.layout_kind>1 ||
               (f.layout_kind==0 && f.layout_width) || (f.layout_kind==1 && !f.layout_width)) throw std::invalid_argument("bundle field catalog");
            if(f.addressed) {f.addressing.validate(f.descriptor); if(f.rows!=f.addressing.addresses.size()) throw std::invalid_argument("bundle field rows");}
            auto width=std::visit([](const auto &v){return sizeof(typename std::decay_t<decltype(v)>::value_type);},lifecycle::empty_typed_values(f.descriptor.scalar));
            if(f.initial_scalars.size()!=product(width,f.descriptor.component_count())) throw std::invalid_argument("bundle initializer shape");
        }
        return {std::move(batch),std::move(cursor),std::move(fields)};
    }
    static BundleCursor restore(const std::filesystem::path &path,const consumer::v1::Batch &expected,
                                RegisteredState &state,std::uint64_t budget=lifecycle::native::default_limit) {
        namespace n=lifecycle::native;
        context(expected,state);
        std::ifstream in(path,std::ios::binary); n::prefix(in,magic);
        auto remaining=budget;
        auto read=[&](std::uint32_t kind,std::uint64_t limit) {
            if(remaining<16) throw std::length_error("bundle file budget");
            auto bytes=n::record(in,kind,std::min(limit,remaining-16)); remaining-=bytes.size()+16; return bytes;
        };
        if(read(1,budget)!=expected.encode(budget)) throw std::invalid_argument("bundle metadata mismatch");
        auto cursor=n::unpack<BundleCursor>(read(2,budget),budget); cursor.validate();
        if(read(3,budget)!=n::pack(catalog(state),budget)) throw std::invalid_argument("bundle expected registration mismatch");
        auto staged=state.initialized_copy(); checkpoint_detail::read_registered(staged,read); n::end(in);
        state=std::move(staged);
        return cursor;
    }
};
} // namespace sdb

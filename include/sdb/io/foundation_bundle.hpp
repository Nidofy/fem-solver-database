#pragma once
#include "../batch_catalog.hpp"

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
    friend class MultiBatchBundle;
    static void validate_fields(const std::vector<StateFieldInfo> &fields) {
        std::set<std::string> names;
        for(const auto &f:fields) {
            f.descriptor.validate();
            if(f.schema.empty() || !f.schema_version || !names.insert(f.descriptor.name).second || f.layout_kind>1 ||
               (f.layout_kind==0 && f.layout_width) || (f.layout_kind==1 && !f.layout_width)) throw std::invalid_argument("bundle field catalog");
            if(f.addressed) {f.addressing.validate(f.descriptor); if(f.rows!=f.addressing.addresses.size()) throw std::invalid_argument("bundle field rows");}
            auto width=std::visit([](const auto &v){return sizeof(typename std::decay_t<decltype(v)>::value_type);},lifecycle::empty_typed_values(f.descriptor.scalar));
            if(f.initial_scalars.size()!=product(width,f.descriptor.component_count())) throw std::invalid_argument("bundle initializer shape");
        }
    }
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
    static void context(const auto &batch,const RegisteredState &state) {
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
        validate_fields(fields);
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

struct MultiBundleInspection {
    BatchCatalog catalog;
    BundleCursor cursor;
    std::vector<StateFieldInfo> fields;
};

class MultiBatchBundle {
    using Bytes = lifecycle::native::Bytes;
    using Batch = consumer::v1::Batch;
    // Separate magic and record kinds. The prefix uses the existing index-width epoch.
    static constexpr std::uint64_t magic = UINT64_C(0x314c554d424453);
    struct Header {
        std::uint32_t version = 1;
        std::uint64_t model=0, equations=0, catalog=0, cursor_hash=0, fields_hash=0;
        std::vector<std::uint64_t> children;
        template<class F> void visit(F f) { f(version,model,equations,catalog,cursor_hash,fields_hash,children); }
    };
    struct Child {
        consumer::v1::BatchData data;
        std::vector<lifecycle::MaterialDefinition> materials;
        std::vector<consumer::v1::SectionDefinition> sections;
        template<class F> void visit(F f) { f(data,materials,sections); }
    };
    struct ChildView {
        const Batch &child;
        template<class F> void visit(F f) const {
            f(child.data(),child.materials().definitions(),child.sections().definitions());
        }
    };
    struct Input {
        std::ifstream in;
        std::uint64_t remaining;
        Input(const std::filesystem::path &path,std::uint64_t budget) : in(path,std::ios::binary), remaining(budget) {
            if (remaining < 12) throw std::length_error("multi-bundle prefix budget");
            lifecycle::native::prefix(in,magic); remaining -= 12;
        }
        Bytes read(std::uint32_t kind,std::uint64_t limit) {
            if (remaining < 16) throw std::length_error("multi-bundle record budget");
            auto bytes = lifecycle::native::record(in,kind,std::min(limit,remaining-16));
            remaining -= 16 + bytes.size();
            return bytes;
        }
    };
    static MultiBundleInspection metadata(Input &input,std::uint64_t budget,const BatchCatalog *expected) {
        namespace n = lifecycle::native;
        auto h = n::unpack<Header>(input.read(11,budget),budget);
        if (h.version != 1 || h.children.empty() || h.children.size() > input.remaining / 16 ||
            h.children.size() > budget / sizeof(Batch))
            throw std::invalid_argument("multi-bundle catalog header");
        if (expected && (h.children != expected->identities() || h.catalog != expected->identity()))
            throw std::invalid_argument("multi-bundle expected catalog");
        std::shared_ptr<const DofSpace> space;
        {
            auto wire = input.read(12,budget);
            if (expected) {
                if (wire != expected->dofs().encode(budget)) throw std::invalid_argument("multi-bundle expected DOF space");
                space = (*expected)[0].dofs_;
            } else space = std::make_shared<const DofSpace>(DofSpace::decode(wire,budget));
        }
        if (h.model != space->data().model || h.equations != space->identity())
            throw std::invalid_argument("multi-bundle DOF identity");
        std::vector<Batch> children;
        if (!expected) children.reserve(h.children.size());
        for (std::size_t i = 0; i < h.children.size(); ++i) {
            auto bytes = input.read(13,budget);
            if (expected) {
                if (bytes != n::pack(ChildView{(*expected)[i]},budget))
                    throw std::invalid_argument("multi-bundle child metadata mismatch");
            } else {
                auto child = n::unpack<Child>(bytes,budget);
                children.push_back(Batch(Batch::SharedSpace{},std::move(child.data),space,
                    consumer::v1::MaterialCatalog(std::move(child.materials)),
                    consumer::v1::SectionCatalog(std::move(child.sections))));
                if (children.back().identity() != h.children[i])
                    throw std::invalid_argument("multi-bundle child order/identity");
            }
        }
        auto catalog = expected ? *expected : BatchCatalog(std::move(children));
        if (catalog.identity() != h.catalog) throw std::invalid_argument("multi-bundle aggregate identity");
        auto cursor_bytes = input.read(14,budget);
        auto field_bytes = input.read(15,budget);
        if (n::hash(cursor_bytes) != h.cursor_hash || n::hash(field_bytes) != h.fields_hash)
            throw std::invalid_argument("multi-bundle metadata fingerprint");
        auto cursor = n::unpack<BundleCursor>(cursor_bytes,budget);
        cursor.validate();
        auto fields = n::unpack<std::vector<StateFieldInfo>>(field_bytes,budget);
        FoundationBundle::validate_fields(fields);
        return {std::move(catalog),std::move(cursor),std::move(fields)};
    }
  public:
    static void save(const std::filesystem::path &path,const BatchCatalog &catalog,const RegisteredState &state,
                     const BundleCursor &cursor,std::uint64_t budget=lifecycle::native::default_limit) {
        namespace n = lifecycle::native;
        FoundationBundle::context(catalog,state);
        cursor.validate();
        auto c = n::pack(cursor,budget), fields = n::pack(FoundationBundle::catalog(state),budget);
        Header h{1,catalog.dofs().data().model,catalog.dofs().identity(),catalog.identity(),
                 n::hash(c),n::hash(fields),catalog.identities()};
        if (budget < 12) throw std::length_error("multi-bundle prefix budget");
        std::uint64_t remaining = budget-12;
        n::atomic_stream(path,[&](auto &out) {
            n::prefix(out,magic);
            auto record = [&](std::uint32_t kind,const Bytes &bytes) {
                if (remaining < 16 || bytes.size() > remaining-16) throw std::length_error("multi-bundle file budget");
                n::record(out,kind,bytes);
                remaining -= bytes.size()+16;
            };
            record(11,n::pack(h,remaining));
            record(12,catalog.dofs().encode(remaining));
            for (std::size_t i=0; i<catalog.size(); ++i) record(13,n::pack(ChildView{catalog[i]},remaining));
            record(14,c); record(15,fields);
            checkpoint_detail::write_registered(state,record);
        });
    }
    // Reads metadata through the field catalog. Restore verifies the state records and EOF.
    static MultiBundleInspection inspect(const std::filesystem::path &path,std::uint64_t budget=lifecycle::native::default_limit) {
        Input input(path,budget);
        return metadata(input,budget,nullptr);
    }
    static BundleCursor restore(const std::filesystem::path &path,const BatchCatalog &expected,RegisteredState &state,
                                std::uint64_t budget=lifecycle::native::default_limit) {
        namespace n = lifecycle::native;
        FoundationBundle::context(expected,state);
        Input input(path,budget);
        auto info = metadata(input,budget,&expected);
        if (n::pack(info.fields,budget) != n::pack(FoundationBundle::catalog(state),budget))
            throw std::invalid_argument("multi-bundle expected registration mismatch");
        auto staged = state.initialized_copy();
        checkpoint_detail::read_registered(staged,[&](auto kind,auto limit){ return input.read(kind,limit); });
        n::end(input.in);
        state = std::move(staged);
        return std::move(info.cursor);
    }
};
} // namespace sdb

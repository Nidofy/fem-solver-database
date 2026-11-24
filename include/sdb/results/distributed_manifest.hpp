#pragma once
#include "../io/native.hpp"
#include "../io/crc64.hpp"
#include "../model_ir/model_ir.hpp"
#include "../registered_state.hpp"
#include <map>

namespace sdb {
using GlobalLabel=std::uint64_t;
struct OwnedSourceMap {
    std::vector<GlobalLabel> node_labels,element_labels; // local row order
    template<class F> void visit(F f) { f(node_labels,element_labels); }
    bool operator==(const OwnedSourceMap &) const=default;
    static OwnedSourceMap from(const lifecycle::SourceMap &sources) {
        OwnedSourceMap out;
        for(const auto &s:sources.nodes) out.node_labels.push_back(s.key.label);
        for(const auto &s:sources.elements) out.element_labels.push_back(s.key.label);
        out.sorted(); return out;
    }
    OwnedSourceMap sorted() const {
        auto copy=*this;
        for(auto v:{&copy.node_labels,&copy.element_labels}) {
            std::ranges::sort(*v);
            if((!v->empty() && !v->front()) || std::ranges::adjacent_find(*v)!=v->end())
                throw std::invalid_argument("zero or duplicate owned global label");
        }
        return copy;
    }
    const auto &labels(Association a) const {
        if(a==Association::Node) return node_labels;
        if(a==Association::Element) return element_labels;
        throw std::invalid_argument("partition field requires node or element association");
    }
};
struct PartitionSample {
    std::uint64_t step=0,increment=0;
    double time=0;
    bool operator==(const PartitionSample &) const=default;
    template<class F> void visit(F f) { f(step,increment,time); }
};
struct PartitionEpoch {
    std::uint64_t model=0,constraint_contract=0;
    std::string consumer_schema;
    std::vector<PartitionSample> samples; // last entry is the checkpoint cursor
    template<class F> void visit(F f) { f(model,constraint_contract,consumer_schema,samples); }
    bool operator==(const PartitionEpoch &) const=default;
    void validate() const {
        if(!model || consumer_schema.empty() || samples.empty()) throw std::invalid_argument("empty partition epoch");
        for(std::size_t i=0;i<samples.size();++i) {
            const auto &s=samples[i];
            if(!std::isfinite(s.time) || s.time<0) throw std::invalid_argument("invalid partition sample time");
            if(i) {
                const auto &p=samples[i-1];
                if(s.time<p.time || std::tie(s.step,s.increment)<=std::tie(p.step,p.increment))
                    throw std::invalid_argument("partition step/time axis order");
            }
        }
    }
};
enum class PartitionFile : std::uint8_t { Results=1, Checkpoint=2 };
enum class PartitionFiles : std::uint8_t { Results=1, Checkpoint=2, Both=3 };
namespace partition_detail {
namespace n=lifecycle::native;
using Path=std::filesystem::path;
using Address=lifecycle::ResultAddress;
inline void require(bool ok,const char *reason) { if(!ok) throw std::invalid_argument(reason); }
inline constexpr std::uint64_t manifest_magic=0x31464e4d424453ull;
inline constexpr std::uint64_t result_magic=0x31535250424453ull;
inline constexpr std::uint64_t checkpoint_magic=0x31544350424453ull;
inline std::uint64_t magic(PartitionFile kind) { return kind==PartitionFile::Results?result_magic:checkpoint_magic; }
inline std::string filename(std::uint32_t rank,PartitionFile kind) {
    return "part_"+std::to_string(rank)+(kind==PartitionFile::Results?".sdbres":".sdbrst");
}
using io::crc64;
// Fixed-width prefix is independent of the local SDK Index ABI.
inline void prefix(std::ostream &out,std::uint64_t id) { n::write(out,n::pack(std::pair(id,std::uint32_t(2)))); }
inline std::uint32_t prefix(std::istream &in,std::uint64_t id) {
    std::array<std::uint8_t,12> bytes; n::read(in,bytes);
    const auto [magic,version]=n::unpack<std::pair<std::uint64_t,std::uint32_t>>(bytes);
    require(magic==id && (version==1 || version==2),"partition magic/version");
    return version;
}
struct FieldSchema {
    FieldDescriptor field;
    std::string schema;
    std::uint32_t version=0,layout=0,width=0;
    std::uint64_t basis=0;
    n::Bytes initial;
    template<class F> void visit(F f) { f(field,schema,version,layout,width,basis,initial); }
    std::size_t row_bytes() const {
        const auto scalar=std::visit([](const auto &v){return sizeof(typename std::decay_t<decltype(v)>::value_type);},
                                     lifecycle::empty_typed_values(field.scalar));
        return product(field.component_count(),scalar+(field.missing==MissingPolicy::ValidityMask));
    }
    void validate() const {
        field.validate();
        require((field.association==Association::Node || field.association==Association::Element || field.association==Association::Global) &&
                version && !schema.empty() && layout<=1 && (layout==0 ? width==0 : width>0),"partition field schema");
        require(initial.size()==row_bytes()-(field.missing==MissingPolicy::ValidityMask?field.component_count():0),
                "partition initializer size");
    }
};
inline std::vector<FieldSchema> fields(const RegisteredState &state) {
    std::vector<FieldSchema> out;
    for(auto e:state.entries()) out.push_back({e->descriptor(),e->schema_id(),e->schema_version(),
                                               e->layout_kind(),e->layout_width(),e->basis_id(),e->initialization()});
    return out;
}
inline Address address(const RegisteredFieldBase &e,const OwnedSourceMap &map,std::size_t row) {
    if(e.descriptor().association==Association::Global) require(e.count()==1,"global singleton count");
    if(e.addressing()) return e.addressing()->addresses.at(row);
    if(e.descriptor().association==Association::Global) return {};
    const auto &keys=map.labels(e.descriptor().association);
    require(e.descriptor().location==FieldLocation::Entity && e.count()==keys.size(),"implicit partition field requires one row per owned label");
    Address a; a.entity=keys.at(row); return a;
}
struct Row {
    std::uint64_t sample=0,field=0;
    Address address;
    std::uint32_t crc=0;
    template<class F> void visit(F f) { f(sample,field,address,crc); }
};
struct Header {
    PartitionFile kind=PartitionFile::Results;
    std::uint32_t rank=0,parts=0;
    std::uint64_t local_equation_context=0;
    PartitionEpoch epoch;
    OwnedSourceMap owned;
    std::vector<FieldSchema> fields;
    std::vector<Row> rows;
    template<class F> void visit(F f) { f(kind,rank,parts,local_equation_context,epoch,owned,fields,rows); }
};
// In the candidate v1 wire format every source rank was bound to epoch.dofs.
struct LegacyHeader : Header {
    template<class F> void visit(F f) { f(kind,rank,parts,epoch,owned,fields,rows); }
};
struct ShardIndex {
    Header header;
    std::vector<std::uint64_t> offsets;
    std::uint64_t bytes=0,index_crc=0;
};
inline ShardIndex index(const Path &path,PartitionFile kind) {
    std::ifstream in(path,std::ios::binary); const auto version=prefix(in,magic(kind));
    auto payload=n::record(in,1);
    ShardIndex idx;
    idx.index_crc=crc64(payload);
    auto &h=idx.header;
    if(version==1) {
        h=n::unpack<LegacyHeader>(payload);
        h.local_equation_context=h.epoch.constraint_contract;
    } else h=n::unpack<Header>(payload);
    require(h.kind==kind && h.parts && h.rank<h.parts,"partition rank/type");
    h.epoch.validate();
    const auto owned=h.owned.sorted();
    std::set<std::string> names;
    std::vector<std::size_t> sizes;
    for(const auto &s:h.fields) {
        s.validate(); require(names.insert(s.field.name).second,"duplicate shard field");
        sizes.push_back(s.row_bytes());
    }
    std::vector<std::size_t> order;
    order.reserve(h.rows.size());
    std::vector<std::size_t> global_counts(h.fields.size());
    auto key=[&](std::size_t i) { const auto &r=h.rows[i]; return std::tie(r.sample,r.field,r.address); };
    std::uint64_t pos=static_cast<std::uint64_t>(in.tellg());
    for(const auto &r:h.rows) {
        require(r.field<h.fields.size() && r.sample<h.epoch.samples.size() &&
                (kind==PartitionFile::Results || r.sample==h.epoch.samples.size()-1),"shard row cursor/field");
        const auto &d=h.fields[r.field].field;
        StateLayout::validate_address(r.address,d.association,d.location);
        if(d.association==Association::Global) ++global_counts[r.field];
        else require(std::ranges::binary_search(owned.labels(d.association),r.address.entity),"shard row outside owned source map");
        order.push_back(order.size());
        idx.offsets.push_back(pos);
        if(sizes[r.field]>UINT64_MAX-pos) throw std::length_error("shard extent overflow");
        pos+=sizes[r.field];
    }
    std::ranges::sort(order,{},key);
    require(std::ranges::adjacent_find(order,{},key)==order.end(),"duplicate shard sample row");
    for(std::size_t f=0;f<h.fields.size();++f) if(h.fields[f].field.association==Association::Global)
        require(global_counts[f]==(kind==PartitionFile::Results?h.epoch.samples.size():1),"missing global singleton row");
    idx.bytes=std::filesystem::file_size(path);
    require(idx.bytes==pos,"missing, truncated or extended partition payload");
    return idx;
}
inline void read_row_into(std::ifstream &in,const ShardIndex &idx,std::size_t row,n::Bytes &bytes,std::uint64_t &next) {
    const auto &r=idx.header.rows.at(row);
    const auto offset=idx.offsets.at(row);
    const auto end=row+1<idx.offsets.size()?idx.offsets[row+1]:idx.bytes;
    if(next!=offset && !in.seekg(static_cast<std::streamoff>(offset))) throw std::runtime_error("partition seek failed");
    bytes.resize(static_cast<std::size_t>(end-offset)); n::read(in,bytes); next=end;
    require(checkpoint_detail::crc(bytes)==r.crc,"partition row checksum");
}
inline n::Bytes read_row(std::ifstream &in,const ShardIndex &idx,std::size_t row) {
    n::Bytes bytes; std::uint64_t next=UINT64_MAX;
    read_row_into(in,idx,row,bytes,next); return bytes;
}
// One forward payload pass for coordinator admission and offline deep verification.
inline std::uint64_t checked_digest(const Path &path,const ShardIndex &idx) {
    std::ifstream in(path,std::ios::binary);
    std::uint64_t prefix_bytes=idx.offsets.empty()?idx.bytes:idx.offsets.front();
    std::uint64_t digest=0;
    std::array<std::uint8_t,65536> buffer;
    while(prefix_bytes) {
        auto chunk=std::span(buffer).first(static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),prefix_bytes)));
        n::read(in,chunk); digest=crc64(chunk,digest); prefix_bytes-=chunk.size();
    }
    n::Bytes bytes;
    for(std::size_t i=0;i<idx.offsets.size();++i) {
        const auto end=i+1<idx.offsets.size()?idx.offsets[i+1]:idx.bytes;
        bytes.resize(static_cast<std::size_t>(end-idx.offsets[i])); n::read(in,bytes);
        require(checkpoint_detail::crc(bytes)==idx.header.rows[i].crc,"partition row checksum");
        digest=crc64(bytes,digest);
    }
    n::end(in);
    return digest;
}
// Concurrent retries for the same rank cannot replace a previously sealed shard.
// Each process prepares a private file, then installs its name without replacement.
template<class Write> void publish_shard(const Path &path,Write write) {
    static std::atomic<std::uint64_t> serial{0};
#ifdef _WIN32
    const auto pid=GetCurrentProcessId();
#else
    const auto pid=getpid();
#endif
    Path staged=path;
    staged+=".stage."+std::to_string(pid)+"."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+
            "."+std::to_string(serial++);
    try {
        n::atomic_stream(staged,std::move(write));
#ifdef _WIN32
        if(!MoveFileExW(staged.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH))
            throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"publish immutable shard");
#else
        std::filesystem::create_hard_link(staged,path);
        std::filesystem::remove(staged);
        durability::sync_parent(path);
#endif
    } catch(...) {
        std::error_code ec; std::filesystem::remove(staged,ec); throw;
    }
}
inline void write_shard(const Path &dir,PartitionFile kind,std::uint32_t rank,std::uint32_t parts,
                        const PartitionEpoch &epoch,const OwnedSourceMap &owned,std::span<const RegisteredState* const> frames) {
    epoch.validate(); const auto sorted=owned.sorted();
    require(parts && rank<parts && frames.size()==(kind==PartitionFile::Results?epoch.samples.size():1),"shard rank/frame count");
    const auto path=dir/filename(rank,kind);
    require(!std::filesystem::exists(path),"partition shard already exists; use a fresh generation directory");
    Header h; h.kind=kind; h.rank=rank; h.parts=parts; h.epoch=epoch; h.owned=owned;
    struct SourceRow { const RegisteredFieldBase *field; std::size_t first,count; };
    std::vector<SourceRow> source;
    n::Bytes schema;
    for(std::size_t s=0;s<frames.size();++s) {
        const auto *state=frames[s];
        require(state && !state->in_trial() && state->model_context()==epoch.model && state->equation_context()!=0,
                "partition state context/phase");
        state->validate();
        const auto catalog=fields(*state);
        if(s==0) { h.local_equation_context=state->equation_context(); h.fields=catalog; schema=n::pack(catalog); }
        else require(h.local_equation_context==state->equation_context() && n::pack(catalog)==schema,"shard field schema changed between samples");
        for(std::size_t f=0;f<catalog.size();++f) {
            catalog[f].validate();
            const auto *e=state->entries()[f];
            if(e->descriptor().association==Association::Global) require(e->count()==1,"global singleton count");
            const auto c=e->descriptor().component_count();
            for(std::size_t i=0;i<e->count();++i) {
                auto a=address(*e,owned,i);
                require(e->descriptor().association==Association::Global || std::ranges::binary_search(sorted.labels(e->descriptor().association),a.entity),"field contains non-owned label");
                StateLayout::validate_address(a,e->descriptor().association,e->descriptor().location);
                const auto first=product(i,c);
                h.rows.push_back({kind==PartitionFile::Results?s:epoch.samples.size()-1,f,a,checkpoint_detail::crc(e->block(first,c))});
                source.push_back({e,first,c});
            }
        }
    }
    publish_shard(path,[&](auto &out) {
        prefix(out,magic(kind)); n::record(out,1,n::pack(h));
        for(const auto &r:source) n::write(out,r.field->block(r.first,r.count));
    });
}
inline OwnedSourceMap unite(std::span<const OwnedSourceMap> parts) {
    OwnedSourceMap keys;
    for(const auto &p:parts) {
        keys.node_labels.insert(keys.node_labels.end(),p.node_labels.begin(),p.node_labels.end());
        keys.element_labels.insert(keys.element_labels.end(),p.element_labels.begin(),p.element_labels.end());
    }
    return keys.sorted();
}
} // namespace partition_detail

struct PartitionPart {
    std::uint32_t rank=0;
    PartitionFile kind=PartitionFile::Results;
    std::string relative_path;
    std::uint64_t bytes=0,crc64=0,index_crc64=0;
    template<class F> void visit(F f) { f(rank,kind,relative_path,bytes,crc64,index_crc64); }
};
struct DistributedManifest {
    std::uint32_t part_count=0;
    PartitionFiles files=PartitionFiles::Both;
    PartitionEpoch epoch;
    std::uint64_t node_count=0,element_count=0,source_identity=0,axis_identity=0;
    std::vector<PartitionPart> parts;
    template<class F> void visit(F f) { f(part_count,files,epoch,node_count,element_count,source_identity,axis_identity,parts); }
    using Path=std::filesystem::path;
    bool contains(PartitionFile kind) const { return (static_cast<unsigned>(files)&static_cast<unsigned>(kind))!=0; }
    // Validates rank membership, immutable indices and the entire global ownership set.
    // Deep verification additionally reads each value row and checks the whole-file digest.
    auto indices(const Path &directory,bool deep=false,const OwnedSourceMap *expected_sorted=nullptr) const {
        using namespace partition_detail;
        epoch.validate();
        require(part_count && files>=PartitionFiles::Results && files<=PartitionFiles::Both &&
                axis_identity==crc64(n::pack(epoch.samples)),"manifest count, selection or axis digest");
        const auto kinds=files==PartitionFiles::Both?2u:1u;
        require(parts.size()==std::uint64_t(part_count)*kinds,"missing manifest part");
        std::map<std::pair<PartitionFile,std::uint32_t>,ShardIndex> out;
        std::map<PartitionFile,n::Bytes> schemas;
        std::map<std::uint32_t,OwnedSourceMap> owners;
        std::map<std::uint32_t,std::uint64_t> local_contexts;
        using GlobalRow=std::tuple<PartitionFile,std::uint64_t,std::uint64_t>;
        std::vector<std::pair<GlobalRow,n::Bytes>> reference_globals;
        for(const auto &p:parts) {
            require((p.kind==PartitionFile::Results || p.kind==PartitionFile::Checkpoint) && contains(p.kind) &&
                    p.rank<part_count && p.relative_path==filename(p.rank,p.kind),"manifest part path, kind or rank");
            const auto path=directory/p.relative_path;
            auto idx=index(path,p.kind);
            const auto &h=idx.header;
            require(h.rank==p.rank && h.parts==part_count && h.epoch==epoch && idx.bytes==p.bytes &&
                    idx.index_crc==p.index_crc64,"shard generation, size or metadata digest mismatch");
            auto [ctx,first_rank]=local_contexts.emplace(p.rank,h.local_equation_context);
            require(h.local_equation_context && (first_rank || ctx->second==h.local_equation_context),"rank-local DOF context mismatch");
            auto schema=n::pack(h.fields);
            auto [si,new_schema]=schemas.emplace(p.kind,schema);
            require(new_schema || si->second==schema,"inconsistent shard field schema");
            auto [oi,new_owner]=owners.emplace(p.rank,h.owned.sorted());
            require(new_owner || oi->second==h.owned.sorted(),"result/checkpoint ownership mismatch");
            if(std::ranges::any_of(h.fields,[](const auto &f){return f.field.association==Association::Global;})) {
                std::vector<std::pair<GlobalRow,n::Bytes>> globals;
                std::ifstream in(path,std::ios::binary);
                for(std::size_t i=0;i<h.rows.size();++i) {
                    const auto &row=h.rows[i];
                    if(h.fields[row.field].field.association==Association::Global)
                        globals.emplace_back(GlobalRow{p.kind,row.field,row.sample},read_row(in,idx,i));
                }
                std::ranges::sort(globals,{},&decltype(globals)::value_type::first);
                if(new_schema) reference_globals.insert(reference_globals.end(),globals.begin(),globals.end());
                else {
                    auto start=std::find_if(reference_globals.begin(),reference_globals.end(),[&](const auto &v){return std::get<0>(v.first)==p.kind;});
                    require(start!=reference_globals.end() && std::equal(globals.begin(),globals.end(),start),"divergent replicated global payload");
                }
            }
            if(deep) require(checked_digest(path,idx)==p.crc64,"shard file CRC64 mismatch");
            require(out.emplace(std::pair(p.kind,p.rank),std::move(idx)).second,"duplicate manifest rank");
        }
        std::vector<OwnedSourceMap> maps;
        for(auto &[rank,owned]:owners) maps.push_back(std::move(owned));
        const auto global=unite(maps);
        require(global.node_labels.size()==node_count && global.element_labels.size()==element_count &&
                crc64(n::pack(global))==source_identity && (!expected_sorted || global==*expected_sorted),"global ownership mismatch");
        return out;
    }
    static DistributedManifest publish(const Path &path,std::uint32_t count,const OwnedSourceMap &expected,
                                       const PartitionEpoch &epoch,PartitionFiles files=PartitionFiles::Both) {
        using namespace partition_detail;
        require(count && files>=PartitionFiles::Results && files<=PartitionFiles::Both,"invalid manifest partition count or kind");
        const auto global=expected.sorted(); epoch.validate();
        DistributedManifest m{count,files,epoch,global.node_labels.size(),global.element_labels.size(),
                              crc64(n::pack(global)),crc64(n::pack(epoch.samples)),{}};
        for(std::uint32_t rank=0;rank<count;++rank)
            for(auto k:{PartitionFile::Results,PartitionFile::Checkpoint}) if(m.contains(k)) {
                const auto name=filename(rank,k); const auto part=path.parent_path()/name;
                auto idx=index(part,k);
                m.parts.push_back({rank,k,name,idx.bytes,checked_digest(part,idx),idx.index_crc});
            }
        m.indices(path.parent_path(),false,&global);
        n::atomic_stream(path,[&](auto &out){prefix(out,manifest_magic);n::record(out,1,n::pack(m));});
        return m;
    }
    static DistributedManifest open(const Path &path,bool deep=true) {
        using namespace partition_detail;
        auto m=read(path);
        m.indices(path.parent_path(),deep);
        return m;
    }
    static DistributedManifest read(const Path &path) {
        using namespace partition_detail;
        std::ifstream in(path,std::ios::binary); prefix(in,manifest_magic);
        auto m=n::unpack<DistributedManifest>(n::record(in,1)); n::end(in); return m;
    }
};
class PartitionedResultWriter {
  public:
    static void write(const std::filesystem::path &dir,std::uint32_t rank,std::uint32_t parts,
                      const PartitionEpoch &epoch,const OwnedSourceMap &owned,std::span<const RegisteredState* const> frames) {
        partition_detail::write_shard(dir,PartitionFile::Results,rank,parts,epoch,owned,frames);
    }
};
} // namespace sdb

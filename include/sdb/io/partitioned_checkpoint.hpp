#pragma once
#include "../results/distributed_manifest.hpp"

namespace sdb {
struct PartitionRestoreStats {
    PartitionSample cursor;
    std::uint64_t selected_rows=0,payload_bytes=0,source_rows=0;
};
class PartitionedCheckpoint {
  public:
    static void write(const std::filesystem::path &dir,std::uint32_t rank,std::uint32_t parts,
                      const PartitionEpoch &epoch,const OwnedSourceMap &owned,const RegisteredState &state) {
        const RegisteredState *frame=&state;
        partition_detail::write_shard(dir,PartitionFile::Checkpoint,rank,parts,epoch,owned,{&frame,1});
    }
    static PartitionRestoreStats restore(const std::filesystem::path &path,std::span<const OwnedSourceMap> targets,
                                         std::size_t rank,RegisteredState &state,const PartitionEpoch &expected) {
        using namespace partition_detail;
        require(rank<targets.size() && !state.in_trial() && state.frozen(),"invalid target partition/phase");
        auto m=DistributedManifest::read(path);
        require(m.contains(PartitionFile::Checkpoint) && m.epoch==expected && state.model_context()==expected.model &&
                state.equation_context()!=0,"checkpoint cursor/model/DOF mismatch");
        const auto all=unite(targets);
        require(all.node_labels.size()==m.node_count && all.element_labels.size()==m.element_count &&
                crc64(n::pack(all))==m.source_identity,"target partition is missing or substitutes global entities");
        auto shards=m.indices(path.parent_path(),false,&all);
        const auto &local=targets[rank];
        const auto sorted=local.sorted();
        const auto schema=n::pack(fields(state));
        using Key=std::pair<std::uint64_t,Address>;
        struct Destination { Key key; std::size_t offset=0,count=0; bool filled=false; };
        std::vector<Destination> wanted;
        for(std::size_t f=0;f<state.entries().size();++f) {
            const auto *e=state.entries()[f]; const auto &d=e->descriptor();
            const auto width=d.component_count();
            for(std::size_t i=0;i<e->count();++i) {
                auto a=address(*e,local,i);
                require(d.association==Association::Global || std::ranges::binary_search(sorted.labels(d.association),a.entity),"target state contains non-owned row");
                wanted.push_back({Key{f,a},product(i,width),width,false});
            }
        }
        std::ranges::sort(wanted,{},&Destination::key);
        require(std::ranges::adjacent_find(wanted,{},&Destination::key)==wanted.end(),"duplicate target state address");
        auto staged=state.initialized_copy();
        PartitionRestoreStats stats{m.epoch.samples.back()};
        for(const auto &[part,idx]:shards) if(part.first==PartitionFile::Checkpoint) {
            const auto &h=idx.header;
            require(n::pack(h.fields)==schema,"checkpoint target history/field schema mismatch");
            std::ifstream in(path.parent_path()/filename(part.second,part.first),std::ios::binary);
            n::Bytes bytes; std::uint64_t next=UINT64_MAX;
            for(std::size_t i=0;i<h.rows.size();++i) {
                const auto &r=h.rows[i];
                ++stats.source_rows;
                auto association=h.fields[r.field].field.association;
                if(association==Association::Global) { if(part.second!=0) continue; }
                else if(!std::ranges::binary_search(sorted.labels(association),r.address.entity)) continue;
                Key key{r.field,r.address};
                auto it=std::ranges::lower_bound(wanted,key,{},&Destination::key);
                require(it!=wanted.end() && it->key==key && !it->filled,"duplicate or unexpected owned checkpoint address");
                read_row_into(in,idx,i,bytes,next);
                staged.restore_block(r.field,bytes,it->offset,it->count);
                it->filled=true; ++stats.selected_rows; stats.payload_bytes+=bytes.size();
            }
        }
        require(stats.selected_rows==wanted.size(),"missing target state/history row");
        staged.validate(); state=std::move(staged);
        return stats;
    }
};
} // namespace sdb

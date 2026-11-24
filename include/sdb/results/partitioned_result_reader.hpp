#pragma once
#include "distributed_manifest.hpp"

namespace sdb {
struct PartitionResultRow {
    std::uint64_t sample=0;
    PartitionSample cursor;
    lifecycle::ResultAddress address;
    lifecycle::TypedValues values;
    std::vector<std::uint8_t> validity;
};
class PartitionedResultReader {
    std::filesystem::path directory_;
    DistributedManifest manifest_;
    std::map<std::uint32_t,partition_detail::ShardIndex> shards_;
    using Location=std::pair<std::uint32_t,std::size_t>;
    std::map<std::pair<std::string,GlobalLabel>,std::vector<Location>> lookup_;
    std::map<std::string,Association> fields_;

    template<class Select> auto read(const std::string &field,GlobalLabel key,Select selected) const {
        if(!fields_.contains(field)) throw std::out_of_range("unknown partition result field");
        std::vector<PartitionResultRow> out;
        auto it=lookup_.find({field,key});
        if(it==lookup_.end()) return out;
        const auto rank=it->second.front().first;
        const auto &idx=shards_.at(rank);
        std::ifstream in(directory_/partition_detail::filename(rank,PartitionFile::Results),std::ios::binary);
        for(auto [owner,i]:it->second) {
            const auto &r=idx.header.rows[i];
            const auto &sample=manifest_.epoch.samples.at(r.sample);
            if(!selected(r.sample,sample)) continue;
            const auto &d=idx.header.fields[r.field].field;
            auto bytes=partition_detail::read_row(in,idx,i);
            PartitionResultRow row{r.sample,sample,r.address,lifecycle::empty_typed_values(d.scalar),{}};
            std::size_t pos=0;
            std::visit([&](auto &values) {
                using V=typename std::decay_t<decltype(values)>::value_type;
                values.reserve(d.component_count());
                for(std::size_t c=0;c<d.component_count();++c) {
                    values.push_back(registered_detail::scalar<V>(bytes,pos));
                    if(d.missing==MissingPolicy::ValidityMask) row.validity.push_back(bytes.at(pos++));
                }
            },row.values);
            out.push_back(std::move(row));
        }
        return out;
    }
  public:
    explicit PartitionedResultReader(const std::filesystem::path &manifest)
        :directory_(manifest.parent_path()),manifest_(DistributedManifest::read(manifest)) {
        if(!manifest_.contains(PartitionFile::Results)) throw std::invalid_argument("manifest has no result shards");
        auto indices=manifest_.indices(directory_);
        for(auto &[part,idx]:indices) if(part.first==PartitionFile::Results) {
            const auto rank=part.second;
            for(const auto &f:idx.header.fields) fields_.emplace(f.field.name,f.field.association);
            const auto &rows=idx.header.rows;
            for(std::size_t i=0;i<rows.size();++i)
                lookup_[{idx.header.fields[rows[i].field].field.name,rows[i].address.entity}].emplace_back(rank,i);
            shards_.emplace(rank,std::move(idx));
        }
        for(auto &[key,locations]:lookup_) {
            const auto &rows=shards_.at(locations.front().first).header.rows;
            std::ranges::sort(locations,[&](const Location &a,const Location &b) {
                const auto &x=rows[a.second], &y=rows[b.second];
                return std::tie(x.sample,x.address)<std::tie(y.sample,y.address);
            });
        }
    }
    const PartitionEpoch &epoch() const noexcept { return manifest_.epoch; }
    // Half-open sample range; closed physical time range. Rows are in sample/address order.
    auto query_samples(const std::string &field,GlobalLabel key,std::size_t first,std::size_t end) const {
        if(first>end || end>manifest_.epoch.samples.size()) throw std::out_of_range("partition sample range");
        return read(field,key,[&](auto i,const auto &){return i>=first && i<end;});
    }
    auto query_time(const std::string &field,GlobalLabel key,double first,double last) const {
        if(!std::isfinite(first) || !std::isfinite(last) || first>last) throw std::invalid_argument("partition time range");
        return read(field,key,[&](auto,const auto &s){return s.time>=first && s.time<=last;});
    }
};
} // namespace sdb

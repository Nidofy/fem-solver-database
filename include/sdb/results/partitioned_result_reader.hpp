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
    using Key=std::pair<std::size_t,GlobalLabel>;
    struct Location { Key key; std::uint32_t rank; std::size_t row; };
    std::vector<Location> lookup_;
    std::map<std::string,std::size_t> fields_;

    template<class Select> auto read(const std::string &field,GlobalLabel key,Select selected) const {
        if(!fields_.contains(field)) throw std::out_of_range("unknown partition result field");
        std::vector<PartitionResultRow> out;
        auto matches=std::ranges::equal_range(lookup_,Key{fields_.at(field),key},{},&Location::key);
        if(matches.empty()) return out;
        const auto rank=matches.front().rank;
        const auto &idx=shards_.at(rank);
        std::ifstream in(directory_/partition_detail::filename(rank,PartitionFile::Results),std::ios::binary);
        for(auto [identity,owner,i]:matches) {
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
                    const auto value=registered_detail::scalar<V>(bytes,pos);
                    if constexpr(std::is_floating_point_v<V>)
                        partition_detail::require(std::isfinite(value),"nonfinite partition result");
                    if(d.missing==MissingPolicy::ValidityMask) {
                        const auto valid=bytes.at(pos++);
                        partition_detail::require(valid<=1 && (valid || value==V{}),"invalid partition result mask/value");
                        if constexpr(std::is_floating_point_v<V>)
                            partition_detail::require(valid || !std::signbit(value),"invalid partition result negative zero");
                        row.validity.push_back(valid);
                    }
                    values.push_back(value);
                }
            },row.values);
            out.push_back(std::move(row));
        }
        return out;
    }
  public:
    explicit PartitionedResultReader(const std::filesystem::path &manifest,std::uint64_t budget=partition_metadata_budget)
        :directory_(manifest.parent_path()),manifest_(DistributedManifest::read(manifest,budget)) {
        if(!manifest_.contains(PartitionFile::Results)) throw std::invalid_argument("manifest has no result shards");
        auto indices=manifest_.indices(directory_,false,nullptr,budget,PartitionFile::Results);
        for(auto &[part,idx]:indices) if(part.first==PartitionFile::Results) {
            const auto rank=part.second;
            for(std::size_t f=0;f<idx.header.fields.size();++f) fields_.emplace(idx.header.fields[f].field.name,f);
            const auto &rows=idx.header.rows;
            for(std::size_t i=0;i<rows.size();++i)
                if(rank==0 || idx.header.fields[rows[i].field].field.association!=Association::Global)
                    lookup_.push_back({{rows[i].field,rows[i].address.entity},rank,i});
            shards_.emplace(rank,std::move(idx));
        }
        std::ranges::sort(lookup_,[&](const Location &a,const Location &b) {
            if(a.key!=b.key) return a.key<b.key;
            const auto &x=shards_.at(a.rank).header.rows[a.row], &y=shards_.at(b.rank).header.rows[b.row];
            return std::tie(x.sample,x.address)<std::tie(y.sample,y.address);
        });
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

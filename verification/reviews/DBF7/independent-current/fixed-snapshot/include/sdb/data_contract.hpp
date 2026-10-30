#pragma once
#include "io/native.hpp"

namespace sdb {
enum class DataDisposition : std::uint8_t { Stored, Reconstructed, ConsumerOwned, Unsupported };
struct DataResponsibility {
    std::string source, target, owner, semantics, evidence;
    std::uint32_t schema_version=1;
    DataDisposition disposition=DataDisposition::Unsupported;
    template<class F> void visit(F f) { f(source,target,owner,semantics,evidence,schema_version,disposition); }
};
// A neutral capture inventory. Native names are caller data, never database dependencies.
// This verifies completeness of declarations, not physical equivalence of a translator.
struct DataCoverage {
    std::uint32_t version=1;
    std::string source_revision, consumer;
    std::vector<DataResponsibility> entries;
    template<class F> void visit(F f) { f(version,source_revision,consumer,entries); }
    void validate(std::span<const std::string> populated,bool allow_unsupported=false) const {
        if(version!=1 || source_revision.empty() || consumer.empty()) throw std::invalid_argument("coverage provenance");
        std::set<std::string> expected;
        for(const auto &key:populated) if(key.empty() || !expected.insert(key).second) throw std::invalid_argument("capture inventory duplicate/empty");
        std::set<std::string> seen;
        for(const auto &e:entries) {
            if(!expected.contains(e.source) || !seen.insert(e.source).second || e.target.empty() || e.owner.empty() ||
               e.semantics.empty() || e.evidence.empty() || !e.schema_version || !enum_at_most(e.disposition,DataDisposition::Unsupported))
                throw std::invalid_argument("coverage entry incomplete/unknown/duplicate");
            if(e.disposition==DataDisposition::Unsupported && !allow_unsupported) throw std::invalid_argument("populated source field unsupported");
        }
        if(seen!=expected) throw std::invalid_argument("capture fields silently omitted");
    }
};
} // namespace sdb

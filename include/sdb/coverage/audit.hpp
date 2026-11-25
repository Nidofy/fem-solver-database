#pragma once
#include "../data_contract.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <string_view>

namespace sdb::coverage {
// Reconcile a versioned declaration catalog with the subset populated by one capture.
// A declared but unsupported key is allowed in a catalog. A populated one needs an adapter.
struct Audit {
    std::array<std::size_t,4> declared_by_disposition{};
    std::vector<std::string> unsupported_populated;

    bool admits_capture() const noexcept { return unsupported_populated.empty(); }
};

inline Audit audit(const DataCoverage &contract, std::span<const std::string> declared,
                   std::span<const std::string> populated) {
    contract.validate(declared, true);
    Audit result;
    std::map<std::string_view,DataDisposition> remaining;
    for (const auto &entry : contract.entries) {
        ++result.declared_by_disposition.at(static_cast<std::size_t>(entry.disposition));
        remaining.emplace(entry.source, entry.disposition);
    }
    for (const auto &key : populated) {
        auto entry = remaining.find(key);
        if (entry == remaining.end()) throw std::invalid_argument("unknown or repeated populated coverage key");
        if (entry->second == DataDisposition::Unsupported) result.unsupported_populated.push_back(key);
        remaining.erase(entry);
    }
    std::ranges::sort(result.unsupported_populated);
    return result;
}
} // namespace sdb::coverage

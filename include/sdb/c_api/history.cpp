#include "history.h"
#include "../results/results.hpp"
#include <cstdio>

namespace {
template<class Read>
int visit_file(const char *path, const char *field, double begin, double end,
               char *error, std::size_t capacity, Read read) noexcept {
    if (capacity && !error) return -1;
    if (capacity) error[0] = '\0';
    try {
        if (!path || !*path || !field || !*field || !std::isfinite(begin) ||
            !std::isfinite(end) || begin > end)
            throw std::invalid_argument("invalid result path, field or time interval");
        sdb::lifecycle::ResultReader reader{std::filesystem::path(reinterpret_cast<const char8_t *>(path))};
        read(reader);
        return 0;
    } catch (const std::exception &e) {
        if (capacity) std::snprintf(error, capacity, "%s", e.what());
    } catch (...) {
        if (capacity) std::snprintf(error, capacity, "unknown result reader error");
    }
    return -1;
}
} // namespace

extern "C" int sdb_visit_history_f64_v1(const char *path, const char *field,
    uint64_t entity, uint32_t component, double begin, double end,
    sdb_f64_sample_v1 visit, void *context, char *error, size_t capacity) {
    return visit_file(path, field, begin, end, error, capacity, [&](const auto &reader) {
        if (!visit) throw std::invalid_argument("missing Float64 history visitor");
        sdb::lifecycle::ResultQuery::visit_history(reader, field, entity, [&](double time, double value) {
            return visit(context, time, value) != 0;
        }, component, "", false, begin, end);
    });
}

extern "C" int sdb_visit_history_u64_v1(const char *path, const char *field,
    uint64_t entity, uint32_t component, double begin, double end,
    sdb_u64_sample_v1 visit, void *context, char *error, size_t capacity) {
    return visit_file(path, field, begin, end, error, capacity, [&](const auto &reader) {
        if (!visit) throw std::invalid_argument("missing UInt64 history visitor");
        sdb::lifecycle::ResultAddress address;
        address.entity = entity;
        sdb::lifecycle::ResultQuery::visit_typed_history<uint64_t>(reader, field, address,
            [&](double time, std::optional<uint64_t> value) {
                return visit(context, time, value.value_or(0), value.has_value()) != 0;
            }, component, begin, end);
    });
}

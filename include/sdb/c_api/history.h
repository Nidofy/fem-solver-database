#ifndef SDB_C_API_HISTORY_H
#define SDB_C_API_HISTORY_H
#include "../c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Synchronous read-only visitors over indexed result files. The time window is
   inclusive. A callback returns nonzero to continue, zero to stop successfully.
   Callbacks already delivered remain visible if a later read fails. No borrowed
   pointers escape the call. The caller owns the optional error buffer.
   Returns 0 on completion or early stop and -1 on error. */
typedef int (*sdb_f64_sample_v1)(void *context, double time, double value);
typedef int (*sdb_u64_sample_v1)(void *context, double time, uint64_t value, uint8_t valid);

SDB_C_API int sdb_visit_history_f64_v1(const char *utf8_path, const char *field,
    uint64_t entity, uint32_t component, double begin, double end,
    sdb_f64_sample_v1 visit, void *context, char *error, size_t error_capacity);

/* Typed entity fields. Masked samples deliver value zero and valid zero.
   Exact integer values never pass through a floating-point representation. */
SDB_C_API int sdb_visit_history_u64_v1(const char *utf8_path, const char *field,
    uint64_t entity, uint32_t component, double begin, double end,
    sdb_u64_sample_v1 visit, void *context, char *error, size_t error_capacity);

#ifdef __cplusplus
}
#endif
#endif

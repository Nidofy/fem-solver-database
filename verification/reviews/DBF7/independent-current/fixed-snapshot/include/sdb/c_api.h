#ifndef SDB_C_API_H
#define SDB_C_API_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
# if defined(SDB_C_BUILD)
#  define SDB_C_API __declspec(dllexport)
# else
#  define SDB_C_API __declspec(dllimport)
# endif
#else
# define SDB_C_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* ABI v1. Paths are UTF-8. Opaque handles own readers. Serialize calls per handle;
   no pointers into database storage escape. Output arguments change on success
   only. All errors return -1 and set thread-local error text. */
typedef struct sdb_reader sdb_reader;
SDB_C_API uint32_t sdb_abi_version(void);
SDB_C_API const char *sdb_last_error(void);
SDB_C_API int sdb_reader_open(const char *utf8_path,sdb_reader **out);
SDB_C_API void sdb_reader_close(sdb_reader *reader);
SDB_C_API int sdb_reader_frame_count(sdb_reader *reader,uint64_t *out);
SDB_C_API int sdb_reader_time(sdb_reader *reader,uint64_t frame,double *out);
/* Legacy real-valued field history; capacity is a strict output sample budget.
   No implicit conversion of exact typed integer fields to double. */
SDB_C_API int sdb_reader_history_f64(sdb_reader *reader,const char *field,uint64_t entity,uint32_t component,
    double begin,double end,double *times,double *values,size_t capacity,size_t *count);
/* Exact typed node/entity field (entity location, no face/contact/IP selectors). */
SDB_C_API int sdb_reader_entity_u64(sdb_reader *reader,uint64_t frame,const char *field,uint64_t entity,
    uint32_t component,uint64_t *value,uint8_t *valid);
#ifdef __cplusplus
}
#endif
#endif

#ifndef SDB_MATERIAL_ABI_H
#define SDB_MATERIAL_ABI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SDB_MATERIAL_ABI_V1 1u
enum sdb_material_scalar { SDB_MAT_F32=1, SDB_MAT_F64=2 };
enum sdb_material_tangent { SDB_MAT_NO_TANGENT=0, SDB_MAT_SYMMETRIC=1, SDB_MAT_GENERAL=2 };
/* A tensor uses xx yy zz xy yz zx, with tensor shear. A tangent is row-major
   d(stress)/d(tensor strain). Inputs and outputs are disjoint, host-owned.
   Return zero on success. C++ exceptions must not cross this boundary. */
typedef int32_t (*sdb_material_eval_v1)(const void *parameters, const void *strain6,
    const void *committed_history, void *stress6, void *trial_history, void *tangent36);
typedef struct sdb_material_plugin_v1 {
    uint32_t abi_version, struct_size, algorithm, schema_version;
    const char *schema_id;
    uint32_t scalar, parameter_count, history_count, tangent_kind;
    const char *const *parameter_names;
    const char *const *history_names;
    sdb_material_eval_v1 evaluate;
} sdb_material_plugin_v1;
#ifdef __cplusplus
}
#endif
#endif

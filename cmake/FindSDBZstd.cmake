# Optional external dependency: no downloading, vendoring or fallback codec.
find_path(SDB_ZSTD_INCLUDE_DIR NAMES zstd.h)
find_library(SDB_ZSTD_LIBRARY NAMES zstd libzstd)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDBZstd REQUIRED_VARS SDB_ZSTD_INCLUDE_DIR SDB_ZSTD_LIBRARY)
if(SDBZstd_FOUND AND NOT TARGET SDBZstd::Zstd)
  include(CheckCXXSourceCompiles)
  set(_sdb_saved_includes "${CMAKE_REQUIRED_INCLUDES}")
  set(_sdb_saved_libraries "${CMAKE_REQUIRED_LIBRARIES}")
  set(CMAKE_REQUIRED_INCLUDES "${SDB_ZSTD_INCLUDE_DIR}")
  set(CMAKE_REQUIRED_LIBRARIES "${SDB_ZSTD_LIBRARY}")
  unset(SDB_ZSTD_ADVANCED_API CACHE)
  check_cxx_source_compiles("#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#if ZSTD_VERSION_NUMBER < 10505
#error zstd 1.5.5 or newer required
#endif
int main() { ZSTD_customMem m{}; auto c=ZSTD_createCCtx_advanced(m); auto d=ZSTD_createDCtx_advanced(m); ZSTD_freeCCtx(c); ZSTD_freeDCtx(d); return 0; }" SDB_ZSTD_ADVANCED_API)
  set(CMAKE_REQUIRED_INCLUDES "${_sdb_saved_includes}")
  set(CMAKE_REQUIRED_LIBRARIES "${_sdb_saved_libraries}")
  if(NOT SDB_ZSTD_ADVANCED_API)
    message(FATAL_ERROR "zstd SDK/library lacks required bounded-allocation APIs (1.5.5+)")
  endif()
  add_library(SDBZstd::Zstd UNKNOWN IMPORTED)
  set_target_properties(SDBZstd::Zstd PROPERTIES IMPORTED_LOCATION "${SDB_ZSTD_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${SDB_ZSTD_INCLUDE_DIR}")
endif()

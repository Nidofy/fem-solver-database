if(SDB_C_API)
  target_sources(sdb_reader PRIVATE include/sdb/c_api/history.cpp)
  install(FILES python/sdb_stream.py DESTINATION share/SolverDatabaseV2/python)
endif()
if(NOT BUILD_TESTING OR NOT Python3_Interpreter_FOUND)
  return()
endif()

add_executable(sdb_dbf13_coverage tests/dbf13_coverage.cpp)
target_link_libraries(sdb_dbf13_coverage PRIVATE solver_database_v2)
add_test(NAME dbf13_coverage COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/dbf13_coverage.py
  $<TARGET_FILE:sdb_dbf13_coverage> ${CMAKE_CURRENT_BINARY_DIR}/dbf13-coverage)

add_executable(sdb_dbf13_partition_publish tests/dbf13_partition_publish.cpp)
target_link_libraries(sdb_dbf13_partition_publish PRIVATE solver_database_v2)
add_test(NAME dbf13_partition_faults COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/dbf13_partition_faults.py
  $<TARGET_FILE:sdb_dbf13_partition_publish> $<TARGET_FILE:sdb_dbf11_partitions>
  ${CMAKE_CURRENT_BINARY_DIR}/dbf11-parts ${CMAKE_CURRENT_BINARY_DIR}/dbf13-faults)
set_tests_properties(dbf13_partition_faults PROPERTIES FIXTURES_REQUIRED dbf11_files)

foreach(test_target IN ITEMS sdb_dbf13_coverage sdb_dbf13_partition_publish)
  if(MSVC)
    target_compile_options(${test_target} PRIVATE /bigobj)
  elseif(MINGW)
    target_compile_options(${test_target} PRIVATE -Wa,-mbig-obj)
  endif()
endforeach()

if(SDB_C_API)
  set_property(TEST dbf5_compressed APPEND PROPERTY FIXTURES_SETUP dbf13_masked_samples)
  include(CheckLanguage)
  check_language(C)
  if(CMAKE_C_COMPILER)
    enable_language(C)
    add_executable(sdb_dbf13_c_stream tests/dbf13_stream.c)
    set_target_properties(sdb_dbf13_c_stream PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
    target_link_libraries(sdb_dbf13_c_stream PRIVATE sdb_reader)
    add_test(NAME dbf13_c_stream COMMAND sdb_dbf13_c_stream ${CMAKE_CURRENT_BINARY_DIR}/dbf7-query/range.sdbres
      ${CMAKE_CURRENT_BINARY_DIR}/dbf7-query/typed.sdbres
      ${CMAKE_CURRENT_BINARY_DIR}/dbf5-compressed-scratch/typed3.sdbres)
    set_tests_properties(dbf13_c_stream PROPERTIES FIXTURES_REQUIRED "dbf7_query_files;dbf13_masked_samples")
  endif()
  add_test(NAME dbf13_python_stream COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/dbf13_stream.py
    $<TARGET_FILE:sdb_reader> ${CMAKE_CURRENT_BINARY_DIR}/dbf7-query ${CMAKE_CURRENT_BINARY_DIR}/dbf5-compressed-scratch/typed3.sdbres "${SDB_TEST_COMPILER_DIRECTORY}")
  set_tests_properties(dbf13_python_stream PROPERTIES FIXTURES_REQUIRED "dbf7_query_files;dbf13_masked_samples")
endif()

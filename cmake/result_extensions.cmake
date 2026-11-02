add_executable(sdb_mapped_result_tests tests/mapped_result_tests.cpp)
target_link_libraries(sdb_mapped_result_tests PRIVATE sdb_lifecycle)
add_test(NAME mapped_result_contract COMMAND sdb_mapped_result_tests
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/abaqus/forced_tet.inp" "${CMAKE_CURRENT_BINARY_DIR}/mapped-read")

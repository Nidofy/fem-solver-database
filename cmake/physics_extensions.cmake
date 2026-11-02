add_executable(sdb_distributed_loads_tests tests/distributed_loads_tests.cpp)
target_link_libraries(sdb_distributed_loads_tests PRIVATE sdb_lifecycle)
add_test(NAME distributed_loads COMMAND sdb_distributed_loads_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/abaqus/distributed_hex.inp ${CMAKE_CURRENT_BINARY_DIR}/dload-scratch)
add_executable(sdb_penalty_constraints_tests tests/penalty_constraints_tests.cpp)
target_link_libraries(sdb_penalty_constraints_tests PRIVATE sdb_lifecycle)
add_test(NAME penalty_constraints COMMAND sdb_penalty_constraints_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/abaqus/forced_tet.inp ${CMAKE_CURRENT_BINARY_DIR}/penalty-scratch)

include(${CMAKE_CURRENT_LIST_DIR}/material_plugin.cmake)

add_executable(sdb_finite_tet_tests tests/finite_tet_tests.cpp)
target_link_libraries(sdb_finite_tet_tests PRIVATE sdb_lifecycle)
add_test(NAME finite_tet COMMAND sdb_finite_tet_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/abaqus/finite_tet.inp ${CMAKE_CURRENT_BINARY_DIR}/finite-tet-scratch)

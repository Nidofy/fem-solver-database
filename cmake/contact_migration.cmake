if(Python3_Interpreter_FOUND)
  add_test(NAME analytic_reference_suite COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/reference_suite.py $<TARGET_FILE:sdb_run> ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/abaqus)
endif()

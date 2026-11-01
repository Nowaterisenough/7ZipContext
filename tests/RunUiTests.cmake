if(NOT DEFINED TEST_LOG)
    set(TEST_LOG "ui-tests.log")
endif()
execute_process(
    COMMAND "${TEST_EXECUTABLE}" -platform offscreen -o "${TEST_LOG},txt"
    RESULT_VARIABLE _result
)
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${TEST_LOG}")
    file(READ "${CMAKE_CURRENT_BINARY_DIR}/${TEST_LOG}" _log)
    message("${_log}")
endif()
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Qt tests failed: ${_result}")
endif()

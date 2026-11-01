if(NOT DEFINED TEST_LOG)
    set(TEST_LOG "ui-tests.log")
endif()
# Qt 6.8's offscreen font backend does not discover Windows fonts automatically.
# Use installed fonts without changing the native application's font selection.
if(CMAKE_HOST_WIN32 AND "$ENV{QT_QPA_FONTDIR}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{SystemRoot}/Fonts" _system_fonts)
    if(IS_DIRECTORY "${_system_fonts}")
        set(ENV{QT_QPA_FONTDIR} "${_system_fonts}")
    endif()
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

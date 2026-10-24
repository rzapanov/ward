# run_list_tests.cmake
# Called: cmake -DBINARY=<path> -DOUTPUT=<path> -P this_file
#
# Runs the test binary with --list-tests and writes the output to a file.

if(NOT DEFINED BINARY OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "run_list_tests.cmake requires -DBINARY and -DOUTPUT")
endif()

execute_process(
    COMMAND "${BINARY}" --list-tests
    OUTPUT_VARIABLE _output
    RESULT_VARIABLE _result
)

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "run_list_tests.cmake: ${BINARY} --list-tests failed with status ${_result}")
endif()

file(WRITE "${OUTPUT}" "${_output}")

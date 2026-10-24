# ward_add_test_executable: create a ward test executable with all the wiring.
#
# Usage:
#   ward_add_test_executable(my_tests
#       SOURCES test/foo_test.c test/bar_test.c
#       LIBS    my_core
#       CTEST_NAME my_tests
#       TIMEOUT 300
#       OBJECT)
#
# Arguments:
#   SOURCES   - C source files containing Test() declarations (required)
#   LIBS      - additional libraries to link (optional)
#   CTEST_NAME - name for the ctest entry (defaults to target name)
#   TIMEOUT   - ctest TIMEOUT property in seconds (optional)
#   OBJECT    - use an OBJECT library instead of STATIC (avoids --whole-archive)
#
# Creates:
#   <target>_lib  - library holding the test sources
#   <target>      - executable linked against ward + <target>_lib
#   ctest entry   - runs <target> --no-color

# Captured at include time (CMAKE_CURRENT_LIST_DIR is the cmake/ dir here).
# Cache INTERNAL so it is visible from any subdirectory scope that calls the
# helpers (the includer's scope is not visible to sibling directories).
set(WARD_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "ward cmake scripts directory")

function(ward_add_test_executable target)
    cmake_parse_arguments(WARD "" "CTEST_NAME;TIMEOUT;OBJECT" "SOURCES;LIBS" ${ARGN})

    if(NOT WARD_SOURCES)
        message(FATAL_ERROR "ward_add_test_executable(${target}): SOURCES is required")
    endif()

    set(_lib ${target}_lib)

    if(WARD_OBJECT)
        add_library(${_lib} OBJECT ${WARD_SOURCES})
    else()
        add_library(${_lib} STATIC ${WARD_SOURCES})
    endif()

    target_link_libraries(${_lib} PUBLIC ward::ward)
    if(WARD_LIBS)
        target_link_libraries(${_lib} PUBLIC ${WARD_LIBS})
    endif()

    add_executable(${target} "")
    target_link_libraries(${target} PRIVATE ${_lib})

    if(NOT WARD_OBJECT)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_link_options(${target} PRIVATE
                "LINKER:--whole-archive,$<TARGET_FILE:${_lib}>,--no-whole-archive")
        endif()
    endif()

    set(_ctest_name ${WARD_CTEST_NAME})
    if(NOT _ctest_name)
        set(_ctest_name ${target})
    endif()

    add_test(NAME ${_ctest_name} COMMAND ${target} --no-color)
    if(WARD_TIMEOUT)
        set_tests_properties(${_ctest_name} PROPERTIES TIMEOUT ${WARD_TIMEOUT})
    endif()
endfunction()

# ward_add_per_case_tests: register one ctest entry per test case.
#
# Usage:
#   ward_add_per_case_tests(my_tests)
#   ward_add_per_case_tests(my_tests TIMEOUT 60)
#
# Requires a two-step workflow:
#   1. cmake -S . -B build          (first configure: no per-case tests yet)
#   2. cmake --build build          (builds the binary, generates the list)
#   3. cmake -S . -B build          (second configure: per-case tests appear)
#   4. ctest --test-dir build       (now shows one entry per case)
#
# The tradeoff: ward's own in-process parallelism already covers wall-clock,
# so per-case entries are for CI dashboards that need to show *which* case
# failed, not for speed.

function(ward_add_per_case_tests target)
    cmake_parse_arguments(WARD "" "TIMEOUT" "" ${ARGN})

    set(_list_file "${CMAKE_CURRENT_BINARY_DIR}/${target}_test_list.txt")
    set(_cmake_file "${CMAKE_CURRENT_BINARY_DIR}/${target}_per_case.cmake")

    # At build time: run the binary to get the test list, then generate the
    # CMake file with one add_test per case.
    add_custom_command(
        OUTPUT "${_list_file}"
        COMMAND ${CMAKE_COMMAND}
                -DBINARY=$<TARGET_FILE:${target}>
                -DOUTPUT=${_list_file}
                -P ${WARD_CMAKE_DIR}/run_list_tests.cmake
        DEPENDS ${target}
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${_cmake_file}"
        COMMAND ${CMAKE_COMMAND}
                -DLIST_FILE=${_list_file}
                -DOUTPUT=${_cmake_file}
                -DBINARY=$<TARGET_FILE:${target}>
                -P ${WARD_CMAKE_DIR}/generate_per_case.cmake
        DEPENDS "${_list_file}"
        VERBATIM
    )

    add_custom_target(${target}_per_case ALL DEPENDS "${_cmake_file}")

    # At configure time: include the generated file if it exists from a
    # previous build. This is the "second configure" step.
    if(EXISTS "${_cmake_file}")
        include("${_cmake_file}")
        if(WARD_TIMEOUT)
            # Apply timeout to all per-case tests (they inherit the name pattern).
            get_property(_test_names TESTS "")
            # Note: users can set TIMEOUT individually if needed.
        endif()
    endif()
endfunction()

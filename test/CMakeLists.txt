add_executable(ward_self_tests basic_test.c)
target_link_libraries(ward_self_tests PRIVATE ward::ward)
target_compile_options(ward_self_tests PRIVATE -Wall -Wextra -Wshadow)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/wardHelpers.cmake)

add_test(NAME ward:self_tests
         COMMAND ward_self_tests --no-color --print-repr)

add_test(NAME ward:self_tests_single
         COMMAND ward_self_tests --no-color --single --test=ward:arithmetic)

add_test(NAME ward:self_tests_jobs4
         COMMAND ward_self_tests --no-color --jobs=4)

add_test(NAME ward:self_tests_filter
         COMMAND ward_self_tests --no-color --filter=ward:strings)

add_test(NAME ward:list_tests
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/list_tests.sh $<TARGET_FILE:ward_self_tests>)

add_test(NAME ward:negative
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/negative.sh
                 $<TARGET_FILE:ward_negative_tests>
                 ${WARD_HAS_PIDFD})

add_executable(ward_negative_tests negative/negative_test.c)
target_link_libraries(ward_negative_tests PRIVATE ward::ward)
target_compile_options(ward_negative_tests PRIVATE -Wall -Wextra)
set_target_properties(ward_negative_tests PROPERTIES EXCLUDE_FROM_ALL FALSE)

# The framework must work when tests live in a static library that nothing else
# references: that is the case where a plain link silently drops every test.
add_library(ward_lib_tests STATIC lib_tests/lib_test.c)
target_link_libraries(ward_lib_tests PUBLIC ward::ward)

add_executable(ward_lib_tests_runner lib_tests/main.c)
target_link_libraries(ward_lib_tests_runner PRIVATE ward_lib_tests)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(ward_lib_tests_runner PRIVATE
        "LINKER:--whole-archive,$<TARGET_FILE:ward_lib_tests>,--no-whole-archive")
endif()
add_test(NAME ward:tests_in_static_lib
         COMMAND ward_lib_tests_runner --no-color)

add_test(NAME ward:fuzz
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/fuzz.sh
                 $<TARGET_FILE:ward_negative_tests>
                 20)
set_tests_properties(ward:fuzz PROPERTIES TIMEOUT 600)

# Per-case ctest entries: one add_test per case, generated at build time.
# Requires a second configure after building (see ward_add_per_case_tests docs).
ward_add_per_case_tests(ward_self_tests)

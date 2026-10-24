# ward unit test framework
cmake_minimum_required(VERSION 3.22)

project(ward VERSION 0.1.0 LANGUAGES C)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/wardHelpers.cmake)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Export compile_commands.json so clangd (and friends) see the real flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WARD_BUILD_TESTS "Build the ward self tests" ${PROJECT_IS_TOP_LEVEL})
option(WARD_ENABLE_IPO "Enable interprocedural optimization for ward" OFF)

add_library(ward STATIC)
add_library(ward::ward ALIAS ward)

target_sources(ward PRIVATE
    src/internal/discover.c
    src/internal/options.c
    src/internal/proc.c
    src/internal/report.c
    src/internal/serial.c
    src/internal/ward.c
    src/support/arena.c
    src/support/fatal.c
    src/support/map.c
    src/support/str.c
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(ward PRIVATE src/internal/proc_linux.c)
endif()

target_include_directories(ward
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(ward PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION ${WARD_ENABLE_IPO}
)

target_compile_options(ward PRIVATE
    -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes
    -Wpointer-arith -Wwrite-strings -Wformat=2 -Wconversion -Wsign-conversion
    -fno-omit-frame-pointer
)

if(WARD_BUILD_TESTS)
    add_subdirectory(test)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------

install(TARGETS ward EXPORT wardTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT wardTargets
    FILE wardTargets.cmake
    NAMESPACE ward::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ward
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/wardConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(cmake/wardConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/wardConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ward
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/wardConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/wardConfigVersion.cmake"
    cmake/wardHelpers.cmake
    cmake/generate_per_case.cmake
    cmake/run_list_tests.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ward
)

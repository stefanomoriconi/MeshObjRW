# =============================================================================
# czmesh - fast, portable OBJ triangular-mesh reader/writer
# Shared library (C-ABI) + CLI + tests + optional CUDA.
# =============================================================================
cmake_minimum_required(VERSION 3.16)

project(czmesh
    VERSION 1.0.0
    DESCRIPTION "Fast, portable OBJ triangular-mesh reader/writer (ASCII + czobj binary)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Default to Release build when the user has not chosen one.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(CZMESH_BUILD_TESTS  "Build czmesh C test suite"          ON)
option(CZMESH_BUILD_CLI    "Build the czmesh_cli command-line tool" ON)
option(CZMESH_ENABLE_OPENMP "Enable OpenMP parallelism if available" ON)
option(CZMESH_ENABLE_CUDA  "Enable CUDA GPU port (requires nvcc)"   OFF)

# -----------------------------------------------------------------------------
# OpenMP (optional, auto-detected)
# -----------------------------------------------------------------------------
if(CZMESH_ENABLE_OPENMP)
    find_package(OpenMP QUIET)
    if(OpenMP_CXX_FOUND)
        message(STATUS "czmesh: OpenMP enabled")
    else()
        message(STATUS "czmesh: OpenMP not found - building sequential")
    endif()
endif()

# -----------------------------------------------------------------------------
# Shared library
# -----------------------------------------------------------------------------
set(CZMESH_CORE_SOURCES
    src/czmesh.cpp
    src/czmesh_ascii.cpp
    src/czmesh_binary.cpp
)

add_library(czmesh SHARED ${CZMESH_CORE_SOURCES})
add_library(czmesh::czmesh ALIAS czmesh)

target_include_directories(czmesh
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(czmesh PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME czmesh
    CXX_VISIBILITY_PRESET default
)

target_compile_features(czmesh PUBLIC cxx_std_17)

if(OpenMP_CXX_FOUND)
    target_link_libraries(czmesh PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(czmesh PRIVATE CZMESH_HAVE_OPENMP)
endif()

# Sensible, portable warning flags.
if(MSVC)
    target_compile_options(czmesh PRIVATE /W4 /permissive-)
    target_compile_definitions(czmesh PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(czmesh PRIVATE -Wall -Wextra -Wpedantic)
endif()

# -----------------------------------------------------------------------------
# Optional CUDA port
# -----------------------------------------------------------------------------
if(CZMESH_ENABLE_CUDA)
    enable_language(CUDA)
    list(APPEND CZMESH_CORE_SOURCES src/czmesh_cuda.cu)
    target_compile_definitions(czmesh PRIVATE CZMESH_ENABLE_CUDA)
    set_target_properties(czmesh PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
    message(STATUS "czmesh: CUDA enabled")
endif()

# -----------------------------------------------------------------------------
# CLI tool
# -----------------------------------------------------------------------------
if(CZMESH_BUILD_CLI)
    add_executable(czmesh_cli tools/czmesh_cli.cpp)
    target_link_libraries(czmesh_cli PRIVATE czmesh::czmesh)
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
if(CZMESH_BUILD_TESTS)
    enable_testing()
    add_executable(czmesh_tests tests/czmesh_tests.cpp)
    target_link_libraries(czmesh_tests PRIVATE czmesh::czmesh)
    add_test(NAME czmesh_tests COMMAND czmesh_tests)
endif()

# -----------------------------------------------------------------------------
# Install rules
# -----------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS czmesh
    EXPORT czmeshTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(CZMESH_BUILD_CLI)
    install(TARGETS czmesh_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT czmeshTargets
    FILE czmeshConfig.cmake
    NAMESPACE czmesh::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/czmesh
)

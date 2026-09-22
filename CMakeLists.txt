# =============================================================================
# objrw - fast, portable OBJ triangular-mesh reader/writer
# Shared library (C-ABI) + CLI + tests + optional CUDA.
# =============================================================================
cmake_minimum_required(VERSION 3.16)

project(objrw
    VERSION 1.0.0
    DESCRIPTION "Fast, portable OBJ triangular-mesh reader/writer (ASCII + objrw binary)"
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
option(OBJRW_BUILD_TESTS  "Build objrw C test suite"          ON)
option(OBJRW_BUILD_CLI    "Build the objrw_cli command-line tool" ON)
option(OBJRW_ENABLE_OPENMP "Enable OpenMP parallelism if available" ON)
option(OBJRW_ENABLE_CUDA  "Enable CUDA GPU port (requires nvcc)"   OFF)

# -----------------------------------------------------------------------------
# OpenMP (optional, auto-detected)
# -----------------------------------------------------------------------------
if(OBJRW_ENABLE_OPENMP)
    find_package(OpenMP QUIET)
    if(OpenMP_CXX_FOUND)
        message(STATUS "objrw: OpenMP enabled")
    else()
        message(STATUS "objrw: OpenMP not found - building sequential")
    endif()
endif()

# -----------------------------------------------------------------------------
# Shared library
# -----------------------------------------------------------------------------
set(OBJRW_CORE_SOURCES
    src/objrw.cpp
    src/objrw_ascii.cpp
    src/objrw_binary.cpp
)

add_library(objrw SHARED ${OBJRW_CORE_SOURCES})
add_library(objrw::objrw ALIAS objrw)

target_include_directories(objrw
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(objrw PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME objrw
    CXX_VISIBILITY_PRESET default
)

target_compile_features(objrw PUBLIC cxx_std_17)

if(OpenMP_CXX_FOUND)
    target_link_libraries(objrw PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(objrw PRIVATE OBJRW_HAVE_OPENMP)
endif()

# Sensible, portable warning flags.
if(MSVC)
    target_compile_options(objrw PRIVATE /W4 /permissive-)
    target_compile_definitions(objrw PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(objrw PRIVATE -Wall -Wextra -Wpedantic)
endif()

# -----------------------------------------------------------------------------
# Optional CUDA port
# -----------------------------------------------------------------------------
if(OBJRW_ENABLE_CUDA)
    enable_language(CUDA)
    list(APPEND OBJRW_CORE_SOURCES src/objrw_cuda.cu)
    target_compile_definitions(objrw PRIVATE OBJRW_ENABLE_CUDA)
    set_target_properties(objrw PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
    message(STATUS "objrw: CUDA enabled")
endif()

# -----------------------------------------------------------------------------
# CLI tool
# -----------------------------------------------------------------------------
if(OBJRW_BUILD_CLI)
    add_executable(objrw_cli tools/objrw_cli.cpp)
    target_link_libraries(objrw_cli PRIVATE objrw::objrw)
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
if(OBJRW_BUILD_TESTS)
    enable_testing()
    add_executable(objrw_tests tests/objrw_tests.cpp)
    target_link_libraries(objrw_tests PRIVATE objrw::objrw)
    add_test(NAME objrw_tests COMMAND objrw_tests)
endif()

# -----------------------------------------------------------------------------
# Install rules
# -----------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS objrw
    EXPORT objrwTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(OBJRW_BUILD_CLI)
    install(TARGETS objrw_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT objrwTargets
    FILE objrwConfig.cmake
    NAMESPACE objrw::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/objrw
)

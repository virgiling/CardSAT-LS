cmake_minimum_required(VERSION 3.15)

project(CardSAT-LS VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(CARDSAT_NATIVE_OPTIMIZATION
       "Optimize the solver for the CPU used to build it" ON)
option(CARDSAT_ENABLE_IPO "Enable interprocedural optimization" ON)
set(CARDSAT_CCDCL_JOBS "2" CACHE STRING
    "Parallel jobs used while building the vendored CCDCL library")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEBUG)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        add_compile_options(-ggdb)
    endif()
elseif(CARDSAT_NATIVE_OPTIMIZATION AND
       CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-march=native)
endif()

if(CARDSAT_ENABLE_IPO AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT CARDSAT_IPO_SUPPORTED OUTPUT CARDSAT_IPO_ERROR)
    if(CARDSAT_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    else()
        message(WARNING "IPO is unavailable: ${CARDSAT_IPO_ERROR}")
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_subdirectory(src)

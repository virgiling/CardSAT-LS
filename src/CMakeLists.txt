add_library(LocalSearch STATIC
    LocalSearch/cardsat.cpp
    LocalSearch/heuristic.cpp
    LocalSearch/init.cpp
    LocalSearch/reader.cpp
    LocalSearch/weight.cpp
)

add_library(Preprocess STATIC
    PreProcess/binary.cpp
    PreProcess/card.cpp
    PreProcess/preprocess.cpp
    PreProcess/propagation.cpp
    PreProcess/resolution.cpp
)

add_library(Utils STATIC
    utils/bitset.cpp
    utils/hashmap.cpp
    utils/parse.cpp
)

set(CCDCL_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/ccdcl")
if(CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR
        "The vendored CCDCL build requires a single-configuration generator")
endif()
set(CCDCL_BUILD_ROOT "${CMAKE_CURRENT_BINARY_DIR}/ccdcl")
set(CCDCL_STATIC_LIB "${CCDCL_BUILD_ROOT}/build/libcadical.a")
file(GLOB_RECURSE CCDCL_SOURCE_DEPENDENCIES CONFIGURE_DEPENDS
    "${CCDCL_SRC_DIR}/src/*"
    "${CCDCL_SRC_DIR}/scripts/*")
list(APPEND CCDCL_SOURCE_DEPENDENCIES
    "${CCDCL_SRC_DIR}/configure"
    "${CCDCL_SRC_DIR}/makefile.in"
    "${CCDCL_SRC_DIR}/VERSION")

find_program(CARDSAT_MAKE_EXECUTABLE NAMES gmake make)
if(NOT CARDSAT_MAKE_EXECUTABLE)
    message(FATAL_ERROR "GNU Make is required for CCDCL")
endif()
execute_process(
    COMMAND "${CARDSAT_MAKE_EXECUTABLE}" --version
    RESULT_VARIABLE CCDCL_MAKE_VERSION_RESULT
    OUTPUT_VARIABLE CCDCL_MAKE_VERSION_OUTPUT
    ERROR_QUIET)
if(NOT CCDCL_MAKE_VERSION_RESULT EQUAL 0 OR
   NOT CCDCL_MAKE_VERSION_OUTPUT MATCHES "GNU Make")
    message(FATAL_ERROR "CCDCL requires GNU Make")
endif()

set(CCDCL_CONFIGURE_OPTIONS --quiet -O3)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CCDCL_CONFIGURE_OPTIONS --quiet --debug)
endif()

set(CCDCL_CXXFLAGS "${CMAKE_CXX_FLAGS}")
if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" CCDCL_BUILD_TYPE_UPPER)
    string(APPEND CCDCL_CXXFLAGS
        " ${CMAKE_CXX_FLAGS_${CCDCL_BUILD_TYPE_UPPER}}")
endif()

set(CCDCL_CONFIG_STAMP "${CMAKE_CURRENT_BINARY_DIR}/ccdcl-config.txt")
file(GENERATE OUTPUT "${CCDCL_CONFIG_STAMP}" CONTENT
    "compiler=${CMAKE_CXX_COMPILER}\nid=${CMAKE_CXX_COMPILER_ID}\nversion=${CMAKE_CXX_COMPILER_VERSION}\nbuild_type=${CMAKE_BUILD_TYPE}\nflags=${CCDCL_CXXFLAGS}\noptions=${CCDCL_CONFIGURE_OPTIONS}\n")

add_custom_command(
    OUTPUT "${CCDCL_STATIC_LIB}"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CCDCL_BUILD_ROOT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CCDCL_BUILD_ROOT}"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${CCDCL_SRC_DIR}/configure"
            "${CCDCL_SRC_DIR}/makefile.in"
            "${CCDCL_SRC_DIR}/VERSION"
            "${CCDCL_BUILD_ROOT}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CCDCL_SRC_DIR}/src" "${CCDCL_BUILD_ROOT}/src"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CCDCL_SRC_DIR}/scripts" "${CCDCL_BUILD_ROOT}/scripts"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${CCDCL_BUILD_ROOT}"
            "${CMAKE_COMMAND}" -E env
            "CXX=${CMAKE_CXX_COMPILER}"
            "CXXFLAGS=${CCDCL_CXXFLAGS}"
            /bin/sh ./configure ${CCDCL_CONFIGURE_OPTIONS}
    COMMAND "${CARDSAT_MAKE_EXECUTABLE}"
            -C "${CCDCL_BUILD_ROOT}/build"
            -j${CARDSAT_CCDCL_JOBS} libcadical.a
    DEPENDS ${CCDCL_SOURCE_DEPENDENCIES} "${CCDCL_CONFIG_STAMP}"
    COMMENT "Building an isolated CCDCL library"
    VERBATIM
)

add_library(ccdcl_interface INTERFACE)
target_include_directories(ccdcl_interface INTERFACE "${CCDCL_SRC_DIR}/src")

add_custom_target(build_ccdcl DEPENDS "${CCDCL_STATIC_LIB}")
add_library(ccdcl_imported STATIC IMPORTED GLOBAL)
set_target_properties(ccdcl_imported PROPERTIES
    IMPORTED_LOCATION "${CCDCL_STATIC_LIB}"
)
add_dependencies(ccdcl_imported build_ccdcl)

target_include_directories(LocalSearch PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_include_directories(Preprocess PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_include_directories(Utils PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_executable(cardsat main.cpp solver.cpp)
add_dependencies(cardsat build_ccdcl)
target_link_libraries(cardsat PRIVATE
    LocalSearch Preprocess Utils ccdcl_interface ccdcl_imported)
target_include_directories(cardsat PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

install(TARGETS cardsat LocalSearch Preprocess Utils)

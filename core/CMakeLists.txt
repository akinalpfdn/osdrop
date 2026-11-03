cmake_minimum_required(VERSION 3.28)

# The version lives in the public header so C, Swift and C# consumers see the same numbers.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/osdrop/osdrop.h" _osdrop_version_lines
     REGEX "^#define OSDROP_CORE_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(_line IN LISTS _osdrop_version_lines)
    string(REGEX MATCH "OSDROP_CORE_VERSION_([A-Z]+) ([0-9]+)" _ "${_line}")
    set(_osdrop_version_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
endforeach()

project(OsdropCore
    VERSION "${_osdrop_version_MAJOR}.${_osdrop_version_MINOR}.${_osdrop_version_PATCH}"
    DESCRIPTION "Shared protocol, crypto, transport and input model library for Osdrop"
    LANGUAGES C CXX)

option(OSDROP_CORE_BUILD_TESTS "Build the core unit tests" ${PROJECT_IS_TOP_LEVEL})
option(OSDROP_CORE_BUILD_FUZZER "Build the libFuzzer decoder fuzzers (Clang only)" OFF)

if(OSDROP_CORE_BUILD_FUZZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "OSDROP_CORE_BUILD_FUZZER requires Clang with libFuzzer for C and C++")
    endif()
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(NOT TARGET osdrop::warnings)
    include(cmake/OsdropWarnings.cmake)
endif()

# Third-party C, built without Osdrop's warning policy (docs/THIRD_PARTY.md).
add_library(osdrop_monocypher STATIC third_party/monocypher/monocypher.c)
target_include_directories(osdrop_monocypher PUBLIC third_party/monocypher)

add_library(osdrop_core STATIC
    src/capi/discovery_capi.cpp
    src/capi/identity_capi.cpp
    src/capi/mesh_capi.cpp
    src/capi/protocol_capi.cpp
    src/capi/version.cpp
    src/crypto/input_crypto.cpp
    src/crypto/noise.cpp
    src/crypto/primitives.cpp
    src/protocol/advertisement.cpp
    src/protocol/control_codec.cpp
    src/protocol/input_codec.cpp
    src/protocol/text.cpp
    src/protocol/transfer_codec.cpp
    src/mesh/mesh.cpp
    src/mesh/transfer_channel.cpp
    src/protocol/wire.cpp
    src/session/control_framer.cpp
    src/session/session_engine.cpp
    src/transport/asio_node.cpp
    src/transport/traffic_class.cpp)
add_library(osdrop::core ALIAS osdrop_core)

target_include_directories(osdrop_core
    PUBLIC include
    PRIVATE src)
target_compile_features(osdrop_core PUBLIC cxx_std_20)
set_target_properties(osdrop_core PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(osdrop_core PRIVATE osdrop_monocypher osdrop::warnings)

# Asio is header-only; SYSTEM keeps its headers out of Osdrop's warning policy. The tests use it
# too, so it is an interface target rather than settings on osdrop_core.
find_package(Threads REQUIRED)
add_library(osdrop_asio INTERFACE)
target_include_directories(osdrop_asio SYSTEM INTERFACE third_party/asio/include)
target_compile_definitions(osdrop_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(osdrop_asio INTERFACE Threads::Threads)
if(WIN32)
    target_compile_definitions(osdrop_asio INTERFACE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(osdrop_asio INTERFACE ws2_32 mswsock)
endif()
target_link_libraries(osdrop_core PRIVATE osdrop_asio)

if(OSDROP_CORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/cpp)
endif()

add_subdirectory(fuzz)

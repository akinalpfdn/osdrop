include(FetchContent)

FetchContent_Declare(googletest
    URL https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# Match the MSVC runtime used by the rest of the build: the DLL runtime by default, the static one
# when the build asks for it (the Windows release preset).
if(CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
else()
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(osdrop_core_tests
    capi_discovery_test.cpp
    capi_protocol_test.cpp
    protocol_codec_test.cpp
    protocol_fixture_test.cpp
    version_test.cpp)
# Tests reach internal headers; only the C header is public API.
target_include_directories(osdrop_core_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(osdrop_core_tests PRIVATE
    OSDROP_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../protocol/fixtures")
target_link_libraries(osdrop_core_tests PRIVATE osdrop::core osdrop::warnings GTest::gtest_main)
set_target_properties(osdrop_core_tests PROPERTIES CXX_EXTENSIONS OFF)

include(GoogleTest)
gtest_discover_tests(osdrop_core_tests DISCOVERY_MODE PRE_TEST)

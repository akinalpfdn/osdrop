# Platform-independent pieces of the Windows build (names, protocols, mappings), kept free of
# Windows headers so they can be unit tested and linted on every host.
add_library(osdrop_win_shared STATIC
    src/clipboard_sync.cpp
    src/clipboard_text.cpp
    src/group_engine.cpp
    src/product_names.cpp
    src/screens.cpp
    src/txt_mapping.cpp)
add_library(osdrop::win_shared ALIAS osdrop_win_shared)

target_include_directories(osdrop_win_shared PUBLIC include)
target_compile_features(osdrop_win_shared PUBLIC cxx_std_20)
set_target_properties(osdrop_win_shared PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(osdrop_win_shared PUBLIC osdrop::core PRIVATE osdrop::warnings)

if(OSDROP_CORE_BUILD_TESTS)
    add_executable(osdrop_win_shared_tests
        tests/clipboard_sync_test.cpp
        tests/clipboard_text_test.cpp
        tests/group_engine_test.cpp
        tests/product_names_test.cpp
        tests/screens_test.cpp
        tests/txt_mapping_test.cpp)
    target_link_libraries(osdrop_win_shared_tests PRIVATE osdrop::win_shared osdrop::warnings GTest::gtest_main)
    set_target_properties(osdrop_win_shared_tests PROPERTIES CXX_EXTENSIONS OFF)
    include(GoogleTest)
    gtest_discover_tests(osdrop_win_shared_tests DISCOVERY_MODE PRE_TEST)
endif()

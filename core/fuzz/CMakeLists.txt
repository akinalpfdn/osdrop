set(OSDROP_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/fixtures")

# One target per decoder, so each one's iteration rate and corpus stay its own.
set(OSDROP_FUZZ_TARGETS control input transfer discovery)

foreach(target IN LISTS OSDROP_FUZZ_TARGETS)
    # Replays the fixtures and seeds through each entry point in every test run.
    if(OSDROP_CORE_BUILD_TESTS)
        add_executable(osdrop_${target}_fuzz_replay ${target}_fuzz.cpp replay_main.cpp)
        target_include_directories(osdrop_${target}_fuzz_replay PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(osdrop_${target}_fuzz_replay PRIVATE osdrop::core osdrop::warnings)
        set_target_properties(osdrop_${target}_fuzz_replay PROPERTIES CXX_EXTENSIONS OFF)
        add_test(NAME ${target}_fuzz_replay
            COMMAND osdrop_${target}_fuzz_replay ${OSDROP_FIXTURES_DIR}
                    ${CMAKE_CURRENT_SOURCE_DIR}/seeds)
    endif()

    # libFuzzer binaries; the whole core is instrumented when OSDROP_CORE_BUILD_FUZZER is on.
    if(OSDROP_CORE_BUILD_FUZZER)
        add_executable(osdrop_${target}_fuzzer ${target}_fuzz.cpp)
        target_include_directories(osdrop_${target}_fuzzer PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(osdrop_${target}_fuzzer PRIVATE osdrop::core osdrop::warnings)
        target_link_options(osdrop_${target}_fuzzer PRIVATE -fsanitize=fuzzer)
        set_target_properties(osdrop_${target}_fuzzer PROPERTIES CXX_EXTENSIONS OFF)
    endif()
endforeach()

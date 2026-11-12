cmake_minimum_required(VERSION 3.28)

project(OsdropWindows
    DESCRIPTION "Osdrop native engine, input process and service for Windows"
    LANGUAGES CXX)

enable_testing()

set(OSDROP_CORE_OPTIMIZE_DEBUG ON)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../core" core)
add_subdirectory(shared)

if(WIN32)
    add_subdirectory(system)
    add_subdirectory(engine)
    add_subdirectory(input)
    add_subdirectory(service)

    # One folder for every binary, as the installer lays them out.
    foreach(target osdrop_engine osdrop_input osdrop_input_tool osdrop_service)
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endforeach()
else()
    # Other hosts build the platform-independent libraries and tests only (the "portable" preset),
    # so Windows logic is tested and linted on every platform.
    message(STATUS "Non-Windows host: building portable Windows libraries and tests only")
endif()

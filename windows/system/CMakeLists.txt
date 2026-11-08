# Win32 pieces shared by OsdropInput, OsdropService, the input tool and the engine: handles, the
# log file, sessions and their users, and the input pipe's client.
add_library(osdrop_win_system STATIC
    src/file_log.cpp
    src/input_client.cpp
    src/session.cpp)
add_library(osdrop::win_system ALIAS osdrop_win_system)

target_include_directories(osdrop_win_system PUBLIC include)
target_compile_features(osdrop_win_system PUBLIC cxx_std_20)
target_compile_definitions(osdrop_win_system PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osdrop_win_system
    PUBLIC osdrop::win_shared advapi32 wtsapi32
    PRIVATE osdrop::warnings)
set_target_properties(osdrop_win_system PROPERTIES CXX_EXTENSIONS OFF)

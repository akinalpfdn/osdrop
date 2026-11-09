# A GUI-subsystem executable: it runs inside the user's session without a console window.
add_executable(osdrop_input WIN32
    src/cursors.cpp
    src/hooks.cpp
    src/injector.cpp
    src/main.cpp
    src/pipe_server.cpp)

target_compile_features(osdrop_input PRIVATE cxx_std_20)
target_compile_definitions(osdrop_input PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osdrop_input PRIVATE osdrop::win_system osdrop::win_shared osdrop::warnings user32)
set_target_properties(osdrop_input PROPERTIES
    OUTPUT_NAME OsdropInput
    CXX_EXTENSIONS OFF)

# The developer tool: captures this PC's input through OsdropInput, then plays it back.
add_executable(osdrop_input_tool
    tool/main.cpp)

target_compile_features(osdrop_input_tool PRIVATE cxx_std_20)
target_compile_definitions(osdrop_input_tool PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osdrop_input_tool PRIVATE osdrop::win_system osdrop::win_shared osdrop::warnings user32)
set_target_properties(osdrop_input_tool PROPERTIES
    OUTPUT_NAME OsdropInputTool
    CXX_EXTENSIONS OFF)

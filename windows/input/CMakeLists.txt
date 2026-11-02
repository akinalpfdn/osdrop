# A GUI-subsystem executable: it runs inside the user's session without a console window.
add_executable(osdrop_input WIN32
    src/main.cpp)

target_compile_features(osdrop_input PRIVATE cxx_std_20)
target_compile_definitions(osdrop_input PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osdrop_input PRIVATE osdrop::win_shared osdrop::warnings)
set_target_properties(osdrop_input PROPERTIES
    OUTPUT_NAME OsdropInput
    CXX_EXTENSIONS OFF)

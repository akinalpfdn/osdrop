add_executable(osdrop_service
    src/main.cpp)

target_compile_features(osdrop_service PRIVATE cxx_std_20)
target_compile_definitions(osdrop_service PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osdrop_service PRIVATE osdrop::win_shared osdrop::warnings)
set_target_properties(osdrop_service PROPERTIES
    OUTPUT_NAME OsdropService
    CXX_EXTENSIONS OFF)

add_library(osdrop_engine SHARED
    src/capi/engine_capi.cpp
    src/clipboard/wic_images.cpp
    src/clipboard/win_clipboard.cpp
    src/discovery/dns_sd_discovery.cpp
    src/drag/drag_starter.cpp
    src/drag/edge_targets.cpp
    src/drag/loopback.cpp
    src/input/input_bridge.cpp
    src/screens/monitors.cpp
    src/store/dpapi_store.cpp)

target_include_directories(osdrop_engine
    PUBLIC include
    PRIVATE src)
target_compile_features(osdrop_engine PRIVATE cxx_std_20)
target_compile_definitions(osdrop_engine PRIVATE OSDROP_ENGINE_BUILDING UNICODE _UNICODE NOMINMAX
    WIN32_LEAN_AND_MEAN)
target_link_libraries(osdrop_engine
    PRIVATE osdrop::core osdrop::win_shared osdrop::win_system osdrop::warnings
        bcrypt crypt32 dnsapi gdi32 ole32 shcore shell32 user32 uuid windowscodecs)
set_target_properties(osdrop_engine PROPERTIES
    OUTPUT_NAME OsdropEngine
    CXX_EXTENSIONS OFF)

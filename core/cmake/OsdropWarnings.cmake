# Warning policy shared by every native Osdrop target: warnings are errors on all compilers.
add_library(osdrop_warnings INTERFACE)
add_library(osdrop::warnings ALIAS osdrop_warnings)

if(MSVC)
    target_compile_options(osdrop_warnings INTERFACE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
else()
    target_compile_options(osdrop_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Werror)
endif()

#include <osdrop/osdrop.h>

extern "C" osdrop_version osdrop_core_version() {
    return osdrop_version{
        .major = OSDROP_CORE_VERSION_MAJOR,
        .minor = OSDROP_CORE_VERSION_MINOR,
        .patch = OSDROP_CORE_VERSION_PATCH,
    };
}

#pragma once

// This PC's monitors in physical pixels, as a per-monitor DPI aware thread sees them.

#include <osdrop/win/screens.hpp>

#include <vector>

namespace osdrop::engine {

[[nodiscard]] std::vector<win::PhysicalMonitor> physical_monitors();

} // namespace osdrop::engine

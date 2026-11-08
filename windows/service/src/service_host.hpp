#pragma once

namespace osdrop::service {

/// Connects to the Service Control Manager and runs the service until it is stopped. False when
/// the process was not started by the SCM.
[[nodiscard]] bool run_as_service();

} // namespace osdrop::service

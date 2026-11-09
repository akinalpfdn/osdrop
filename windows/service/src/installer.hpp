#pragma once

namespace osdrop::service {

/// Registers this executable as the auto-start service (or points an existing one at it) and
/// starts it. Needs an administrator; returns the process exit code.
[[nodiscard]] int install_service();
/// Stops and removes the service; 0 when it was not installed.
[[nodiscard]] int uninstall_service();
/// Stops the service and waits, so its files can be replaced.
[[nodiscard]] int stop_service();

} // namespace osdrop::service

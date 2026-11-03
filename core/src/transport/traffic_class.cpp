#include "transport/traffic_class.hpp"

#ifdef __APPLE__
#include <sys/socket.h>
#endif

namespace osdrop::transport {
namespace {

#ifdef __APPLE__
bool set_service_type(int socket, int service_type) {
    return setsockopt(socket, SOL_SOCKET, SO_NET_SERVICE_TYPE, &service_type,
                      sizeof(service_type)) == 0;
}
#endif

} // namespace

bool mark_as_interactive_voice([[maybe_unused]] asio::ip::udp::socket& socket) {
#ifdef __APPLE__
    return socket.is_open() && set_service_type(socket.native_handle(), NET_SERVICE_TYPE_VO);
#else
    return false;
#endif
}

bool mark_as_signaling([[maybe_unused]] asio::ip::tcp::socket& socket) {
#ifdef __APPLE__
    return socket.is_open() && set_service_type(socket.native_handle(), NET_SERVICE_TYPE_SIG);
#else
    return false;
#endif
}

} // namespace osdrop::transport

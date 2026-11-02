#include <osdrop/win/product_names.hpp>

namespace osdrop::win {

std::wstring input_pipe_name(std::uint32_t session_id) {
    std::wstring name{kInputPipePrefix};
    name += std::to_wstring(session_id);
    return name;
}

} // namespace osdrop::win

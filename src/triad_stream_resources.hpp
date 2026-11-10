#pragma once

#include <cstddef>

namespace datapump::triad::v3::detail {

std::size_t stream_controller_footprint_bytes();
std::size_t stream_worker_stack_bytes();

} // namespace datapump::triad::v3::detail

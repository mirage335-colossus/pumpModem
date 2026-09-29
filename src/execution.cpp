#include "datapump/execution.hpp"

namespace datapump::execution {
Context current_context() noexcept {
    static thread_local unsigned char identity;
    return reinterpret_cast<Context>(&identity);
}
unsigned concurrency() noexcept { return std::thread::hardware_concurrency(); }
}

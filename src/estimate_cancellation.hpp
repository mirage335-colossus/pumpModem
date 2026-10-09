#pragma once
#include "datapump/execution.hpp"
#include <exception>
#include <stop_token>

namespace datapump::estimate_detail {
// Cancellation is not an invalid geometry or a partial probability result.
// Keep it separate from Error so numerical fallback handlers cannot swallow it.
struct Cancelled final : std::exception {
    const char* what() const noexcept override { return "Calculation cancelled"; }
};
inline void check(std::stop_token stop) {
    if(!stop.stop_possible())return;
    if(stop.stop_requested())throw Cancelled{};
    execution::checkpoint();
    if(stop.stop_requested())throw Cancelled{};
}
template<class Lock> void lock(Lock& lock,std::stop_token stop) {
    check(stop);
    if(!stop.stop_possible()) {lock.lock();return;}
    while(!lock.try_lock()) {
        check(stop);execution::sleep_for(std::chrono::milliseconds(1));
    }
    check(stop);
}
}

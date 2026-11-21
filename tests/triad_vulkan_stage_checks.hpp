#pragma once

// Fine-stage device checks of test_triad_vulkan: --gain-rank, --drift and
// --varying construct the Vulkan backend. The device-free policy checks are
// test_triad_vulkan_stage_policy, which every build has.

#include "datapump/triad_acceleration.hpp"
#include "triad_receiver_bulk.hpp"

#include <functional>
#include <span>
#include <stop_token>

namespace datapump::triad::v3::test {

void vulkan_gain_rank_checks(std::stop_token);
void vulkan_drift_checks(std::stop_token);
void vulkan_varying_checks(std::stop_token);

} // namespace datapump::triad::v3::test

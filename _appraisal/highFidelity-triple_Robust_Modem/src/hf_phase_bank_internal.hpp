#pragma once

#include <span>

namespace appraisal::detail {
// Shared log-mean-exp used for model and bit priors. The kernel has no input
// for a model selection, so duplicate copies of the entire bank are invariant.
long double hf_phase_log_mean_exp(std::span<const long double> scores);
}

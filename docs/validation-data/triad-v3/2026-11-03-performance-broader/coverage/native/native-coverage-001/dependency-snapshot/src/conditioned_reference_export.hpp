#pragma once
// PRIVATE metadata export; not a maintained receiver or wire API.
#include "pattern_fft_batch.hpp"
#include <string>
namespace datapump::modem::detail {
struct ConditionedReferenceDescriptor {
    Config config;
    FftSearchGeometry geometry;
    std::vector<FftSearchJob> jobs;
    std::vector<std::uint64_t> source_offsets,phase_uppers;
    std::size_t original_starts=0,original_pairs=0,initial_symbols=0;
    std::uint64_t original_trial_charge=0,original_threshold_trials=0;
    double original_acquisition_threshold=0;
    double frequency_cell_radius_hz=0; // Actual native operator; not generated RF lattice spacing.
    bool competing_frequencies=false;
    std::size_t full_input_samples=0,pristine_workspace_bytes=0;
    // Retained descriptor struct, vector capacities and reason capacity+NUL.
    // Shared Config key/provider ownership and external receiver state are
    // separate. This is not temporary/allocator/runtime peak accounting.
    std::size_t descriptor_bytes=0;
    std::string reason;
};
}

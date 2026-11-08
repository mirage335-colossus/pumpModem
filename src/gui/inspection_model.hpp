#pragma once
#include "datapump/transfer.hpp"
#include <complex>
#include <optional>
#include <string>
#include <vector>

namespace datapump::gui {
enum class InspectionState { active, off, unavailable };
struct Step {
    std::string title,detail;
    InspectionState state=InspectionState::active;
};
struct FlowLane { std::string label; std::vector<Step> steps; };
struct StructureSection {
    std::string title,detail;
    std::optional<std::size_t> bytes,symbols;
    std::optional<double> duration_seconds;
    // Logical components describe fields within the fixed coded intervals;
    // they add no separate airtime to the physical sequence.
    bool logical=false;
    bool coding=false;
};
struct Field { std::string name,value; };
struct Constellation {
    std::string title,detail;
    std::vector<std::complex<double>> points;
};
struct Inspection {
    std::string title,summary;
    bool binary=false;
    // Empty GUI drafts use a single raw zero only for analytical estimates.
    bool preview_only=false;
    std::vector<FlowLane> lanes;
    std::vector<StructureSection> sections;
    std::vector<Field> fields;
    std::vector<Constellation> constellations;
    std::string preamble_description,chip_description;
    transfer::Estimate estimate;
    std::optional<StreamLayout> stream_layout;
};
struct InspectionRequest {
    Message message;
    std::optional<Bytes> binary;
    transfer::Options options;
    std::string requested_pattern;
    double target_snr=0;
    bool simulation=false;
    std::string device;
};
// UI boundary for the replacement triad backend. No waveform or receiver
// estimate is produced while that backend is disconnected.
Inspection inspect(const InspectionRequest& request,std::stop_token stop = {});
}

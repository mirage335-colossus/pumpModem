#pragma once
#include "datapump/transfer.hpp"
#include "datapump/lpi_estimate.hpp"
#include "pattern_space.hpp"
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
    std::vector<FlowLane> lanes;
    std::vector<StructureSection> sections;
    std::vector<Field> fields;
    std::vector<Constellation> constellations;
    std::optional<inspection::PatternSpace> pattern_space;
    std::string preamble_description,chip_description;
    transfer::Estimate estimate;
    lpi::Estimate lpi_estimate;
    std::string lpi_summary,lpi_description;
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
// Run alongside the transmission estimate worker. Encodes a bounded source to
// inspect its fixed interval layout; never generates a waveform or retains
// source data, metadata strings, key bytes, masks, or coded bytes in the model.
Inspection inspect(const InspectionRequest& request);
}

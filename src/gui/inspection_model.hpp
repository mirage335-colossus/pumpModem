#pragma once
#include "datapump/transfer.hpp"
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
    // These components describe the body before column interleaving; they
    // are not additional contiguous fields after the on-air sequence.
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
    std::optional<PacketLayout> packet_layout;
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
// Run alongside the transmission estimate worker. Encodes a bounded packet to
// inspect its actual layout; never generates a waveform or retains its data,
// metadata strings, key bytes, masks, or encoded packet in the returned model.
Inspection inspect(const InspectionRequest& request);
}

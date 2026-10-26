#pragma once
#include <cstdint>

namespace datapump::audio {
// A construction witness for the exact raw-to-logical support map. Unknown
// includes adjustable, initialized, transformed or discontinuous converters.
// The capture owner separately binds generation/provider/gauge identities and
// actually observed bounds; equal rates or a current timing status prove none
// of these facts. Origins are transport sample zero, never a keyed TX epoch.
enum class CaptureTransportKind : std::uint8_t {
    unknown,fixed_nominal_audio,sampled_physical_halfband
};
struct CaptureTransportRecipe {
    CaptureTransportKind kind=CaptureTransportKind::unknown;
    std::uint32_t raw_rate=0,logical_rate=0;
    std::uint64_t raw_zero=0,logical_zero=0;
    bool operator==(const CaptureTransportRecipe&)const=default;
};
static_assert(sizeof(CaptureTransportRecipe)<=32);
}

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace datapump::triad::v3 {

// Nested local evidence filters, not calibrated wrong-key probabilities or
// fixed sensitivity losses. Current preserves the baseline DATA decision.
enum class WrongKeyAdmission { current, guarded, strict, very_strict, extreme, maximum };
inline constexpr std::array wrong_key_admission_policies{
    WrongKeyAdmission::current, WrongKeyAdmission::guarded, WrongKeyAdmission::strict,
    WrongKeyAdmission::very_strict, WrongKeyAdmission::extreme, WrongKeyAdmission::maximum};
inline constexpr std::size_t wrong_key_admission_count = wrong_key_admission_policies.size();
const char* wrong_key_admission_name(WrongKeyAdmission);
WrongKeyAdmission parse_wrong_key_admission(std::string_view);

// Compact episode evidence for reversible presentation. Counts refer to
// baseline DATA bits, not received erasures or physical quiet. A missing or
// incompatible score makes the whole summary unavailable, never confident.
struct WrongKeyAdmissionEvidence {
    bool available = false;
    std::uint64_t data_bits = 0;
    std::array<std::uint64_t, wrong_key_admission_count> passing_bits{};
};

struct ReceiverSlotScore;
// Test an unmodified baseline score without copying DSP receipts or changing
// the decision. Non-DATA is false; it never becomes quiet or an admitted bit.
bool wrong_key_admission_passes(const ReceiverSlotScore&, WrongKeyAdmission);
// The caller supplies physically validated ordered slots. DATA counts remain
// complete even if its rating is unavailable; unavailable remains sticky.
void accumulate_wrong_key_admission(WrongKeyAdmissionEvidence&, const ReceiverSlotScore&);

} // namespace datapump::triad::v3

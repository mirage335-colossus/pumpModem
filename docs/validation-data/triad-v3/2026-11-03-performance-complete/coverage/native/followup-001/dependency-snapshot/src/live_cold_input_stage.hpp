#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace datapump::live::detail::cold_input {

// The caller owns DSP admission, worker joins and all ordinary
// decoding. No timestamp here proves an empty payload interval or physical END.
struct Identity {
    std::uint64_t generation=0, revision=0, clear_generation=0, capture_domain=0;
    bool operator==(const Identity&) const = default;
};
struct Aperture {
    // Counts come from the actual observer descriptor, in this logical clock.
    std::uint64_t startup_samples=0, maximum_fit_samples=0, cold_samples=0;
};
// A raw interval [a,a+N) maps outward to
// [floor(a*L/R),ceil((a+N)*L/R)). Integer raw origins have residues that are
// multiples of gcd(R,L), so the largest image has
// ceil((N*L + R-gcd(R,L))/R) samples. This is a lattice enclosure, not an
// extension of the measured fit aperture. Equal grids (and integer upsampling)
// add no rounding sample. Split products keep the full uint64 range checked.
inline std::optional<std::uint64_t> maximum_support_image_samples(
        std::uint64_t raw_samples,std::uint32_t raw_rate,std::uint32_t logical_rate) noexcept {
    if(!raw_samples || !raw_rate || !logical_rate)return {};
    const auto whole=raw_samples/raw_rate,remainder=raw_samples%raw_rate;
    if(whole>UINT64_MAX/logical_rate)return {};
    auto result=whole*logical_rate;
    const auto product=remainder*logical_rate; // both factors are below2^32.
    const auto integer=product/raw_rate;
    const auto residual=product%raw_rate+raw_rate-std::gcd(raw_rate,logical_rate);
    const auto carry=residual/raw_rate+(residual%raw_rate!=0);
    if(integer>UINT64_MAX-result || carry>UINT64_MAX-result-integer)return {};
    return result+integer+carry;
}
enum class EligibilityPolicy { all_long, descriptor_bounded };
struct Eligibility {
    std::size_t owner_count=0;
    bool all_owners_cold=false;
    std::uint64_t minimum_symbol_samples=0;
    EligibilityPolicy policy=EligibilityPolicy::all_long;
    // Explicit opt-in proof over every current AND admissible native/compact
    // owner. minimum_symbol_samples must be their actual minimum, not a
    // substituted long-symbol value or a received length/timecode estimate.
    bool all_current_and_admissible_supported_and_cold=false;
    bool accepts(std::uint32_t rate,std::uint64_t cold_deadline_samples=0) const noexcept {
        if(!rate || !owner_count || !all_owners_cold)return false;
        switch(policy) {
        case EligibilityPolicy::all_long:return minimum_symbol_samples>=60ULL*rate;
        case EligibilityPolicy::descriptor_bounded:
            return all_current_and_admissible_supported_and_cold && cold_deadline_samples &&
                minimum_symbol_samples>=cold_deadline_samples;
        }
        return false;
    }
};
struct JoinProof {
    // The caller must actually join the observer and every mandatory borrower.
    // These assertions describe a joined boundary; they do not join a thread.
    bool weak_worker_joined=false, mandatory_borrowers_joined=false;
    bool valid() const noexcept {return weak_worker_joined && mandatory_borrowers_joined;}
};
struct InvalidationProof {
    Identity invalidated;
    bool ordinary_capture_already_invalidated=false;
};
enum class Reason {
    none, qualified_frequency, deadline, reference_lost, storage_pressure,
    owner_no_longer_cold, capture_mismatch, explicit_fallback, capture_invalidated
};
enum class State { holding, frequency_drain, broad_drain, closed };
enum class Append { retained, release_broad, already_releasing };
struct Shape {
    Identity identity;
    std::uint64_t first=0, count=0;
    // Include every newly owned allocation's actual capacity: PCM, metadata,
    // nested views, handles/control allocated for this stage, allocator bounds.
    // Entry/Block inline storage is already in sizeof(Stage).
    std::size_t new_dynamic_bytes=0;
    // Native queue allocations remain charged to the existing queue grant.
    // Moving ownership must transfer that charge, never duplicate/free it.
    std::size_t externally_charged_bytes=0;
};
struct FrequencyCertificate {
    Identity identity;
    std::uint32_t sample_rate=0;
    std::uint64_t first=0, end=0; // Actual fit support, end exclusive.
    double frequency_hz=0, uncertainty_hz=0;
    bool independently_qualified=false, useful=false;
    // Deliberately no phase, punctuation, timestamp or payload authority.
};
struct Limits {
    Identity identity;
    std::uint32_t sample_rate=0;
    std::uint64_t first_sample=0;
    Aperture aperture;
    std::size_t granted_bytes=0;
};
struct LifecycleDiagnostic {
    void* context=nullptr;
    void (*unclosed)(void*) noexcept=nullptr;
};

// Block is the caller's typed, move-only ownership value. It must retain the
// original PCM, dates, timing, revision, Prepared/key/profile/stop association,
// origin/view map and capture provenance. front() exposes it only as const.
// There is no callback drain: take() first detaches one ownership value, so
// ordinary feed cannot re-enter this component with numeric loans borrowed.
//
// Every success/exception/cancellation path must either drain+close at a joined
// boundary or call abandon_invalidated after the ordinary generation was
// invalidated. Destruction is not a fallback. The optional diagnostic reports
// a violated caller precondition without letting optional work kill the app.
template<class Block, std::size_t MaximumBlocks=64>
class Stage {
    static_assert(MaximumBlocks>0);
    static_assert(std::is_nothrow_move_constructible_v<Block>);
public:
    struct Entry {Shape shape; Block block;};
    explicit Stage(Limits limits, LifecycleDiagnostic diagnostic={})
        : limits_(limits), diagnostic_(diagnostic), captured_end_(limits.first_sample) {
        const auto& a=limits_.aperture;
        if(!limits_.sample_rate || !a.startup_samples || !a.maximum_fit_samples ||
           a.maximum_fit_samples>a.startup_samples || a.startup_samples>a.cold_samples ||
           a.cold_samples>UINT64_MAX-limits_.first_sample)
            throw std::invalid_argument("invalid cold input aperture/clock");
        if(limits_.granted_bytes<sizeof(Stage))release_broad(Reason::storage_pressure);
    }
    Stage(const Stage&)=delete;
    Stage& operator=(const Stage&)=delete;
    Stage(Stage&&)=delete;
    Stage& operator=(Stage&&)=delete;
    ~Stage() noexcept {
        if(state_!=State::closed && diagnostic_.unclosed)diagnostic_.unclosed(diagnostic_.context);
    }
    static constexpr std::size_t fixed_bytes() noexcept {return sizeof(Stage);}
    State state() const noexcept {return state_;}
    Reason reason() const noexcept {return reason_;}
    std::size_t size() const noexcept {return count_;}
    bool empty() const noexcept {return !count_;}
    std::size_t owned_bytes() const noexcept {return sizeof(Stage)+dynamic_bytes_;}
    std::size_t externally_charged_bytes() const noexcept {return external_bytes_;}
    std::uint64_t captured_end() const noexcept {return captured_end_;}
    std::uint64_t maximum_fit_samples() const noexcept {return limits_.aperture.maximum_fit_samples;}
    // Staging never certifies processing. The ordinary caller advances its own
    // frontier only after the detached block's consumers have really joined.
    std::uint64_t processed_frontier_at_entry() const noexcept {return limits_.first_sample;}
    std::optional<std::uint64_t> deadline() const noexcept {return deadline_;}
    bool startup_ready() const noexcept {
        return captured_end_-limits_.first_sample>=limits_.aperture.startup_samples;
    }
    const FrequencyCertificate* frequency() const noexcept {
        return state_==State::frequency_drain && certificate_?&*certificate_:nullptr;
    }
    const Entry* front() const noexcept {return count_?&*entries_[head_]:nullptr;}
    std::uint64_t input_room_samples() const noexcept {
        if(state_!=State::holding)return 0;
        const auto end=deadline_.value_or(limits_.first_sample+limits_.aperture.cold_samples);
        return end>captured_end_?end-captured_end_:0;
    }
    Append append(Block&& block, const Shape& shape, const Eligibility& owners) noexcept {
        if(state_!=State::holding)return Append::already_releasing;
        if(!owners.accepts(limits_.sample_rate,limits_.aperture.cold_samples))return reject(Reason::owner_no_longer_cold);
        if(!(shape.identity==limits_.identity) || !shape.count || shape.first!=captured_end_ ||
           shape.count>UINT64_MAX-shape.first)return reject(Reason::capture_mismatch);
        if(shape.count>input_room_samples())return reject(Reason::deadline);
        if(count_==MaximumBlocks || shape.new_dynamic_bytes>limits_.granted_bytes-sizeof(Stage) ||
           dynamic_bytes_>limits_.granted_bytes-sizeof(Stage)-shape.new_dynamic_bytes ||
           shape.externally_charged_bytes>SIZE_MAX-external_bytes_)
            return reject(Reason::storage_pressure);
        // No potentially throwing allocation/move occurs after admission.
        entries_[(head_+count_)%MaximumBlocks].emplace(Entry{shape,std::move(block)});
        ++count_;dynamic_bytes_+=shape.new_dynamic_bytes;external_bytes_+=shape.externally_charged_bytes;
        captured_end_+=shape.count;
        if(!deadline_)deadline_=shape.first+limits_.aperture.cold_samples;
        if(captured_end_==*deadline_)release_broad(Reason::deadline);
        return Append::retained;
    }
    void reevaluate(const Eligibility& owners) noexcept {
        if((state_==State::holding || state_==State::frequency_drain) &&
           !owners.accepts(limits_.sample_rate,limits_.aperture.cold_samples))
            release_broad(Reason::owner_no_longer_cold);
    }
    bool release_frequency(const FrequencyCertificate& certificate, const Eligibility& owners) noexcept {
        if((state_==State::holding || state_==State::frequency_drain) &&
           !owners.accepts(limits_.sample_rate,limits_.aperture.cold_samples)) {
            release_broad(Reason::owner_no_longer_cold);return false;
        }
        if(state_!=State::holding || !count_ || !(certificate.identity==limits_.identity) ||
           certificate.sample_rate!=limits_.sample_rate || !certificate.independently_qualified ||
           !certificate.useful || !std::isfinite(certificate.frequency_hz) ||
           !std::isfinite(certificate.uncertainty_hz) || certificate.uncertainty_hz<0 ||
           certificate.first<limits_.first_sample || certificate.first>=certificate.end ||
           certificate.end>captured_end_ || certificate.end-certificate.first>limits_.aperture.maximum_fit_samples)
            return false;
        certificate_=certificate;reason_=Reason::qualified_frequency;state_=State::frequency_drain;return true;
    }
    void release_broad(Reason reason) noexcept {
        if(state_==State::closed || state_==State::broad_drain)return;
        if(reason==Reason::none || reason==Reason::qualified_frequency || reason==Reason::capture_invalidated)
            reason=Reason::explicit_fallback;
        certificate_.reset();reason_=reason;state_=State::broad_drain;
    }
    std::optional<Entry> take(const JoinProof& proof) noexcept {
        if(!proof.valid() || state_==State::holding || state_==State::closed || !count_)return {};
        auto result=std::move(entries_[head_]);entries_[head_].reset();
        head_=(head_+1)%MaximumBlocks;--count_;
        dynamic_bytes_-=result->shape.new_dynamic_bytes;external_bytes_-=result->shape.externally_charged_bytes;
        // The detached allocation and its charge now belong to the caller.
        return result;
    }
    bool close(const JoinProof& proof) noexcept {
        if(!proof.valid() || count_ || state_==State::holding)return false;
        certificate_.reset();state_=State::closed;return true;
    }
    bool abandon_invalidated(const InvalidationProof& invalidation, const JoinProof& proof) noexcept {
        if(!proof.valid() || !invalidation.ordinary_capture_already_invalidated ||
           !(invalidation.invalidated==limits_.identity) || state_==State::closed)return false;
        // Only the caller's pre-existing ordinary reset/drop semantics authorize
        // this release. It scores no sample, absence, completion or coverage.
        for(auto& entry:entries_)entry.reset();
        count_=head_=dynamic_bytes_=external_bytes_=0;certificate_.reset();
        reason_=Reason::capture_invalidated;state_=State::closed;return true;
    }
private:
    Append reject(Reason reason) noexcept {release_broad(reason);return Append::release_broad;}
    Limits limits_;
    LifecycleDiagnostic diagnostic_;
    std::array<std::optional<Entry>,MaximumBlocks> entries_{};
    std::optional<FrequencyCertificate> certificate_;
    std::optional<std::uint64_t> deadline_;
    std::uint64_t captured_end_=0;
    std::size_t head_=0,count_=0,dynamic_bytes_=0,external_bytes_=0;
    State state_=State::holding;
    Reason reason_=Reason::none;
};

} // namespace datapump::live::detail::cold_input

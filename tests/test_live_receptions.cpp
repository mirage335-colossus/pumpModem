#include "../src/live_receptions.hpp"
#include "../src/live_deferred_pattern_history.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_code.hpp"
#include <iostream>
#include <array>
#include <limits>
#include <stdexcept>

namespace {
using History = datapump::live::detail::ReceptionHistory;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
History::Candidate candidate(std::uint64_t id, std::uint64_t first, std::uint64_t end, double score) {
    return {id, 1, first, end, 1500, 10, 6000, score, false};
}
void amplitude_passband_aliases() {
    History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    auto first=candidate(1,100,1100,80);first.frequency=1499;first.frequency_tolerance=.002;
    first.amplitude_low_hz=1497;first.amplitude_high_hz=1501;
    const auto initial=history.observe(first,allocate);
    auto overlap=first;overlap.id=2;overlap.frequency=1501;overlap.score=90;
    overlap.amplitude_low_hz=1499;overlap.amplitude_high_hz=1503;
    const auto alias=history.observe(overlap,allocate);
    check(alias.selected&&alias.signal_id==initial.signal_id&&ids==1,
          "overlapping amplitude centers duplicated one physical reception");
    auto weaker=overlap;weaker.score=1;weaker.frequency=1505;
    weaker.amplitude_low_hz=1503;weaker.amplitude_high_hz=1507;
    check(history.observe(weaker,allocate).signal_id==initial.signal_id,
          "weaker owner update abandoned its original row");
    auto separate=overlap;separate.id=3;separate.frequency=1505;
    separate.amplitude_low_hz=1503;separate.amplitude_high_hz=1507;
    check(history.observe(separate,allocate).signal_id!=initial.signal_id&&ids==2,
          "nonoverlapping amplitude passbands merged separate carriers");
    auto coherent=first;coherent.id=4;coherent.frequency=1500;coherent.score=100;
    coherent.amplitude_low_hz=coherent.amplitude_high_hz=0;
    check(history.observe(coherent,allocate).signal_id!=initial.signal_id&&ids==3,
          "coarse amplitude uncertainty widened a resolved coherent carrier");
    overlap.complete=true;
    check(history.observe(overlap,allocate).signal_id==initial.signal_id,
          "amplitude physical completion abandoned its pending identity");
}
void optional_history_priority() {
    using datapump::live::detail::optional_pattern_history_ready;
    check(optional_pattern_history_ready(2048,2048,2048,true),"caught-up diagnostics were suppressed");
    check(!optional_pattern_history_ready(2048,1024,2048,true),"numeric backlog ran optional history");
    check(!optional_pattern_history_ready(2048,2048,4096,true),"popped block tail was ignored");
    check(!optional_pattern_history_ready(2048,2048,2048,false),"queued input was ignored");
    check(optional_pattern_history_ready(8192,8192,8192,true),"prospective null coordinates were treated as old PCM ingress");
    check(!optional_pattern_history_ready(8192,8192,4096,true),"uncaptured future frontier admitted diagnostics");
}
void deferred_provenance_growth() {
    using O=datapump::live::detail::DeferredPatternHistoryOrdinals;
    O value;std::uint64_t next_id=1;
    const auto ids=[&](std::uint64_t n){const auto first=next_id;next_id+=n;return first;};
    const auto time=O::Clock::time_point{}+std::chrono::seconds(19);
    for(std::uint64_t i=1;i<=64;++i)value.record(i,time+std::chrono::milliseconds(i),ids);
    check(!value.can_record() && value.heap_bytes()==0,"inline provenance did not reach original bound");
    const auto old=value.observation(17,42.);std::size_t charged=0;
    check(value.grow(128,[&](auto n){charged=n;}),"funded provenance growth failed");
    check(charged==O::growth_bytes(128) && value.heap_bytes()==charged && value.capacity()==128,
          "provenance omitted full replacement/retained storage charge");
    const auto same=value.observation(17,42.);
    check(same.id==old.id && same.observed_at==old.observed_at && same.admission_threshold==old.admission_threshold,
          "provenance growth changed original observation identity or time");
    value.record(67,time+std::chrono::seconds(1),ids);
    check(value.observation(65,9.).id==65 && value.observation(67,9.).id==67 && next_id==68,
          "growth rewound IDs or omitted a completed generation");
    value.discard_resolved_through(65);
    check(value.oldest_recorded()==66 && value.observation(66,9.).id==66,"partial provenance retirement changed IDs");
    std::stop_source stop;
    check(!value.grow(256,[&](auto){stop.request_stop();},stop.get_token()) && value.capacity()==128 &&
          value.oldest_recorded()==66 && value.observation(67,9.).id==67,"cancelled provenance growth published partial state");
    bool failed=false;try{value.grow(256,[&](auto){throw std::bad_alloc();});}catch(const std::bad_alloc&){failed=true;}
    check(failed && value.capacity()==128 && value.observation(67,9.).id==67,"allocation failure damaged provenance");
    O moved(std::move(value));
    check(moved.capacity()==128 && moved.observation(67,9.).id==67 && value.capacity()==64 && value.generation()==0,
          "moving grown provenance left dangling inline storage or invalid source state");
    moved={};check(moved.heap_bytes()==0 && moved.capacity()==64 && moved.generation()==0,"provenance reset retained heap or generation");
}
void immediate_progress_and_physical_completion() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto stream = candidate(1, 100, 1100, 80);
    const auto first = history.observe(stream, allocate);
    check(first.selected && !first.limited && first.signal_id == 1, "first accepted bit was withheld");
    // These are the next polls for 0, 00 and 001; no byte/token boundary is
    // needed to select each accepted prefix under the same pending identity.
    stream.end = 2100; stream.score = 160;
    const auto second = history.observe(stream, allocate);
    stream.end = 3100; stream.score = 240;
    const auto third = history.observe(stream, allocate);
    check(second.selected && third.selected && second.signal_id == first.signal_id &&
          third.signal_id == first.signal_id && ids == 1, "accepted short prefixes lost their shared pending identity");
    check(first.revision && first.revision == second.revision && second.revision == third.revision,
          "ordinary bit progress changed the selected interpretation revision");
    stream.complete = true;
    const auto complete = history.observe(stream, allocate);
    check(complete.selected && complete.signal_id == first.signal_id, "physical completion did not update the pending row");
    check(!history.observe(stream, allocate).selected, "the same physical completion was reported twice");
}
void incompatible_same_duration_clocks_stay_independent() {
    datapump::modem::Config geometry;geometry.sample_rate=512;geometry.bandwidth_hz=128;
    geometry.carrier_hz=128;geometry.spreading_factor=16;
    const auto tolerance=datapump::modem::pattern_chip_samples(geometry);
    check(tolerance==8,"compact raw fixture changed its actual chip tolerance");
    const auto raw=[&](std::uint64_t id,std::uint64_t first,std::uint64_t end,double score) {
        auto c=candidate(id,first,end,score);c.frequency=128;c.frequency_tolerance=4;
        c.symbol_samples=128;c.timing_tolerance=tolerance;return c;
    };
    for(const bool rival_first:{false,true}) {
        History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
        auto exact=raw(1,1164,1292,128);
        auto rival=raw(2,488,1520,789.72522131542394);rival.frequency=127;
        if(rival_first)history.observe(rival,allocate);
        const auto initial=history.observe(exact,allocate);
        exact.end=1548;exact.score=384;
        // Match the bank's all-candidate pre-observation before publishing an
        // earlier chunk. A rival's longer span cannot buy this clock's row.
        const auto exact_row=history.observe(exact,allocate);
        const auto rival_row=history.observe(rival,allocate);
        check(initial.selected && exact_row.selected && rival_row.selected && ids==2 &&
              initial.signal_id==exact_row.signal_id && exact_row.signal_id!=rival_row.signal_id,
              "an incompatible overlapping clock replaced an accepted raw prefix");
        auto earlier=raw(1,1164,1292,128);
        check(history.observe(earlier,allocate).selected,
              "bank pre-observation withheld an independent clock's earlier chunk");
        exact.complete=true;rival.complete=true;
        const auto completed=history.observe(exact,allocate);
        const auto other_completed=history.observe(rival,allocate);
        check(completed.selected && other_completed.selected &&
              completed.signal_id==initial.signal_id && completed.signal_id!=other_completed.signal_id,
              "a competing clock suppressed exact physical completion");
    }
    for(const std::uint64_t offset:std::array<std::uint64_t,6>{0,tolerance,tolerance+1,127-tolerance,128-tolerance,128}) {
        History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
        const auto initial=history.observe(raw(1,1164,1676,128),allocate);
        const auto next=history.observe(raw(2,1164+offset,1804,256),allocate);
        const bool aligned=offset==0 || offset==tolerance || offset==128-tolerance || offset==128;
        check(next.selected && (next.signal_id==initial.signal_id)==aligned && ids==(aligned?1U:2U),
              "ordinary overlap ignored its symmetric symbol-clock tolerance");
    }
}
void stronger_profile_replaces_prefix() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto prefix = candidate(1, 100, 1100, 80);
    const auto initial = history.observe(prefix, allocate);
    auto full = candidate(2, 100, 2100, 180);
    const auto replacement = history.observe(full, allocate);
    check(replacement.selected && replacement.signal_id == initial.signal_id && ids == 1,
          "a stronger duration created a second reception");
    prefix.complete = true;
    check(!history.observe(prefix, allocate).selected, "weaker completion finished a stronger pending reception");
    // A bank first observes every burst, then publishes their original chunks.
    // The owner must remain selected when an earlier chunk has a lower score.
    auto earlier = full; earlier.end = 1100; earlier.score = 90;
    check(history.observe(earlier, allocate).selected, "bank pre-observation withheld an earlier accepted owner chunk");
    prefix.complete = false; prefix.score = 170;
    check(!history.observe(prefix, allocate).selected, "an earlier owner chunk lowered the retained winning evidence");
    full.complete = true;
    check(history.observe(full, allocate).selected, "winning profile did not finish");
    prefix.complete = true;
    check(!history.observe(prefix, allocate).selected && ids == 1, "late profile completion duplicated a finished message");
    auto late = candidate(3, 1800, 2600, 170); late.complete = true;
    check(!history.observe(late, allocate).selected && ids == 1,
          "delayed overlapping profile reopened already reported physical completion");
    late.id = 4; late.end = 1000000;
    check(!history.observe(late, allocate).selected, "long delayed rival escaped completed overlap suppression");
    const auto next = history.observe(candidate(5, 10000, 11000, 200), allocate);
    check(next.selected && next.signal_id != initial.signal_id && ids == 2,
          "a delayed rival extended completed suppression into a separate later message");
}
void stronger_symbol_after_prefix_completion() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto prefix = candidate(1, 64, 576, 98); prefix.complete = true;
    const auto short_row = history.observe(prefix, allocate);
    auto long_symbol = candidate(2, 64, 287382, 1000);
    const auto long_row = history.observe(long_symbol, allocate);
    check(long_row.selected && long_row.signal_id == short_row.signal_id && ids == 1 &&
          long_row.revision > short_row.revision,
          "a stronger fully scored long symbol did not revise its completed short-prefix row");
    auto weaker_alias = candidate(3, 64, 576, 97);
    const auto alias = history.observe(weaker_alias, allocate);
    check(!alias.selected && alias.signal_id == long_row.signal_id && ids == 1,
          "a late weak alias matched completed history before the active long reception");
    check(!history.observe(prefix, allocate).selected && ids == 1,
          "the earlier completed owner reported a duplicate final");
    long_symbol.end += 287318; long_symbol.score += 1000;
    check(history.observe(long_symbol, allocate).selected,
          "the longer profile did not retain immediate progress after its delayed discovery");
}
void bridges_retire_every_fragment_in_any_order() {
    std::array<unsigned, 3> order{0, 1, 2};
    do {
        History history; std::uint64_t ids = 0;
        const auto allocate = [&] { return ++ids; };
        std::array<History::Candidate, 3> fragments{
            candidate(10, 100, 1100, 80), candidate(20, 10000, 11000, 90),
            candidate(30, 20000, 21000, 100)};
        std::array<std::uint64_t, 3> fragment_ids{};
        for (const auto index : order) {
            fragments[index].complete = true;
            fragment_ids[index] = history.observe(fragments[index], allocate).signal_id;
        }
        check(history.size() == 3, "disjoint completed fragments were prematurely merged");
        auto whole = candidate(40, 100, 21000, 500);
        whole.symbol_samples = 21000;
        const auto joined = history.observe(whole, allocate);
        check(joined.selected && joined.signal_id == 1 && history.size() == 1 && ids == 3 &&
              joined.superseded_ids.size() == 2 && joined.superseded_ids[0] == 2 && joined.superseded_ids[1] == 3,
              "a stronger bridge failed to retire every completed fragment under the oldest row");
        const auto revision = joined.revision;
        whole.signal_id = fragment_ids[2]; whole.end += 21000; whole.score += 500;
        const auto continued = history.observe(whole, allocate);
        check(continued.selected && continued.signal_id == 1 && continued.revision == revision &&
              continued.superseded_ids.size() == 2,
              "merged aliases or immediate selected-owner progress were lost");
        auto late = fragments[0]; late.id = 50; late.signal_id = fragment_ids[1];
        late.first = 90000; late.end = 91000;
        const auto suppressed = history.observe(late, allocate);
        check(!suppressed.selected && suppressed.signal_id == 1 && ids == 3,
              "a remembered alias created a new row after its original fragment was retired");
        whole.complete = true;
        const auto done = history.observe(whole, allocate);
        check(done.selected && done.signal_id == 1 && done.revision == revision,
              "the selected bridge's own physical completion was lost");
        whole.complete = false; whole.score += 500;
        check(!history.observe(whole, allocate).selected,
              "a stale pending observation reopened its own completed interpretation");
    } while (std::next_permutation(order.begin(), order.end()));
}
void equal_evidence_uses_intrinsic_geometry() {
    for (const bool reverse : {false, true}) {
        History history; std::uint64_t ids = 0;
        const auto allocate = [&] { return ++ids; };
        auto shorter = candidate(reverse ? 2 : 1, 100, 1100, 100);
        auto longer = candidate(reverse ? 1 : 2, 100, 1100, 100);
        shorter.symbol_samples = 100; longer.symbol_samples = 1000;
        const auto first = history.observe(reverse ? longer : shorter, allocate);
        const auto second = history.observe(reverse ? shorter : longer, allocate);
        check(first.signal_id == second.signal_id && ids == 1 && second.selected == !reverse,
              "equal evidence selected an owner according to RX target order");
        check(history.observe(longer, allocate).selected && !history.observe(shorter, allocate).selected,
              "the intrinsic equal-evidence owner did not remain selected");
    }
}
void supported_media_outweighs_overconfident_short_prefixes() {
    // These media lengths/native scores were observed by sampled cross-target
    // probes. SF16/32 use exact sample fits here; SF64 uses compact bins, whose
    // conservative covariance bound gives perfect fits much smaller scores.
    constexpr std::uint64_t chip = 8;
    const auto full = 3 * datapump::modem::pattern_symbol_support(512, 293./3, chip);
    const auto short32 = 5 * datapump::modem::pattern_symbol_support(256, 893./5, chip);
    const auto short16 = 9 * datapump::modem::pattern_symbol_support(128, 434./9, chip);
    check(full == 1536 && short32 == 1280 && short16 == 1152,
          "native short-fit evidence claimed support beyond its admitted media");
    std::array<unsigned, 3> order{0, 1, 2};
    do {
        History history; std::uint64_t ids = 0;
        const auto allocate = [&] { return ++ids; };
        std::array<History::Candidate, 3> fits{
            candidate(1, 100, 1636, full), candidate(2, 100, 1380, short32),
            candidate(3, 100, 1252, short16)};
        fits[0].symbol_samples = 512; fits[1].symbol_samples = 256; fits[2].symbol_samples = 128;
        for (const auto index : order) history.observe(fits[index], allocate);
        check(history.observe(fits[0], allocate).selected &&
              !history.observe(fits[1], allocate).selected && !history.observe(fits[2], allocate).selected && ids == 1,
              "short exact-fit prefixes defeated the full admitted stream under an RX target permutation");
    } while (std::next_permutation(order.begin(), order.end()));

    const auto single = datapump::modem::pattern_symbol_support(512, 98, chip);
    const auto exact_prefix = datapump::modem::pattern_symbol_support(256, 372, chip);
    check(single == 512 && exact_prefix == 256 && single > exact_prefix,
          "an overconfident half-symbol prefix defeated a complete one-bit message");
    const auto correct_long = datapump::modem::pattern_symbol_support(4096, 790, chip);
    const auto weak_long = datapump::modem::pattern_symbol_support(16384, 150, chip);
    check(correct_long == 4096 && weak_long == 1200 && correct_long > weak_long,
          "a weak long partial fit gained confidence merely from its configured duration");
    const auto keyed_correct = 3 * datapump::modem::pattern_symbol_support(512, 293.496/3, chip);
    const auto keyed_alias = datapump::modem::pattern_symbol_support(1024, 196.557, chip) +
                             datapump::modem::pattern_symbol_support(1024, 56.061, chip);
    check(keyed_correct == 1536 && keyed_alias > 1472 && keyed_alias < 1473 && keyed_correct > keyed_alias,
          "a keyed alias borrowed its strong first symbol's surplus to inflate a weak next symbol");
}
void unobserved_positions_add_no_selection_support() {
    constexpr std::uint64_t chip = 8;
    check(datapump::modem::pattern_symbol_support(0, 10000, chip) == 0 &&
          datapump::modem::pattern_symbol_support(4096, 0, chip) == 0 &&
          datapump::modem::pattern_symbol_support(4096, -100, chip) == 0,
          "empty media or absent evidence acquired arbitration support");
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto known = candidate(1, 100, 1636, 3 * datapump::modem::pattern_symbol_support(512, 293./3, chip));
    const auto original = history.observe(known, allocate);
    // Compact missing runs and missing_pattern_bit placeholders occupy sample
    // positions but leave the known-media count and admitted evidence unchanged.
    auto missing = candidate(2, 100, 1380, 5 * datapump::modem::pattern_symbol_support(256, 893./5, chip));
    check(!history.observe(missing, allocate).selected, "the erasure fixture unexpectedly won");
    missing.end = 1000000000;
    const auto extended = history.observe(missing, allocate);
    check(!extended.selected && extended.signal_id == original.signal_id && ids == 1,
          "a long unobserved run increased a losing interpretation's selection support");
    known.end += 512; known.score = 4 * datapump::modem::pattern_symbol_support(512, 391./4, chip);
    check(history.observe(known, allocate).selected,
          "unknown rival positions withheld a selected owner's next known symbol");
}
void losing_spans_cannot_poison_later_receptions() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    const auto first = history.observe(candidate(1, 100, 1100, 1000), allocate);
    auto losing = candidate(2, 100, 1000000, 100); losing.absence_samples = 1000000;
    check(!history.observe(losing, allocate).selected, "the weak long fixture unexpectedly won");
    const auto later = history.observe(candidate(3, 10000, 11000, 900), allocate);
    check(later.selected && later.signal_id != first.signal_id && history.size() == 2,
          "a losing long span/silence allowance swallowed an independent transmission");
    // The same weak observation touches both stronger rows. It cannot use its
    // unselected envelope as a connection between their independent evidence.
    const auto weak_bridge = history.observe(losing, allocate);
    check(!weak_bridge.selected && history.size() == 2 && weak_bridge.superseded_ids.empty(),
          "a weak bridge merged disjoint stronger receptions");
    auto winner = candidate(1, 100, 11000, 2000);
    const auto actual_bridge = history.observe(winner, allocate);
    check(actual_bridge.selected && actual_bridge.signal_id == first.signal_id &&
          history.size() == 1 && actual_bridge.superseded_ids.size() == 1,
          "the growing selected owner's real envelope failed to merge an overlapping fragment");
}
void alias_storage_is_bounded_without_hiding_owner_progress() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto winner = candidate(1, 100, 1100, 1000);
    const auto original = history.observe(winner, allocate);
    for (std::size_t i = 0; i < History::capacity; ++i) {
        auto fragment = candidate(100 + i, winner.end + 10000, winner.end + 11000, 80);
        fragment.complete = true;
        check(history.observe(fragment, allocate).selected, "alias storage fixture merged too early");
        winner.end = fragment.end; winner.score += 1000;
        const auto joined = history.observe(winner, allocate);
        check(joined.selected && !joined.limited && joined.superseded_ids.size() == i + 1,
              "bounded alias history lost a permitted merge");
    }
    winner.end += 1; winner.score += 1000;
    const auto progress = history.observe(winner, allocate);
    check(progress.selected && !progress.limited && progress.signal_id == original.signal_id &&
          progress.superseded_ids.size() == History::capacity,
          "full alias storage withheld an existing owner's newly accepted bits");
    auto fragment = candidate(1000, winner.end + 10000, winner.end + 11000, 80);
    fragment.complete = true;
    const auto separate = history.observe(fragment, allocate);
    auto contender = candidate(2000, 100, fragment.end, winner.score + 1000);
    const auto refused = history.observe(contender, allocate);
    check(refused.limited && !refused.selected && history.size() == 2,
          "alias overflow silently exceeded its bound or forgot a retired identity");
    const auto retained = history.observe(winner, allocate);
    check(retained.selected && !retained.limited && retained.signal_id != separate.signal_id &&
          retained.superseded_ids.size() == History::capacity,
          "refused alias overflow corrupted existing owner progress");
    winner.end = fragment.end; winner.score += 2000;
    const auto extended = history.observe(winner, allocate);
    check(extended.selected && extended.limited && extended.signal_id == original.signal_id &&
          extended.superseded_ids.size() == History::capacity && history.size() == 2,
          "an existing owner's accepted bit was hidden by an impossible additional alias merge");
}
void separate_messages_and_families() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    const auto initial = history.observe(candidate(1, 100, 1100, 80), allocate);
    // A repeated long symbol's short prefix can look like a disjoint fragment.
    auto fragment = candidate(2, 2100, 3100, 90);
    check(history.observe(fragment, allocate).signal_id == initial.signal_id && ids == 1,
          "a fragment inside the pending absence window created a separate row");
    auto independent = candidate(3, 9100, 10100, 100);
    check(history.observe(independent, allocate).signal_id != initial.signal_id && ids == 2,
          "a full physical absence did not separate a later message");
    auto other_frequency = independent; other_frequency.id = 4; other_frequency.frequency += 30;
    check(history.observe(other_frequency, allocate).signal_id == 3,
          "resolvable frequencies were merged");
    auto other_family = independent; other_family.id = 5; other_family.family = 2;
    check(history.observe(other_family, allocate).signal_id == 4,
          "independent key families were merged");
    independent.complete = true;
    check(history.observe(independent, allocate).selected, "independent message could not finish");
    auto following = candidate(6, independent.end + 1, independent.end + 1001, 110);
    check(history.observe(following, allocate).signal_id == 5,
          "completed history bridged a nonoverlapping independent reception");
}
void sample_clock_and_long_absence() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    const auto origin = std::numeric_limits<std::uint64_t>::max() - 10000;
    auto first = candidate(1, origin, origin + 1000, 80);
    const auto id = history.observe(first, allocate).signal_id;
    auto second = candidate(2, origin + 6999, origin + 7999, 90);
    check(history.observe(second, allocate).signal_id == id && ids == 1,
          "large global sample coordinates overflowed the pending absence comparison");

    History long_symbols; ids = 0;
    constexpr std::uint64_t duration = 4ULL * 60 * 60 * 8000;
    first = candidate(3, 100, 100 + duration, 80);
    first.absence_samples = first.symbol_samples = duration;
    first.timing_tolerance = 8;
    const auto long_id = long_symbols.observe(first, allocate).signal_id;
    second = candidate(4, first.end + 4, first.end + 1004, 70);
    second.absence_samples = 6 * 8000; second.symbol_samples = 1000; second.timing_tolerance = 8;
    const auto aligned = long_symbols.observe(second, allocate);
    check(!aligned.selected && aligned.signal_id == long_id && ids == 1,
          "a timing-aligned next-slot prefix separated a pending long reception");
    auto independent = second; independent.id = 5;
    independent.first = first.end + 8000 * 60; independent.end = independent.first + 1000;
    check(long_symbols.observe(independent, allocate).signal_id != long_id && ids == 2,
          "partial long-symbol absence swallowed a distinct later short reception");
    first.end += duration; first.score += 1000;
    const auto progress = long_symbols.observe(first, allocate);
    check(progress.selected && progress.signal_id == long_id,
          "a fully accepted hours-long owner symbol lost immediate pending progress");
}
void pending_fragments_must_share_the_long_symbol_clock() {
    for (const bool reverse : {false, true}) {
        History history; std::uint64_t ids = 0;
        const auto allocate = [&] { return ++ids; };
        auto long_symbol = candidate(1, 64, 2112, 1000);
        long_symbol.symbol_samples = long_symbol.absence_samples = 2048;
        long_symbol.timing_tolerance = 8;
        auto separate = candidate(2, 3000, 3128, 100);
        separate.symbol_samples = 128; separate.absence_samples = 384; separate.timing_tolerance = 8;
        const auto first = history.observe(reverse ? separate : long_symbol, allocate);
        const auto second = history.observe(reverse ? long_symbol : separate, allocate);
        check(first.selected && second.selected && first.signal_id != second.signal_id && history.size() == 2,
              "a long receiver's partial absence swallowed an independent later short send");
    }
    for (const std::uint64_t drift : {0, 4, 8}) {
        History history; std::uint64_t ids = 0;
        const auto allocate = [&] { return ++ids; };
        auto long_symbol = candidate(1, 64, 2112, 1000);
        long_symbol.symbol_samples = long_symbol.absence_samples = 2048;
        long_symbol.timing_tolerance = 8;
        const auto original = history.observe(long_symbol, allocate);
        auto prefix = candidate(2, long_symbol.end + drift, long_symbol.end + drift + 128, 100);
        prefix.symbol_samples = 128; prefix.absence_samples = 384; prefix.timing_tolerance = 8;
        const auto aligned = history.observe(prefix, allocate);
        check(!aligned.selected && aligned.signal_id == original.signal_id && ids == 1,
              "a contiguous short prefix escaped its pending long-symbol reception");
        prefix.id = 3; prefix.first = long_symbol.end + long_symbol.symbol_samples - 1;
        prefix.end = prefix.first + 128;
        check(history.observe(prefix, allocate).signal_id != original.signal_id && ids == 2,
              "timing tolerance crossed a complete long-symbol absence boundary");
    }
    // A later profile's own absence allowance cannot reach backward past the
    // earlier short receiver's complete six-second failure window.
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto earlier = candidate(1, 64, 192, 100);
    earlier.symbol_samples = 128; earlier.absence_samples = 384;
    const auto first = history.observe(earlier, allocate);
    auto later = candidate(2, 2112, 4160, 1000);
    later.symbol_samples = later.absence_samples = 2048;
    check(history.observe(later, allocate).signal_id != first.signal_id && ids == 2,
          "a later long profile retroactively expanded a shorter stream's absence window");
    // Completed future fragments still require actual overlap with admitted
    // long-symbol media, even when their starts are exactly clock-aligned.
    History completed; ids = 0;
    auto future = candidate(3, 2112, 2240, 100); future.complete = true;
    future.symbol_samples = 128; future.absence_samples = 384;
    const auto closed = completed.observe(future, allocate);
    later.first = 64; later.end = 2112;
    check(completed.observe(later, allocate).signal_id != closed.signal_id && ids == 2,
          "unscored future media erased an independently completed fragment");
    later.end = 4160; later.score += 1000;
    const auto known_overlap = completed.observe(later, allocate);
    check(known_overlap.selected && known_overlap.signal_id == closed.signal_id &&
          completed.size() == 1 && known_overlap.superseded_ids.size() == 1,
          "an actually accepted next long symbol failed to merge the overlapping completed prefix");
}
void retired_history_preserves_active_aliases() {
    History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    const auto first=history.observe(candidate(1,100,1100,80),allocate).signal_id;
    const auto alias=history.observe(candidate(2,10100,11100,90),allocate).signal_id;
    auto bridge=candidate(3,100,11100,300);
    check(history.observe(bridge,allocate).signal_id==first && first!=alias,"alias retention fixture did not merge histories");
    // A losing profile may still publish later under a superseded alias.
    history.retire_unreferenced([&](auto id){return id==alias;});
    auto continuation=candidate(4,12100,13100,400);
    check(history.observe(continuation,allocate).signal_id==first,"active losing alias lost pending gap continuity");
    history.retire_unreferenced([](auto){return false;});
    auto later=candidate(5,14100,15100,500);
    check(history.observe(later,allocate).signal_id!=first,"retired evidence bridged an unrelated later gap");

    History overlap;std::uint64_t next=0;const auto new_id=[&]{return ++next;};
    const auto original=overlap.observe(candidate(10,100,1100,100),new_id).signal_id;
    overlap.retire_unreferenced([](auto){return false;});
    check(overlap.observe(candidate(11,200,1000,50),new_id).signal_id==original,
          "retired overlap lost its existing row identity");
    check(overlap.observe(candidate(12,2100,3100,200),new_id).signal_id==original,
          "losing overlap observation failed to reactivate its group");

    History bounded;std::uint64_t allocated=0;const auto alloc=[&]{return ++allocated;};
    const auto active=bounded.observe(candidate(1,100,1100,80),alloc).signal_id;
    for(std::uint64_t i=2;i<=3*History::capacity;++i) {
        bounded.retire_unreferenced([&](auto id){return id==active;});
        check(bounded.observe(candidate(i,i*20000,i*20000+1000,90),alloc).selected && bounded.size()<=History::capacity,
              "abandoned incomplete history exhausted the bounded table");
    }
    check(bounded.observe(candidate(1,100,1100,80),alloc).signal_id==active,"retirement evicted an active pending row");
}
void bounded_history_preserves_active_rows() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto active = candidate(1, 100, 1100, 80);
    const auto active_id = history.observe(active, allocate).signal_id;
    for (std::uint64_t i = 1; i < History::capacity; ++i) {
        auto finished = candidate(i + 1, i * 10000, i * 10000 + 1000, 80); finished.complete = true;
        check(history.observe(finished, allocate).selected, "completed history fixture was unexpectedly merged");
    }
    auto next = candidate(1000, 1000000, 1001000, 100);
    check(history.observe(next, allocate).selected && history.size() == History::capacity,
          "completed history was not evicted at the fixed bound");
    check(history.observe(active, allocate).signal_id == active_id,
          "history eviction discarded an active pending reception");

    History pending; ids = 0;
    for (std::uint64_t i = 0; i < History::capacity; ++i)
        check(pending.observe(candidate(i + 1, i * 10000, i * 10000 + 1000, 80), allocate).selected,
              "active capacity fixture was unexpectedly merged");
    const auto refused = pending.observe(next, allocate);
    check(refused.limited && !refused.selected && refused.signal_id == 0 &&
          pending.size() == History::capacity && ids == History::capacity,
          "full active history grew or evicted a pending row");
}
void burst_calendar_continuation() {
    using datapump::modem::SymbolBurstCoordinate;
    const auto earlier=[] {
        auto c=candidate(1,1000,2000,100);c.symbol_samples=1000;c.absence_samples=9000;
        c.source_identity=History::BurstIdentity{{100,3},{190,3},true,true};return c;
    };
    const auto later=[] {
        auto c=candidate(2,10407,11407,80);c.symbol_samples=1000;c.absence_samples=9000;
        c.source_identity=History::BurstIdentity{{190,3},{280,3},true,true};return c;
    };
    for(unsigned variant=0;variant<10;++variant) {
        History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
        auto first=earlier(),next=later();
        switch(variant) {
        case 1:next.burst_identity()->first.epoch+=30;break;
        case 2:++next.burst_identity()->first.phase;break;
        case 3:next.symbol_samples*=2;break;
        case 4:++next.family;break;
        case 5:next.frequency+=100;break;
        case 6:first.complete=true;break;
        case 7:next.source_identity=std::monostate{};break;
        case 9:next.score=120;break;
        default:break;
        }
        const auto original=history.observe(first,allocate);
        if(variant==8)history.retire_unreferenced([](auto){return false;});
        const auto result=history.observe(next,allocate);
        check((result.signal_id==original.signal_id)==(variant==0),
              "Burst history did not require the exact active same-profile next calendar address");
    }
    History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    const auto original=history.observe(earlier(),allocate);
    auto advanced=earlier();advanced.end=11000;advanced.score=300;
    advanced.burst_identity()->next=SymbolBurstCoordinate{280,3};history.observe(advanced,allocate);
    history.observe(earlier(),allocate); // A provisional replay must not rewind nextC.
    auto continuation=later();continuation.id=3;continuation.first=19407;continuation.end=20407;
    continuation.score=200;continuation.burst_identity()->first=SymbolBurstCoordinate{280,3};
    continuation.burst_identity()->next=SymbolBurstCoordinate{370,3};
    check(history.observe(continuation,allocate).signal_id==original.signal_id && ids==1,
          "A stale Burst observation rewound its owner's next calendar address");
    auto partial=earlier();partial.source_identity=History::BurstIdentity{{100,3},{190,3},false,true};
    check(!partial.burst_first() && partial.burst_next() && partial.burst_next()->epoch==190,
          "typed Burst identity lost independent first/next validity");
    partial.burst_identity()->first_valid=true;partial.burst_identity()->next_valid=false;
    check(partial.burst_first() && !partial.burst_next(),"typed Burst identity invented an absent next calendar address");
}
void helper_source_domains_remain_independent() {
    using datapump::modem::PatternSourceIdentity;
    if constexpr(sizeof(void*)==8 && sizeof(std::optional<datapump::modem::SymbolBurstCoordinate>)==24) {
        check(sizeof(History::Candidate)==160 && sizeof(History)==45072,
              "source identity changed the ordinary candidate/history workspace layout");
    }
    const auto first=[] {
        auto c=candidate(1,1000,40000,80);c.symbol_samples=1000;c.timing_tolerance=10;
        c.helper_assisted=true;c.source_identity=PatternSourceIdentity{.epoch=100,.first_symbol=0,.sample_rate=1000};return c;
    };
    const auto later=[] {
        auto c=candidate(2,11000,12000,100);c.symbol_samples=1000;c.timing_tolerance=10;
        c.source_identity=PatternSourceIdentity{.epoch=100,.first_symbol=0,.sample_rate=1000};return c;
    };
    History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    auto old=first(),next=later();
    const auto a=history.observe(old,allocate),b=history.observe(next,allocate);
    check(a.selected && b.selected && a.signal_id!=b.signal_id && history.size()==2 && ids==2 &&
          b.superseded_ids.empty(),"helper-born independent keyed address withdrew or merged an established row");
    old.end=45000;old.score=120;old.helper_assisted=false; // A refresh cannot erase past helper involvement.
    check(history.observe(old,allocate).signal_id==a.signal_id,"helper source progress changed its original pending identity");
    next.complete=true;next.end=13000;next.score=200;
    const auto ended=history.observe(next,allocate);
    check(ended.selected && ended.signal_id==b.signal_id && ended.superseded_ids.empty() && history.size()==2,
          "independent helper source completion withdrew another row's prefix");
    for(unsigned variant=0;variant<8;++variant) {
        History ambiguous;ids=0;old=first();next=later();
        switch(variant) {
        case 0:old.helper_assisted=false;break;
        case 1:old.source_identity=std::monostate{};break;
        case 2:std::get<PatternSourceIdentity>(old.source_identity).clock_half_width_ppm=
                   std::numeric_limits<double>::quiet_NaN();break;
        case 3:std::get<PatternSourceIdentity>(next.source_identity).sample_rate=500;break;
        case 4:next.symbol_samples=2000;break;
        case 5:std::get<PatternSourceIdentity>(next.source_identity).first_symbol=10;break;
        case 6:std::get<PatternSourceIdentity>(old.source_identity).epoch=std::numeric_limits<std::uint64_t>::max();break;
        case 7:std::get<PatternSourceIdentity>(old.source_identity).clock_half_width_ppm=1e6;break;
        }
        const auto original=ambiguous.observe(old,allocate),observed=ambiguous.observe(next,allocate);
        check(observed.signal_id==original.signal_id && ids==1,
              "unknown, unassisted or compatible source identity changed ordinary overlap arbitration");
    }
    History rebased;ids=0;old=first();const auto original=rebased.observe(old,allocate);
    old.first=11000;old.end=45000;rebased.observe(old,allocate);
    next=later();next.first=21000;next.end=22000;
    check(rebased.observe(next,allocate).signal_id==original.signal_id && ids==1,
          "rebasing a physical start retained an identity anchored at a different sample");
    History replaced;ids=0;old=first();const auto helper_row=replaced.observe(old,allocate);
    auto alias=old;alias.id=3;alias.helper_assisted=false;alias.score=100;
    check(replaced.observe(alias,allocate).signal_id==helper_row.signal_id && ids==1,
          "a stronger same-source alias abandoned its original GUI row");
    next=later();next.score=200;
    const auto independent=replaced.observe(next,allocate);
    check(independent.selected && independent.signal_id!=helper_row.signal_id && ids==2 &&
          independent.superseded_ids.empty(),"alias owner replacement erased durable helper provenance");
}
void established_helper_rows_survive_widening_clock_cones() {
    using datapump::modem::PatternSourceIdentity;
    const PatternSourceIdentity source{.epoch=100,.first_symbol=0,.clock_half_width_ppm=1000,.sample_rate=1000};
    auto a=candidate(1,1000,20000,80),b=candidate(2,11000,12000,100);
    a.symbol_samples=b.symbol_samples=1000;a.timing_tolerance=b.timing_tolerance=10;
    a.helper_assisted=true;a.source_identity=b.source_identity=source;
    check(datapump::modem::pattern_source_identities_disjoint(source,a.first,a.end,1000,source,b.first,b.end,1000),
          "long helper row fixture did not begin with independently proved source domains");
    History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    const auto first=history.observe(a,allocate),second=history.observe(b,allocate);
    check(first.signal_id!=second.signal_id && ids==2,"independent helper sources failed to publish separate rows");
    a.end=1000000000;a.score=120;
    check(history.observe(a,allocate).signal_id==first.signal_id,"long helper progress lost its original row");
    b.end=a.end;b.score=200;
    check(!datapump::modem::pattern_source_identities_disjoint(source,a.first,a.end,1000,source,b.first,b.end,1000),
          "long helper row fixture did not reach an inconclusive clock-cone overlap");
    const auto progressed=history.observe(b,allocate);
    check(progressed.selected && progressed.signal_id==second.signal_id && progressed.superseded_ids.empty() &&
          history.size()==2 && ids==2,"widening helper clock cones merged established rows or withdrew an old prefix");
    a.complete=true;a.score=300;const auto ended_a=history.observe(a,allocate);
    b.complete=true;b.score=400;const auto ended_b=history.observe(b,allocate);
    check(ended_a.signal_id==first.signal_id && ended_b.signal_id==second.signal_id &&
          ended_a.superseded_ids.empty() && ended_b.superseded_ids.empty() && history.size()==2,
          "independent established helper rows lost their identities at completion");

    // Affirmatively identical source anchors retain ordinary alias merging,
    // even when separate initial RF measurements first produced two rows.
    History aliases;ids=0;a=candidate(3,1000,20000,80);b=candidate(4,1000,12000,100);
    a.symbol_samples=b.symbol_samples=1000;a.source_identity=b.source_identity=source;a.helper_assisted=true;
    a.frequency=1480;b.frequency=1520;
    const auto original=aliases.observe(a,allocate),other=aliases.observe(b,allocate);
    check(original.signal_id!=other.signal_id,"same-source control did not begin with distinct RF observations");
    b.frequency=a.frequency;b.score=200;
    const auto merged=aliases.observe(b,allocate);
    check(merged.selected && merged.signal_id==original.signal_id && aliases.size()==1,
          "affirmatively identical helper source anchors stopped merging ordinary aliases");
    for(unsigned invalid=0;invalid<3;++invalid) {
        History unknown;ids=0;a=candidate(5,1000,20000,80);b=candidate(6,1000,12000,100);
        auto identity=source;
        if(invalid==0){identity.epoch=std::numeric_limits<std::uint64_t>::max();identity.first_symbol=1;}
        if(invalid==1)identity.first_symbol=std::numeric_limits<std::uint64_t>::max();
        if(invalid==2)a.end=b.end=std::numeric_limits<std::uint64_t>::max();
        a.symbol_samples=b.symbol_samples=1000;a.source_identity=b.source_identity=identity;a.helper_assisted=true;
        a.frequency=1480;b.frequency=1520;
        const auto old=unknown.observe(a,allocate),separate=unknown.observe(b,allocate);
        b.frequency=a.frequency;b.score=200;
        const auto still_separate=unknown.observe(b,allocate);
        check(old.signal_id!=separate.signal_id && still_separate.signal_id==separate.signal_id &&
              still_separate.superseded_ids.empty() && unknown.size()==2,
              "identical but overflowing source addresses merged previously established rows");
    }
}
}
int main() {
    try {
        amplitude_passband_aliases();
        optional_history_priority();
        deferred_provenance_growth();
        immediate_progress_and_physical_completion();
        incompatible_same_duration_clocks_stay_independent();
        stronger_profile_replaces_prefix();
        stronger_symbol_after_prefix_completion();
        bridges_retire_every_fragment_in_any_order();
        equal_evidence_uses_intrinsic_geometry();
        supported_media_outweighs_overconfident_short_prefixes();
        unobserved_positions_add_no_selection_support();
        losing_spans_cannot_poison_later_receptions();
        alias_storage_is_bounded_without_hiding_owner_progress();
        separate_messages_and_families();
        sample_clock_and_long_absence();
        pending_fragments_must_share_the_long_symbol_clock();
        bounded_history_preserves_active_rows();
        retired_history_preserves_active_aliases();
        burst_calendar_continuation();
        helper_source_domains_remain_independent();
        established_helper_rows_survive_widening_clock_cones();
        std::cout << "live reception arbitration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "../src/live_receptions.hpp"
#include "datapump/pattern_receiver.hpp"
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
}
int main() {
    try {
        immediate_progress_and_physical_completion();
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
        std::cout << "live reception arbitration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

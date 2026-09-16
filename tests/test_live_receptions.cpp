#include "../src/live_receptions.hpp"
#include <iostream>
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
    check(long_row.selected && long_row.signal_id != short_row.signal_id && ids == 2,
          "a stronger fully scored long symbol was discarded after short-prefix completion");
    auto weaker_alias = candidate(3, 64, 576, 97);
    const auto alias = history.observe(weaker_alias, allocate);
    check(!alias.selected && alias.signal_id == long_row.signal_id && ids == 2,
          "a late weak alias matched completed history before the active long reception");
    check(!history.observe(prefix, allocate).selected && ids == 2,
          "the earlier completed owner reported a duplicate final");
    long_symbol.end += 287318; long_symbol.score += 1000;
    check(history.observe(long_symbol, allocate).selected,
          "the longer profile did not retain immediate progress after its delayed discovery");
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
    first = candidate(3, 100, 1000, 80); first.absence_samples = 4ULL * 60 * 60 * 8000;
    const auto long_id = long_symbols.observe(first, allocate).signal_id;
    second = candidate(4, 8000 * 60, 8000 * 60 + 1000, 100);
    second.absence_samples = first.absence_samples;
    check(long_symbols.observe(second, allocate).signal_id == long_id && ids == 1,
          "six seconds of partial long-symbol absence separated a pending reception");
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
        separate_messages_and_families();
        sample_clock_and_long_absence();
        bounded_history_preserves_active_rows();
        std::cout << "live reception arbitration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

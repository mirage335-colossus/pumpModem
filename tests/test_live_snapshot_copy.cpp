#include "../src/live_snapshot_copy.hpp"

#include <array>
#include <complex>
#include <cstdlib>
#include <new>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// The standalone test has no Session or worker threads. Observe actual default
// allocator calls, not just equal capacities or values after an assignment.
namespace allocation_probe {
thread_local bool enabled=false;
thread_local std::size_t allocations=0,deallocations=0;
struct Counts { std::size_t allocations,deallocations; };
template<class Work> Counts measure(Work&& work) {
    allocations=deallocations=0;enabled=true;
    try { work(); } catch(...) { enabled=false;throw; }
    enabled=false;return {allocations,deallocations};
}
}
void* operator new(std::size_t size) {
    if(allocation_probe::enabled)++allocation_probe::allocations;
    if(auto* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept {
    if(p && allocation_probe::enabled)++allocation_probe::deallocations;
    std::free(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p,std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p,std::size_t) noexcept { ::operator delete(p); }

using datapump::live::Snapshot;
using datapump::live::detail::prepare_snapshot_copy;
using datapump::live::detail::snapshot_copy_fits;
using datapump::live::detail::snapshot_copy_requirement;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void populate(Snapshot& value) {
    value.receiver_error = "receiver diagnostic with enough content to exceed SSO";
    value.clock_timing_status = "clock timing detail with enough content to exceed SSO";
    value.status = "transmitting with a deliberately long status value";
    value.error = "current error with enough content to exceed SSO";
    value.waveform = {1.f, -2.f, 3.5f};
    value.spectrum_db = {-10., -20., -30.};
    value.spectrum_complex = {{1.f, 2.f}, {3.f, 4.f}};
    value.constellation = {{5., 6.}, {7., 8.}};
    value.pattern_scores = {{9., 10.}, {11., 12.}};
    value.pattern_score_observations = {{41, {}, 0.125}, {42, {}, 0.25}};
    auto& trace = value.transmit_trace;
    trace.source = {1, 2};
    trace.compressed_bits = {3, 4, 5};
    trace.wire_plain_bits = {6, 7};
    trace.wire_bits = {8, 9, 10};
    trace.data_key_bits = {11};
    trace.pattern_input = {12, 13};
    trace.pattern_output = {14, 15, 16};
    trace.pattern_key = {17};
    trace.dsss_key = {18, 19};
    trace.outer_dsss_key = {20};
    trace.dsss_permutation_seed = {21, 22};
    trace.fhss_key = {23, 24, 25};
    trace.fhss_hop = 987654;
    value.sequence = 123456;
}

void prepared_copy_covers_every_owned_dynamic_field() {
    Snapshot source, destination;
    populate(source);
    const std::string transmit_error = "transmit failure detail longer than SSO";
    const auto required = snapshot_copy_requirement(source, transmit_error);
    check(!snapshot_copy_fits(destination, required),
          "fresh destination unexpectedly fits the populated snapshot");
    prepare_snapshot_copy(destination, required);
    check(snapshot_copy_fits(destination, required),
          "prepared destination does not fit every measured field");

    const auto copy_calls=allocation_probe::measure([&] { destination = source; });
    check(copy_calls.allocations==0 && copy_calls.deallocations==0,
          "prepared snapshot assignment entered the allocator");
    check(destination.receiver_error == source.receiver_error &&
          destination.clock_timing_status == source.clock_timing_status &&
          destination.status == source.status && destination.error == source.error,
          "snapshot string fields did not copy exactly");
    check(destination.waveform == source.waveform &&
          destination.spectrum_db == source.spectrum_db &&
          destination.spectrum_complex == source.spectrum_complex &&
          destination.constellation == source.constellation &&
          destination.pattern_scores == source.pattern_scores &&
          destination.pattern_score_observations == source.pattern_score_observations,
          "snapshot plot and score vectors did not copy exactly");
    const auto& a = destination.transmit_trace;
    const auto& b = source.transmit_trace;
    check(a.source == b.source && a.compressed_bits == b.compressed_bits &&
          a.wire_plain_bits == b.wire_plain_bits && a.wire_bits == b.wire_bits &&
          a.data_key_bits == b.data_key_bits && a.pattern_input == b.pattern_input &&
          a.pattern_output == b.pattern_output && a.pattern_key == b.pattern_key &&
          a.dsss_key == b.dsss_key && a.outer_dsss_key == b.outer_dsss_key &&
          a.dsss_permutation_seed == b.dsss_permutation_seed && a.fhss_key == b.fhss_key,
          "one or more transmit trace byte vectors did not copy exactly");
    check(destination.sequence == source.sequence && a.fhss_hop == b.fhss_hop,
          "ordinary scalar snapshot fields did not copy exactly");

    // The caller's post-copy fallback writes are covered by the same measured
    // capacities, while preserving receiver diagnostics in their own field.
    const auto fallback_calls=allocation_probe::measure([&] {
        destination.error = transmit_error;
        destination.status = "Transmission failed";
    });
    check(fallback_calls.allocations==0 && fallback_calls.deallocations==0,
          "prepared fallback assignments entered the allocator");
    check(destination.error == transmit_error && destination.status == "Transmission failed" &&
          destination.receiver_error == source.receiver_error &&
          destination.error.capacity() >= required.strings[3] &&
          destination.status.capacity() >= required.strings[2],
          "transmit fallback assignments exceed their prepared capacity");
}

void empty_and_short_string_sources_fit() {
    Snapshot empty, short_source, fresh_destination;
    short_source.status = "ok";
    short_source.error = "err";
    const auto empty_required = snapshot_copy_requirement(empty, {});
    check(snapshot_copy_fits(fresh_destination, empty_required),
          "empty source should fit a fresh destination");
    const auto short_required = snapshot_copy_requirement(short_source, {});
    check(snapshot_copy_fits(fresh_destination, short_required),
          "short SSO strings should fit a fresh destination");

    Snapshot retained_capacity_source;
    populate(retained_capacity_source);
    retained_capacity_source.receiver_error = "rx";
    retained_capacity_source.clock_timing_status.clear();
    retained_capacity_source.status = "ok";
    retained_capacity_source.error.clear();
    retained_capacity_source.waveform.clear();
    retained_capacity_source.spectrum_db.clear();
    retained_capacity_source.spectrum_complex.clear();
    retained_capacity_source.constellation.clear();
    retained_capacity_source.pattern_scores.clear();
    retained_capacity_source.pattern_score_observations.clear();
    auto& trace = retained_capacity_source.transmit_trace;
    trace.source.clear(); trace.compressed_bits.clear(); trace.wire_plain_bits.clear();
    trace.wire_bits.clear(); trace.data_key_bits.clear(); trace.pattern_input.clear();
    trace.pattern_output.clear(); trace.pattern_key.clear(); trace.dsss_key.clear();
    trace.outer_dsss_key.clear(); trace.dsss_permutation_seed.clear(); trace.fhss_key.clear();
    const auto retained_capacity_required = snapshot_copy_requirement(retained_capacity_source, {});
    check(retained_capacity_required.vectors == std::array<std::size_t, 6>{} &&
          retained_capacity_required.transmit_trace == std::array<std::size_t, 12>{} &&
          snapshot_copy_fits(fresh_destination, retained_capacity_required),
          "source capacity instead of current size affected the copy requirement");

    // Shrinking/empty sources must keep destination storage in place too.
    Snapshot large_destination;
    populate(large_destination);
    const auto shrink_calls=allocation_probe::measure([&] {
        large_destination=retained_capacity_source;
        large_destination=empty;
    });
    check(shrink_calls.allocations==0 && shrink_calls.deallocations==0,
          "shrinking or empty snapshot assignment entered the allocator");

    // Confirm the observer sees the operation that caused the old lock wait.
    Snapshot unprepared;
    const auto construction_calls=allocation_probe::measure([&] {
        Snapshot copy_source;
        // Empty vectors are intentional above; use populated owned fields here.
        populate(copy_source);
        unprepared=copy_source;
    });
    check(construction_calls.allocations>0 && construction_calls.deallocations>0,
          "allocation observer did not see unprepared owned storage");

    Snapshot fallback_source, fallback_destination;
    const std::string tx_error = "Transmission failed with detailed text";
    const auto fallback_required = snapshot_copy_requirement(fallback_source, tx_error);
    check(fallback_required.strings[3] == tx_error.size() &&
          fallback_required.strings[2] == std::string_view("Transmission failed").size(),
          "transmit error or status fallback requirement is incorrect");
    prepare_snapshot_copy(fallback_destination, fallback_required);
    fallback_destination.error = tx_error;
    fallback_destination.status = "Transmission failed";
    check(snapshot_copy_fits(fallback_destination, fallback_required),
          "fallback assignments exceed measured destination capacity");
}
}

int main() {
    try {
        prepared_copy_covers_every_owned_dynamic_field();
        empty_and_short_string_sources_fit();
        std::cout << "Snapshot copy capacity checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

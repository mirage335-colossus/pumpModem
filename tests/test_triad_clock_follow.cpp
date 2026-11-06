#include "datapump/triad_clock_follow.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using datapump::triad::v3::SourceClockFollower;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(long double a, long double b, long double tolerance, const char* message) {
    check(std::abs(a-b) <= tolerance, message);
}
template<class F> void rejects(F operation, const char* message) {
    try { operation(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}

void positive_and_negative() {
    for (const auto sign : {-1.L, 1.L}) {
        SourceClockFollower map;
        const auto duration = map.minimum_duration(2);
        near(duration, 12500, 1e-12L, "default minimum duration changed");
        map.update(100, sign*2);
        const auto begin = map.sample(100);
        near(begin.offset_seconds, 0, 0, "correction jumped at start");
        near(begin.rate_scale, 1, 0, "rate jumped at start");
        near(begin.acceleration, 0, 0, "acceleration jumped at start");
        long double last = begin.source_seconds;
        for (unsigned i = 1; i <= 10000; ++i) {
            const auto t = 100 + duration*i/10000;
            const auto sample = map.sample(t);
            check(sample.source_seconds > last, "source time stopped or reversed");
            check(std::abs(sample.offset_seconds) <= 2+1e-15L, "offset escaped original envelope");
            check(std::abs(sample.rate_scale-1) <= 300e-6L+1e-18L, "rate bound violated");
            check(std::abs(sample.acceleration) <= .2e-6L+1e-18L, "acceleration bound violated");
            last = sample.source_seconds;
        }
        const auto end = map.sample(100+duration);
        near(end.offset_seconds, sign*2, 0, "wrong final correction");
        near(end.rate_scale, 1, 0, "rate did not settle");
        near(end.acceleration, 0, 0, "acceleration did not settle");
        check(!map.transitioning(), "completed transition stayed active");
    }
}

void queued_changes() {
    SourceClockFollower map, reference;
    map.update(0, 2); reference.update(0, 2);
    const auto before = map.sample(1000);
    map.update(1000, -2);
    const auto after = map.sample(1000);
    near(before.offset_seconds, after.offset_seconds, 0, "queued update changed current phase");
    near(before.rate_scale, after.rate_scale, 0, "queued update changed current rate");
    near(before.acceleration, after.acceleration, 0, "queued update changed current acceleration");
    check(map.target_queued(), "active target was not queued");
    map.update(2000, -1); // latest target replaces pending -2
    for (const auto time : {2000.L, 6000.L, 12500.L}) {
        const auto a=map.sample(time), b=reference.sample(time);
        near(a.offset_seconds, b.offset_seconds, 0, "queued correction disturbed accepted transition");
        near(a.rate_scale, b.rate_scale, 0, "queued transition rate is discontinuous");
    }
    const auto settled = map.sample(12500+map.minimum_duration(-3));
    near(settled.offset_seconds, -1, 0, "replacement target did not settle");
    check(!map.transitioning() && !map.target_queued(), "queue did not empty");
    map.update(40000, 2);
    map.update(41000, -2);
    map.update(42000, 2); // restore accepted target, cancel pending reversal
    check(!map.target_queued(), "same target did not cancel queue");
    near(map.sample(100000).offset_seconds, 2, 0, "canceled target was applied");
}

void invalid_inputs_do_not_move_map() {
    SourceClockFollower map, reference;
    map.update(0, 1); reference.update(0, 1);
    rejects([&]{map.update(500, 2.00001L);}, "out-of-envelope target accepted");
    rejects([&]{map.update(500, -2, 1);}, "unsafe short transition accepted");
    rejects([&]{map.update(500, 1, 0);}, "zero duration accepted");
    rejects([&]{map.update(500, std::numeric_limits<long double>::infinity());}, "infinite target accepted");
    const auto a = map.sample(400), b = reference.sample(400);
    near(a.offset_seconds, b.offset_seconds, 0, "rejected update advanced time or changed map");
    rejects([&]{map.sample(399);}, "backward map time accepted");
    rejects([&]{map.sample(std::numeric_limits<long double>::quiet_NaN());}, "NaN time accepted");
    rejects([]{SourceClockFollower bad({2, .001L, .2e-6L});}, "excessive rate accepted");
    rejects([]{SourceClockFollower bad({2, .0003L, 1e-6L});}, "excessive acceleration accepted");
    rejects([]{SourceClockFollower bad({3, .0003L, .2e-6L});}, "renewed envelope accepted");
}

void no_op_long_span_and_chunk_invariance() {
    SourceClockFollower none;
    for (const auto time : {-10.L, 0.L, 86400.L*38, 1e12L}) {
        const auto s = none.sample(time);
        near(s.source_seconds, time, 0, "no-op changed source coordinate");
        near(s.rate_scale, 1, 0, "no-op changed rate");
    }
    SourceClockFollower dense, sparse;
    const long double start = 86400.L*38;
    dense.update(start, 2); sparse.update(start, 2);
    dense.update(start+100, -1); sparse.update(start+100, -1);
    for (unsigned i=100; i<=50000; ++i) {
        const auto a=dense.sample(start+i);
        if (i%997==0 || i==50000) {
            const auto b=sparse.sample(start+i);
            near(a.offset_seconds,b.offset_seconds,0,"map depends on sampling/chunk cadence");
            near(a.rate_scale,b.rate_scale,0,"rate depends on sampling/chunk cadence");
        }
    }
    SourceClockFollower far;
    far.update(1e12L, -2);
    const auto mid=far.sample(1e12L+6250);
    near(mid.offset_seconds,-1,1e-15L,"large relative coordinate lost local phase");
    near(mid.source_seconds,1e12L+6249,0,"large coordinate mapping incorrect");
}

void endpoint_support_is_not_truncated() {
    for (const auto late : {false, true}) {
        SourceClockFollower map;
        const long double end = 1000000;
        map.update(late ? end-1 : 0, -2);
        // The public +2s observation endpoint covers even the worst negative
        // displacement, including a still-pending late correction.
        check(map.sample(end+2).source_seconds >= end,
              "public clock-follow endpoint truncated source support");
    }
}
}

int main() {
    try {
        positive_and_negative(); queued_changes(); invalid_inputs_do_not_move_map();
        no_op_long_span_and_chunk_invariance();
        endpoint_support_is_not_truncated();
        std::cout << "triad clock-follow map tests passed\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "triad clock-follow map: " << e.what() << '\n'; return 1;
    }
}

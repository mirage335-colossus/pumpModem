// Standalone microbenchmark of the native drift ridge search at a fixed
// production geometry (default: the HF ridge shape, 1,762 segments x 16,384
// bins, centers +-1967 at stride 4, both bits, random FP64 power tables).
//
// It times three loops on identical tables and checks that their records are
// bit-identical:
//   old     - the former center-major loop (source cdd1cb20), kept here only
//             as the timing baseline;
//   new     - the segment-major loop, one slope per pass;
//   blocked - slope blocks, the loop of drift_ridge_records in
//             src/triad_receiver_fine.cpp (keep the two in step).
//
// Purpose: the open measurement "time the slope-blocked ridge on the CPU class
// that runs the HF slots" (docs/triad-gpu-optimization.md, Next levers, and
// docs/validation.md, 21 November 2026 entries). Remove this file once that
// measurement is recorded or the ridge search changes form.
//
// Not part of the CMake build. Build with the receiver's floating-point flags:
//   g++ -std=c++20 -O3 -DNDEBUG -ffp-contract=off -fno-fast-math -pthread \
//       tools/triad_drift_ridge_bench.cpp -o triad_drift_ridge_bench
// Run: triad_drift_ridge_bench SEGMENTS ROWS RADIUS STRIDE THREADS OLD_SLOPES NEW_SLOPES BLOCK
//   e.g. triad_drift_ridge_bench 1762 16384 1967 4 8 4 64 16
// Each thread owns its tables (462 MB each at the HF shape). Multi-GB
// allocations raise kernel page-zeroing load; on a load-gated host, treat
// multi-lane runs as indicative unless the tables are pre-faulted.
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <thread>
#include <barrier>
#include <vector>

constexpr double inf = std::numeric_limits<double>::infinity();
struct Rec { std::array<double, 2> score{-inf, -inf}; std::array<std::int64_t, 2> center{}; };

static std::size_t fft_bin(std::int64_t signed_bin, std::size_t rows) {
    return signed_bin >= 0 ? static_cast<std::size_t>(signed_bin) : static_cast<std::size_t>(signed_bin + static_cast<std::int64_t>(rows));
}

// Base loop, as in cdd1cb20 drift_slope_proposals::consider (score part).
[[gnu::noinline]] Rec old_record(const double* power, std::size_t S, std::size_t rows, std::int64_t radius,
                                 std::int64_t stride, const std::int64_t* offset) {
    Rec score;
    for (auto center = -radius; center <= radius; center += stride) {
        std::array<double, 2> evidence{};
        for (std::size_t segment = 0; segment < S; ++segment) {
            const auto signed_bin = center + offset[segment];
            if (std::abs(signed_bin) >= static_cast<std::int64_t>(rows / 2)) continue;
            const auto bin = fft_bin(signed_bin, rows);
            for (std::size_t bit = 0; bit < 2; ++bit)
                evidence[bit] += power[(bit * S + segment) * rows + bin];
        }
        for (std::size_t bit = 0; bit < 2; ++bit)
            if (evidence[bit] > score.score[bit]) { score.score[bit] = evidence[bit]; score.center[bit] = center; }
    }
    return score;
}

constexpr std::size_t max_centers = 1024;
// Interchanged loop, as in the working tree's drift_ridge_record.
[[gnu::noinline]] Rec new_record(const double* power, std::size_t S, std::size_t rows, std::int64_t radius,
                                 std::int64_t stride, std::size_t ncenters, const std::int64_t* offsets) {
    const auto half = static_cast<std::int64_t>(rows / 2);
    const auto count = static_cast<std::int64_t>(ncenters);
    const auto below = [&](std::int64_t bound) {
        return bound <= 0 ? std::int64_t{0} : std::min(count, (bound - 1) / stride + 1);
    };
    std::array<std::array<double, max_centers>, 2> evidence;
    for (auto& sums : evidence) std::fill_n(sums.begin(), ncenters, 0.);
    for (std::size_t segment = 0; segment < S; ++segment) {
        const auto origin = offsets[segment] - radius;
        const auto low = below(1 - half - origin), negative = below(-origin), high = below(half - origin);
        const auto wrapped = origin + static_cast<std::int64_t>(rows);
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto* row = power + (bit * S + segment) * rows;
            auto* sum = evidence[bit].data();
            for (auto i = low; i < negative; ++i) sum[i] += row[wrapped + i * stride];
            for (auto i = negative; i < high; ++i) sum[i] += row[origin + i * stride];
        }
    }
    Rec record;
    for (std::size_t bit = 0; bit < 2; ++bit)
        for (std::int64_t i = 0; i < count; ++i)
            if (evidence[bit][i] > record.score[bit]) { record.score[bit] = evidence[bit][i]; record.center[bit] = i * stride - radius; }
    return record;
}

// Slope-blocked variant: B slopes share each segment row pass. Each (slope, bit, center)
// sum keeps the same addends in segment order, so records equal new_record bit for bit.
[[gnu::noinline]] void blocked_records(const double* power, std::size_t S, std::size_t rows, std::int64_t radius,
                                       std::int64_t stride, std::size_t ncenters, const std::int64_t* const* offsets,
                                       std::size_t B, double* evidence /* B*2*max_centers */, Rec* out) {
    const auto half = static_cast<std::int64_t>(rows / 2);
    const auto count = static_cast<std::int64_t>(ncenters);
    const auto below = [&](std::int64_t bound) {
        return bound <= 0 ? std::int64_t{0} : std::min(count, (bound - 1) / stride + 1);
    };
    for (std::size_t b = 0; b < 2 * B; ++b) std::fill_n(evidence + b * max_centers, ncenters, 0.);
    for (std::size_t segment = 0; segment < S; ++segment) {
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto* row = power + (bit * S + segment) * rows;
            for (std::size_t b = 0; b < B; ++b) {
                const auto origin = offsets[b][segment] - radius;
                const auto low = below(1 - half - origin), negative = below(-origin), high = below(half - origin);
                const auto wrapped = origin + static_cast<std::int64_t>(rows);
                auto* sum = evidence + (b * 2 + bit) * max_centers;
                for (auto i = low; i < negative; ++i) sum[i] += row[wrapped + i * stride];
                for (auto i = negative; i < high; ++i) sum[i] += row[origin + i * stride];
            }
        }
    }
    for (std::size_t b = 0; b < B; ++b) {
        Rec record;
        for (std::size_t bit = 0; bit < 2; ++bit) {
            const auto* e = evidence + (b * 2 + bit) * max_centers;
            for (std::int64_t i = 0; i < count; ++i)
                if (e[i] > record.score[bit]) { record.score[bit] = e[i]; record.center[bit] = i * stride - radius; }
        }
        out[b] = record;
    }
}

int main(int argc, char** argv) {
    const std::size_t S = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 1762;
    const std::size_t rows = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 16384;
    const std::int64_t radius = argc > 3 ? std::strtol(argv[3], nullptr, 10) : 1967;
    const std::int64_t stride = argc > 4 ? std::strtol(argv[4], nullptr, 10) : 4;
    const unsigned threads = argc > 5 ? std::strtoul(argv[5], nullptr, 10) : 1;
    const unsigned slopes_old = argc > 6 ? std::strtoul(argv[6], nullptr, 10) : 4;
    const unsigned slopes_new = argc > 7 ? std::strtoul(argv[7], nullptr, 10) : 64;
    const std::size_t ncenters = static_cast<std::size_t>((2 * radius) / stride + 1);
    if (ncenters > max_centers) { std::fprintf(stderr, "too many centers\n"); return 2; }
    std::printf("S=%zu rows=%zu radius=%lld stride=%lld centers=%zu threads=%u table=%.1f MB each\n", S, rows,
                (long long)radius, (long long)stride, ncenters, threads, 2.0 * S * rows * 8 / 1e6);
    std::vector<std::vector<double>> tables(threads);
    std::vector<double> old_ns(threads), new_ns(threads), blk_ns(threads);
    const std::size_t blockB = argc > 8 ? std::strtoul(argv[8], nullptr, 10) : 16;
    std::vector<int> mismatch(threads);
    std::barrier sync(threads);
    auto work = [&](unsigned t) {
        auto& table = tables[t];
        table.resize(2 * S * rows);
        std::mt19937_64 rng(1234 + t);
        std::uniform_real_distribution<double> u(0, 1);
        for (auto& v : table) v = u(rng);
        std::vector<std::int64_t> off(S);
        // Linear ridge offsets spanning up to +-1500 bins at the public slope edge.
        auto offsets_for = [&](int k, int n) {
            const double slope = (2.0 * k / std::max(1, n - 1)) - 1.0;
            for (std::size_t s = 0; s < S; ++s)
                off[s] = std::llround(slope * 1500.0 * ((s + .5) / S - .5) * 2.0);
        };
        double sink = 0;
        sync.arrive_and_wait();
        auto t0 = std::chrono::steady_clock::now();
        std::vector<Rec> olds;
        for (unsigned k = 0; k < slopes_old; ++k) {
            offsets_for(k, slopes_old);
            olds.push_back(old_record(table.data(), S, rows, radius, stride, off.data()));
            sink += olds.back().score[0];
        }
        auto t1 = std::chrono::steady_clock::now();
        sync.arrive_and_wait();
        const auto t1b = std::chrono::steady_clock::now();
        std::vector<Rec> news;
        for (unsigned k = 0; k < slopes_new; ++k) {
            offsets_for(k, slopes_new);
            const auto r = new_record(table.data(), S, rows, radius, stride, ncenters, off.data());
            sink += r.score[0];
            news.push_back(r);
        }
        // old vs new on the old phase's slopes (recomputed with the same offsets)
        for (unsigned k = 0; k < slopes_old; ++k) {
            offsets_for(k, slopes_old);
            const auto r = new_record(table.data(), S, rows, radius, stride, ncenters, off.data());
            const auto& o = olds[k];
            for (int b = 0; b < 2; ++b)
                if (std::bit_cast<std::uint64_t>(o.score[b]) != std::bit_cast<std::uint64_t>(r.score[b]) ||
                    o.center[b] != r.center[b]) mismatch[t]++;
        }
        auto t2 = std::chrono::steady_clock::now();
        sync.arrive_and_wait();
        const auto t2b = std::chrono::steady_clock::now();
        {
            const std::size_t B = blockB;
            std::vector<std::vector<std::int64_t>> offs(B, std::vector<std::int64_t>(S));
            std::vector<const std::int64_t*> ptrs(B);
            std::vector<double> ev(B * 2 * max_centers);
            std::vector<Rec> out(B);
            for (unsigned k0 = 0; k0 < slopes_new; k0 += B) {
                for (std::size_t b = 0; b < B; ++b) {
                    offsets_for(k0 + b, slopes_new);
                    offs[b] = off; ptrs[b] = offs[b].data();
                }
                blocked_records(table.data(), S, rows, radius, stride, ncenters, ptrs.data(), B, ev.data(), out.data());
                for (std::size_t b = 0; b < B; ++b) {
                    sink += out[b].score[0];
                    const auto k = k0 + b;
                    if (k < slopes_new) {
                        const auto& o = news[k];
                        for (int q = 0; q < 2; ++q)
                            if (std::bit_cast<std::uint64_t>(o.score[q]) != std::bit_cast<std::uint64_t>(out[b].score[q]) ||
                                o.center[q] != out[b].center[q]) mismatch[t]++;
                    }
                }
            }
        }
        const auto t3 = std::chrono::steady_clock::now();
        blk_ns[t] = std::chrono::duration<double, std::nano>(t3 - t2b).count() / slopes_new / (2.0 * S * ncenters);
        const double loads = 2.0 * S * ncenters;
        old_ns[t] = std::chrono::duration<double, std::nano>(t1 - t0).count() / slopes_old / loads;
        new_ns[t] = std::chrono::duration<double, std::nano>(t2 - t1b).count() / slopes_new / loads;
        if (sink == 42) std::puts("");
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(work, t);
    for (auto& th : pool) th.join();
    for (unsigned t = 0; t < threads; ++t)
        std::printf("thread %u: old %.2f ns/load (%.1f ms/slope), new %.3f ns/load (%.2f ms/slope), blocked(B=%zu) %.3f ns/load (%.2f ms/slope), old/new %.1f, new/blocked %.1f, mismatches %d\n", t,
                    old_ns[t], old_ns[t] * 2.0 * S * ncenters / 1e6, new_ns[t], new_ns[t] * 2.0 * S * ncenters / 1e6,
                    blockB, blk_ns[t], blk_ns[t] * 2.0 * S * ncenters / 1e6, old_ns[t] / new_ns[t], new_ns[t] / blk_ns[t], mismatch[t]);
    return 0;
}

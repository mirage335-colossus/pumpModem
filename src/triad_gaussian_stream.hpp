#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace datapump::triad::v3::detail {

// The draws of std::normal_distribution<double>(0, 1) on a std::mt19937_64,
// in two parts. Draw j is the value of the distribution's j-th call.
// libstdc++ forms its draws in pairs by the polar method: an ordered loop
// takes two canonical uniforms of the engine and accepts them when
// 0 < r2 <= 1, and the pair's two draws are y*m and then x*m, with
// m = sqrt(-2 log r2 / r2), each times one plus zero. accept() runs that loop
// in order; finish() forms the draws of accepted pairs on any thread, with
// the library's operations in its order. Only the loop depends on earlier
// draws. The split is used only with libstdc++, and only when the first
// draws of several seeds, formed this way, equal the compiled distribution's
// bit for bit; otherwise, as with other standard libraries, accept() runs the
// distribution whole, which is exact but sequential. Draws and accepted pairs
// live in rings that the caller keeps from overwriting values it still reads.
// tests/test_triad_v3_channel.cpp compares the draws with the distribution's
// and requires the split with libstdc++.
class GaussianStream {
public:
    // Whether this build forms draws in two parts; decided once.
    static bool split_supported() {
#ifdef __GLIBCXX__
        static const bool value = matches_distribution();
        return value;
#else
        return false;
#endif
    }
    explicit GaussianStream(std::uint64_t seed) : GaussianStream(seed, split_supported()) {}
    static std::size_t bytes(std::size_t draw_capacity, std::size_t pair_capacity) {
        return draw_capacity * sizeof(double) + (split_supported() ? pair_capacity * sizeof(Pair) : 0);
    }
    bool split() const { return split_; }
    // Rings of draw_capacity draws (even) and pair_capacity accepted pairs.
    void open(std::size_t draw_capacity, std::size_t pair_capacity) {
        draws_.resize(draw_capacity);
        if (split_) pairs_.resize(pair_capacity);
    }
    // Draws [0, accepted()) were accepted and [0, finished()) formed.
    std::uint64_t accepted() const { return accepted_; }
    std::uint64_t finished() const { return finished_; }
    // How far accept() may go while draws from oldest on are still read and
    // the accepted pairs from finished() on are not yet formed.
    std::uint64_t accept_limit(std::uint64_t oldest) const {
        return split_ ? finished_ + 2 * pairs_.size() : oldest + draws_.size();
    }
    // How far finish() may form draws while those from oldest on are read.
    std::uint64_t finish_limit(std::uint64_t oldest) const {
        return split_ ? (oldest + draws_.size()) & ~std::uint64_t{1} : accepted_;
    }
    // The first whole pair boundary at or after draw j.
    std::uint64_t pair_end(std::uint64_t j) const { return split_ ? j + (j & 1) : j; }
    // On one thread: accept draws through at least target.
    void accept(std::uint64_t target) {
        if (split_) {
            while (accepted_ < target) {
                double x, y, r2;
                do {
                    x = 2.0 * canonical() - 1.0;
                    y = 2.0 * canonical() - 1.0;
                    r2 = x * x + y * y;
                } while (r2 > 1.0 || r2 == 0.0);
                pairs_[static_cast<std::size_t>((accepted_ / 2) % pairs_.size())] = {x, y, r2};
                accepted_ += 2;
            }
        } else {
            while (accepted_ < target) draws_[static_cast<std::size_t>(accepted_++ % draws_.size())] = whole_(engine_);
        }
    }
    // Forms the accepted draws [first, end), whole pairs, on any thread.
    void finish(std::uint64_t first, std::uint64_t end) {
        if (split_) {
            constexpr double stddev = 1.0, mean = 0.0;
            const auto size = draws_.size();
            for (auto j = first; j < end; j += 2) {
                const auto& pair = pairs_[static_cast<std::size_t>((j / 2) % pairs_.size())];
                const double mult = std::sqrt(-2 * std::log(pair.r2) / pair.r2);
                const double second = pair.x * mult;
                draws_[static_cast<std::size_t>(j % size)] = pair.y * mult * stddev + mean;
                draws_[static_cast<std::size_t>((j + 1) % size)] = second * stddev + mean;
            }
        }
    }
    // After the jobs that formed them: draws [0, end) are formed.
    void finished_through(std::uint64_t end) { finished_ = split_ ? end : accepted_; }
    double operator[](std::uint64_t j) const { return draws_[static_cast<std::size_t>(j % draws_.size())]; }
    // The next draw in order, formed at once. Only before any staged use.
    double next() {
        if (taken_ == finished_) {
            accept(taken_ + 1);
            finish(finished_, accepted_);
            finished_through(accepted_);
        }
        return (*this)[taken_++];
    }
    std::uint64_t taken() const { return taken_; }

private:
    struct Pair { double x, y, r2; };
    GaussianStream(std::uint64_t seed, bool split) : engine_(seed), split_(split) {}
    // The first draws of a few seeds, split, against the distribution.
    static bool matches_distribution() {
        for (const std::uint64_t seed : {1ULL, 0x9e3779b97f4a7c15ULL, 0xffffffffffffffffULL}) {
            GaussianStream stream(seed, true);
            stream.open(2, 1);
            std::mt19937_64 engine(seed);
            std::normal_distribution<double> normal{0, 1};
            for (int i = 0; i < 1024; ++i) {
                const double a = stream.next(), b = normal(engine);
                if (std::memcmp(&a, &b, sizeof a) != 0) return false;
            }
        }
        return true;
    }
    double canonical() {
        return std::generate_canonical<double, std::numeric_limits<double>::digits>(engine_);
    }
    std::mt19937_64 engine_;
    bool split_;
    std::normal_distribution<double> whole_{0, 1};
    std::vector<Pair> pairs_;
    std::vector<double> draws_;
    std::uint64_t accepted_ = 0, finished_ = 0, taken_ = 0;
};

} // namespace datapump::triad::v3::detail

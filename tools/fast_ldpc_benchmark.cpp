// Ideal-AWGN diagnostic for the production LDPC engine. No sound devices used.
#include "datapump/fast/ldpc.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>

using namespace datapump;
using namespace datapump::fast;
namespace {
CodeRate parse_rate(std::string_view name) {
    if (name == "7/9") return CodeRate::seven_ninths;
    if (name == "8/9") return CodeRate::eight_ninths;
    if (name == "9/10") return CodeRate::nine_tenths;
    throw Error("Expected 7/9, 8/9 or 9/10");
}
double elapsed(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 6 || argc > 7) {
            std::cerr << "usage: fast_ldpc_benchmark ORDER RATE ESN0_DB FRAMES SEED [MAX_ITERATIONS]\n";
            return 2;
        }
        const auto order = static_cast<unsigned>(std::stoul(argv[1]));
        const auto rate = parse_rate(argv[2]); const double db = std::stod(argv[3]);
        const auto frames = static_cast<unsigned>(std::stoul(argv[4]));
        const auto seed = std::stoull(argv[5]);
        const auto max_iterations = argc == 7 ? static_cast<unsigned>(std::stoul(argv[6])) : ldpc::default_iterations;
        if (order < 4 || order > 4194304 || !std::has_single_bit(order) || std::countr_zero(order) % 2 ||
            !frames || frames > 10000 || !std::isfinite(db) || db < 0 || db > 100 ||
            !max_iterations || max_iterations > ldpc::maximum_iterations)
            throw Error("Invalid bounded benchmark arguments");
        const auto bps = static_cast<unsigned>(std::countr_zero(order)), axis_bits = bps / 2;
        const auto side = 1U << axis_bits; const auto k = ldpc::data_bits(rate);
        const double scale = std::sqrt(2. * (order - 1) / 3), n0 = std::pow(10., -db / 10), sigma = std::sqrt(n0 / 2);
        std::vector<unsigned> inverse(side);
        std::vector<double> levels(side), metric(side);
        for (unsigned v = 0; v < side; ++v) { inverse[v ^ (v >> 1)] = v; levels[v] = (2. * v - side + 1) / scale; }
        std::mt19937_64 random(seed); std::normal_distribution<double> gaussian;
        std::uint64_t raw_errors = 0, wrong_bits = 0, total_iterations = 0;
        unsigned wrong_frames = 0, nonconverged_frames = 0, undetected_frames = 0;
        double conditional_entropy = 0, decoder_seconds = 0, channel_seconds = 0;
        Bytes source(k / 8); std::vector<float> soft(ldpc::coded_bits);
        const auto began = std::chrono::steady_clock::now();
        for (unsigned frame = 0; frame < frames; ++frame) {
            for (auto& b : source) b = static_cast<std::uint8_t>(random());
            const auto bits = ldpc::interleave(ldpc::encode(source, rate));
            const auto channel_start = std::chrono::steady_clock::now();
            for (std::size_t symbol = 0; symbol < bits.size(); symbol += bps) {
                for (unsigned axis = 0; axis < 2; ++axis) {
                    unsigned gray = 0;
                    for (unsigned bit = 0; bit < axis_bits; ++bit) {
                        const auto position = symbol + axis * axis_bits + bit;
                        if (position < bits.size()) gray |= unsigned(bits[position]) << bit;
                    }
                    const double received = levels[inverse[gray]] + sigma * gaussian(random);
                    double largest = -std::numeric_limits<double>::infinity();
                    for (unsigned v = 0; v < side; ++v) {
                        const double delta = received - levels[v]; metric[v] = -delta * delta / n0;
                        largest = std::max(largest, metric[v]);
                    }
                    for (auto& p : metric) p = std::exp(p - largest);
                    for (unsigned bit = 0; bit < axis_bits; ++bit) {
                        const auto position = symbol + axis * axis_bits + bit;
                        if (position >= bits.size()) continue;
                        double p0 = 0, p1 = 0;
                        for (unsigned v = 0; v < side; ++v)
                            (((v ^ (v >> 1)) >> bit) & 1U ? p1 : p0) += metric[v];
                        const double llr = std::clamp(std::log(std::max(p1, 1e-300) / std::max(p0, 1e-300)), -50., 50.);
                        soft[position] = static_cast<float>(llr);
                        raw_errors += (llr > 0) != bool(bits[position]);
                        conditional_entropy += std::log1p(std::exp(-llr * (bits[position] ? 1 : -1))) / std::log(2.);
                    }
                }
            }
            channel_seconds += elapsed(channel_start);
            const auto canonical = ldpc::deinterleave(soft);
            const auto decoder_start = std::chrono::steady_clock::now();
            const auto decoded = ldpc::decode(canonical, rate, max_iterations);
            decoder_seconds += elapsed(decoder_start);
            total_iterations += decoded.iterations;
            std::uint64_t errors = 0;
            for (std::size_t byte = 0; byte < source.size(); ++byte)
                errors += std::popcount(unsigned(source[byte] ^ decoded.bytes[byte]));
            wrong_bits += errors; wrong_frames += errors != 0;
            nonconverged_frames += !decoded.converged; undetected_frames += decoded.converged && errors != 0;
        }
        std::cout << std::setprecision(10) << argv[2] << ',' << order << ',' << db << ',' << ldpc::coded_bits << ',' << k << ','
                  << frames << ',' << seed << ',' << max_iterations << ',' << wrong_frames << ',' << nonconverged_frames << ','
                  << undetected_frames << ',' << wrong_bits << ',' << double(raw_errors) / (ldpc::coded_bits * frames) << ','
                  << bps * (1 - conditional_entropy / (ldpc::coded_bits * frames)) << ',' << double(total_iterations) / frames << ','
                  << decoder_seconds / frames << ',' << channel_seconds / frames << ',' << elapsed(began) << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

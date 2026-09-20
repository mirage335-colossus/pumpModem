#include "datapump/fast/ldpc.hpp"
#include "../third_party/ldpc/tables.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>

using namespace datapump;
using namespace datapump::fast;
namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> void rejects(F&& f) {
    try { f(); } catch (const Error&) { return; }
    throw std::runtime_error("LDPC accepted invalid geometry/likelihood");
}
Bytes source(CodeRate rate) {
    Bytes result(ldpc::data_bits(rate) / 8);
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = (i * 73 + i / 7 + 0x5a) & 255U;
    return result;
}
std::string digest(const Bytes& bytes) {
    std::array<unsigned char, 32> out{}; unsigned size = 0;
    check(EVP_Digest(bytes.data(), bytes.size(), out.data(), &size, EVP_sha256(), nullptr) == 1 && size == 32,
          "SHA-256 fixture digest");
    std::ostringstream text;
    for (auto byte : out) text << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    return text.str();
}
template<class Table> bool independent_syndrome(const Bytes& bits) {
    // Direct DVB accumulator equations, not the production graph construction
    // or traversal. Also checks the last parity node and accumulator boundary.
    constexpr int r = Table::N - Table::K, q = r / Table::M;
    std::array<unsigned char, r> sums{};
    int bit = 0, position = 0;
    for (int d = 0; Table::DEG[d]; ++d) {
        for (int group = 0; group < Table::LEN[d]; ++group) {
            for (int within = 0; within < Table::M; ++within, ++bit)
                for (int e = 0; e < Table::DEG[d]; ++e)
                    sums[(Table::POS[position + e] + within * q) % r] ^= bits[bit];
            position += Table::DEG[d];
        }
    }
    for (int j = 0; j < r; ++j)
        if ((sums[j] ^ bits[Table::K + j] ^ (j ? bits[Table::K + j - 1] : 0)) != 0) return false;
    return true;
}
constexpr std::array rates{CodeRate::half, CodeRate::three_quarters, CodeRate::seven_ninths,
                           CodeRate::eight_ninths, CodeRate::nine_tenths};
void fixtures() {
    // Frozen SHA-256 of 64800 unpacked 0/1 code bits from the independently
    // compiled xdsopl encoder.hh at pinned commit 32357d8..., with source()'s
    // byte pattern, positive upstream signs denoting zero and MSB-first input.
    constexpr std::array expected{
        "ae4a2202bd42e246e012b7f095abe6fcbd7c42def4e1f2414c29907e32079aea",
        "5d505f18fa8929976fbddad7ea0f479a4dfa5a731b551ed123a54cd5dc0fe7f7",
        "91bfdb7f3945900cbc08f59643a26930235bc36331052e66355e2d8b9899dbaf",
        "ee760163d51f6d37ef6377e785951f419eec865f089a37a5e07b7588b1f3a94d",
        "89e91cee15cbb4e8e01a8ac1de3cbad2b564e849416417dc62ee2b593220b0bb"};
    constexpr std::array<std::size_t, 5> k{32400, 48600, 50400, 57600, 58320};
    for (std::size_t r = 0; r < rates.size(); ++r) {
        const auto rate = rates[r]; const auto bytes = source(rate);
        check(ldpc::data_bits(rate) == k[r], "DVB K geometry");
        const auto bits = ldpc::encode(bytes, rate);
        check(digest(bits) == expected[r], "independent upstream codeword fixture");
        check(ldpc::valid_codeword(bits, rate), "clean production syndrome");
        const bool independent = r == 0 ? independent_syndrome<DVB_S2_TABLE_B4>(bits) :
            r == 1 ? independent_syndrome<DVB_S2_TABLE_B7>(bits) :
            r == 2 ? independent_syndrome<DVB_S2X_TABLE_B10>(bits) :
            r == 3 ? independent_syndrome<DVB_S2_TABLE_B10>(bits) :
                     independent_syndrome<DVB_S2_TABLE_B11>(bits);
        check(independent, "independent parity equations");
        std::vector<float> soft(bits.size());
        for (std::size_t b = 0; b < bits.size(); ++b) soft[b] = bits[b] ? 14.F : -14.F;
        auto decoded = ldpc::decode(soft, rate);
        check(decoded.converged && decoded.iterations == 0 && decoded.bytes == bytes, "clean systematic decode");
        for (auto bit : {std::size_t{0}, k[r] - 1, k[r], ldpc::coded_bits - 1}) {
            auto wrong = bits; wrong[bit] ^= 1;
            check(!ldpc::valid_codeword(wrong, rate), "syndrome detects flipped boundary bit");
            soft[bit] = -soft[bit]; decoded = ldpc::decode(soft, rate); soft[bit] = -soft[bit];
            check(decoded.converged && decoded.bytes == bytes, "single boundary error correction");
        }
        const Bytes zero(bytes.size());
        const auto zero_word = ldpc::encode(zero, rate);
        check(std::all_of(zero_word.begin(), zero_word.end(), [](auto b) { return b == 0; }),
              "linear all-zero word");
        std::fill(soft.begin(), soft.end(), -50.F);
        decoded = ldpc::decode(soft, rate);
        check(decoded.converged && decoded.bytes == zero, "observed zero word is distinct from zero evidence");
    }
}
void interleaver() {
    const auto bits = ldpc::encode(source(CodeRate::seven_ninths), CodeRate::seven_ninths);
    const auto shuffled = ldpc::interleave(bits);
    std::vector<float> soft(shuffled.begin(), shuffled.end());
    const auto recovered = ldpc::deinterleave(soft);
    std::vector<bool> seen(ldpc::coded_bits);
    Bytes serialized; serialized.reserve(2 * ldpc::coded_bits);
    for (std::size_t b = 0; b < bits.size(); ++b) {
        const auto index = ldpc::interleave_index(b);
        check(!seen[index], "permutation bijection"); seen[index] = true;
        check(recovered[b] == bits[b], "permutation inversion");
        serialized.push_back(static_cast<std::uint8_t>(index >> 8));
        serialized.push_back(static_cast<std::uint8_t>(index));
    }
    // Wire-stability fixture generated with an independent Python uint32 loop.
    check(ldpc::interleave_index(0) == 61579 && ldpc::interleave_index(64799) == 17783,
          "frozen bit permutation");
    check(digest(serialized) == "66d86cce87d998b41d6b992b5581cf1dc8987ddb4a59b561b46e4fce1e716255",
          "independent complete permutation fixture");
}
void bpsk_noise(unsigned frames) {
    std::mt19937 random(0x1d9c); std::normal_distribution<double> gaussian;
    for (auto rate : rates) {
        const auto began = std::chrono::steady_clock::now();
        unsigned total_iterations = 0, raw_errors = 0;
        for (unsigned frame = 0; frame < frames; ++frame) {
            auto bytes = source(rate); for (auto& b : bytes) b = static_cast<std::uint8_t>(random());
            const auto bits = ldpc::encode(bytes, rate);
            const double sigma = .32; std::vector<float> soft(bits.size());
            for (std::size_t i = 0; i < bits.size(); ++i) {
                soft[i] = static_cast<float>(2 * ((bits[i] ? 1 : -1) + sigma * gaussian(random)) / (sigma * sigma));
                raw_errors += (soft[i] > 0) != bool(bits[i]);
            }
            const auto decoded = ldpc::decode(soft, rate);
            check(decoded.converged && decoded.bytes == bytes, "high-margin seeded noisy frame");
            total_iterations += decoded.iterations;
        }
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        std::cout << "LDPC K=" << ldpc::data_bits(rate) << " frames=" << frames
                  << " raw_errors=" << raw_errors << " wrong_frames=0 mean_iterations="
                  << double(total_iterations) / frames << " full_pipeline_ms/frame=" << 1000 * seconds / frames << '\n';
    }
}
void qam_noise(unsigned order, CodeRate rate, double db) {
    // Independent reflected-Gray square-QAM and separable exact log-sum LLRs.
    // This verifies LDPC plus mixing against nonuniform QAM bit reliability.
    const auto bytes = source(rate), code = ldpc::encode(bytes, rate), bits = ldpc::interleave(code);
    const auto side = static_cast<unsigned>(std::sqrt(order));
    const auto axis_bits = static_cast<unsigned>(std::log2(side)); const auto bits_per_symbol = 2 * axis_bits;
    const double scale = std::sqrt(2. * (order - 1) / 3), n0 = std::pow(10., -db / 10), sigma = std::sqrt(n0 / 2);
    std::mt19937 random(94731); std::normal_distribution<double> gaussian;
    std::vector<float> soft(bits.size()); std::vector<double> likelihood(side);
    unsigned raw_errors = 0;
    for (std::size_t symbol = 0; symbol < bits.size(); symbol += bits_per_symbol) {
        for (unsigned axis = 0; axis < 2; ++axis) {
            unsigned gray = 0;
            for (unsigned b = 0; b < axis_bits; ++b) {
                const auto position = symbol + axis * axis_bits + b;
                if (position < bits.size()) gray |= unsigned(bits[position]) << b;
            }
            unsigned binary = gray; for (unsigned shift = gray >> 1; shift; shift >>= 1) binary ^= shift;
            const double received = (2. * binary - side + 1) / scale + sigma * gaussian(random);
            double max_metric = -std::numeric_limits<double>::infinity();
            for (unsigned v = 0; v < side; ++v) {
                const auto delta = received - (2. * v - side + 1) / scale;
                likelihood[v] = -delta * delta / n0; max_metric = std::max(max_metric, likelihood[v]);
            }
            for (auto& p : likelihood) p = std::exp(p - max_metric);
            for (unsigned b = 0; b < axis_bits; ++b) {
                const auto position = symbol + axis * axis_bits + b; if (position >= bits.size()) continue;
                double p0 = 0, p1 = 0;
                for (unsigned v = 0; v < side; ++v)
                    ((v ^ (v >> 1)) & (1U << b) ? p1 : p0) += likelihood[v];
                soft[position] = static_cast<float>(std::clamp(std::log(std::max(p1, 1e-100) / std::max(p0, 1e-100)), -50., 50.));
                raw_errors += (soft[position] > 0) != bool(bits[position]);
            }
        }
    }
    const auto started = std::chrono::steady_clock::now();
    const auto decoded = ldpc::decode(ldpc::deinterleave(soft), rate);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "QAM=" << order << " K=" << ldpc::data_bits(rate) << " EsN0=" << db
              << " raw_errors=" << raw_errors << " iterations=" << decoded.iterations
              << " decoder_ms=" << seconds * 1000 << '\n';
    check(decoded.converged && decoded.bytes == bytes, "Gray QAM noisy frame");
}
void invalid_and_bounded() {
    const auto rate = CodeRate::seven_ninths;
    rejects([&] { ldpc::data_bits(CodeRate::seven_eighths); });
    rejects([&] { ldpc::encode(Bytes(1), rate); });
    rejects([&] { ldpc::decode(std::vector<float>(1), rate); });
    std::vector<float> soft(ldpc::coded_bits);
    check(!ldpc::decode(soft, rate).converged, "zero evidence must not converge");
    rejects([&] { ldpc::decode(soft, rate, 0); });
    rejects([&] { ldpc::decode(soft, rate, 101); });
    soft[0] = std::numeric_limits<float>::quiet_NaN(); rejects([&] { ldpc::decode(soft, rate); });
    soft[0] = std::numeric_limits<float>::infinity(); rejects([&] { ldpc::decode(soft, rate); });
    std::mt19937 random(4271); std::normal_distribution<float> noise;
    for (auto& value : soft) value = noise(random);
    const auto result = ldpc::decode(soft, rate, 2);
    check(!result.converged && result.iterations == 2 && result.bytes.size() == ldpc::data_bits(rate) / 8,
          "bounded noise failure retains hard posterior bytes");
    rejects([&] { ldpc::interleave_index(ldpc::coded_bits); });
    rejects([&] { ldpc::interleave(Bytes(7)); });
    rejects([&] { ldpc::deinterleave(std::vector<float>(7)); });
    auto nonbits = Bytes(ldpc::coded_bits); nonbits.back() = 2;
    rejects([&] { ldpc::valid_codeword(nonbits, rate); });
}
void parallel_calls() {
    std::array<std::future<void>, rates.size()> jobs;
    for (std::size_t i = 0; i < jobs.size(); ++i) jobs[i] = std::async(std::launch::async, [i] {
        const auto data = source(rates[i]), bits = ldpc::encode(data, rates[i]);
        std::vector<float> soft(bits.size());
        for (std::size_t b = 0; b < bits.size(); ++b) soft[b] = bits[b] ? 10.F : -10.F;
        soft[319 + i] = -soft[319 + i];
        const auto result = ldpc::decode(soft, rates[i]);
        check(result.converged && result.bytes == data, "concurrent decoder scratch isolation");
    });
    for (auto& job : jobs) job.get();
}
}
int main(int argc, char** argv) {
    try {
        const unsigned frames = argc == 2 ? static_cast<unsigned>(std::stoul(argv[1])) : 12;
        check(frames > 0 && frames <= 10000, "bounded benchmark frame count");
        fixtures(); interleaver(); invalid_and_bounded(); parallel_calls(); bpsk_noise(frames);
        qam_noise(4, CodeRate::half, 2.);
        qam_noise(16, CodeRate::half, 7.);
        qam_noise(4096, CodeRate::seven_ninths, 31.8);
        qam_noise(16384, CodeRate::seven_ninths, 37.8);
        qam_noise(16384, CodeRate::eight_ninths, 40.);
        qam_noise(16384, CodeRate::nine_tenths, 42.);
        qam_noise(65536, CodeRate::nine_tenths, 48.);
        std::cout << "Fast LDPC tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

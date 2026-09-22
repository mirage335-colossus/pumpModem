#include "datapump/fast/ldpc.hpp"
#include "../../third_party/ldpc/tables.hpp"
#include "../../third_party/ldpc/short_tables.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace datapump::fast::ldpc {
namespace {
// A graph is initialized once, never mutated, and shared safely across callers.
// Every decode owns its messages and posterior values (under 3 MiB per call).
struct Graph {
    std::size_t n = 0, k = 0;
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint16_t> variables;
    std::vector<std::uint16_t> parity_rows;
};
template<class Table> Graph make_graph() {
    static_assert((Table::N == coded_bits || Table::N == short_coded_bits) && Table::M == 360);
    Graph g; g.n = Table::N; g.k = Table::K;
    const auto r = g.n - g.k;
    const auto q = r / Table::M;
    std::vector<std::vector<std::uint16_t>> checks(r);
    std::size_t bit = 0, address = 0;
    for (std::size_t degree_group = 0; Table::DEG[degree_group]; ++degree_group) {
        const auto degree = Table::DEG[degree_group];
        for (int group = 0; group < Table::LEN[degree_group]; ++group) {
            for (int within = 0; within < Table::M; ++within, ++bit) {
                for (int edge = 0; edge < degree; ++edge) {
                    const auto row = (Table::POS[address + edge] + within * q) % r;
                    checks[row].push_back(static_cast<std::uint16_t>(bit));
                }
            }
            address += degree;
        }
    }
    if (bit != g.k) throw Error("Invalid LDPC data geometry");
    g.offsets.reserve(r + 1); g.variables.reserve(Table::LINKS_TOTAL);
    g.parity_rows.reserve(r);
    // The quasi-cyclic layered order used by the upstream reference decoder.
    for (std::size_t layer = 0; layer < q; ++layer) {
        for (int within = 0; within < Table::M; ++within) {
            const auto row = q * within + layer;
            g.offsets.push_back(static_cast<std::uint32_t>(g.variables.size()));
            g.parity_rows.push_back(static_cast<std::uint16_t>(row));
            g.variables.insert(g.variables.end(), checks[row].begin(), checks[row].end());
            g.variables.push_back(static_cast<std::uint16_t>(g.k + row));
            if (row) g.variables.push_back(static_cast<std::uint16_t>(g.k + row - 1));
            if (checks[row].size() + 2 > 32) throw Error("Invalid LDPC check degree");
        }
    }
    g.offsets.push_back(static_cast<std::uint32_t>(g.variables.size()));
    if (g.variables.size() != Table::LINKS_TOTAL) throw Error("Invalid LDPC edge count");
    return g;
}
const Graph& graph(CodeRate rate, std::size_t frame_bits) {
    if (frame_bits == short_coded_bits) {
        switch (rate) {
        case CodeRate::half: { static const auto g = make_graph<DVB_S2_TABLE_C4>(); return g; }
        case CodeRate::two_thirds: { static const auto g = make_graph<DVB_S2_TABLE_C6>(); return g; }
        case CodeRate::three_quarters: { static const auto g = make_graph<DVB_S2_TABLE_C7>(); return g; }
        default: throw Error("Unsupported Fast short-frame LDPC rate");
        }
    }
    if (frame_bits != coded_bits) throw Error("Unsupported Fast LDPC frame size");
    switch (rate) {
    case CodeRate::two_thirds: { static const auto g = make_graph<DVB_S2_TABLE_B6>(); return g; }
    case CodeRate::half: { static const auto g = make_graph<DVB_S2_TABLE_B4>(); return g; }
    case CodeRate::three_quarters: { static const auto g = make_graph<DVB_S2_TABLE_B7>(); return g; }
    case CodeRate::seven_ninths: { static const auto g = make_graph<DVB_S2X_TABLE_B10>(); return g; }
    case CodeRate::eight_ninths: { static const auto g = make_graph<DVB_S2_TABLE_B10>(); return g; }
    case CodeRate::nine_tenths: { static const auto g = make_graph<DVB_S2_TABLE_B11>(); return g; }
    default: throw Error("Unsupported Fast LDPC rate");
    }
}
template<class Bit> bool syndrome(const Graph& g, Bit bit) {
    for (std::size_t row = 0; row < g.parity_rows.size(); ++row) {
        unsigned parity = 0;
        for (auto edge = g.offsets[row]; edge < g.offsets[row + 1]; ++edge)
            parity ^= bit(g.variables[edge]);
        if (parity) return false;
    }
    return true;
}
// phi(x) = -log(tanh(x/2)). These forms avoid cancellation at both extremes.
// The finite cap bounds saturated extrinsic messages without a min-sum loss.
double phi(double x) {
    if (x <= 1e-12) return 28.324168296488494;
    if (x > 20) return 2 * std::exp(-x);
    return std::log1p(2 / std::expm1(x));
}
template<std::size_t N> const std::array<std::uint16_t, N>& make_permutation() {
    static const auto result = [] {
        std::array<std::uint16_t, N> indices{};
        std::iota(indices.begin(), indices.end(), std::uint16_t{0});
        // Explicit xorshift32 plus descending Fisher-Yates: no implementation-
        // dependent standard-library distribution or shuffle on the wire.
        std::uint32_t state = 0x6c647063U;
        for (std::size_t n = indices.size(); n > 1; --n) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            std::swap(indices[n - 1], indices[state % n]);
        }
        return indices;
    }();
    return result;
}
std::span<const std::uint16_t> permutation(std::size_t frame_bits) {
    if (frame_bits == coded_bits) return make_permutation<coded_bits>();
    if (frame_bits == short_coded_bits) return make_permutation<short_coded_bits>();
    throw Error("Unsupported Fast LDPC frame size");
}
}

std::size_t data_bits(CodeRate rate, std::size_t frame_bits) { return graph(rate, frame_bits).k; }
Bytes encode(std::span<const std::uint8_t> bytes, CodeRate rate, std::size_t frame_bits) {
    const auto& g = graph(rate, frame_bits);
    if (bytes.size() != g.k / 8) throw Error("Fast LDPC requires exactly one data frame");
    Bytes bits(frame_bits);
    for (std::size_t bit = 0; bit < g.k; ++bit)
        bits[bit] = (bytes[bit / 8] >> (7 - bit % 8)) & 1U;
    for (std::size_t row = 0; row < g.parity_rows.size(); ++row) {
        const auto parity_row = g.parity_rows[row];
        const auto data_end = g.offsets[row + 1] - (parity_row ? 2 : 1);
        for (auto edge = g.offsets[row]; edge < data_end; ++edge)
            bits[g.k + parity_row] ^= bits[g.variables[edge]];
    }
    for (std::size_t bit = g.k + 1; bit < frame_bits; ++bit) bits[bit] ^= bits[bit - 1];
    return bits;
}
bool valid_codeword(std::span<const std::uint8_t> bits, CodeRate rate, std::size_t frame_bits) {
    const auto& g = graph(rate, frame_bits);
    if (bits.size() != frame_bits) throw Error("Fast LDPC requires exactly one codeword");
    if (std::any_of(bits.begin(), bits.end(), [](auto b) { return b > 1; }))
        throw Error("Fast LDPC codeword contains a non-bit");
    return syndrome(g, [&](std::size_t bit) { return bits[bit]; });
}
DecodeResult decode(std::span<const float> soft, CodeRate rate, unsigned limit, std::size_t frame_bits) {
    const auto& g = graph(rate, frame_bits);
    if (soft.size() != frame_bits) throw Error("Fast LDPC requires exactly one soft frame");
    if (!limit || limit > maximum_iterations) throw Error("Invalid Fast LDPC iteration limit");
    std::vector<double> posterior(frame_bits);
    bool informative = false;
    for (std::size_t bit = 0; bit < frame_bits; ++bit) {
        if (!std::isfinite(soft[bit])) throw Error("Nonfinite Fast LDPC likelihood");
        posterior[bit] = -std::clamp(double(soft[bit]), -50., 50.);
        informative |= soft[bit] != 0;
    }
    DecodeResult result; result.bytes.resize(g.k / 8);
    if (!informative) return result;
    auto satisfied = [&] {
        // Exactly unknown variables cannot masquerade as a valid all-zero word.
        if (std::any_of(posterior.begin(), posterior.end(), [](auto p) { return p == 0; })) return false;
        return syndrome(g, [&](std::size_t bit) { return posterior[bit] < 0; });
    };
    result.converged = satisfied();
    if (!result.converged) {
        std::vector<double> messages(g.variables.size());
        std::array<double, 32> extrinsic{}, phis{};
        while (!result.converged && result.iterations < limit) {
            for (std::size_t row = 0; row < g.parity_rows.size(); ++row) {
                const auto start = g.offsets[row], end = g.offsets[row + 1];
                double sum = 0; unsigned negative = 0;
                for (auto edge = start; edge < end; ++edge) {
                    const auto local = edge - start;
                    const auto value = posterior[g.variables[edge]] - messages[edge];
                    extrinsic[local] = value;
                    phis[local] = phi(std::abs(value)); sum += phis[local];
                    negative ^= value < 0;
                }
                for (auto edge = start; edge < end; ++edge) {
                    const auto local = edge - start;
                    auto value = phi(std::max(0., sum - phis[local]));
                    if (negative ^ (extrinsic[local] < 0)) value = -value;
                    messages[edge] = value;
                    posterior[g.variables[edge]] = extrinsic[local] + value;
                }
            }
            ++result.iterations; result.converged = satisfied();
        }
    }
    for (std::size_t bit = 0; bit < g.k; ++bit)
        if (posterior[bit] < 0) result.bytes[bit / 8] |= 1U << (7 - bit % 8);
    return result;
}
std::size_t interleave_index(std::size_t bit, std::size_t frame_bits) {
    if (bit >= frame_bits) throw Error("Fast LDPC interleave index outside frame");
    return permutation(frame_bits)[bit];
}
Bytes interleave(std::span<const std::uint8_t> bits, std::size_t frame_bits) {
    if (bits.size() != frame_bits) throw Error("Fast LDPC requires exactly one interleave frame");
    const auto p = permutation(frame_bits);
    Bytes out(frame_bits);
    for (std::size_t i = 0; i < frame_bits; ++i) out[i] = bits[p[i]];
    return out;
}
std::vector<float> deinterleave(std::span<const float> soft, std::size_t frame_bits) {
    if (soft.size() != frame_bits) throw Error("Fast LDPC requires exactly one deinterleave frame");
    const auto p = permutation(frame_bits);
    std::vector<float> out(frame_bits);
    for (std::size_t i = 0; i < frame_bits; ++i) out[p[i]] = soft[i];
    return out;
}
}

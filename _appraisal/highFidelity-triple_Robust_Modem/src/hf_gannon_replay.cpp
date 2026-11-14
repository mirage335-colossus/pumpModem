#include "appraisal/hf_gannon_replay.hpp"

#include <openssl/evp.h>

#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace appraisal {
namespace {
constexpr std::size_t knots = 36000;
constexpr std::size_t fixture_bytes = knots * 2 * sizeof(double);
constexpr std::string_view uuid = "0e51e045ca1d42868f3c9cd483112bd7";
constexpr std::array<HfGannonTraceInfo, 2> records{{
    {17, 1715360400, knots, 10, 10000000, 4, uuid,
     "data/psws_grapeDRF/ch0_W2NAF_2024-05-10/2024-05-10T00-00-00/rf@1715360400.000.h5",
     "8397dc68dcae74222252ddc93162e331a96931d12db9a9e656af031c361dffc5",
     "2c56dd525dde8e58e3d78add0e8e9e5c08eddb28473748358aa38c42d30af8d0",
     "w2naf-20240510-1700-col4.f64iq"},
    {20, 1715371200, knots, 10, 10000000, 4, uuid,
     "data/psws_grapeDRF/ch0_W2NAF_2024-05-10/2024-05-10T00-00-00/rf@1715371200.000.h5",
     "5b9e38b1319cb172d1c6c161b28960e10607d69b951593cd1700df91517173c7",
     "c23ab7e470eaaeb1b501c397ac3552ea04e13cec13a07e497e1c1b41e7cb098b",
     "w2naf-20240510-2000-col4.f64iq"}
}};

void require(bool value, const char* message) {
    if (!value) throw std::invalid_argument(message);
}

std::string sha256(const std::vector<unsigned char>& bytes) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned size = 0;
    require(EVP_Digest(bytes.data(), bytes.size(), digest.data(), &size,
                       EVP_sha256(), nullptr) == 1 && size == 32,
            "Gannon fixture SHA-256 computation failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (unsigned i = 0; i < size; ++i) {
        result.push_back(hex[digest[i] >> 4]);
        result.push_back(hex[digest[i] & 15]);
    }
    return result;
}

double read_double_le(const unsigned char* bytes) {
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
                  "Gannon fixture requires IEEE-754 binary64");
    std::uint64_t bits = 0;
    for (unsigned i = 0; i < 8; ++i) bits |= std::uint64_t(bytes[i]) << (8 * i);
    const auto value = std::bit_cast<double>(bits);
    require(std::isfinite(value) && value >= -32768 && value <= 32767 &&
            value == std::trunc(value), "Gannon fixture lost raw int16 sample values");
    return value;
}

struct RawHour {
    std::vector<std::complex<double>> samples;
    long double energy = 0;
};

RawHour read_hour(const std::filesystem::path& directory, const HfGannonTraceInfo& info) {
    std::ifstream input(directory / info.fixture_filename, std::ios::binary | std::ios::ate);
    require(bool(input), "Gannon fixture cannot be opened");
    require(input.tellg() == static_cast<std::streamoff>(fixture_bytes),
            "Gannon fixture must contain exactly 36000 binary64 I/Q pairs");
    input.seekg(0);
    std::vector<unsigned char> bytes(fixture_bytes);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(input), "Gannon fixture truncated while reading");
    require(sha256(bytes) == info.serialized_iq_sha256, "Gannon fixture SHA-256 mismatch");
    RawHour hour;
    hour.samples.reserve(knots);
    for (std::size_t i = 0; i < knots; ++i) {
        const auto real = read_double_le(bytes.data() + i * 16);
        const auto imag = read_double_le(bytes.data() + i * 16 + 8);
        hour.samples.emplace_back(real, imag);
        hour.energy += static_cast<long double>(real) * real +
                       static_cast<long double>(imag) * imag;
    }
    require(hour.energy > 0, "Gannon fixture has zero energy");
    return hour;
}
}

const HfGannonTraceInfo& hf_gannon_2024_info(unsigned utc_hour) {
    for (const auto& record : records) if (record.utc_hour == utc_hour) return record;
    throw std::invalid_argument("Gannon replay only admits 17 or 20 UTC on 2024-05-10");
}

std::filesystem::path hf_gannon_2024_fixture_directory() {
#ifdef APPRAISAL_GANNON_FIXTURE_DIR
    return APPRAISAL_GANNON_FIXTURE_DIR;
#else
    throw std::invalid_argument("Gannon fixture directory was not configured at build time");
#endif
}

std::shared_ptr<const HfMeasuredTrace> load_gannon_2024_trace(
    unsigned utc_hour, HfReplayPart part, const std::filesystem::path& fixture_directory) {
    const auto& info = hf_gannon_2024_info(utc_hour);
    (void)hf_replay_part_name(part);
    const auto directory = fixture_directory.empty() ? hf_gannon_2024_fixture_directory() : fixture_directory;
    // Always verify and use both complete hours. Selecting a different record
    // or message window must not change the gain or hide relative attenuation.
    auto hour17 = read_hour(directory, records[0]);
    auto hour20 = read_hour(directory, records[1]);
    const long double joint_mean_power = (hour17.energy + hour20.energy) / (2 * knots);
    auto& selected = utc_hour == 17 ? hour17 : hour20;
    auto trace = std::make_shared<HfMeasuredTrace>();
    trace->sample_rate_hz = info.sample_rate_hz;
    trace->raw_mean_power = static_cast<double>(selected.energy / knots);
    trace->normalization_gain = static_cast<double>(1 / std::sqrt(joint_mean_power));
    trace->part = part;
    trace->source_sha256 = info.source_hdf5_sha256;
    trace->serialized_iq_sha256 = info.serialized_iq_sha256;
    trace->samples = std::move(selected.samples);
    for (auto& value : trace->samples) value *= trace->normalization_gain;
    validate_hf_replay_trace(*trace);
    return trace;
}

std::shared_ptr<const HfMeasuredTrace> make_hf_measurement_noise_proxy(bool beacon_present) {
    constexpr std::array<std::complex<double>, 4> noise{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
    auto trace = std::make_shared<HfMeasuredTrace>();
    trace->sample_rate_hz = 10;
    trace->raw_mean_power = beacon_present ? 2 : 1;
    trace->normalization_gain = 1;
    // Hash of the exact UTF-8 definition in the fixture README, and of the
    // raw LE binary64 real,imag pairs generated by this fixed definition.
    trace->source_sha256 = "233bdf29e84e7cdecad274daf6534befe16697a47cc75bcaa143f9c59eb57ed8";
    trace->serialized_iq_sha256 = beacon_present ?
        "76bd47fbfe16fa18dd29cc890c7032326183cf9fea0a67d3ba0e54558840da29" :
        "cfb25bc81335ef2f0d0140325fff88bc2761b95b9f20fd3a93c4aa2f99e5d5dd";
    trace->samples.reserve(knots);
    const std::complex<double> carrier{beacon_present ? 1. : 0., 0};
    for (std::size_t i = 0; i < knots; ++i) trace->samples.push_back(carrier + noise[i % noise.size()]);
    validate_hf_replay_trace(*trace);
    return trace;
}

}

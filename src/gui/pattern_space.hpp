#pragma once
#include "datapump/modem.hpp"
#include "datapump/pattern_code.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace datapump::gui::inspection {
struct PatternEvidence {
    std::string name;
    std::vector<int> code;
    double correlation = 0; // Signed normalized weighted matched-template mean.
    double residual_fraction = 0; // Energy outside the valid code's complex span.
    // Time-integrated squared distance for unit-amplitude templates, after
    // fitting their common phase but retaining equal amplitude. In seconds.
    double squared_distance = 0;
};
struct PatternSpace {
    // A bounded prefix of each independent input-chip pattern before pulse
    // shaping, never transmitted PCM or received sample history. Code signs
    // provide row labels; complex codewords carry the logical chip phase and
    // amplitude. Coefficients retain the display row count.
    std::vector<int> code;
    std::vector<std::complex<double>> coefficients;
    // Pattern transport has independent codeword rows. Only a bounded prefix
    // is illustrated; an hours-long keyed symbol must not allocate its chips.
    std::vector<std::vector<std::complex<double>>> codewords;
    bool bounded_pattern_preview = false, truncated = false;
    std::uint64_t full_symbol_samples = 0;
    double independent_squared_distance = 0;
    std::vector<std::uint64_t> chip_weights;
    std::uint64_t chip_samples = 0, symbol_samples = 0, period_samples = 0;
    std::uint64_t complete_periods = 0, tail_samples = 0;
    std::size_t effective_chips = 0;
    double chip_seconds = 0, symbol_seconds = 0;
    double chip_esn0_db = 0, symbol_esn0_db = 0, processing_gain_db = 0;
    bool representative_keyed = false;
    // Code structure and one-chip timing evidence are distinct from ordinary
    // coherent integration. A tone still rejects nonconstant chip vectors.
    bool code_selective = false, timing_selective = false;
    double minimum_squared_distance = 0, minimum_noise_squared_distance = 0;
    std::pair<std::size_t, std::size_t> nearest_symbols{};
    std::optional<PatternEvidence> unused_pattern;
    PatternEvidence one_chip_shift;
    double cn0_db_hz = 0;
    std::complex<double> chip_value(std::size_t symbol,std::size_t chip) const {
        return codewords.empty()?coefficients.at(symbol)*static_cast<double>(code.at(chip)):codewords.at(symbol).at(chip);
    }

    // Weighted complex distance over the illustrated input-chip prefix before
    // pulse shaping. This excludes filter overlap, tails and crest limiting;
    // it is not a shaped-waveform distance or acquisition confidence. Private
    // patterns retain their varying chip amplitudes; continuous tones use
    // their chip-center preview. Synthetic fixtures may use scalar rows.
    double squared_distance(std::size_t a, std::size_t b) const {
        if (a >= coefficients.size() || b >= coefficients.size()) throw Error("pattern symbol index is out of range");
        double result = 0;
        if(codewords.empty())result=symbol_seconds*std::norm(coefficients[a]-coefficients[b]);
        else result=a==b?0:independent_squared_distance;
        if (!std::isfinite(result)) throw Error("pattern distance exceeds numeric range");
        return result;
    }
    // Conditional coherent AWGN distance in quadrature-noise units. This
    // assumes ideal matched chip coordinates; it does not model the emitted
    // shaped PCM and is not a BER estimate or an acquisition-lock measurement.
    double noise_squared_distance(std::size_t a, std::size_t b) const {
        const auto distance = squared_distance(a, b);
        if (distance == 0) return 0;
        const auto logarithm = std::log(distance / modem::nominal_signal_power) + cn0_db_hz * std::log(10.) / 10;
        if (!std::isfinite(logarithm) || logarithm < std::log(std::numeric_limits<double>::min()) ||
            logarithm > std::log(std::numeric_limits<double>::max()))
            throw Error("pattern noise distance exceeds numeric range");
        return std::exp(logarithm);
    }
};

inline PatternSpace inspect_pattern_space(const modem::Config& config, double cn0_db_hz,
                                          bool public_keyed_illustration = false) {
    modem::validate(config);
    if (!std::isfinite(cn0_db_hz)) throw Error("pattern inspection requires finite C/N0");
    auto illustrated = config;
    PatternSpace result;
    result.representative_keyed = public_keyed_illustration && (config.scramble || config.dsss);
    if (result.representative_keyed) {
        // Deliberately public, reproducible demonstration material. A GUI
        // configuration need not contain the epoch-derived transmit seeds.
        // Never silently portray this illustration as the secret wire code.
        illustrated.stream_epoch=0;illustrated.data_key.reset();
        for (std::size_t i = 0; i < illustrated.spreading_seed.size(); ++i) {
            illustrated.spreading_seed[i] = static_cast<std::uint8_t>(0x39U + 17U * i);
            illustrated.dsss_seed[i] = static_cast<std::uint8_t>(0xa7U + 29U * i);
        }
    }
    result.bounded_pattern_preview=true;
    modem::PatternCode generator(illustrated,illustrated.stream_epoch);
    result.chip_samples=generator.chip_samples();
    result.full_symbol_samples=generator.symbol_samples();
    const auto shown=static_cast<std::size_t>(std::min<std::uint64_t>(generator.chips_per_symbol(),16384));
    result.symbol_samples=std::min(result.full_symbol_samples,result.chip_samples*shown);
    result.truncated=result.symbol_samples<result.full_symbol_samples;
    result.period_samples=result.symbol_samples;result.tail_samples=result.symbol_samples;
    result.chip_seconds=static_cast<double>(result.chip_samples)/config.sample_rate;
    result.symbol_seconds=static_cast<double>(result.symbol_samples)/config.sample_rate;
    result.cn0_db_hz=cn0_db_hz;
    result.chip_esn0_db=cn0_db_hz+10*std::log10(result.chip_seconds);
    result.symbol_esn0_db=cn0_db_hz+10*std::log10(result.symbol_seconds);
    result.processing_gain_db=10*std::log10(result.symbol_seconds/result.chip_seconds);
    const auto amplitude=std::sqrt(2*modem::nominal_signal_power);
    result.coefficients={{amplitude,0},{amplitude,0}};
    result.code.resize(shown);result.chip_weights.resize(shown);
    result.codewords.assign(2,std::vector<std::complex<double>>(shown));
    for(std::size_t chip=0;chip<shown;++chip) {
        result.chip_weights[chip]=std::min(result.chip_samples,result.symbol_samples-chip*result.chip_samples);
        const auto fraction=.5*static_cast<double>(result.chip_weights[chip])/static_cast<double>(result.chip_samples);
        for(unsigned bit=0;bit<2;++bit)result.codewords[bit][chip]=amplitude*generator.value(chip,bit,fraction);
        result.code[chip]=result.codewords[0][chip].real()<0?-1:1;
        result.independent_squared_distance+=static_cast<double>(result.chip_weights[chip])/config.sample_rate*
            std::norm(result.codewords[0][chip]-result.codewords[1][chip]);
    }
    result.effective_chips=shown;
    result.code_selective=shown>1 && config.spreading_mode==modem::SpreadingMode::pattern;
    std::complex<double> shifted_dot{};double energy=0,shifted_energy=0;
    for(std::size_t chip=0;chip<shown;++chip) {
        const auto next=amplitude*generator.value(chip+1,0,.5);
        const auto weight=static_cast<double>(result.chip_weights[chip])/static_cast<double>(result.symbol_samples);
        shifted_dot+=weight*result.codewords[0][chip]*std::conj(next);
        energy+=weight*std::norm(result.codewords[0][chip]);shifted_energy+=weight*std::norm(next);
    }
    result.one_chip_shift.name="One-chip offset of illustrated pattern";
    const auto normalization=std::sqrt(energy*shifted_energy);
    result.one_chip_shift.correlation=normalization>0?std::clamp(std::abs(shifted_dot)/normalization,0.,1.):0;
    result.one_chip_shift.residual_fraction=std::max(0.,1-result.one_chip_shift.correlation*result.one_chip_shift.correlation);
    result.one_chip_shift.squared_distance=2*result.symbol_seconds*(1-result.one_chip_shift.correlation);
    result.timing_selective=shown>1 && result.one_chip_shift.residual_fraction>1e-12;
    result.nearest_symbols={0,1};
    result.minimum_squared_distance=result.squared_distance(0,1);
    result.minimum_noise_squared_distance=result.noise_squared_distance(0,1);
    return result;
}
}

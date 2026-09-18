#include "datapump/lpi_estimate.hpp"
#include "datapump/pattern_pulse.hpp"
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::lpi {
Estimate estimate(const transfer::Estimate& transmission,const transfer::Options& options,double cn0_db_hz) {
    auto config=options.modem;
    const bool private_transmission=options.key.has_value() && config.spreading_mode==modem::SpreadingMode::pattern;
    // Validate the actual effective transport without deriving/examining keys.
    // Tone clears private layers before validation in the transfer API.
    if(config.spreading_mode==modem::SpreadingMode::tone) {
        config.scramble=false;config.dsss=false;config.data_key.reset();
    } else if(options.key)config.scramble=true;
    modem::validate(config);
    if(!std::isfinite(cn0_db_hz) || !std::isfinite(transmission.total_seconds) || transmission.total_seconds<0)
        throw Error("invalid LPI estimate input");
    Estimate result;
    result.hypothetical_encryption=!private_transmission;
    // The advisory always compares a private pattern at the current timing.
    // This local copy changes neither actual tone/public modulation nor keys,
    // source encoding, automatic profile selection or current draft airtime.
    config.spreading_mode=modem::SpreadingMode::pattern;
    config.scramble=true;
    result.cn0_db_hz=cn0_db_hz;
    result.symbol_seconds=static_cast<double>(modem::symbol_sample_count(config))/config.sample_rate;
    // Intended RRC support, not the nominal Rate (which is twice chip rate).
    // Rectangular pulses use the nominal band as an explicit approximation.
    // This models all received power inside that band, not a measured spectrum.
    result.observation_bandwidth_hz=modem::pattern_pulse_enabled(config)?
        (1+modem::pattern_pulse_rolloff)*config.sample_rate/static_cast<double>(modem::pattern_chip_samples(config)):
        config.bandwidth_hz;
    const auto log_band=std::log(static_cast<long double>(result.observation_bandwidth_hz));
    const auto log_snr=static_cast<long double>(cn0_db_hz)*(std::numbers::ln10_v<long double>/10)-log_band;
    result.in_band_snr_db=cn0_db_hz-10*std::log10(result.observation_bandwidth_hz);
    // Stable both far below the noise and for enormous finite positive inputs.
    result.noise_rise_db=log_snr>0?
        result.in_band_snr_db+static_cast<double>(10/std::numbers::ln10_v<long double>*std::log1p(std::exp(-log_snr))):
        static_cast<double>(10/std::numbers::ln10_v<long double>*std::log1p(std::exp(log_snr)));
    // A normal approximation to average Gaussian signal-plus-noise power.
    // At this cutoff B*T90 exceeds 1,395 independent complex samples; the
    // small-sample/high-SNR case is deliberately not extrapolated.
    if(result.in_band_snr_db> -10) {
        result.status=Status::outside_weak_signal_model;
        return result;
    }
    constexpr long double z_false_alarm=2.3263478740408408L; // Phi^-1(.99)
    constexpr long double z_detection=1.2815515655446004L;   // Phi^-1(.90)
    const auto snr=std::exp(log_snr);
    // sqrt(B*T)*S/N = z_.99 + (1+S/N)*z_.90.
    // Logs avoid overflow in inverse-SNR squared for very weak inputs.
    const auto log_time=2*std::log(z_false_alarm+(1+snr)*z_detection)-log_band-2*log_snr;
    const auto log_symbols=log_time-std::log(static_cast<long double>(result.symbol_seconds));
    const auto maximum_log=std::log(static_cast<long double>(std::numeric_limits<double>::max()));
    if(!std::isfinite(log_time) || log_time>maximum_log || log_symbols>maximum_log) {
        result.status=Status::numeric_limit;
        return result;
    }
    result.detection_seconds=static_cast<double>(std::exp(log_time));
    result.equivalent_symbols=static_cast<double>(std::exp(log_symbols));
    result.burst_exposure_ratio=transmission.total_seconds/result.detection_seconds;
    if(!std::isfinite(result.detection_seconds) || !std::isfinite(result.equivalent_symbols) ||
       !std::isfinite(result.burst_exposure_ratio)) {
        result.status=Status::numeric_limit;
        return result;
    }
    result.status=Status::available;
    return result;
}
}

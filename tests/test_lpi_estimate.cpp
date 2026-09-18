#include "datapump/lpi_estimate.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message) {
    check(std::abs(actual-expected)<=1e-10*std::max(std::abs(expected),1e-300),message);
}
transfer::Options private_options() {
    transfer::Options options;
    options.modem=tuning::resolve(100,-3,tuning::PatternMode::auto_keystream,true).config;
    options.key.emplace(Bytes(32,0x37));options.timestamp=1800000000;
    return options;
}
void reference_and_scaling() {
    auto options=private_options();
    const auto transmission=transfer::estimate_binary(Bytes{0,0,1},options);
    const auto result=lpi::estimate(transmission,options);
    check(result.status==lpi::Status::available && !result.hypothetical_encryption,
          "private weak signal must receive an actual-waveform model estimate");
    // Independent fixed reference values: Es/N0=18 dB for ONE receiver symbol,
    // Pd=.90, Pfa=.01, Gaussian energy detector at the same received C/N0.
    near(lpi::receiver_reference_symbol_snr_db,18,"receiver reference changed");
    near(result.observation_bandwidth_hz,62.5,"shaped signal must use intended RRC band, not nominal Rate");
    near(result.symbol_seconds,163.84,"symbol conversion must follow transmitted sample geometry");
    near(result.reference_cn0_db_hz,-4.144199392957368,"one-bit receiver normalization changed");
    near(result.detection_seconds,5509.6971517094125,"relative radiometer time reference changed");
    near(result.equivalent_symbols,33.62852265447639,"total observer-to-receiver ratio changed");
    near(result.additional_symbols,32.62852265447639,"additional symbols must exclude receiver's one symbol");
    near(result.noise_rise_db,.026677785882117257,"normalized noise rise changed");
    near(result.burst_exposure_ratio,transmission.total_seconds/result.detection_seconds,"full burst exposure missing");
    check(result.burst_exposure_ratio>3/result.equivalent_symbols,"suppression/filter exposure omitted");
    auto twice=transmission;twice.total_seconds*=2;
    const auto accumulated=lpi::estimate(twice,options);
    near(accumulated.equivalent_symbols,result.equivalent_symbols,"draft length cannot change relative detection ratio");
    near(accumulated.burst_exposure_ratio,2*result.burst_exposure_ratio,"repeated on-air exposure must accumulate");
    options.modem.integration_seconds=320;
    const auto longer=lpi::estimate(transmission,options);
    near(longer.reference_cn0_db_hz,-7.051499783199059,"longer symbol must normalize to lower received power");
    near(longer.equivalent_symbols,65.54078554614466,"longer integration should increase relative observation ratio");
    options.modem.integration_seconds=14400;
    near(lpi::estimate(transmission,options).equivalent_symbols,2942.8829410868257,
         "four-hour receiver reference was treated as the original absolute link power");
    options.modem.pulse_shaping=false;
    const auto rectangular=lpi::estimate(transmission,options);
    near(rectangular.observation_bandwidth_hz,100,"rectangular model must explicitly use nominal band");
    near(rectangular.equivalent_symbols,4708.524766937335,"rectangular relative reference changed");
    options=private_options();options.modem.bandwidth_hz=200;options.modem.integration_seconds=0;
    const auto same_bt=lpi::estimate(transmission,options);
    near(same_bt.equivalent_symbols,result.equivalent_symbols,"equal time-bandwidth products must give equal relative ratios");
    near(same_bt.detection_seconds,result.detection_seconds/2,"time conversion must follow symbol duration");
    options.modem.bandwidth_hz=110;options.modem.integration_seconds=163.84001;
    const auto quantized=lpi::estimate(transmission,options);
    near(quantized.observation_bandwidth_hz,7500./110,"observation band must respect ceil-quantized chip samples");
    near(quantized.symbol_seconds,983041./6000,"receiver normalization must use whole transmitted samples");
    near(quantized.reference_cn0_db_hz,18-10*std::log10(983041./6000),"reference used unquantized integration time");
}
void eligibility_and_limits() {
    auto options=private_options();const auto transmission=transfer::estimate_binary(Bytes{0},options);
    const auto baseline=lpi::estimate(transmission,options);
    options.modem.dsss=true;
    near(lpi::estimate(transmission,options).equivalent_symbols,baseline.equivalent_symbols,
         "independent DSSS mixing must not multiply the spreading gain");
    options.modem.scramble=false;
    check(lpi::estimate(transmission,options).status==lpi::Status::available,"DSSS-only private waveform omitted");
    options.modem.dsss=false;
    check(!lpi::estimate(transmission,options).hypothetical_encryption,
          "transfer always enables private scrambling for a non-tone key, even with manual settings");
    options.key.emplace(Bytes(32,0x92));options.timestamp++;
    near(lpi::estimate(transmission,options).equivalent_symbols,baseline.equivalent_symbols,
         "key material and timestamp must not affect energy detection ratio");
    options.modem.scramble=true;options.key.reset();
    const auto public_seed=lpi::estimate(transmission,options);
    check(public_seed.status==lpi::Status::available && public_seed.hypothetical_encryption,
          "publicly configured seeds must show a hypothetical encrypted estimate");
    options.modem.scramble=false;options.modem.data_key.emplace(Bytes(32,0x37));
    const auto data_only=lpi::estimate(transmission,options);
    check(data_only.status==lpi::Status::available && data_only.hypothetical_encryption,
          "low-level Data masking alone must leave the private-waveform estimate hypothetical");
    options=private_options();options.modem.spreading_mode=modem::SpreadingMode::tone;
    const auto tone=lpi::estimate(transmission,options);
    check(tone.status==lpi::Status::available && tone.hypothetical_encryption,
          "tone estimate must remain hypothetical even when its input supplies a key");
    near(tone.observation_bandwidth_hz,62.5,"hypothetical tone scenario must use the corresponding shaped pattern band");
    near(tone.equivalent_symbols,baseline.equivalent_symbols,"tone experiment changed the encrypted scenario's geometry");
    check(options.modem.spreading_mode==modem::SpreadingMode::tone && options.key && options.modem.scramble,
          "hypothetical tone scenario must not mutate caller options");
    options=private_options();options.modem.pulse_shaping=false;
    options.modem.integration_seconds=6.31;
    check(lpi::estimate(transmission,options).status==lpi::Status::available,
          "weak side of -10 dB reference-SNR cutoff changed");
    options.modem.integration_seconds=6.309;
    const auto strong=lpi::estimate(transmission,options);
    check(strong.status==lpi::Status::outside_weak_signal_model && strong.detection_seconds==0,
          "strong normalized reference must not extrapolate weak-signal counts");
    for(const auto bad:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid=transmission;invalid.total_seconds=bad;
        bool rejected=false;try {(void)lpi::estimate(invalid,options);}catch(const Error&){rejected=true;}
        check(rejected,"invalid burst exposure was accepted");
    }
    auto huge_burst=transmission;huge_burst.total_seconds=std::numeric_limits<double>::max();
    options=private_options();options.modem.bandwidth_hz=30000000;options.modem.sample_rate=120000000;
    options.modem.carrier_hz=30000000;options.modem.spreading_factor=1024;options.modem.integration_seconds=0;
    check(lpi::estimate(huge_burst,options).status==lpi::Status::numeric_limit,
          "unrepresentable exposure must not report a finite available estimate");
    options.key.reset();
    const auto hypothetical_limit=lpi::estimate(huge_burst,options);
    check(hypothetical_limit.status==lpi::Status::numeric_limit && hypothetical_limit.hypothetical_encryption,
          "numeric limit must not discard the hypothetical scenario flag");
}
void hypothetical_scenarios() {
    auto options=private_options();
    const auto keyed=lpi::estimate(transfer::estimate_binary(Bytes{0,0,1},options),options);
    options.key.reset();options.modem.scramble=false;
    Message message;message.data=Bytes{'e'};
    const auto before=transfer::estimate(message,options);
    const auto wire_before=transfer::message_wire_bits(message,options);
    const auto hypothetical=lpi::estimate(before,options);
    check(hypothetical.status==lpi::Status::available && hypothetical.hypothetical_encryption,
          "keyless experimentation must retain a numerical encrypted scenario");
    near(hypothetical.equivalent_symbols,keyed.equivalent_symbols,"keyless and keyed scenarios differ at the same timing");
    const auto after=transfer::estimate(message,options);
    check(!options.key && !options.modem.data_key && !options.modem.scramble && !options.modem.dsss &&
          wire_before==Bytes({0,0,1}) && transfer::message_wire_bits(message,options)==wire_before &&
          after.waveform_samples==before.waveform_samples,
          "advisory experimentation must not enable encryption or alter the exact public transmission");
    options.modem.integration_seconds=.01;
    const auto strong=lpi::estimate(before,options);
    check(strong.status==lpi::Status::outside_weak_signal_model && strong.hypothetical_encryption,
          "strong keyless scenario must keep both model-limit status and hypothetical warning");
    options.modem.integration_seconds=0;options.modem.spreading_mode=modem::SpreadingMode::tone;
    const auto tone_before=transfer::estimate(message,options);
    const auto tone=lpi::estimate(tone_before,options);
    check(tone.hypothetical_encryption && tone.status==lpi::Status::available,"tone-only experimentation requires no keyfile");
    near(tone.equivalent_symbols,keyed.equivalent_symbols,"hypothetical tone must assume private patterns at current timing");
    near(tone.burst_exposure_ratio,tone_before.total_seconds/tone.detection_seconds,
         "tone exposure must compare the current draft airtime, without inventing encrypted overhead");
    check(transfer::estimate(message,options).waveform_samples==tone_before.waveform_samples &&
          options.modem.spreading_mode==modem::SpreadingMode::tone,
          "hypothetical estimate changed the actual tone waveform");
}
void framing_unchanged() {
    auto options=private_options();Message message;message.data=Bytes{'e'};
    const auto before=transfer::estimate(message,options);
    check(before.wire_bits==3,"independent e endpoint must remain 001");
    const auto short_text=lpi::estimate(before,options);
    const auto raw=lpi::estimate(transfer::estimate_binary(Bytes{0,0,1},options),options);
    near(short_text.burst_exposure_ratio,raw.burst_exposure_ratio,"same wire bits must give same exposure");
    const auto after=transfer::estimate(message,options);
    check(after.wire_bits==before.wire_bits && after.waveform_samples==before.waveform_samples,
          "advisory model must not alter wire bits or waveform samples");
    options.fec=FecMode::off;
    near(lpi::estimate(transfer::estimate(message,options),options).burst_exposure_ratio,
         short_text.burst_exposure_ratio,"saved FEC must not inflate short-message exposure");
    message.data=Bytes(17,'e');
    const auto interval=transfer::estimate(message,options);
    check(interval.wire_bits==1216,"long path must retain 192-bit marker and 128-coded-byte interval");
    const auto longer=lpi::estimate(interval,options);
    near(longer.equivalent_symbols,short_text.equivalent_symbols,"codec/FEC cannot change relative detection ratio");
    check(longer.burst_exposure_ratio>1216/longer.equivalent_symbols,
          "interval exposure must include every marker and coded bit plus waveform overhead");
}
}
int main() {
    try {reference_and_scaling();eligibility_and_limits();hypothetical_scenarios();framing_unchanged();std::cout<<"LPI estimate tests passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

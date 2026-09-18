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
    const auto result=lpi::estimate(transmission,options,-3);
    check(result.status==lpi::Status::available,"private weak signal must receive a model estimate");
    // Independent fixed reference values for the Gaussian radiometer model.
    near(result.observation_bandwidth_hz,62.5,"shaped signal must use intended RRC band, not nominal Rate");
    near(result.symbol_seconds,163.84,"symbol conversion must follow transmitted sample geometry");
    near(result.detection_seconds,3257.3126172241587,"radiometer time reference changed");
    near(result.equivalent_symbols,19.881058454737296,"wire-symbol detection reference changed");
    near(result.noise_rise_db,.03468716302439728,"noise rise is separate from processing gain");
    near(result.burst_exposure_ratio,transmission.total_seconds/result.detection_seconds,"full burst exposure missing");
    check(result.burst_exposure_ratio>3/result.equivalent_symbols,"suppression/filter exposure omitted");
    auto twice=transmission;twice.total_seconds*=2;
    const auto accumulated=lpi::estimate(twice,options,-3);
    near(accumulated.detection_seconds,result.detection_seconds,"message length cannot change per-link detection time");
    near(accumulated.burst_exposure_ratio,2*result.burst_exposure_ratio,"repeated on-air exposure must accumulate");
    const auto weaker=lpi::estimate(transmission,options,-6);
    check(weaker.detection_seconds>3.9*result.detection_seconds && weaker.detection_seconds<4*result.detection_seconds,
          "3 dB less signal should need about four times the radiometer observation");
    options.modem.integration_seconds=320;
    const auto longer_symbol=lpi::estimate(transmission,options,-3);
    near(longer_symbol.detection_seconds,result.detection_seconds,"longer bit integration does not lower received power");
    near(longer_symbol.equivalent_symbols,result.equivalent_symbols*163.84/320,
         "same energy observation needs fewer longer symbols");
    options.modem.integration_seconds=14400;
    check(lpi::estimate(transmission,options,-3).equivalent_symbols<1,
          "unkeyed energy detection can occur before a four-hour symbol completes");
    options.modem.pulse_shaping=false;
    const auto rectangular=lpi::estimate(transmission,options,-3);
    near(rectangular.observation_bandwidth_hz,100,"rectangular model must explicitly use nominal band");
    near(rectangular.detection_seconds,5200.603951310766,"rectangular reference changed");
    options.modem.pulse_shaping=true;options.modem.bandwidth_hz=110;
    near(lpi::estimate(transmission,options,-3).observation_bandwidth_hz,7500./110,
         "observation band must respect ceil-quantized chip samples");
}
void eligibility_and_limits() {
    auto options=private_options();const auto transmission=transfer::estimate_binary(Bytes{0},options);
    const auto baseline=lpi::estimate(transmission,options,-3);
    options.modem.dsss=true;
    near(lpi::estimate(transmission,options,-3).detection_seconds,baseline.detection_seconds,
         "independent DSSS mixing must not multiply the spreading gain");
    options.modem.scramble=false;
    check(lpi::estimate(transmission,options,-3).status==lpi::Status::available,"DSSS-only private waveform omitted");
    options.modem.dsss=false;
    check(lpi::estimate(transmission,options,-3).status==lpi::Status::available,
          "transfer always enables private scrambling for a non-tone key, even with manual settings");
    options.modem.scramble=true;options.key.reset();
    check(lpi::estimate(transmission,options,-3).status==lpi::Status::public_waveform,
          "publicly configured seeds must not imply a secret waveform");
    options.modem.scramble=false;options.modem.data_key.emplace(Bytes(32,0x37));
    check(lpi::estimate(transmission,options,-3).status==lpi::Status::public_waveform,
          "low-level Data masking alone must not imply a private transmitted waveform");
    options=private_options();options.modem.spreading_mode=modem::SpreadingMode::tone;
    check(lpi::estimate(transmission,options,-3).status==lpi::Status::public_waveform,"tones cannot advertise LPI gain");
    options=private_options();options.modem.pulse_shaping=false;
    check(lpi::estimate(transmission,options,10).status==lpi::Status::available,
          "inclusive -10 dB weak-signal endpoint changed");
    const auto strong=lpi::estimate(transmission,options,10.001);
    check(strong.status==lpi::Status::outside_weak_signal_model && strong.detection_seconds==0,
          "strong signal must not extrapolate weak-signal detection counts");
    check(lpi::estimate(transmission,options,-1e308).status==lpi::Status::numeric_limit,
          "numeric overflow must not masquerade as infinite hidden traffic");
    const auto huge=lpi::estimate(transmission,options,1e308);
    check(huge.status==lpi::Status::outside_weak_signal_model && std::isfinite(huge.noise_rise_db),
          "finite high C/N0 must keep advisory output finite");
    const auto tiny=lpi::estimate(transmission,options,-1000);
    check(tiny.status==lpi::Status::available && std::isfinite(tiny.detection_seconds) && tiny.noise_rise_db>0,
          "weak numerical inputs lost log-domain precision");
    for(const auto invalid:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected=false;try {(void)lpi::estimate(transmission,options,invalid);}catch(const Error&){rejected=true;}
        check(rejected,"nonfinite C/N0 was accepted");
    }
    auto invalid=transmission;invalid.total_seconds=-1;
    bool rejected=false;try {(void)lpi::estimate(invalid,options,-3);}catch(const Error&){rejected=true;}
    check(rejected,"negative burst exposure was accepted");
    auto huge_burst=transmission;huge_burst.total_seconds=std::numeric_limits<double>::max();
    // B*T90 is fixed here, while the wider band makes T90 less than a second.
    options.modem.bandwidth_hz=10000;options.modem.sample_rate=48000;options.modem.carrier_hz=12000;
    check(lpi::estimate(huge_burst,options,30).status==lpi::Status::numeric_limit,
          "unrepresentable exposure must not report a finite available estimate");
}
void framing_unchanged() {
    auto options=private_options();Message message;message.data=Bytes{'e'};
    const auto before=transfer::estimate(message,options);
    check(before.wire_bits==3,"independent e endpoint must remain 001");
    const auto short_text=lpi::estimate(before,options,-3);
    const auto raw=lpi::estimate(transfer::estimate_binary(Bytes{0,0,1},options),options,-3);
    near(short_text.burst_exposure_ratio,raw.burst_exposure_ratio,"same wire bits must give same exposure");
    const auto after=transfer::estimate(message,options);
    check(after.wire_bits==before.wire_bits && after.waveform_samples==before.waveform_samples,
          "advisory model must not alter wire bits or waveform samples");
    options.fec=FecMode::off;
    near(lpi::estimate(transfer::estimate(message,options),options,-3).burst_exposure_ratio,
         short_text.burst_exposure_ratio,"saved FEC must not inflate short-message exposure");
    message.data=Bytes(17,'e');
    const auto interval=transfer::estimate(message,options);
    check(interval.wire_bits==1216,"long path must retain 192-bit marker and 128-coded-byte interval");
    const auto longer=lpi::estimate(interval,options,-3);
    near(longer.detection_seconds,short_text.detection_seconds,"codec/FEC cannot change keyless detection time");
    check(longer.burst_exposure_ratio>1216/longer.equivalent_symbols,
          "interval exposure must include every marker and coded bit plus waveform overhead");
}
}
int main() {
    try {reference_and_scaling();eligibility_and_limits();framing_unchanged();std::cout<<"LPI estimate tests passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

#include "datapump/lpi_estimate.hpp"
#include "datapump/tuning.hpp"
#include "../src/lpi_hopping.hpp"
#include <algorithm>
#include <array>
#include <bit>
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
void close_math(long double actual,long double expected,const char* message) {
    check(std::abs(actual-expected)<=3e-9L*std::max(std::abs(expected),1e-300L),message);
}
void gamma_references() {
    using namespace lpi::detail;
    for(const auto x:{.01L,1.L,20.L,500.L}) {
        MathBudget budget;
        const auto result=gamma_logs(1,x,budget);
        check(result.has_value(),"exponential gamma evaluation failed");
        close_math(result->survival,-x,"exact exponential survival changed");
        const auto log_cdf=x>.5L?std::log1p(-std::exp(-x)):std::log(-std::expm1(-x));
        close_math(result->cdf,log_cdf,"exact exponential CDF changed");
    }
    MathBudget budget;
    const auto extreme=gamma_upper_quantile(1,std::log(1e-280L),budget);
    check(extreme.has_value(),"extreme exponential family tail failed");
    close_math(*extreme,-std::log(1e-280L),"extreme exponential quantile changed");
    const auto below_mean=gamma_upper_quantile(2,std::log(.9L),budget);
    check(below_mean&&*below_mean<2,"gamma inversion cannot assume its quantile exceeds the mean");
    const auto closed=gamma_logs(2,3,budget);
    check(closed.has_value(),"shape-two gamma evaluation failed");
    close_math(closed->survival,-3+std::log(4.L),"shape-two closed survival changed");
    // Independent 65-digit Decimal references from the integer-shape identity
    // Q(n,x)=exp(-x)*sum(k=0..n-1,x^k/k!), with 180 bisections.
    struct Reference {long double shape,tail,quantile,active_tail;};
    constexpr Reference references[]{
        {2,.001L,9.23341347645158573043L,.00184925460647338654061L},
        {25,5e-8L,61.2776832048408818694L,7.74765026526549656718e-7L},
        {900,1e-7L,1064.75109450192262546L,.00265653460500352596028L}
    };
    for(const auto& ref:references) {
        MathBudget local;
        const auto threshold=gamma_upper_quantile(ref.shape,std::log(ref.tail),local);
        check(threshold.has_value(),"reference gamma inversion failed");
        close_math(*threshold,ref.quantile,"independent gamma quantile reference changed");
        const auto null=gamma_logs(ref.shape,*threshold,local);
        const auto active=gamma_logs(ref.shape,*threshold/1.08L,local);
        check(null&&active,"reference gamma tail evaluation failed");
        close_math(std::exp(null->survival),ref.tail,"gamma null tail reference changed");
        close_math(std::exp(active->survival),ref.active_tail,"gamma active tail reference changed");
    }
    MathBudget empty{0};
    check(!gamma_logs(25,20,empty),"gamma iteration budget was ignored");
    check(!gamma_logs(maximum_gamma_shape+1,20,budget)&&!gamma_logs(0,20,budget)&&
          !gamma_upper_quantile(2,0,budget),"unsupported gamma inputs were accepted");
}
void channel_bank_probabilities() {
    using namespace lpi::detail;
    MathBudget budget;
    const auto fit=channel_cells(900,.08L,200,500,budget);
    check(fit.has_value(),"channel/dwell probability evaluation failed");
    // Independent Decimal65 finite-sum result, not a receiver Monte Carlo rate.
    close_math(-std::expm1(fit->log_miss),.739141440192579852277L,
               "globally corrected channel-bank detection reference changed");
    close_math(-std::expm1(fit->log_false_alarm_survival),.01L,
               "channel-bank family false-alarm allocation changed");
    check(-std::expm1(fit->log_false_alarm_survival)<=.01L+3e-12L,
          "channel-bank quantile spent more than the global false-alarm budget");
    long double previous=0;
    for(const auto dwells:{1u,10u,100u,500u,1000u}) {
        MathBudget local;
        const auto point=channel_cells(900,.08L,200,dwells,local);
        check(point.has_value(),"complete-dwell probability evaluation failed");
        const auto power=-std::expm1(point->log_miss);
        check(power>previous,"complete-dwell maximum-test power must increase");previous=power;
    }
    const auto bank=channel_bank(2250,.08L,lpi::Hopping{200,.4},1e6L);
    check(bank.status==lpi::ChannelBankStatus::available&&bank.dwells>1&&bank.seconds>0,
          "complete-dwell bank search failed");
    MathBudget endpoints;
    const auto accepted=channel_cells(bank.samples_per_cell,.08L,200,bank.dwells,endpoints);
    const auto preceding=channel_cells(bank.samples_per_cell,.08L,200,bank.dwells-1,endpoints);
    check(accepted&&preceding&&accepted->log_miss<=std::log(.1L)+3e-9L&&
          preceding->log_miss>std::log(.1L)-3e-9L,"bank search did not bracket 90% detection");
    check(channel_bank(.01L,.08L,lpi::Hopping{200,.4},1e6L).status==
          lpi::ChannelBankStatus::insufficient_time_bandwidth,
          "subsample dwell must be explicitly unsupported, not a zero-time detection");
    check(channel_bank(1,.000001L,lpi::Hopping{200,1e9},1e30L).status==
          lpi::ChannelBankStatus::numeric_limit,"large-shape bank limitation was hidden");
    check(channel_bank(2.5L,.001L,lpi::Hopping{200,.4},1e30L).status==
          lpi::ChannelBankStatus::numeric_limit,"dwell-count limitation was hidden");
    check(!channel_cells(0,.08L,200,1,budget)&&!channel_cells(1,0,200,1,budget),
          "empty cell or zero signal was treated as a meaningful bank fit");
}
void same_legacy_values(const lpi::Estimate& a,const lpi::Estimate& b) {
    check(a.status==b.status&&a.hypothetical_encryption==b.hypothetical_encryption,
          "optional hopping changed legacy status");
    const auto values=[](const lpi::Estimate& e) {return std::array{
        e.reference_cn0_db_hz,e.observation_bandwidth_hz,e.in_band_snr_db,e.noise_rise_db,
        e.symbol_seconds,e.detection_seconds,e.equivalent_symbols,e.additional_symbols,e.burst_exposure_ratio};};
    const auto av=values(a),bv=values(b);
    for(std::size_t i=0;i<av.size();++i)
        check(std::bit_cast<std::uint64_t>(av[i])==std::bit_cast<std::uint64_t>(bv[i]),
              "Off or one-channel hopping changed a legacy numeric bit");
}
void full_capture_hopping() {
    auto options=private_options();
    const auto transmission=transfer::estimate_binary(Bytes{0,0,1},options);
    const auto baseline=lpi::estimate(transmission,options);
    const auto off=lpi::estimate(transmission,options,std::nullopt);
    same_legacy_values(baseline,off);
    const auto one=lpi::estimate(transmission,options,lpi::Hopping{1,.4});
    same_legacy_values(baseline,one);
    check(one.hypothetical_hopping&&one.captured_signal_fraction==1&&
          one.observer_strategy==lpi::ObserverStrategy::known_band_radiometer,
          "one-channel hypothetical hopping metadata changed");
    const auto short_dwell=lpi::estimate(transmission,options,lpi::Hopping{200,.4});
    check(short_dwell.status==lpi::Status::available&&short_dwell.hypothetical_hopping&&
          short_dwell.captured_signal_fraction==1&&
          short_dwell.observer_strategy==lpi::ObserverStrategy::hopset_aggregate&&
          short_dwell.channel_bank_status==lpi::ChannelBankStatus::not_faster,
          "weak short dwells must retain the faster full-capture aggregate model");
    near(short_dwell.captured_noise_bandwidth_hz,200*baseline.observation_bandwidth_hz,
         "full capture must account for every channel's noise bandwidth");
    near(short_dwell.single_channel_detection_seconds,baseline.detection_seconds,
         "FHSS must not discard 199/200 of the captured signal energy");
    // Independent Decimal65 evaluation at B=62.5 Hz, Ts=163.84 s and K=200.
    near(short_dwell.aggregate_detection_seconds,1097155.72723892256031,
         "full-capture aggregate reference changed");
    check(short_dwell.detection_seconds>190*baseline.detection_seconds&&
          short_dwell.detection_seconds<200*baseline.detection_seconds,
          "aggregate capture variance scaling changed");
    options.modem.integration_seconds=12.8;
    const auto long_dwell=lpi::estimate(transmission,options,lpi::Hopping{200,100});
    check(long_dwell.status==lpi::Status::available&&
          long_dwell.channel_bank_status==lpi::ChannelBankStatus::available&&
          long_dwell.observer_strategy==lpi::ObserverStrategy::dwell_channel_maximum&&
          long_dwell.channel_bank_dwells==1&&long_dwell.channel_bank_samples_per_cell>0&&
          long_dwell.detection_seconds>0&&long_dwell.detection_seconds<100&&
          long_dwell.detection_seconds<long_dwell.aggregate_detection_seconds,
          "long dwells must let a channelized observer avoid a universal 200x penalty");
    const auto rho=std::pow(10.L,static_cast<long double>(long_dwell.in_band_snr_db)/10);
    lpi::detail::MathBudget budget;
    const auto fit=lpi::detail::channel_cells(long_dwell.channel_bank_samples_per_cell,rho,200,1,budget);
    const auto previous=lpi::detail::channel_cells(long_dwell.channel_bank_samples_per_cell-1,rho,200,1,budget);
    check(fit&&previous&&fit->log_miss<=std::log(.1L)+3e-9L&&
          previous->log_miss>std::log(.1L)-3e-9L,
          "first-dwell whole-sample search did not bracket the detection target");
    for(const auto hopping:{lpi::Hopping{0,.4},lpi::Hopping{200,0},
                            lpi::Hopping{200,std::numeric_limits<double>::infinity()}}) {
        bool rejected=false;try{(void)lpi::estimate(transmission,options,hopping);}catch(const Error&){rejected=true;}
        check(rejected,"invalid hypothetical hopping geometry was accepted");
    }
    check(options.modem.dsss_factor==1&&transmission.wire_bits==3,
          "hypothetical hopping changed modem geometry or framing");
}
}
int main() {
    try {reference_and_scaling();eligibility_and_limits();hypothetical_scenarios();framing_unchanged();
         gamma_references();channel_bank_probabilities();full_capture_hopping();
         std::cout<<"LPI estimate tests passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

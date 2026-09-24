#include "../src/gui/pattern_space.hpp"
#include "datapump/tuning.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_pulse.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>

using namespace datapump;
using gui::inspection::PatternSpace;
using gui::inspection::inspect_pattern_space;
namespace {
void check(bool value, const char* message) { if (!value) throw Error(message); }
void near(double value, double expected, const char* message, double tolerance = 1e-11) {
    check(std::abs(value - expected) <= tolerance * std::max(1., std::abs(expected)), message);
}
void exact_transmitter_templates() {
    modem::Config config;config.bandwidth_hz=1100;
    for(const auto factor:{3U,4U,8U,16U,128U}) {
        config.spreading_factor=factor;
        for(unsigned mode=0;mode<4;++mode) {
            config.scramble=(mode&1U)!=0;config.dsss=(mode&2U)!=0;
            config.spreading_seed[7]=71;config.dsss_seed[3]=19;
            const auto model=inspect_pattern_space(config,40);
            check(model.codewords.size()==2 && model.coefficients.size()==2,
                  "inspection must retain both independent bit patterns");
            check(std::accumulate(model.chip_weights.begin(),model.chip_weights.end(),std::uint64_t{})==model.symbol_samples,
                  "all illustrated samples must have exactly one chip weight");
            auto rectangular=config;rectangular.pulse_shaping=false;
            check(inspect_pattern_space(rectangular,40).codewords==model.codewords,
                  "pulse shaping must not change the logical input-chip design preview");
            for(unsigned bit=0;bit<2;++bit) {
                modem::PatternTransmitter transmitter(Bytes{static_cast<std::uint8_t>(bit)},config,0,0,false);
                std::vector<std::complex<double>> analytic(transmitter.total_samples());
                std::vector<std::complex<double>> chips;
                check(transmitter.read_analytic(analytic,{},[&](auto value){chips.push_back(value);})==analytic.size() && transmitter.finished(),
                      "fixture must emit one complete independent pattern");
                check(chips.size()==model.code.size(),"pulse tails must not add cells to the input-chip preview");
                for(std::size_t chip=0;chip<chips.size();++chip)
                    check(std::abs(chips[chip]-model.chip_value(bit,chip))<1e-12,
                          "illustrated I/Q cells must match the transmitter's logical input chips before pulse shaping");
                if(!modem::pattern_pulse_enabled(config))for(std::size_t sample=0;sample<analytic.size();++sample) {
                    const auto chip=sample/model.chip_samples;
                    const auto oscillator=std::polar(1.,2*std::numbers::pi*config.carrier_hz*static_cast<double>(sample)/config.sample_rate);
                    check(std::abs(analytic[sample]-model.chip_value(bit,chip)*oscillator)<1e-10,
                          "rectangular legacy PCM must still equal its input-chip preview after carrier modulation");
                }
            }
            double integrated=0;
            for(std::size_t chip=0;chip<model.code.size();++chip)
                integrated+=std::norm(model.chip_value(0,chip)-model.chip_value(1,chip))*static_cast<double>(model.chip_weights[chip])/config.sample_rate;
            near(model.squared_distance(0,1),integrated,"pattern distance must retain actual chip amplitudes and partial-sample weights");
            near(model.noise_squared_distance(0,1),integrated*10000/modem::nominal_signal_power,
                 "statistical distance must use the illustrated pattern energy");
        }
    }
}
void durations_and_modes() {
    modem::Config config;config.spreading_factor=3;config.integration_seconds=77.25/config.sample_rate;
    auto model=inspect_pattern_space(config,0);
    check(model.symbol_samples==78 && model.chip_samples==10 &&
          model.chip_weights==std::vector<std::uint64_t>({10,10,10,10,10,10,10,8}),
          "pattern preview must retain absolute chips including the partial final chip");
    near(model.processing_gain_db,10*std::log10(7.8),"integration gain includes fractional chips");
    near(model.symbol_esn0_db-model.chip_esn0_db,model.processing_gain_db,"chip and symbol SNR share the same C/N0");
    config.integration_seconds=4.25/config.sample_rate;
    model=inspect_pattern_space(config,0);
    check(model.chip_weights==std::vector<std::uint64_t>{5} && !model.code_selective && !model.timing_selective,
          "a single illustrated chip cannot establish chip sequence timing");
    config.integration_seconds=0;config.spreading_factor=128;config.scramble=true;
    model=inspect_pattern_space(config,0);
    modem::PatternCode generator(config);const auto amplitude=std::sqrt(2*modem::nominal_signal_power);
    std::complex<double> dot{};double energy=0,shifted_energy=0;
    for(std::size_t chip=0;chip<model.code.size();++chip) {
        const auto weight=static_cast<double>(model.chip_weights[chip])/model.symbol_samples;
        const auto value=model.chip_value(0,chip),shifted=amplitude*generator.value(chip+1,0,.5);
        dot+=weight*value*std::conj(shifted);energy+=weight*std::norm(value);shifted_energy+=weight*std::norm(shifted);
    }
    const auto correlation=std::abs(dot)/std::sqrt(energy*shifted_energy);
    near(model.one_chip_shift.correlation,correlation,"timing correlation must normalize the energy of both variable-amplitude patterns");
    near(model.one_chip_shift.residual_fraction,1-correlation*correlation,"timing residual must fit arbitrary complex gain");
    check(model.code_selective && model.timing_selective,"private pattern preview lost timing evidence");
}
void bounded_and_public_illustration() {
    modem::Config config; config.spreading_factor = 16384;
    config.integration_seconds = 1e8; config.scramble = true; config.dsss = true;
    config.spreading_seed[1] = 71; config.dsss_seed[11] = 34;
    const auto private_model = inspect_pattern_space(config, -60);
    const auto public_model = inspect_pattern_space(config, -60, true);
    check(!private_model.representative_keyed && public_model.representative_keyed,
          "public illustrations must be explicitly distinguished from the caller's real configured seeds");
    check(public_model.code != private_model.code && public_model.code.size() == 16384 && public_model.coefficients.size() == 2,
          "very slow large patterns must retain only one bounded code period and the small symbol alphabet");
    config.spreading_seed.fill(197); config.dsss_seed.fill(21);config.stream_epoch=987654321;
    const auto another = inspect_pattern_space(config, -60, true);
    check(public_model.codewords == another.codewords, "representative public code must not depend on private configuration seeds or epoch");
    config.integration_seconds = 0;
    const auto weak_chip = inspect_pattern_space(config, 10, true);
    check(weak_chip.chip_esn0_db < 0 && weak_chip.symbol_esn0_db > 10,
          "matched full-pattern evidence must be modeled above noise even when individual phase/amplitude chips are below noise");
    config.scramble = false; config.dsss = false;
    check(!inspect_pattern_space(config, -60, true).representative_keyed, "plain fixed code must not be mislabeled as a keyed illustration");
    config.sample_rate = 120000000; config.bandwidth_hz = 30000000; config.carrier_hz = 22500000;
    config.integration_seconds = 0;
    check(std::isfinite(inspect_pattern_space(config, 120).noise_squared_distance(0, 1)), "30 MHz model must remain bounded and finite");
    bool rejected = false;
    try { (void)inspect_pattern_space(config, std::numeric_limits<double>::quiet_NaN()); } catch (const Error&) { rejected = true; }
    check(rejected, "nonfinite C/N0 must be rejected before display");
}
void binary_pattern_preview() {
    for(const auto mode:{tuning::PatternMode::auto_pattern,tuning::PatternMode::auto_keystream,tuning::PatternMode::auto_tone}) {
        auto config=tuning::resolve(1200,40,mode,true).config;
        const auto model=inspect_pattern_space(config,40,true);
        check(model.bounded_pattern_preview && model.coefficients.size()==2 && model.codewords.size()==2,
              "binary pattern inspection must preserve two independent codeword rows");
        check(model.squared_distance(0,1)>0 && model.squared_distance(0,0)==0,
              "binary pattern rows must have measured nonzero distance despite equal scalar coefficients");
        modem::PatternCode expected(config,config.stream_epoch);
        if(!config.scramble)for(std::size_t i=0;i<model.code.size();++i)
            check(std::abs(model.chip_value(1,i)-std::sqrt(2*modem::nominal_signal_power)*expected.value(i,1,.5))<1e-10,
                  "pattern preview must render the actual selected codeword");
        config.integration_seconds=3600;
        const auto long_model=inspect_pattern_space(config,0,true);
        check(long_model.truncated && long_model.code.size()==16384 && long_model.symbol_samples<long_model.full_symbol_samples,
              "hour-long pattern inspection must retain only a bounded explicitly labeled prefix");
    }
}
}
int main() {
    try {
        exact_transmitter_templates(); durations_and_modes(); bounded_and_public_illustration();binary_pattern_preview();
        std::cout << "Static pattern space tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

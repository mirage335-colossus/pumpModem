#include "datapump/audio_output.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace datapump::audio;
using datapump::background_noise::Config;
using datapump::background_noise::Mode;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}

std::int16_t original_pack(float x, double gain) {
    return gain == 1.0
        ? static_cast<std::int16_t>(std::clamp(x,-1.f,1.f)*32767)
        : static_cast<std::int16_t>(std::clamp(static_cast<double>(x)*gain,-1.0,1.0)*32767);
}

void off_is_exact() {
    const std::vector<float> pcm{-1.f,-.75f,-.125f,0.f,.125f,.75f,1.f};
    for (const auto gain : {1., .75}) {
        PlaybackOutput output(48000, {}, gain);
        for (const auto mode : {ChannelMode::left_mono,ChannelMode::right_mono,ChannelMode::stereo}) {
            const unsigned channels = mode == ChannelMode::stereo ? 2 : 1;
            std::vector<std::int16_t> actual(pcm.size()*channels);
            output.pack_s16(pcm, actual, channels, mode);
            for (std::size_t i=0;i<pcm.size();++i) {
                const auto sample=original_pack(pcm[i],gain);
                const auto expected_left=channels==2 && mode==ChannelMode::right_mono ? 0 : sample;
                const auto expected_right=mode==ChannelMode::left_mono ? 0 : sample;
                check(actual[i*channels]==expected_left,"off output changed left S16 packing");
                if(channels==2)check(actual[i*channels+1]==expected_right,"off output changed right S16 packing");
            }
        }
        const auto report=output.report();
        check(!report.enabled && report.active_samples==0 && report.expected_variance==0,
              "off report is not empty");
        check(output.provider_gain()==gain,"off provider gain changed");
    }
    std::vector<float> floating=pcm;
    PlaybackOutput float_output(48000, {}, .75);
    const auto original=floating;
    float_output.mix_float(floating);
    check(floating==original,"off float output changed samples");
}

void s16_sub_lsb_and_prefix() {
    Config level{Mode::EffectiveBits,16};
    PlaybackOutput output(48000,level);
    std::vector<float> input(140000,0.f);
    constexpr std::size_t active_begin=10000;
    std::vector<std::int16_t> codes(input.size());
    output.pack_s16(input,codes,1,ChannelMode::left_mono,active_begin);
    for(std::size_t i=0;i<active_begin;++i)check(codes[i]==0,"leading silence received noise");
    const auto receipt=output.report();
    check(receipt.enabled && receipt.sample_rate_hz==48000 && receipt.active_samples==input.size()-active_begin,
          "S16 output lifecycle counters differ");
    check(receipt.sample_format==AudioOutputSampleFormat::S16,"S16 output format is missing");
    check(receipt.peak>0 && receipt.measured_variance>0,"16-bit-equivalent noise vanished below the S16 LSB");
    const auto relative=receipt.measured_variance/receipt.expected_variance;
    check(std::abs(relative-1)<.10,"S16 sub-LSB noise has the wrong calibrated power");
    check(output.provider_gain()==1,"enabled S16 provider gain was not consumed by the output stage");
}

void cached_s16_preserves_codes_and_adds_noise() {
    std::vector<std::int16_t> samples{-32768, -12345, -1, 0, 1, 12345, 32767};
    PlaybackOutput off(48000);
    const auto original = samples;
    off.mix_s16(samples);
    check(samples == original, "off unity S16 stage changed cached PCM codes");

    std::vector<std::int16_t> quiet(140000, 0);
    constexpr std::size_t active_begin = 512;
    PlaybackOutput enabled(48000, Config{Mode::EffectiveBits,16});
    enabled.mix_s16(quiet, active_begin);
    for (std::size_t i=0;i<active_begin;++i)
        check(quiet[i] == 0, "cached S16 inactive prefix received noise");
    const auto receipt = enabled.report();
    check(receipt.active_samples == quiet.size()-active_begin && receipt.peak > 0 &&
          receipt.measured_variance > 0,
          "cached S16 stage failed to add noise to active zero samples");
}

void enabled_zero_noise_preserves_legacy_quantizers_and_gains_preroll() {
    Config zero{Mode::RelativeWatts,0,1,0.5,40};
    const std::vector<float> input{-.625f,-.125f,0.f,.125f,.625f};
    std::vector<std::int16_t> packed(input.size());
    PlaybackOutput integer_output(48000,zero,.73);
    integer_output.pack_s16(input,packed,1,ChannelMode::left_mono);
    for(std::size_t i=0;i<input.size();++i)
        check(packed[i]==original_pack(input[i],.73),
              "enabled S16 noise stage changed the legacy non-unity gain quantizer");

    std::vector<float> floating{.25f,-.125f,.5f};
    PlaybackOutput float_output(48000,zero,1.5);
    float_output.mix_float(floating,2);
    check(floating[0]==.375f && floating[1]==-.1875f && floating[2]==.75f,
          "enabled float stage failed to apply gain to preroll or active samples");
    check(float_output.report().active_samples==1,
          "float preroll was counted as active transmitted noise");
}

void float_power_and_headroom() {
    Config level{Mode::SuppressionDb,40};
    constexpr std::size_t n=200000;
    std::vector<float> mono(n,0.f);
    PlaybackOutput output(96000,level,.5);
    output.mix_float(mono,2000);
    for(std::size_t i=0;i<2000;++i)check(mono[i]==0,"inactive float prefix got noise");
    const auto receipt=output.report();
    check(receipt.enabled && receipt.sample_rate_hz==96000 &&
          receipt.sample_format==AudioOutputSampleFormat::Float32 && receipt.active_samples==n-2000,
          "float output lifecycle counters differ");
    check(std::abs(receipt.measured_variance/receipt.expected_variance-1)<.025,
          "float full-rate noise power differs from its selected level");
    check(output.provider_gain()==1,"enabled float provider gain was not consumed by the output stage");

    std::vector<float> fullscale(4,.9999f);
    PlaybackOutput unsafe(48000,Config{Mode::EffectiveBits,10});
    rejects([&]{unsafe.mix_float(fullscale);},"float headroom overflow was clipped or accepted");
    std::vector<float> integer_fullscale{1.f};
    std::vector<std::int16_t> packed(1);
    PlaybackOutput unsafe_integer(48000,Config{Mode::EffectiveBits,10});
    rejects([&]{unsafe_integer.pack_s16(integer_fullscale,packed,1,ChannelMode::left_mono);},
            "S16 headroom overflow was clipped or accepted");
}

void contract_errors() {
    rejects([]{PlaybackOutput out(0);},"zero final sample rate accepted");
    rejects([]{PlaybackOutput out(48000,{},2.0);},"invalid output gain accepted");
    rejects([]{PlaybackOutput out(48000,Config{Mode::RelativeWatts,1,1,1.01,40});},
            "out-of-range relative reference power accepted by playback output");
    PlaybackOutput out(48000);
    std::vector<float> input(2);
    std::vector<std::int16_t> codes(3);
    rejects([&]{out.pack_s16(input,codes,2,ChannelMode::stereo);},"misaligned S16 output accepted");
    codes.resize(4);
    rejects([&]{out.pack_s16(input,codes,2,ChannelMode::stereo,3);},"invalid active boundary accepted");
    codes = {111,111,111,111,2345,6789};
    out.pack_s16(input,codes,2,ChannelMode::stereo);
    check(codes[4]==2345 && codes[5]==6789,
          "S16 packer overwrote trailing capacity in a reusable output buffer");
}
} // namespace

int main() {
    try { off_is_exact(); s16_sub_lsb_and_prefix(); cached_s16_preserves_codes_and_adds_noise();
          enabled_zero_noise_preserves_legacy_quantizers_and_gains_preroll();
          float_power_and_headroom(); contract_errors(); }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    std::cout<<"shared audio output checks passed\n";
}

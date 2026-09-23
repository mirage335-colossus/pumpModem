#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "../src/pattern_carrier.hpp"
#include "datapump/transfer.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/crypto.hpp"
#include "datapump/symbol_schedule.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

using namespace datapump;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw Error(message);
}
template<class Work> void rejects(Work work, const char* message) {
    try { work(); } catch (const Error&) { return; }
    throw Error(message);
}
modem::Config config() {
    modem::Config result;
    result.pattern_symbols = true; result.constellation_bits = 1;
    result.spreading_factor = 128;
    for (std::size_t i = 0; i < result.spreading_seed.size(); ++i) {
        result.spreading_seed[i] = static_cast<std::uint8_t>(3 * i + 7);
        result.dsss_seed[i] = static_cast<std::uint8_t>(5 * i + 11);
    }
    return result;
}
std::uint32_t phase_word(std::complex<double> value) {
    auto phase=std::arg(value);
    if(phase<0)phase+=2*std::numbers::pi;
    return static_cast<std::uint32_t>(std::llround(phase/(2*std::numbers::pi)*4294967296.-.5));
}
void seek_and_domains() {
    auto c = config(); c.scramble = true; c.dsss = true;
    constexpr std::uint64_t epoch = 1789312671;
    modem::PatternCode code(c, epoch);
    Crypto pattern(c.spreading_seed), dsss(c.dsss_seed);
    const std::uint64_t positions[]{0,1,63,64,127,128,4095,4096,16383,16384,
        1ULL << 40, (1ULL << 40) + 127, (std::numeric_limits<std::uint64_t>::max()-7)/8};
    for (auto chip : positions) {
        const auto address=modem::symbol_stream_address(epoch,c.stream_phase_samples,
            chip/code.chips_per_symbol(),code.symbol_samples(),c.sample_rate);
        const auto local=modem::symbol_stream_chip(address,code.chips_per_symbol(),chip%code.chips_per_symbol());
        const auto a=pattern.stream(StreamPurpose::Scrambler,address.epoch,8*local,8);
        const auto b=dsss.stream(StreamPurpose::Dsss,address.epoch,8*local,8);
        std::uint32_t expected=0;
        for(unsigned i=4;i<8;++i)expected=(expected<<8)|(a[i]^b[i]);
        check(phase_word(code.value(chip,0))==expected,
              "seeked noise must mix each symbol's purpose/epoch byte streams before mapping");
    }
    modem::PatternCode sequential(c,epoch);
    for(std::uint64_t chip=0;chip<145;++chip)
        check(sequential.value(chip,0)==code.value(chip,0),"cache-boundary seek changed the noise");
    modem::PatternCode next_epoch(c, epoch + 1);
    bool changed = false;
    for (std::uint64_t i = 0; i < 128; ++i) changed |= code.value(i, 0) != next_epoch.value(i, 0);
    check(changed, "changing the clock epoch must select another private pattern");
    // Absolute chip coordinates no longer consume one unbounded byte stream:
    // the per-second position remains small even after very long transmissions.
    check(std::isfinite(std::abs(code.value(std::numeric_limits<std::uint64_t>::max()/8+1,0))),
          "a large cumulative chip count must remain seekable after epoch rollovers");
}
void symbol_epoch_schedule() {
    auto c=config();c.scramble=true;c.dsss=true;c.integration_seconds=.3;
    constexpr std::uint64_t epoch=1800000000;
    modem::PatternCode code(c,epoch);
    const auto n=code.symbol_samples(),chips=code.chips_per_symbol();
    const std::array<std::uint64_t,8> expected_epochs{0,0,0,0,1,1,1,2};
    const std::array<std::uint64_t,8> expected_ordinals{0,1,2,3,0,1,2,0};
    for(std::uint64_t j=0;j<expected_epochs.size();++j) {
        const auto address=modem::symbol_stream_address(epoch,0,j,n,c.sample_rate);
        check(address.epoch==epoch+expected_epochs[j] && address.ordinal==expected_ordinals[j],
              "only a new symbol may advance its stream epoch, retaining same-second counter positions");
        auto rebased=c;rebased.stream_phase_samples=address.sample_in_second;
        modem::PatternCode later(rebased,address.epoch);
        for(auto local:std::array<std::uint64_t,3>{0,1,chips-1})
            check(later.value(local,0)==code.value(j*chips+local,0),
                  "a later symbol must be reproducible from its own start second and phase");
    }
    const auto later=modem::symbol_stream_address(epoch,0,4,n,c.sample_rate);
    auto changed=c;changed.stream_phase_samples=later.sample_in_second;
    modem::PatternCode shifted(changed,epoch);
    const auto original=code.shaped_value(chips,0,static_cast<double>(code.chip_samples()));
    code.set_stream_phase_samples(later.sample_in_second);
    check(code.shaped_value(chips,0,static_cast<double>(code.chip_samples()))==
          shifted.shaped_value(chips,0,static_cast<double>(code.chip_samples())),
          "changing a timing hypothesis must invalidate shaped chip mappings");
    code.set_stream_phase_samples(0);
    check(code.shaped_value(chips,0,static_cast<double>(code.chip_samples()))==original,
          "revisiting a timing hypothesis must recreate its exact template");
    rejects([&]{code.set_stream_phase_samples(c.sample_rate);},
            "a timing hypothesis phase must remain within one second");

    c.integration_seconds=4*3600+.5;
    modem::PatternCode long_code(c,epoch);
    const auto long_chips=long_code.chips_per_symbol();
    const auto long_n=long_code.symbol_samples();
    check(modem::symbol_stream_address(epoch,0,1,long_n,c.sample_rate).epoch==epoch+14400 &&
          modem::symbol_stream_address(epoch,0,2,long_n,c.sample_rate).epoch==epoch+28801,
          "hours-long symbols must skip intervening seconds without changing their own epoch");
    Crypto pattern(c.spreading_seed),dsss(c.dsss_seed);
    const auto verify=[&](std::uint64_t chip,std::uint64_t time,std::uint64_t position) {
        const auto a=pattern.stream(StreamPurpose::Scrambler,time,position*8,8);
        const auto b=dsss.stream(StreamPurpose::Dsss,time,position*8,8);
        std::uint32_t expected=0;
        for(unsigned i=4;i<8;++i)expected=(expected<<8)|(a[i]^b[i]);
        check(phase_word(long_code.value(chip,0))==expected,
              "a complete long pattern must retain its start epoch through its final chip");
    };
    verify(long_chips-1,epoch,long_chips-1);
    verify(long_chips,epoch+14400,0);
    verify(0,epoch,0); // Reuse byte offset zero while switching back to an old epoch.
    verify(long_chips,epoch+14400,0);
    check(long_code.working_bytes()<8192,
          "epoch rollover and random seeks must retain fixed-size keystream caches");
}
void symbol_schedule_integer_bounds() {
    const auto max=std::numeric_limits<std::uint64_t>::max();
    // The product is wider than 64 bits, while the resulting seconds still fit.
    const auto large=modem::symbol_stream_address(0,0,max,3,3);
    check(large.epoch==max && large.sample_in_second==0 && large.ordinal==0,
          "symbol timing arithmetic must not overflow an intermediate sample product");
    const auto rate=std::numeric_limits<std::uint32_t>::max();
    const auto fractional=modem::symbol_stream_address(0,rate-1,rate-1,rate-1,rate);
    check(fractional.epoch==rate-1 && fractional.sample_in_second==0,
          "maximum sample-rate remainders must fit exact integer timing arithmetic");
    rejects([&]{(void)modem::symbol_stream_address(max,0,1,3,3);},
            "symbol epoch addition must reject overflow");
    rejects([&]{(void)modem::symbol_stream_address(0,0,max,4,3);},
            "symbol time multiplication must reject a true epoch overflow");
    rejects([&]{(void)modem::symbol_stream_address(0,3,0,1,3);},
            "symbol start phases outside their epoch must be rejected");
    rejects([&]{(void)modem::symbol_stream_chip({0,max,0},2,0);},
            "symbol stream chip multiplication must reject overflow");
}
void alphabet_and_repetition() {
    auto c = config(); modem::PatternCode public_code(c);
    const auto length = public_code.chips_per_symbol();
    std::complex<double> normalized_overlap{};
    for (std::uint64_t i = 0; i < length; ++i) {
        const auto zero = public_code.value(i, 0), one = public_code.value(i, 1);
        normalized_overlap += std::conj(zero) * one / std::norm(zero);
        check(zero == public_code.value(length + i, 0) && one == public_code.value(length + i, 1),
              "public symbols must be identifiable without their transmission index");
    }
    check(std::abs(normalized_overlap) < 1e-12,
          "complete binary mask periods must preserve balanced internal transitions");
    c.scramble = true; modem::PatternCode private_code(c, 73);
    bool adjacent_changed = false, legacy_period_changed = false;
    for (std::uint64_t i = 0; i < length; ++i) {
        adjacent_changed |= private_code.value(i, 0) != private_code.value(length + i, 0);
        legacy_period_changed |= private_code.value(i, 0) != private_code.value(16384 + i, 0);
    }
    check(adjacent_changed && legacy_period_changed,
          "private noise must neither reset per symbol nor repeat a finite template");
    for (unsigned chips : {2U,3U,4U,8U}) {
        c.spreading_factor = chips; modem::PatternCode short_code(c, 73);
        std::vector<int> relative(chips);
        for (unsigned i = 0; i < chips; ++i)
            relative[i]=std::real(short_code.value(i,0)/short_code.value(i,1))>0?1:-1;
        check(std::find(relative.begin(), relative.end(), 1) != relative.end() &&
              std::find(relative.begin(), relative.end(), -1) != relative.end(),
              "binary rows must differ in internal transitions, not only absolute phase");
        if (chips >= 3)
            check(relative[0] * relative[1] != relative[1] * relative[2],
                  "the binary distinction must not be an alternating carrier alias");
    }
    c.spreading_factor = 1; modem::PatternCode degenerate(c);
    check(degenerate.value(0, 0) == degenerate.value(0, 1),
          "one-chip binary template must expose its unavoidable unknown-phase ambiguity");
}
void public_waveform_uses_amplitude_and_phase() {
    auto c=config();c.spreading_factor=4096;
    modem::PatternCode code(c,73),other_epoch(c,74);
    const auto count=code.chips_per_symbol();
    std::complex<double> mean{},square{};double energy=0,energy_square=0;
    for(std::uint64_t chip=0;chip<count;++chip) {
        const auto value=code.value(chip,0);
        check(value==other_epoch.value(chip,0) && value==code.value(chip+count,0),
              "public circular templates must be independent of epoch and repeat each symbol");
        mean+=value;square+=value*value;
        const auto power=std::norm(value);energy+=power;energy_square+=power*power;
    }
    check(std::abs(mean)/count<.06 && std::abs(square)/energy<.06,
          "public pattern chips must occupy both quadratures, not a two-point binary line");
    check(std::abs(energy/count-1)<.08 && energy_square/count-std::pow(energy/count,2)>.4,
          "public pattern chips must vary amplitude as well as phase");
}
void carrier_phase_integer_positions() {
    struct Example {std::uint64_t cursor;std::uint32_t rate;double carrier,cycles;};
    // Expected values use exact rational arithmetic on the binary64 carrier,
    // integer cursor and rate, followed by one final rounding to double. They
    // do not use the production reduction or a large floating-point angle.
    for(const auto& example:std::array{
        Example{0,64,16,0},Example{1,64,16,.25},Example{64,64,16,0},
        Example{691200017,48000,1500,-.46875},
        Example{281474976710655ULL,192000,.1,.37013282063802083},
        Example{18446744073709551615ULL,48000,1234.5,-.40171875},
        Example{18446744073709551615ULL,44100,1234.567,.41254950113378686},
        Example{18446744073709551615ULL,120000000,59999999.25,-.18469759375}}) {
        const auto cycles=modem::detail::pattern_carrier_cycles(example.cursor,example.rate,example.carrier);
        check(std::abs(std::remainder(cycles-example.cycles,1.))<1e-15,
              "carrier phase lost a fractional cycle at a long integer sample position");
    }
}
void exact_pcm_and_chunks() {
    auto c = config(); c.scramble = true; c.dsss = true;
    c.data_key.emplace(c.spreading_seed);
    c.bandwidth_hz = 1100; c.integration_seconds = .071; // Partial final chip.
    const Bytes bits{0,1,0};
    const auto first_chip=3*modem::pattern_chips_per_symbol(c);
    modem::PatternTransmitter whole(bits, c, 91, first_chip), chunked(bits, c, 91, first_chip);
    check(whole.total_samples() == modem::training_sample_count(c)+bits.size() * modem::symbol_sample_count(c)+
          2*modem::pattern_pulse_padding_samples(c)+modem::suppression_sample_count(c),
          "three bits must retain exactly three payload symbols plus settling and filter tails");
    const auto count = static_cast<std::size_t>(whole.total_samples());
    std::vector<std::complex<double>> a(count), b(count);
    check(whole.read_analytic(a) == count && whole.finished(), "complete analytic capture must end exactly");
    for (std::size_t offset = 0; offset < b.size();) {
        const auto n = std::min<std::size_t>(17, b.size() - offset);
        check(chunked.read_analytic(std::span(b).subspan(offset, n)) == n,
              "short PCM reads must preserve progress");
        offset += n;
    }
    for (std::size_t i = 0; i < count; ++i) {
        check(std::abs(a[i] - b[i]) < 1e-11, "chunk boundaries must not alter transmitted phase or chip positions");
        check(std::norm(a[i])<1,"private payload and settling noise must remain inside PCM peak headroom");
    }
    std::array<std::complex<double>, 79> preview{};
    whole.preview_last_analytic(preview);
    check(whole.samples_emitted() == count, "preview must not advance or rewind the transmitter");
    for (std::size_t i = 0; i < preview.size(); ++i)
        check(std::abs(preview[i] - a[count - preview.size() + i]) < 1e-11,
              "preview must reconstruct the actual final partial-chip waveform");
    std::array<float, 31> after{};
    check(whole.read(after) == 0, "finished waveform must not append synthetic samples");
    modem::PatternTransmitter real(bits, c, 91, first_chip);
    std::vector<float> pcm(count); real.read(pcm);
    for (std::size_t i = 0; i < count; ++i)
        check(std::abs(pcm[i] - a[i].real()) < 1e-7, "PCM must be the real projection of the same analytic waveform");
    modem::PatternTransmitter mixed(bits,c,91,first_chip);
    std::size_t observed=0;
    const auto observer=[&](auto) { ++observed; };
    std::array<float,17> real_chunk{};
    std::array<std::complex<double>,31> analytic_chunk{};
    bool use_real=true;
    while(!mixed.finished()) {
        const auto before=static_cast<std::size_t>(mixed.samples_emitted());
        check(mixed.read({}, {}, observer)==0 && mixed.samples_emitted()==before,
              "empty reads must not advance the pulse train or emit chip observations");
        if(use_real) {
            const auto read=mixed.read(real_chunk,{},observer);
            for(std::size_t i=0;i<read;++i)
                check(std::abs(real_chunk[i]-a[before+i].real())<1e-7,"mixed real and analytic reads must share one physical sample cursor");
        } else {
            const auto read=mixed.read_analytic(analytic_chunk,{},observer);
            for(std::size_t i=0;i<read;++i)
                check(std::abs(analytic_chunk[i]-a[before+i])<1e-11,"real reads must not discard analytic filter state");
        }
        mixed.preview_last_analytic(analytic_chunk);
        const auto end=static_cast<std::size_t>(mixed.samples_emitted());
        for(std::size_t i=0;i<analytic_chunk.size();++i) {
            const auto distance=analytic_chunk.size()-i;
            const auto expected=end>=distance?a[end-distance]:std::complex<double>{};
            check(std::abs(analytic_chunk[i]-expected)<1e-11,
                  "repeated previews across prefix, symbol and tail boundaries must reconstruct without changing stream state");
        }
        use_real=!use_real;
    }
    check(observed==bits.size()*modem::pattern_chips_per_symbol(c),
          "mixed reads and previews must report each logical payload chip exactly once");
    check(std::abs(real.bit_rate() - c.sample_rate / static_cast<double>(modem::symbol_sample_count(c))) < 1e-12,
          "pattern throughput must count one meaningful bit per symbol");
}
void streaming_and_modem_integration() {
    auto c = config(); c.scramble = true; c.stream_epoch = 54321;
    c.integration_seconds = .029;
    const Bytes bits{1,0,1};
    modem::StreamingTransmitter wrapped(modem::RawBits{bits}, c);
    modem::PatternTransmitter direct(bits, c, c.stream_epoch);
    check(wrapped.total_samples() == direct.total_samples(),
          "streaming raw wrapper must preserve exact bit count and hardware settling");
    rejects([&] { (void)wrapped.next_symbol(); },
            "pattern mode must not provide oracle-despread symbol observations");
    std::array<std::complex<double>, 37> blank;
    blank.fill({9, 3}); wrapped.preview_last_analytic(blank);
    check(std::all_of(blank.begin(), blank.end(), [](auto value) { return value == std::complex<double>{}; }),
          "preview before transmission must contain only zero history");
    std::vector<std::complex<double>> observed(static_cast<std::size_t>(wrapped.total_samples())), expected(observed.size());
    check(wrapped.read_analytic(observed) == observed.size() && wrapped.finished(),
          "streaming wrapper must report complete sample progress");
    direct.read_analytic(expected);
    for (std::size_t i = 0; i < observed.size(); ++i)
        check(std::abs(observed[i] - expected[i]) < 1e-12, "streaming wrapper must preserve epoch-derived waveform");
    wrapped.preview_last_analytic(blank);
    for (std::size_t i = 0; i < blank.size(); ++i)
        check(std::abs(blank[i] - observed[observed.size() - blank.size() + i]) < 1e-11,
              "streaming wrapper preview must retain actual pattern samples");
    const Bytes packed{0xa5};
    modem::StreamingTransmitter packed_tx(packed, c);
    const auto packed_samples = modem::training_sample_count(c)+8 * modem::symbol_sample_count(c)+2*modem::pattern_pulse_padding_samples(c)+modem::suppression_sample_count(c);
    check(packed_tx.total_samples() == packed_samples && modem::waveform_sample_count(packed.size(), c) == packed_samples,
          "packed bytes must expand to eight meaningful bits after hardware settling");
    check(modem::preamble(c).empty() && modem::memory_supported(packed.size(), 0, c),
          "pattern modem estimation must accept a zero-length preamble");
    const auto status = modem::modulate_status(bits, c);
    check(status.size() == modem::training_sample_count(c)+bits.size() * modem::symbol_sample_count(c)+2*modem::pattern_pulse_padding_samples(c)+modem::suppression_sample_count(c) && modem::detect_status(status, bits, c) > .999999,
          "status helper must use the same exact unframed pattern waveform");
    rejects([&] { (void)modem::demodulate(status, c, {}); },
            "legacy known-training demodulator must reject pattern mode explicitly");
}
void complete_symbols_require_aligned_starts() {
    auto c=config();c.integration_seconds=.3;
    for(const bool private_pattern:{false,true})for(const bool shaped:{false,true}) {
        c.scramble=private_pattern;c.pulse_shaping=shaped;
        const auto chips=modem::pattern_chips_per_symbol(c);
        rejects([&]{modem::PatternTransmitter invalid({0},c,73,chips+1,false);},
                "complete symbols must reject chip fragments that would cross a logical epoch boundary");
        modem::PatternTransmitter aligned({0},c,73,3*chips,false);
        check(aligned.total_samples()==modem::symbol_sample_count(c)+2*modem::pattern_pulse_padding_samples(c),
              "an aligned nonzero stream seek must retain one complete symbol");
        modem::PatternCode fragment(c,73);
        check(std::isfinite(std::abs(fragment.value(chips+1,0))),
              "PatternCode must retain arbitrary chip fragment access");
    }
}
void tones_and_bounded_state() {
    auto c = config(); c.spreading_mode = modem::SpreadingMode::tone;
    modem::PatternCode tone(c);
    const auto quarter = tone.value(1, 1);
    check(std::abs(quarter - std::complex<double>{0,1}) < 1e-12 &&
          std::abs(tone.value(1, 0) - std::conj(quarter)) < 1e-12,
          "tone templates must use opposite quarter-turn chip increments");
    modem::PatternTransmitter tx({1}, c,0,0,false);
    std::array<std::complex<double>, 19> samples{}; tx.read_analytic(samples);
    const auto angular = 2 * std::numbers::pi * c.carrier_hz / c.sample_rate +
                         std::numbers::pi / (2 * static_cast<double>(tone.chip_samples()));
    for (std::size_t i = 1; i < samples.size(); ++i)
        check(std::abs(samples[i] / samples[i - 1] - std::polar(1., angular)) < 1e-12,
              "tone PCM must use the advertised carrier frequency offset");
    modem::PatternTransmitter tone_prefix({1},c);
    tone_prefix.read_analytic(samples);
    const auto refresh=tone.chip_samples();
    for(std::size_t i=1;i<samples.size();++i) {
        const auto change=std::abs(samples[i]-samples[i-1]*
            std::polar(1.,2*std::numbers::pi*c.carrier_hz/c.sample_rate));
        check(i%refresh==0?change>1e-6:change<1e-12,
              "tone settling must refresh independent I/Q noise at the chip cadence");
    }
    c = config(); c.scramble = true; c.integration_seconds = 4 * 3600;
    modem::PatternCode long_code(c, 73);
    modem::PatternTransmitter long_tx({0,1,0}, c, 73);
    check(long_code.working_bytes() < 8192 && long_tx.working_bytes() < 16384,
          "multi-hour symbols must not allocate waveform or complete keystream history");
    check(long_tx.total_samples() == 12ULL * 3600 * c.sample_rate+2*modem::pattern_pulse_padding_samples(c)+modem::suppression_sample_count(c),
          "multi-hour symbol durations must remain exact with bounded state");
    c.spreading_factor=16384;
    transfer::Options options;options.modem=c;options.dsp_workspace_bytes=384*1024;
    options.key=Crypto(c.spreading_seed);
    const auto estimate=transfer::estimate_binary(Bytes{0,0,1},options);
    check(estimate.memory_supported && !estimate.batch_memory_supported &&
          estimate.waveform_samples==12ULL*3600*c.sample_rate+2*modem::pattern_pulse_padding_samples(c)+modem::suppression_sample_count(c),
          "three multi-hour pattern bits require bounded transmitter state, not retained chips or PCM");
    modem::StreamingTransmitter wrapped(modem::RawBits{{0,0,1}},c,options.dsp_workspace_bytes/4);
    check(wrapped.working_bytes()<128*1024,"streaming wrapper memory must remain independent of integration duration");
    rejects([&] { modem::PatternTransmitter invalid({0,2}, c); }, "invalid raw bit must be rejected");
    rejects([&] { modem::PatternTransmitter invalid({}, c); }, "empty transmission must be rejected");
    rejects([&] { modem::PatternTransmitter invalid({0}, c, 0, std::numeric_limits<std::uint64_t>::max()); },
            "transmission must reject stream address exhaustion");
    rejects([&] { (void)long_code.value(0, 0, 1.); }, "out-of-range chip fractions must be rejected");
    std::stop_source stop; stop.request_stop();
    rejects([&] { long_tx.read_analytic(samples, stop.get_token()); }, "waveform generation must honor cancellation");
    check(long_tx.samples_emitted() == 0, "cancelled generation must not advance time");
}
void rounded_hardware_duration() {
    auto c=config();
    for(const auto [seconds,expected]:std::array<std::pair<double,unsigned>,8>{{{.1,20},{1,2},{2,1},{3,1},{4,1},{4.01,0},{20,0},{3600,0}}}) {
        c.integration_seconds=seconds;
        check(modem::training_sample_count(c)==expected*modem::symbol_sample_count(c),
              "hardware settling must round two seconds to the nearest whole payload symbol");
    }
    c.integration_seconds=.1;
    modem::PatternTransmitter framed({0,0,1},c,73),bare({0,0,1},c,73,0,false);
    check(framed.total_samples()-bare.total_samples()==modem::training_sample_count(c)+modem::suppression_sample_count(c),
          "optional settling must not change the exact payload length");
}
void hardware_noise_keystreams() {
    constexpr std::uint64_t epoch=1800000000;
    const auto prefix=[](const modem::Config& c,std::uint64_t time,const Bytes& bits=Bytes{0,0,1}) {
        modem::PatternTransmitter tx(bits,c,time);
        std::vector<float> samples(static_cast<std::size_t>(modem::training_sample_count(c)));
        tx.read(samples);return samples;
    };
    auto c=config();c.scramble=true;c.dsss=true;
    c.data_key.emplace(Bytes(32,0x37));
    const auto both=prefix(c,epoch);
    auto changed=c;changed.spreading_seed[0]^=0x80;
    check(prefix(changed,epoch)!=both,"Scrambler must affect settling when DSSS is also enabled");
    changed=c;changed.dsss_seed[0]^=0x80;
    check(prefix(changed,epoch)!=both,"DSSS must affect settling when Scrambler is also enabled");
    changed=c;changed.data_key.emplace(Bytes(32,0xb7));
    check(prefix(changed,epoch)!=both,"hardware Data encryption key must affect the prefix");
    check(prefix(c,epoch+1)!=both,"hardware streams must advance with the clock epoch");
    check(prefix(c,epoch,Bytes{1,1,0,1})==both,"settling cannot encode payload bits or length");
    for(bool scrambler:{false,true}) {
        auto disabled=c;disabled.scramble=!scrambler;disabled.dsss=scrambler;
        auto ignored=disabled;
        (scrambler?ignored.spreading_seed:ignored.dsss_seed)[0]^=0x80;
        check(prefix(disabled,epoch)==prefix(ignored,epoch),"a disabled spreading layer must not affect settling");
    }
    const auto first_chip=19*modem::pattern_chips_per_symbol(c);
    modem::PatternTransmitter original({0,0,1},c,epoch,first_chip),other({0,0,1},changed,epoch,first_chip),bare({0,0,1},c,epoch,first_chip,false);
    std::vector<std::complex<double>> a(static_cast<std::size_t>(original.total_samples())),b(a.size()),payload(static_cast<std::size_t>(bare.total_samples()));
    original.read_analytic(a);other.read_analytic(b);bare.read_analytic(payload);
    const auto offset=static_cast<std::size_t>(modem::training_sample_count(c));
    std::complex<double> mean{},quadrature{};double power=0;std::size_t observations=0;
    for(std::size_t i=2*modem::pattern_pulse_padding_samples(c);i<offset;++i) {
        const auto value=a[i]*std::polar(1.,-2*std::numbers::pi*i*c.carrier_hz/c.sample_rate)/
            std::sqrt(2*modem::nominal_signal_power);
        mean+=value;quadrature+=value*value;power+=std::norm(value);++observations;
    }
    check(std::abs(mean)/observations<.06 && std::abs(quadrature)/observations<.08 &&
          std::abs(power/observations-1)<.1,
          "hardware noise must fill both quadratures with the payload's mean power, not a binary line");
    const auto settled=offset+2*modem::pattern_pulse_padding_samples(c);
    check(std::equal(a.begin()+static_cast<std::ptrdiff_t>(settled),a.end()-static_cast<std::ptrdiff_t>(modem::suppression_sample_count(c)),b.begin()+static_cast<std::ptrdiff_t>(settled)),
          "hardware Data encryption key must not change payload beyond the filter overlap");
    const auto rotation=std::polar(1.,2*std::numbers::pi*static_cast<double>(offset)*c.carrier_hz/c.sample_rate);
    for(std::size_t i=2*modem::pattern_pulse_padding_samples(c);i<payload.size();++i)
        check(std::abs(a[offset+i]-payload[i]*rotation)<1e-8,"settling must not consume or reset payload stream positions");
    transfer::Options options;options.modem=config();
    options.key.emplace(Bytes(32,0x19));
    auto first=transfer::seeded_config(options,epoch);first.scramble=false;first.dsss=false;
    options.key.emplace(Bytes(32,0xa7));
    auto second=transfer::seeded_config(options,epoch);second.scramble=false;second.dsss=false;
    check(first.data_key && second.data_key && !first.scramble && !first.dsss,
          "data-only encryption must supply the selected Data key to the waveform");
    check(prefix(first,epoch)!=prefix(second,epoch) && prefix(first,epoch)!=prefix(options.modem,epoch),
          "data-only encrypted settling must depend on the selected key rather than the public waveform");
}
void hardware_data_byte_encryption() {
    // Recover the phase word from the transmitted I/Q waveform, then verify
    // that encryption changes those bytes by exactly the selected key's Data mask.
    // Positions straddle cache boundaries; neither source has spreading enabled.
    constexpr std::uint64_t epoch=1800000000;
    auto plain=config();plain.pulse_shaping=false; // Expose unchanged input chip bytes directly.
    transfer::Options options;options.modem=plain;options.key.emplace(plain.spreading_seed);
    auto encrypted=transfer::seeded_config(options,epoch);encrypted.scramble=false;encrypted.dsss=false;
    const auto mask=options.key->stream(StreamPurpose::Data,epoch,0,513*8,StreamDomain::Preamble);
    modem::PatternTransmitter public_tx({0},plain,epoch),encrypted_tx({0},encrypted,epoch);
    const auto refresh=modem::pattern_chip_samples(plain);
    std::vector<std::complex<double>> a(513*refresh),b(a.size());
    public_tx.read_analytic(a);encrypted_tx.read_analytic(b);
    const auto phase_word=[&](std::complex<double> value,std::size_t sample) {
        value*=std::polar(1.,-2*std::numbers::pi*sample*plain.carrier_hz/plain.sample_rate);
        auto angle=std::arg(value);
        if(angle<0)angle+=2*std::numbers::pi;
        return static_cast<std::uint32_t>(std::llround(angle/(2*std::numbers::pi)*4294967296.-.5));
    };
    for(const auto position:{0U,1U,63U,64U,65U,127U,128U,511U,512U}) {
        std::uint32_t word=0;
        for(unsigned j=4;j<8;++j)word=(word<<8)|mask[position*8+j];
        const auto sample=position*refresh;
        check((phase_word(a[sample],sample)^phase_word(b[sample],sample))==word,
              "preamble bytes must be XOR-encrypted by the Data stream before noise mapping");
    }
    for(unsigned layers=1;layers<=3;++layers) {
        auto spread=encrypted;spread.scramble=(layers&1U)!=0;spread.dsss=(layers&2U)!=0;
        modem::PatternTransmitter tx({0},spread,epoch);
        std::vector<std::complex<double>> observed(b.size());tx.read_analytic(observed);
        const Crypto pattern(spread.spreading_seed),dsss(spread.dsss_seed);
        const auto chips=observed.size()/refresh;
        const auto pattern_mask=pattern.stream(StreamPurpose::Scrambler,epoch,0,chips*8,StreamDomain::Preamble);
        const auto dsss_mask=dsss.stream(StreamPurpose::Dsss,epoch,0,chips*8,StreamDomain::Preamble);
        for(std::size_t chip=0;chip<chips;++chip) {
            std::uint32_t word=0;
            for(unsigned j=4;j<8;++j)word=(word<<8)|
                ((spread.scramble?pattern_mask[chip*8+j]:0)^(spread.dsss?dsss_mask[chip*8+j]:0));
            check((phase_word(observed[chip*refresh],chip*refresh)^phase_word(b[chip*refresh],chip*refresh))==word,
                  "preamble must mix every enabled private stream before amplitude and phase mapping");
        }
    }
    const auto used=encrypted_tx.working_bytes();
    encrypted.memory_limit=used-1;
    rejects([&] { modem::PatternTransmitter too_small({0},encrypted,epoch); },
            "hardware Data cache must count toward the transmitter memory ceiling");
}
void private_waveform_has_no_fixed_squared_carrier() {
    constexpr std::uint64_t epoch=1800000000;
    for(unsigned layers=1;layers<=3;++layers) {
        auto c=config();c.scramble=(layers&1U)!=0;c.dsss=(layers&2U)!=0;
        modem::PatternCode code(c,epoch);
        std::complex<double> mean{},square{};double energy=0,energy_square=0;
        constexpr unsigned count=4096;
        for(unsigned chip=0;chip<count;++chip) {
            const auto value=code.value(chip,chip%2);
            mean+=value;square+=value*value;
            const auto power=std::norm(value);energy+=power;energy_square+=power*power;
        }
        check(std::abs(mean)/count<.06 && std::abs(square)/energy<.06,
              "private chips must occupy both quadratures without a coherent squared carrier");
        check(std::abs(energy/count-1)<.08 && energy_square/count-std::pow(energy/count,2)>.4,
              "private chip amplitude must vary with bounded, normalized mean power");
        auto other=c;
        if(c.scramble)other.spreading_seed[0]^=0x80;else other.dsss_seed[0]^=0x80;
        modem::PatternTransmitter first({0,0,1},c,epoch,0,false),second({1,0,0},other,epoch,0,false);
        std::vector<float> a(static_cast<std::size_t>(first.total_samples())),b(a.size());
        first.read(a);second.read(b);
        double squared_difference=0;
        for(std::size_t i=0;i<a.size();++i)squared_difference+=std::abs(double(a[i])*a[i]-double(b[i])*b[i]);
        check(squared_difference/a.size()>.05,
              "changing private keys must change squared PCM, not only carrier signs");
    }
    auto c=config();c.scramble=true;c.dsss=true;
    modem::PatternCode original(c,epoch);
    const auto reference=original.value(64,0);
    auto changed=c;changed.spreading_seed[0]^=0x80;
    modem::PatternCode scrambler_changed(changed,epoch);
    changed=c;changed.dsss_seed[0]^=0x80;
    modem::PatternCode dsss_changed(changed,epoch);
    check(reference!=scrambler_changed.value(64,0) && reference!=dsss_changed.value(64,0),
          "both enabled private streams must independently affect amplitude and phase");
}
void short_private_patterns_preserve_noise_and_addressing() {
    constexpr std::uint64_t epoch=1800000000;
    constexpr std::size_t symbol_count=1024;
    for(unsigned layers=1;layers<=3;++layers)for(unsigned chips:{3U,4U,6U,8U,12U,16U,32U}) {
        auto c=config();c.scramble=(layers&1U)!=0;c.dsss=(layers&2U)!=0;
        c.sample_rate=48000;c.bandwidth_hz=12000;c.carrier_hz=9000;c.spreading_factor=chips;
        auto longer=c;longer.spreading_factor=128;
        modem::PatternCode short_code(c,epoch),long_code(longer,epoch);
        for(const auto position:{0ULL,1ULL,63ULL,64ULL,127ULL,128ULL})
            check(short_code.value(position,0)==long_code.value(position,0),
                  "shortening private patterns must preserve within-second chip addressing before rollover");

        Bytes bits(symbol_count),complement(symbol_count);
        for(std::size_t symbol=0;symbol<symbol_count;++symbol) {
            bits[symbol]=static_cast<std::uint8_t>((symbol/3+symbol/7)%2);
            complement[symbol]=static_cast<std::uint8_t>(1-bits[symbol]);
        }
        modem::PatternTransmitter tx(bits,c,epoch,0,false),other_bits(complement,c,epoch,0,false);
        std::vector<std::complex<double>> samples(static_cast<std::size_t>(tx.total_samples())),other(samples.size()),logical,other_logical;
        tx.read_analytic(samples,{},[&](auto value){logical.push_back(value);});
        other_bits.read_analytic(other,{},[&](auto value){other_logical.push_back(value);});
        const auto chip_samples=modem::pattern_chip_samples(c);
        const auto count=symbol_count*chips;
        std::vector<std::complex<double>> values(count),position_mean(chips),position_square(chips);
        std::complex<double> mean{},square{},repetition{};
        double energy=0,power_square=0;
        for(std::size_t chip=0;chip<count;++chip) {
            const auto sample=modem::pattern_pulse_padding_samples(c)+chip*chip_samples;
            const auto value=logical[chip]/std::sqrt(2*modem::nominal_signal_power);
            values[chip]=value;mean+=value;square+=value*value;
            position_mean[chip%chips]+=value;position_square[chip%chips]+=value*value;
            const auto power=std::norm(value);energy+=power;power_square+=power*power;
            check(std::norm(samples[sample])<1 &&
                  std::abs(std::norm(logical[chip])-std::norm(other_logical[chip]))<1e-12,
                  "short private patterns must retain PCM headroom and a payload-independent input chip envelope");
            if(chip>=chips)repetition+=value*std::conj(values[chip-chips]);
        }
        check(std::abs(mean)/count<.06 && std::abs(square)/energy<.06 && std::abs(repetition)/energy<.06,
              "short private transmissions must not restore a fixed carrier, squared carrier or repeated symbol row");
        check(std::abs(energy/count-1)<.08 && power_square/count-std::pow(energy/count,2)>.4,
              "short private transmissions must preserve variable amplitude and normalized mean power");
        for(unsigned position=0;position<chips;++position)
            check(std::abs(position_mean[position])/symbol_count<.15 &&
                  std::abs(position_square[position])/symbol_count<.15,
                  "short private symbol boundaries must not create phase or squared-phase repetition");

        auto changed=c;
        if(c.scramble)changed.spreading_seed[0]^=0x80;else changed.dsss_seed[0]^=0x80;
        modem::PatternCode other_key(changed,epoch),other_epoch(c,epoch+1);
        double key_squared_difference=0,epoch_squared_difference=0;
        for(std::size_t chip=0;chip<128;++chip) {
            const auto value=short_code.value(chip,0);
            key_squared_difference+=std::norm(value*value-std::pow(other_key.value(chip,0),2));
            epoch_squared_difference+=std::norm(value*value-std::pow(other_epoch.value(chip,0),2));
        }
        check(key_squared_difference/128>.5 && epoch_squared_difference/128>.5,
              "short patterns must retain private key and epoch dependence after squaring");
    }
}
void shaped_bandwidth_power_and_constellation() {
    auto c=config();c.scramble=true;c.dsss=true;
    Bytes bits(64);
    for(std::size_t i=0;i<bits.size();++i)bits[i]=static_cast<std::uint8_t>((i/3+i/7)%2);
    modem::PatternTransmitter tx(bits,c,1800000000,0,false);
    modem::PatternCode code(c,1800000000);
    const auto padding=modem::pattern_pulse_padding_samples(c);
    check(padding==80 && tx.total_samples()==bits.size()*code.symbol_samples()+2*padding,
          "600-chip/s shaping must add only the 26.7ms finite burst tails");
    auto legacy=c;legacy.pulse_shaping=false;
    check(tx.bit_rate()==modem::PatternTransmitter(bits,legacy,1800000000,0,false).bit_rate(),
          "pulse shaping must preserve meaningful payload throughput");
    std::vector<std::complex<double>> samples(static_cast<std::size_t>(tx.total_samples()));
    std::size_t observed=0;
    tx.read_analytic(samples,{},[&](auto value) {
        const auto symbol=observed/code.chips_per_symbol();
        check(std::abs(value-std::sqrt(2*modem::nominal_signal_power)*code.value(observed,bits[symbol]))<1e-12,
              "pulse shaping must preserve the keyed logical chip constellation and addresses");
        ++observed;
    });
    check(observed==bits.size()*code.chips_per_symbol(),"filter tails must not create extra observed chips");
    double energy=0;std::complex<double> mean{},square{};
    for(std::size_t i=0;i<samples.size();++i) {
        check(std::abs(samples[i])<=modem::pattern_pcm_radius_limit+1e-10,
              "shaped PCM must remain bounded without a fixed power backoff");
        samples[i]*=std::polar(1.,-2*std::numbers::pi*static_cast<double>(i)*c.carrier_hz/c.sample_rate);
        energy+=std::norm(samples[i]);mean+=samples[i];square+=samples[i]*samples[i];
    }
    check(std::abs(energy/(samples.size()-2*padding)/(2*modem::nominal_signal_power)-1)<.06,
          "shaped waveform must retain mean transmitted power within the sample uncertainty");
    check(std::abs(mean)/samples.size()<.035 && std::abs(square)/energy<.06,
          "shaped private noise must not introduce a coherent carrier or squared carrier");
    // Independent windowed energy checks on the transmitted samples, including
    // the crest limiter, rather than merely checking ideal filter coefficients.
    const auto spectral_power=[&](double frequency) {
        constexpr std::size_t window=2048;
        double result=0;
        const auto step=std::polar(1.,-2*std::numbers::pi*frequency/c.sample_rate);
        for(std::size_t first=0;first+window<=samples.size();first+=window/2) {
            std::complex<double> sum{},oscillator{1,0};
            for(std::size_t i=0;i<window;++i) {
                const auto weight=.5-.5*std::cos(2*std::numbers::pi*static_cast<double>(i)/(window-1));
                sum+=weight*samples[first+i]*oscillator;oscillator*=step;
            }
            result+=std::norm(sum);
        }
        return result;
    };
    double inband=0;
    for(double frequency:{-200.,-100.,0.,100.,200.})inband+=spectral_power(frequency)/5;
    for(double frequency:{-1000.,-700.,-550.,-450.,-400.,400.,450.,550.,700.,1000.})
        check(spectral_power(frequency)<inband*.0025,
              "actual shaped private PCM must suppress sidelobes beyond the roughly 750Hz band by 26dB");
}
void shaped_coordinate_and_duration_bounds() {
    auto c=config();modem::PatternCode code(c);
    for(const auto invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        rejects([&]{code.shaped_value(0,0,invalid);},"nonfinite shaped timing coordinates must be rejected");
    rejects([&]{code.shaped_value(0,2,0);},"shaped templates must reject nonbinary bits");
    rejects([&]{code.shaped_value(std::numeric_limits<std::uint64_t>::max(),0,0);},
            "filter lookahead must reject wrapping an absolute chip address");
    const auto pad=static_cast<double>(modem::pattern_pulse_padding_samples(c));
    check(code.shaped_value(0,0,-pad-1)==std::complex<double>{} &&
          code.shaped_value(0,0,static_cast<double>(code.symbol_samples())+pad+1)==std::complex<double>{},
          "an isolated symbol must vanish outside its finite pulse support");
    rejects([&]{modem::pattern_pulse_sum(0,10,0,[](auto){return std::complex<double>{};});},
            "pulse summation must reject a zero chip duration before integer division");
    const auto near_max=std::numeric_limits<std::uint64_t>::max()-2047;
    (void)modem::pattern_pulse_sum(static_cast<double>(near_max)-4096,near_max,128,[&](auto chip) {
        check(chip<near_max/128+(near_max%128!=0),"rounded long-sequence coordinates must not access a chip past the sequence");
        return std::complex<double>{};
    });
    c.sample_rate=64;c.bandwidth_hz=1;c.carrier_hz=16;
    c.integration_seconds=std::nextafter(std::ldexp(1.,58),0.);
    rejects([&]{modem::PatternTransmitter overflow({0},c,0,0,false);},
            "finite filter tails must not wrap a previously valid 64-bit payload duration");
    c.pulse_shaping=false;
    modem::PatternTransmitter valid({0},c,0,0,false);
    check(valid.total_samples()==near_max,"the duration overflow fixture must fit before adding filter tails");
    c.sample_rate=6000;c.bandwidth_hz=1200;c.carrier_hz=1500;
    c.integration_seconds=std::nextafter(std::ldexp(1.,64)/c.sample_rate,0.);
    modem::PatternTransmitter without_noise({0},c,0,0,false);
    check(without_noise.total_samples()>std::numeric_limits<std::uint64_t>::max()-modem::suppression_sample_count(c),
          "suppression overflow fixture must leave less than three seconds in the sample counter");
    rejects([&]{modem::PatternTransmitter tail_overflow({0},c);},
            "suppression noise must reject a 64-bit sample duration overflow");
}
}
int main() {
    try {
        seek_and_domains(); symbol_epoch_schedule(); symbol_schedule_integer_bounds();
        alphabet_and_repetition(); public_waveform_uses_amplitude_and_phase();
        carrier_phase_integer_positions();exact_pcm_and_chunks(); tones_and_bounded_state();
        streaming_and_modem_integration();complete_symbols_require_aligned_starts();
        rounded_hardware_duration();hardware_noise_keystreams();
        hardware_data_byte_encryption();
        private_waveform_has_no_fixed_squared_carrier();
        short_private_patterns_preserve_noise_and_addressing();
        shaped_bandwidth_power_and_constellation();
        shaped_coordinate_and_duration_bounds();
        std::cout << "Pattern code and binary waveform tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

#include "datapump/pattern_code.hpp"
#include "datapump/transfer.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/crypto.hpp"

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
int stream_sign(const Crypto& key, StreamPurpose purpose, std::uint64_t epoch,
                std::uint64_t chip) {
    const auto byte = key.stream(purpose, epoch, chip / 8, 1).front();
    return ((byte >> (chip % 8)) & 1U) ? -1 : 1;
}
void seek_and_domains() {
    auto c = config(); c.scramble = true; c.dsss = true;
    constexpr std::uint64_t epoch = 1789312671;
    modem::PatternCode code(c, epoch);
    Crypto pattern(c.spreading_seed), dsss(c.dsss_seed);
    const std::uint64_t positions[]{0,1,127,128,4095,4096,16383,16384,
        1ULL << 40, (1ULL << 40) + 127, std::numeric_limits<std::uint64_t>::max()};
    for (auto chip : positions) {
        const auto expected = stream_sign(pattern, StreamPurpose::Scrambler, epoch, chip) *
                              stream_sign(dsss, StreamPurpose::Dsss, epoch, chip);
        check(code.sign(chip, 0) == expected,
              "seeked chip must use the absolute purpose/epoch stream address");
    }
    std::array<int, 19> crossing{};
    code.fill(4087, 0, crossing);
    for (std::size_t i = 0; i < crossing.size(); ++i)
        check(crossing[i] == code.sign(4087 + i, 0), "cache-boundary bulk seek changed the code");
    modem::PatternCode next_epoch(c, epoch + 1);
    bool changed = false;
    for (std::uint64_t i = 0; i < 128; ++i) changed |= code.sign(i, 0) != next_epoch.sign(i, 0);
    check(changed, "changing the clock epoch must select another private pattern");
    rejects([&] { code.fill(std::numeric_limits<std::uint64_t>::max(), 0, crossing); },
            "bulk stream access must reject address wraparound");
}
void alphabet_and_repetition() {
    auto c = config(); modem::PatternCode public_code(c);
    const auto length = public_code.chips_per_symbol();
    int dot = 0;
    for (std::uint64_t i = 0; i < length; ++i) {
        const auto zero = public_code.sign(i, 0), one = public_code.sign(i, 1);
        dot += zero * one;
        check(zero == public_code.sign(length + i, 0),
              "public symbols must be identifiable without their transmission index");
    }
    check(dot == 0, "complete binary mask periods must have zero complex-line overlap");
    c.scramble = true; modem::PatternCode private_code(c, 73);
    bool adjacent_changed = false, legacy_period_changed = false;
    for (std::uint64_t i = 0; i < length; ++i) {
        adjacent_changed |= private_code.sign(i, 0) != private_code.sign(length + i, 0);
        legacy_period_changed |= private_code.sign(i, 0) != private_code.sign(16384 + i, 0);
    }
    check(adjacent_changed && legacy_period_changed,
          "private signs must neither reset per symbol nor repeat the legacy template");
    for (unsigned chips : {2U,3U,4U,8U}) {
        c.spreading_factor = chips; modem::PatternCode short_code(c, 73);
        std::vector<int> relative(chips);
        for (unsigned i = 0; i < chips; ++i) relative[i] = short_code.sign(i, 0) * short_code.sign(i, 1);
        check(std::find(relative.begin(), relative.end(), 1) != relative.end() &&
              std::find(relative.begin(), relative.end(), -1) != relative.end(),
              "binary rows must differ in internal transitions, not only absolute phase");
        if (chips >= 3)
            check(relative[0] * relative[1] != relative[1] * relative[2],
                  "the binary distinction must not be an alternating carrier alias");
    }
    c.spreading_factor = 1; modem::PatternCode degenerate(c);
    check(degenerate.sign(0, 0) == degenerate.sign(0, 1),
          "one-chip binary template must expose its unavoidable unknown-phase ambiguity");
}
void exact_pcm_and_chunks() {
    auto c = config(); c.scramble = true; c.dsss = true;
    c.data_key.emplace(c.spreading_seed);
    c.bandwidth_hz = 1100; c.integration_seconds = .071; // Partial final chip.
    const Bytes bits{0,1,0};
    modem::PatternTransmitter whole(bits, c, 91, 3), chunked(bits, c, 91, 3);
    check(whole.total_samples() == modem::training_sample_count(c)+bits.size() * modem::symbol_sample_count(c),
          "three bits must contain exactly three payload symbols after hardware settling");
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
        if(i>=modem::training_sample_count(c))
            check(std::abs(std::norm(a[i]) - 2 * modem::nominal_signal_power) < 1e-11,
                  "payload binary patterns must retain constant transmitted amplitude");
        else check(std::norm(a[i])<1,"hardware noise must remain inside PCM peak headroom");
    }
    std::array<std::complex<double>, 79> preview{};
    whole.preview_last_analytic(preview);
    check(whole.samples_emitted() == count, "preview must not advance or rewind the transmitter");
    for (std::size_t i = 0; i < preview.size(); ++i)
        check(std::abs(preview[i] - a[count - preview.size() + i]) < 1e-11,
              "preview must reconstruct the actual final partial-chip waveform");
    std::array<float, 31> after{};
    check(whole.read(after) == 0, "finished waveform must not append synthetic samples");
    modem::PatternTransmitter real(bits, c, 91, 3);
    std::vector<float> pcm(count); real.read(pcm);
    for (std::size_t i = 0; i < count; ++i)
        check(std::abs(pcm[i] - a[i].real()) < 1e-7, "PCM must be the real projection of the same analytic waveform");
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
    const auto packed_samples = modem::training_sample_count(c)+8 * modem::symbol_sample_count(c);
    check(packed_tx.total_samples() == packed_samples && modem::waveform_sample_count(packed.size(), c) == packed_samples,
          "packed bytes must expand to eight meaningful bits after hardware settling");
    check(modem::preamble(c).empty() && modem::memory_supported(packed.size(), 0, c),
          "pattern modem estimation must accept a zero-length preamble");
    const auto status = modem::modulate_status(bits, c);
    check(status.size() == modem::training_sample_count(c)+bits.size() * modem::symbol_sample_count(c) && modem::detect_status(status, bits, c) > .999999,
          "status helper must use the same exact unframed pattern waveform");
    rejects([&] { (void)modem::demodulate(status, c, {}); },
            "legacy known-training demodulator must reject pattern mode explicitly");
}
void tones_and_bounded_state() {
    auto c = config(); c.spreading_mode = modem::SpreadingMode::tone;
    modem::PatternCode tone(c);
    const auto quarter = tone.value(1, 1);
    check(std::abs(quarter - std::complex<double>{0,1}) < 1e-12 &&
          std::abs(tone.value(1, 0) - std::conj(quarter)) < 1e-12,
          "tone templates must use opposite quarter-turn chip increments");
    rejects([&] { (void)tone.sign(0, 0); }, "real sign API must not silently approximate a complex tone");
    modem::PatternTransmitter tx({1}, c,0,0,false);
    std::array<std::complex<double>, 19> samples{}; tx.read_analytic(samples);
    const auto angular = 2 * std::numbers::pi * c.carrier_hz / c.sample_rate +
                         std::numbers::pi / (2 * static_cast<double>(tone.chip_samples()));
    for (std::size_t i = 1; i < samples.size(); ++i)
        check(std::abs(samples[i] / samples[i - 1] - std::polar(1., angular)) < 1e-12,
              "tone PCM must use the advertised carrier frequency offset");
    modem::PatternTransmitter tone_prefix({1},c);
    tone_prefix.read_analytic(samples);
    const auto refresh=std::max<std::uint64_t>(1,tone.chip_samples()/2);
    for(std::size_t i=1;i<samples.size();++i) {
        const auto change=std::abs(samples[i]-samples[i-1]*
            std::polar(1.,2*std::numbers::pi*c.carrier_hz/c.sample_rate));
        check(i%refresh==0?change>1e-6:change<1e-12,
              "tone settling must refresh independent I/Q noise at twice the chip cadence");
    }
    c = config(); c.scramble = true; c.integration_seconds = 4 * 3600;
    modem::PatternCode long_code(c, 73);
    modem::PatternTransmitter long_tx({0,1,0}, c, 73);
    check(long_code.working_bytes() < 8192 && long_tx.working_bytes() < 16384,
          "multi-hour symbols must not allocate waveform or complete keystream history");
    check(long_tx.total_samples() == 12ULL * 3600 * c.sample_rate,
          "multi-hour symbol durations must remain exact with bounded state");
    c.spreading_factor=16384;
    transfer::Options options;options.modem=c;options.dsp_workspace_bytes=384*1024;
    options.key=Crypto(c.spreading_seed);
    const auto estimate=transfer::estimate_binary(Bytes{0,0,1},options);
    check(estimate.memory_supported && !estimate.batch_memory_supported &&
          estimate.waveform_samples==12ULL*3600*c.sample_rate,
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
    for(const auto [seconds,expected]:std::array<std::pair<double,unsigned>,7>{{{.1,50},{1,5},{6,1},{10,1},{10.01,0},{20,0},{3600,0}}}) {
        c.integration_seconds=seconds;
        check(modem::training_sample_count(c)==expected*modem::symbol_sample_count(c),
              "hardware settling must round five seconds to the nearest whole payload symbol");
    }
    c.integration_seconds=.1;
    modem::PatternTransmitter framed({0,0,1},c,73),bare({0,0,1},c,73,0,false);
    check(framed.total_samples()-bare.total_samples()==modem::training_sample_count(c),
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
    modem::PatternTransmitter original({0,0,1},c,epoch,19),other({0,0,1},changed,epoch,19),bare({0,0,1},c,epoch,19,false);
    std::vector<std::complex<double>> a(static_cast<std::size_t>(original.total_samples())),b(a.size()),payload(static_cast<std::size_t>(bare.total_samples()));
    original.read_analytic(a);other.read_analytic(b);bare.read_analytic(payload);
    const auto offset=static_cast<std::size_t>(modem::training_sample_count(c));
    std::complex<double> mean{},quadrature{};double power=0;std::size_t observations=0;
    for(std::size_t i=0;i<offset;i+=modem::pattern_chip_samples(c)/2) {
        const auto value=a[i]*std::polar(1.,-2*std::numbers::pi*i*c.carrier_hz/c.sample_rate)/
            std::sqrt(2*modem::nominal_signal_power);
        mean+=value;quadrature+=value*value;power+=std::norm(value);++observations;
    }
    check(std::abs(mean)/observations<.06 && std::abs(quadrature)/observations<.08 &&
          std::abs(power/observations-1)<.1,
          "hardware noise must fill both quadratures with the payload's mean power, not a binary line");
    check(std::equal(a.begin()+static_cast<std::ptrdiff_t>(offset),a.end(),b.begin()+static_cast<std::ptrdiff_t>(offset)),
          "hardware Data encryption key must not change the following payload");
    const auto rotation=std::polar(1.,2*std::numbers::pi*static_cast<double>(offset)*c.carrier_hz/c.sample_rate);
    for(std::size_t i=0;i<payload.size();++i)
        check(std::abs(a[offset+i]-payload[i]*rotation)<1e-8,"settling must not consume or reset payload stream positions");
    transfer::Options options;options.modem=config();
    options.key.emplace(Bytes(32,0x19));
    const auto first=transfer::seeded_config(options,epoch);
    options.key.emplace(Bytes(32,0xa7));
    const auto second=transfer::seeded_config(options,epoch);
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
    auto plain=config();
    transfer::Options options;options.modem=plain;options.key.emplace(plain.spreading_seed);
    auto encrypted=transfer::seeded_config(options,epoch);
    const auto mask=options.key->stream(StreamPurpose::Data,epoch,0,513*8,StreamDomain::Preamble);
    modem::PatternTransmitter public_tx({0},plain,epoch),encrypted_tx({0},encrypted,epoch);
    const auto refresh=std::max<std::uint64_t>(1,modem::pattern_chip_samples(plain)/2);
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
        const auto chips=observed.size()/modem::pattern_chip_samples(spread)+1;
        const auto pattern_mask=pattern.stream(StreamPurpose::Scrambler,epoch,0,(chips+7)/8,StreamDomain::Preamble);
        const auto dsss_mask=dsss.stream(StreamPurpose::Dsss,epoch,0,(chips+7)/8,StreamDomain::Preamble);
        for(std::size_t i=0;i<observed.size();++i) {
            const auto chip=i/modem::pattern_chip_samples(spread);
            unsigned bit=0;
            if(spread.scramble)bit^=(pattern_mask[chip/8]>>(chip%8))&1U;
            if(spread.dsss)bit^=(dsss_mask[chip/8]>>(chip%8))&1U;
            check(std::abs(observed[i]-(bit?-b[i]:b[i]))<1e-11,
                  "preamble spreading must use existing payload keys with only the preamble CTR pad");
        }
    }
    const auto used=encrypted_tx.working_bytes();
    encrypted.memory_limit=used-1;
    rejects([&] { modem::PatternTransmitter too_small({0},encrypted,epoch); },
            "hardware Data cache must count toward the transmitter memory ceiling");
}
}
int main() {
    try {
        seek_and_domains(); alphabet_and_repetition(); exact_pcm_and_chunks(); tones_and_bounded_state();
        streaming_and_modem_integration();rounded_hardware_duration();hardware_noise_keystreams();
        hardware_data_byte_encryption();
        std::cout << "Pattern code and binary waveform tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

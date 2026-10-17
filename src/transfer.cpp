#include "datapump/attachment.hpp"
#include "datapump/compression.hpp"
#include "datapump/transfer.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/symbol_burst.hpp"
#include "datapump/channel.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/receiver_strategy.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace datapump::transfer {
struct RecoverySource {
    Options options;
    std::size_t source_limit=0;
};
namespace {
Options effective_options(const Options& input) {
    auto result=input;
    if(result.modem.symbol_burst.enabled() || (result.modem.scheduled_hopping && result.modem.scheduled_hopping->enabled()))
        result.modem.data_key=result.key; // One immutable schedule key before masking and timing.
    if(result.modem.spreading_mode==modem::SpreadingMode::tone) {
        result.key.reset();result.modem.data_key.reset();
        result.modem.scramble=false;result.modem.dsss=false;result.modem.dsss_factor=1;
        result.modem.spreading_seed.fill(0);result.modem.dsss_seed.fill(0);
        if(result.automatic_receive_profiles && !tuning::tone_mode(result.receive_pattern_mode))
            result.receive_pattern_mode=tuning::PatternMode::auto_tone;
    } else if(result.key) {
        // Encrypted on-air data must also select private pattern waveforms.
        result.modem.scramble=true;
    }
    return result;
}
void check_cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("transfer cancelled");
}

void validate(const Options& options) {
    if(options.modem.fake_hopping && (!options.modem.fake_hopping->mandatory_ramps ||
       (options.modem.fake_hopping->power_ramps && !options.modem.fake_hopping->utc_schedule())))
        throw Error("production FHSS requires a resolved mandatory power plan");
    modem::validate(options.modem);
    if(options.clock_sync)clock_sync::validate(*options.clock_sync);
    clock_sync::validate_audio_error(options.audio_timing_error_seconds);
    if(options.modem.oscillator_search)modem::validate_oscillator_search(*options.modem.oscillator_search);
    if(options.capture_epoch && (!std::isfinite(*options.capture_epoch) || *options.capture_epoch<0 ||
       static_cast<long double>(*options.capture_epoch)>=static_cast<long double>(std::numeric_limits<std::uint64_t>::max())))
        throw Error("invalid capture epoch");
    if((options.modem.scramble || options.modem.dsss) && !options.key)
        throw Error("encrypted spreading requires a symmetric key");
    if(options.modem.scheduled_hopping && options.modem.scheduled_hopping->enabled() && !options.key)
        throw Error("scheduled hopping requires a symmetric key");
    (void)interval_parity_bytes(options.fec);
    (void)source_storage_limit(options.content_limit);
    if(options.recovery_options.enabled &&
       (options.recovery_options.budget.count()<0 || options.recovery_options.workers>1024 ||
        options.recovery_options.retained_bits<1024 || options.recovery_options.retained_bits>1024*1024 ||
        options.recovery_options.extra_errors>24))
        throw Error("invalid post-reception recovery settings");
    if(options.dsp_workspace_bytes<256*1024)throw Error("streaming DSP workspace must be at least 256 KiB");
}
void validate_message(const Message& message,const Options& options) {
    validate(options);
    if(message.data.size()>options.content_limit)throw Error("message exceeds content limit");
}
Options binary_options(std::span<const std::uint8_t> bits,const Options& options) {
    auto result=options;result.fec=FecMode::off;result.compression=false;
    validate(result);
    if(bits.empty() || bits.size()>result.content_limit)throw Error("invalid raw bit input size");
    if(std::any_of(bits.begin(),bits.end(),[](auto b){return b>1;}))throw Error("raw bits must be zero or one");
    return result;
}
// A separate cursor belongs to each monotonic consumer. Its plan's address is
// stable for the cursor's entire lifetime; no whole-message calendar is stored.
struct BurstAddresses {
    modem::SymbolBurstPlan plan;
    modem::SymbolBurstCursor cursor;
    BurstAddresses(const Options& options):plan([&] {auto config=options.modem;config.data_key=options.key;return config;}()),
        cursor(plan,plan.bind_payload(options.timestamp,options.modem.stream_phase_samples)) {}
    BurstAddresses(const Options& options,modem::SymbolBurstSlot checkpoint):
        plan([&] {auto config=options.modem;config.data_key=options.key;return config;}()),cursor(plan,checkpoint) {}
    modem::SymbolStreamAddress address(std::uint64_t index) {
        const auto point=cursor.at(index).payload_begin;
        return {point.epoch,point.phase/plan.symbol_samples(),point.phase};
    }
};
std::unique_ptr<BurstAddresses> burst_addresses(const Options& options) {
    return options.key && options.modem.symbol_burst.enabled()?std::make_unique<BurstAddresses>(options):nullptr;
}
struct ReceiveOrigin {std::uint64_t epoch=0,phase=0;};
std::vector<ReceiveOrigin> receive_origins(const modem::Config& config,std::uint64_t center,unsigned window,bool encrypted) {
    std::vector<ReceiveOrigin> result;
    for(const auto epoch:drift_candidates(center,window,encrypted)) {
        if(config.symbol_burst.enabled()) {
            if(const auto phase=modem::symbol_burst_payload_start_in_epoch(config,epoch))result.push_back({epoch,*phase});
        } else result.push_back({epoch,config.stream_phase_samples});
    }
    return result;
}
IntervalOptions interval_options(const Options& options,std::uint64_t first_symbol,BurstAddresses* burst=nullptr) {
    IntervalOptions result;result.fec=options.fec;
    if(options.key) {
        const auto address=burst?burst->address(first_symbol):modem::symbol_stream_address(options.modem,options.timestamp,options.modem.stream_phase_samples,first_symbol);
        Bytes domain{'D','P','-','I','N','T','E','R','V','A','L',2,
            static_cast<std::uint8_t>(options.fec),static_cast<std::uint8_t>(options.compression)};
        for(auto value:{address.epoch,address.ordinal})
            for(int i=7;i>=0;--i)domain.push_back(static_cast<std::uint8_t>(value>>(8*i)));
        result.authenticator=[key=*options.key,domain](const Bytes& data) {
            auto context=domain;context.insert(context.end(),data.begin(),data.end());return key.mac(context);
        };
        result.verifier=[key=*options.key,domain](const Bytes& data,const Bytes& tag) {
            auto context=domain;context.insert(context.end(),data.begin(),data.end());return key.verify(context,tag);
        };
    }
    return result;
}
// Post-physical-end boundary search for a single interval. Marker observations
// add evidence, but are not required to try RS. Source syntax supplies none.
std::optional<DecodedInterval> recover_interval_alignment(
        std::span<const std::uint8_t> bits,const Options& options,std::uint64_t first_symbol,BurstAddresses* burst=nullptr) {
    // Cover an absent, partial or damaged leading marker and the existing slip
    // allowance. This is one-interval geometry, not a CPU-work limit.
    constexpr auto maximum_endpoint=boundary_sync::marker_bits+boundary_sync::maximum_slip_bits;
    constexpr auto trials=(maximum_endpoint+1)*(boundary_sync::marker_bits+1);
    static_assert(trials==38600);
    // Charge every endpoint and exact-suffix length, including no marker at all.
    // At most 38,600 hypotheses preserve the separate <=2^-128 match budget.
    constexpr auto required_evidence=128+std::bit_width(trials-1);
    const auto parity=interval_parity_bytes(options.fec);
    if(!parity || bits.size()<=short_message_bits ||
       bits.size()>maximum_endpoint+boundary_sync::interval_bits)return {};
    // Derive the public word at runtime, just as the normal collector does.
    auto marker=boundary_sync::insert(Bytes(boundary_sync::interval_bits));
    marker.resize(boundary_sync::marker_bits);
    std::optional<DecodedInterval> accepted;
    // Later endpoints can veto a candidate but cannot be accepted: otherwise
    // the geometry bound itself could hide a competing shifted RS codeword.
    for(std::size_t endpoint=0;endpoint<bits.size();++endpoint) {
        const auto count=bits.size()-endpoint;
        if(count>boundary_sync::interval_bits)continue;
        std::size_t evidence=0;
        while(evidence<std::min(endpoint,boundary_sync::marker_bits) &&
              bits[endpoint-evidence-1]==marker[boundary_sync::marker_bits-evidence-1])++evidence;
        boundary_sync::Interval candidate;
        std::array<std::size_t,stream_interval_bytes> erasures{};
        std::size_t erased=0;
        for(std::size_t i=0;i<boundary_sync::interval_bits;++i) {
            const auto mask=static_cast<std::uint8_t>(1U<<(7-i%8));
            if(i<count)candidate.bytes[i/8]|=static_cast<std::uint8_t>(bits[endpoint+i]*mask);
            else {
                if(!candidate.erasures[i/8])erasures[erased++]=i/8;
                candidate.erasures[i/8]=1;candidate.erasure_bits[i/8]|=mask;
            }
        }
        if(erased>parity || evidence+8*(parity-erased)<required_evidence)continue;
        if(endpoint>std::numeric_limits<std::uint64_t>::max()-first_symbol)continue;
        candidate.first_stream_symbol=first_symbol+endpoint;
        try {
            auto decoded=decode_interval(candidate.bytes,
                interval_options(options,candidate.first_stream_symbol,burst),
                std::span(erasures).first(erased),candidate.erasure_bits);
            const auto& stats=decoded.fec_stats;
            const auto errors=stats.data.repaired_bytes-stats.data.erased_bytes+
                stats.integrity.repaired_bytes-stats.integrity.erased_bytes+
                stats.parity.repaired_bytes-stats.parity.erased_bytes;
            // Discard all observations within erased bytes. For e errors in
            // the others, sum C(128-v,i)*255^i <= (e+1)*2^(15e).
            // The fixed RS parity contributes 8*(p-v) constraint bits; the
            // exact marker suffix supplies disjoint evidence. This deliberately
            // stricter test does not equate RS success with authentication.
            if(evidence+8*(parity-erased)<required_evidence+15*errors+std::bit_width(errors))continue;
            if(accepted || endpoint>maximum_endpoint)return {};
            accepted=std::move(decoded);
        } catch(const Error&) {continue;}
    }
    return accepted;
}
bool better_reception(const Received& candidate,const Received& current) {
    if(candidate.content_validated!=current.content_validated)return candidate.content_validated;
    if(candidate.content_validated) {
        // A later acquisition can authenticate a shorter suffix of a stream.
        // Prefer the widest recovered source before comparing radio scores.
        if(candidate.content.consumed_bytes!=current.content.consumed_bytes)
            return candidate.content.consumed_bytes>current.content.consumed_bytes;
        if(candidate.content.message.data.size()!=current.content.message.data.size())
            return candidate.content.message.data.size()>current.content.message.data.size();
        if(candidate.observed_bits!=current.observed_bits)return candidate.observed_bits>current.observed_bits;
    }
    const auto score=candidate.diagnostics.pattern_score.value_or(-1),prior=current.diagnostics.pattern_score.value_or(-1);
    if(score!=prior)return score>prior;
    return candidate.stream_complete || !current.stream_complete;
}
Bytes encoded_intervals(const Message& message,const Options& options,StreamLayout* layout=nullptr,
                        modem::TransmitTrace* trace=nullptr) {
    validate_message(message,options);
    const auto capacity=interval_data_bytes(options.fec,options.key.has_value());
    const auto application_source=attachment::encode(message,options.content_limit);
    auto source=encode_source(application_source,capacity,options.compression,source_storage_limit(options.content_limit));
    if(trace) {
        trace->compressed_available=true;
        const auto count=std::min(source.size(),modem::TransmitTrace::byte_limit);
        trace->compressed_bits.reserve(count*8);
        for(std::size_t i=0;i<count;++i)for(unsigned bit=0;bit<8;++bit)
            trace->compressed_bits.push_back((source[i]>>(7-bit))&1U);
    }
    const auto count=source.size()/capacity;
    if(count>Bytes{}.max_size()/(boundary_sync::interval_bits+boundary_sync::marker_bits))
        throw Error("encoded source exceeds address space");
    Bytes wire;wire.reserve(count*stream_interval_bytes);
    auto burst=burst_addresses(options);
    for(std::size_t i=0;i<count;++i) {
        const auto first=i*(boundary_sync::marker_bits+boundary_sync::interval_bits)+boundary_sync::marker_bits;
        auto block=encode_interval(std::span(source).subspan(i*capacity,capacity),interval_options(options,first,burst.get()));
        wire.insert(wire.end(),block.begin(),block.end());
    }
    if(layout) {
        layout->fec=options.fec;layout->compressed=options.compression;layout->authenticated=options.key.has_value();
        layout->source_bytes=application_source.size();layout->encoded_source_bytes=source.size();
        layout->wire_bytes=wire.size();layout->intervals=count;layout->data_bytes_per_interval=capacity;
        layout->source_bytes_per_interval=source_bytes_per_interval(capacity,options.compression);
        layout->parity_bytes_per_interval=interval_parity_bytes(options.fec);
        layout->integrity_bytes_per_interval=options.key?stream_mac_bytes:0;
    }
    return wire;
}
Bytes byte_bits(std::span<const std::uint8_t> bytes) {
    if(bytes.size()>Bytes{}.max_size()/8)throw Error("source bits exceed address space");
    Bytes bits;bits.reserve(bytes.size()*8);
    for(auto byte:bytes)for(unsigned i=0;i<8;++i)bits.push_back((byte>>(7-i))&1);
    return bits;
}
std::uint64_t physical_payload_span(const modem::Config& config,std::uint64_t symbols) {
    const auto duration=modem::symbol_sample_count(config);
    if(!modem::scheduled_bursts(config))return modem::symbol_schedule_detail::add(
        modem::scheduled_symbol_offset(config,config.stream_epoch,config.stream_phase_samples,symbols-1),duration);
    const modem::SchedHopSourceMap map(*config.scheduled_hopping,*config.data_key,config.sample_rate);
    const auto anchor=modem::symbol_schedule_detail::add(modem::symbol_schedule_detail::multiply(config.stream_epoch,config.sample_rate),config.stream_phase_samples);
    const auto source_span=modem::packed_scheduled_symbols(config)?modem::symbol_schedule_detail::add(
        modem::ScheduledSymbolCalendar(config).offset_source(config.stream_epoch,config.stream_phase_samples,symbols-1),duration):
        modem::symbol_schedule_detail::multiply(symbols,duration);
    const auto end=map.source_to_physical(map.locate(anchor).source.add_samples(source_span),true);
    if(end.fraction!=0 || end.sample<anchor)throw Error("invalid scheduled payload extent");
    return end.sample-anchor;
}
void mask_binary_bits(std::span<std::uint8_t>,const Options&,std::size_t,modem::TransmitTrace*,BurstAddresses* =nullptr);
}
bool uses_raw_message(const Message& message) noexcept {
    return message.kind==MessageKind::text && !message.data.empty() && message.data.size()<=short_message_bytes;
}
std::size_t source_storage_limit(std::size_t content_limit) {
    if(!content_limit || content_limit>(Bytes{}.max_size()-4096)/2)throw Error("invalid content limit");
    return content_limit*2+4096;
}
std::size_t pattern_bit_limit(std::size_t content_limit) {
    const auto limit=source_storage_limit(content_limit);
    // This is a ceiling for locally configured storage, not an allocation or
    // received length. A large content allowance must still admit small files
    // on a 32-bit host. Divide before multiplying and cap at whole, representable
    // fixed intervals; actual allocations remain subject to their memory quota.
    static_assert(boundary_sync::interval_bits%32==0);
    constexpr auto source_per_interval=boundary_sync::interval_bits/32;
    constexpr auto encoded_interval=boundary_sync::interval_bits+boundary_sync::marker_bits;
    const auto intervals=1+(limit-1)/source_per_interval;
    return std::min(intervals,Bytes{}.max_size()/encoded_interval)*encoded_interval;
}
modem::Config seeded_config(const Options& input_options, std::uint64_t timestamp) {
    const auto options=effective_options(input_options);
    validate(options);
    auto result = options.modem;
    result.stream_epoch=timestamp;
    result.data_key=options.key;
    // Stable, purpose-separated roots. PatternCode selects the epoch at each
    // symbol start; a later independently acquired epoch must not depend on
    // the first timestamp of the original message.
    if (options.key && result.scramble) {
        const auto seed = options.key->stream(StreamPurpose::Scrambler, 0, 0, result.spreading_seed.size());
        std::copy(seed.begin(), seed.end(), result.spreading_seed.begin());
    }
    if (options.key && (result.dsss || result.dsss_factor>1)) {
        const auto seed = options.key->stream(StreamPurpose::Dsss, 0, 0, result.dsss_seed.size());
        std::copy(seed.begin(), seed.end(), result.dsss_seed.begin());
    }
    return result;
}


Estimate estimate(const Message& message,const Options& options) {return estimate(message,options,nullptr);}
Estimate estimate(const Message& message,const Options& input,StreamLayout* layout) {
    auto options=effective_options(input);
    if(options.modem.symbol_burst.enabled()) {
        const auto config=modem::scheduled_estimate_config(seeded_config(options,options.timestamp));
        options.timestamp=config.stream_epoch;options.modem.stream_phase_samples=config.stream_phase_samples;
    }
    Bytes bits;std::size_t coded_bytes;
    if(uses_raw_message(message)) {
        validate_message(message,options);
        bits=compression::encode_short_bits(message.data,short_message_bits);coded_bytes=(bits.size()+7)/8;
        if(layout) {
            *layout={};layout->compressed=true;layout->source_bytes=message.data.size();
            layout->encoded_source_bytes=layout->wire_bytes=coded_bytes;
        }
    } else {
        const auto coded=encoded_intervals(message,options,layout);coded_bytes=coded.size();
        bits=boundary_sync::insert(byte_bits(coded),pattern_bit_limit(options.content_limit));
    }
    auto context=options;context.content_limit=std::max(options.content_limit,bits.size());
    auto result=estimate_binary(bits,context);result.content_bytes=message.data.size();result.coded_bytes=coded_bytes;
    result.repeatable_allowed=message.kind==MessageKind::text &&
        (message.data.size()<=options.repeat_policy.minimum_payload_bytes || result.content_seconds<=options.repeat_policy.maximum_seconds);
    return result;
}
Estimate estimate_binary(std::span<const std::uint8_t> bits,const Options& input_options) {
    const auto options=effective_options(input_options);
    const auto value=binary_options(bits,options);
    const auto symbols=bits.size();
    const auto symbol_samples=modem::symbol_sample_count(value.modem);
    if(symbols>std::numeric_limits<std::uint64_t>::max()/symbol_samples)
        throw Error("transmission duration exceeds 64-bit sample counter");
    const auto resolved=modem::scheduled_estimate_config(seeded_config(value,value.timestamp));
    const auto content_samples=physical_payload_span(resolved,symbols);
    const auto samples=modem::pattern_transmission_sample_count(resolved,symbols);
    Estimate result;result.wire_bits=bits.size();
    result.content_bytes=result.coded_bytes=bits.size()/8+(bits.size()%8!=0);
    result.content_seconds=result.coded_seconds=static_cast<double>(content_samples)/value.modem.sample_rate;
    result.total_seconds=static_cast<double>(samples)/value.modem.sample_rate;
    result.repeatable_allowed=true;
    std::size_t scratch=std::numeric_limits<std::size_t>::max();

    // PatternCode seeks through fixed stream caches; symbol duration must
    // not be charged as a retained chip array or sampled waveform.
    try {
        // Message estimates share this binary probe. Reserve the largest
        // possible prepared source prefix as well as the actual wire-prefix
        // capacity, so trace instrumentation cannot make an admitted TX fail
        // at a nearly full workspace. PatternTransmitter charges its optional
        // fixed mapping-byte capture through working_bytes().
        modem::TransmitTrace trace;trace.active=true;
        trace.source.resize(modem::TransmitTrace::source_limit);
        trace.compressed_bits.resize(modem::TransmitTrace::bit_limit);
        const auto prefix=std::min(bits.size(),modem::TransmitTrace::bit_limit);
        trace.wire_plain_bits.resize(prefix);
        if(value.key)trace.data_key_bits.resize(prefix);
        modem::StreamingTransmitter probe(modem::RawBits{Bytes(bits.begin(),bits.end())},
            resolved,std::max(value.modem.memory_limit,value.dsp_workspace_bytes/4),
            std::move(trace));
        scratch=probe.working_bytes();
        result.memory_supported=scratch<=value.dsp_workspace_bytes/4;
    } catch(const Error&) { result.memory_supported=false; }

    if(samples<=std::numeric_limits<std::size_t>::max()) {
        result.waveform_samples=static_cast<std::size_t>(samples);
        const auto budget=value.modem.memory_limit;
        result.batch_memory_supported=scratch<=budget && result.content_bytes<=budget-scratch &&
            samples<=(budget-scratch-result.content_bytes)/sizeof(float);
    }
    return result;
}

std::unique_ptr<modem::StreamingTransmitter> binary_transmitter(
    std::span<const std::uint8_t> bits,const Options& input_options) {
    const auto options=effective_options(input_options);
    const auto value=binary_options(bits,options);
    const auto production=seeded_config(value,value.timestamp);
    if((value.key || production.dsss || production.dsss_factor>1 || production.fake_hopping || (production.scheduled_hopping&&production.scheduled_hopping->enabled())) &&
       (!value.key || !modem::private_cover_enabled(production)))
        throw Error("production transmission cannot use diagnostic surrounding waveforms");
    modem::RawBits raw{Bytes(bits.begin(),bits.end())};
    modem::TransmitTrace trace;trace.active=true;trace.raw=true;
    mask_binary_bits(raw.bits,value,0,&trace);
    return std::make_unique<modem::StreamingTransmitter>(std::move(raw),production,
        value.dsp_workspace_bytes/4,std::move(trace));
}


Bytes message_bits(const Message& message,const Options& input) {
    const auto options=effective_options(input);
    if(uses_raw_message(message)) {validate_message(message,options);return compression::encode_short_bits(message.data,short_message_bits);}
    return byte_bits(encoded_intervals(message,options));
}
Bytes message_wire_bits(const Message& message,const Options& input) {
    const auto options=effective_options(input);
    auto bits=message_bits(message,options);
    if(!uses_raw_message(message))bits=boundary_sync::insert(bits,pattern_bit_limit(options.content_limit));
    auto context=options;context.content_limit=std::max(context.content_limit,bits.size());xor_binary_bits(bits,context);
    return bits;
}
std::unique_ptr<modem::StreamingTransmitter> message_transmitter(const Message& message,const Options& input) {
    const auto options=effective_options(input);
    const auto production=seeded_config(options,options.timestamp);
    if((options.key || production.dsss || production.dsss_factor>1 || production.fake_hopping || (production.scheduled_hopping&&production.scheduled_hopping->enabled())) &&
       (!options.key || !modem::private_cover_enabled(production)))
        throw Error("production transmission cannot use diagnostic surrounding waveforms");
    modem::TransmitTrace trace;trace.active=true;trace.short_text=uses_raw_message(message);
    trace.source_available=message.kind==MessageKind::text;
    if(trace.source_available)trace.source.assign(message.data.begin(),message.data.begin()+
        static_cast<std::ptrdiff_t>(std::min(message.data.size(),modem::TransmitTrace::source_limit)));
    Bytes bits;
    if(trace.short_text) {
        validate_message(message,options);
        bits=compression::encode_short_bits(message.data,short_message_bits);
        trace.compressed_available=true;
        trace.compressed_bits.assign(bits.begin(),bits.begin()+static_cast<std::ptrdiff_t>(
            std::min(bits.size(),modem::TransmitTrace::bit_limit)));
    } else {
        bits=boundary_sync::insert(byte_bits(encoded_intervals(message,options,nullptr,&trace)),
            pattern_bit_limit(options.content_limit));
    }
    auto context=options;context.content_limit=std::max(context.content_limit,bits.size());
    mask_binary_bits(bits,context,0,&trace);
    if(message.kind==MessageKind::text && message.repeatable && message.data.size()>options.repeat_policy.minimum_payload_bytes &&
       static_cast<long double>(physical_payload_span(production,bits.size()))/production.sample_rate>options.repeat_policy.maximum_seconds)
        throw Error("repeatable content exceeds airtime limit");
    return std::make_unique<modem::StreamingTransmitter>(modem::RawBits{std::move(bits)},
        production,options.dsp_workspace_bytes/4,std::move(trace));
}
namespace {
void mask_binary_bits(std::span<std::uint8_t> bits,const Options& input_options,std::size_t bit_offset,
                      modem::TransmitTrace* trace,BurstAddresses* addresses) {
    const auto options=effective_options(input_options);
    if(bits.empty())return;
    const auto value=binary_options(bits,options);
    if(trace) {
        trace->wire_plain_bits.assign(bits.begin(),bits.begin()+static_cast<std::ptrdiff_t>(
            std::min(bits.size(),modem::TransmitTrace::bit_limit)));
        trace->data_masked=value.key.has_value();
        if(value.key)trace->data_key_bits.reserve(std::min(bits.size(),modem::TransmitTrace::bit_limit));
    }
    if(bits.size()>std::numeric_limits<std::size_t>::max()-bit_offset)
        throw Error("raw binary data-stream offset overflow");
    if(value.key) {
        auto local=addresses?nullptr:burst_addresses(value);
        if(local)addresses=local.get();
        // Data and pattern positions use the same symbol-start clock. A new
        // second restarts its local bit positions even after missed symbols.
        // Keep a bounded cache; never allocate a duration-sized keystream.
        constexpr std::size_t cache_size=512;
        Bytes mask;
        std::uint64_t cached_epoch=0,cached_begin=0;
        bool cached=false;
        std::size_t offset=0;
        while(offset<bits.size()) {
            const auto address=addresses?addresses->address(bit_offset+offset):modem::symbol_stream_address(value.modem,value.timestamp,value.modem.stream_phase_samples,bit_offset+offset);
            const auto byte=address.ordinal/8,begin=byte-byte%cache_size;
            if(!cached || address.epoch!=cached_epoch || begin!=cached_begin) {
                mask=value.key->stream(StreamPurpose::Data,address.epoch,begin,cache_size);
                cached_epoch=address.epoch;cached_begin=begin;cached=true;
            }
            const auto key_bit=static_cast<std::uint8_t>((mask[static_cast<std::size_t>(byte-begin)]>>(7-address.ordinal%8))&1);
            bits[offset]^=key_bit;
            if(trace && offset<modem::TransmitTrace::bit_limit)trace->data_key_bits.push_back(key_bit);
            ++offset;
        }
    }
}
}
void xor_binary_bits(std::span<std::uint8_t> bits,const Options& options,std::size_t bit_offset) {
    mask_binary_bits(bits,options,bit_offset,nullptr);
}


Bytes transmission_wire(const Message& message,const Options& options) {
    const auto bits=message_wire_bits(message,options);
    if(bits.size()%8)throw Error("non-byte-aligned stream requires message_wire_bits or message_transmitter");
    Bytes bytes(bits.size()/8);
    for(std::size_t i=0;i<bits.size();++i)bytes[i/8]|=static_cast<std::uint8_t>(bits[i]<<(7-i%8));
    return bytes;
}

struct StreamReceiver::Impl {
    struct State {
        boundary_sync::Collector collector;
        Bytes source;
        Options options;
        Received result;
        std::uint64_t next_symbol=0;
        std::size_t stored=0;
        bool failed=false,aligned=false;
        // Four hard decisions per byte (0, 1, or 2 for a missing slot).
        // Keep the extra retention inside the existing receive-memory bound.
        Bytes recovery_bits;
        std::size_t recovery_slots=0;
        std::vector<std::uint64_t> recovery_starts;
        std::uint64_t recovery_first=0;
        bool recovery_truncated=false,recovery_contiguous=true,interval_failed=false;
        std::unique_ptr<BurstAddresses> mask_addresses,interval_addresses;
        std::optional<modem::SymbolBurstSlot> recovery_checkpoint;
        State(const Options& value,const modem::PatternBurst& burst)
            :collector(burst.first_stream_symbol,0,true),options(value),next_symbol(burst.first_stream_symbol),
             recovery_first(burst.first_stream_symbol) {
            options.modem.stream_phase_samples=burst.stream_phase_samples;
            mask_addresses=burst_addresses(options);
            if(mask_addresses) {
                recovery_checkpoint=mask_addresses->cursor.at(burst.first_stream_symbol);
                mask_addresses->cursor.checkpoint(*recovery_checkpoint);
                interval_addresses=std::make_unique<BurstAddresses>(options,*recovery_checkpoint);
            }
            result.timestamp=options.timestamp;
            const std::array<std::uint64_t,2> identity{options.timestamp,burst.stream_first_sample};
            for(std::size_t j=0;j<2;++j)for(std::size_t i=0;i<8;++i)
                result.content.message.local_id[j*8+i]=static_cast<std::uint8_t>(identity[j]>>(8*i));
        }
    };
    Options options;
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::unique_ptr<State>> states;
    std::shared_ptr<ReceiveStorageQuota> quota;
    explicit Impl(Options value,std::uint64_t timestamp,std::shared_ptr<ReceiveStorageQuota> budget)
        :options(effective_options(value)),quota(std::move(budget)) {
        options.timestamp=timestamp;validate(options);
        if(!quota)quota=std::make_shared<ReceiveStorageQuota>(ReceiveStorageQuota{source_storage_limit(options.content_limit),0});
        if(quota->used>quota->limit)throw Error("Invalid receive source storage quota");
    }
    ~Impl() {for(auto& [id,state]:states) {quota->used-=state->stored;quota->reserved-=state->source.capacity();}}

    void fail(State& state,const std::string& error) {
        state.failed=true;state.result.error=error;
        quota->used-=state.stored;quota->reserved-=state.source.capacity();state.stored=0;Bytes{}.swap(state.source);
    }
    void accept_interval(State& state,const DecodedInterval& decoded) {
        auto& content=state.result.content;
        content.corrected_bytes+=decoded.corrected_bytes;content.consumed_bytes+=stream_interval_bytes;
        content.authenticated=decoded.authenticated;
        if(!content.pre_fec_accuracy && content.consumed_bytes==stream_interval_bytes)
            content.pre_fec_accuracy=StreamBitAccuracy{};
        if(content.pre_fec_accuracy && decoded.pre_fec_accuracy) {
            content.pre_fec_accuracy->received_data_bits+=decoded.pre_fec_accuracy->received_data_bits;
            content.pre_fec_accuracy->corrected_data_bits+=decoded.pre_fec_accuracy->corrected_data_bits;
            content.pre_fec_accuracy->missing_data_bits+=decoded.pre_fec_accuracy->missing_data_bits;
        } else content.pre_fec_accuracy.reset();
        const auto accumulate=[](FecRegionStats& total,const FecRegionStats& part) {
            total.received_bits+=part.received_bits;total.corrected_bits+=part.corrected_bits;
            total.missing_bits+=part.missing_bits;total.corrected_bytes+=part.corrected_bytes;
            total.erased_bytes+=part.erased_bytes;total.repaired_bytes+=part.repaired_bytes;
        };
        accumulate(content.fec_stats.data,decoded.fec_stats.data);
        accumulate(content.fec_stats.integrity,decoded.fec_stats.integrity);
        accumulate(content.fec_stats.parity,decoded.fec_stats.parity);
        if(state.failed)return;
        const auto limit=std::min(quota->limit,default_memory_limit);
        if(quota->used>limit || decoded.data.size()>limit-quota->used){fail(state,"received source storage quota exhausted");return;}
        if(state.source.size()+decoded.data.size()>state.source.capacity()) {
            const auto available=limit-std::min(limit,quota->reserved);
            const auto old=state.source.capacity();
            if(state.source.size()+decoded.data.size()-old>available) {fail(state,"received source memory quota exhausted");return;}
            state.source.reserve(std::min(old+available,std::max(state.source.size()+decoded.data.size(),old*2)));
            quota->reserved+=state.source.capacity()-old;
        }
        state.source.insert(state.source.end(),decoded.data.begin(),decoded.data.end());
        state.stored+=decoded.data.size();quota->used+=decoded.data.size();
    }
    void interval(State& state,const boundary_sync::Interval& interval) {
        state.aligned|=interval.marker_recognized;
        if(state.options.recovery_options.enabled && state.options.recovery_options.budget.count()>0 &&
           state.options.fec!=FecMode::off && !state.recovery_truncated && interval.marker_recognized)
            state.recovery_starts.push_back(interval.first_stream_symbol);
        if(!state.aligned){fail(state,"alignment marker not established");return;}
        try {
            std::array<std::size_t,stream_interval_bytes> erasures{};std::size_t count=0;
            for(std::size_t i=0;i<interval.erasures.size();++i)if(interval.erasures[i])erasures[count++]=i;
            const auto decoded=decode_interval(interval.bytes,interval_options(state.options,interval.first_stream_symbol,state.interval_addresses.get()),
                std::span(erasures).first(count),interval.erasure_bits);
            accept_interval(state,decoded);
        } catch(const Error& error) {state.interval_failed=true;fail(state,error.what());}
    }
    Received push(modem::PatternBurst burst,modem::Diagnostics diagnostics) {
        if(burst.missing_slots) {
            if(!burst.bits.empty())throw Error("mixed bit and missing-run event");
            auto remaining=burst.missing_slots;
            const auto complete=burst.complete;
            burst.missing_slots=0;burst.complete=false;
            Received result;
            while(remaining) {
                const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(remaining,2048));
                burst.bits.assign(count,modem::missing_pattern_bit);
                result=push(burst,diagnostics);
                if(count>std::numeric_limits<std::uint64_t>::max()-burst.first_stream_symbol)
                    throw Error("missing-run symbol counter exhausted");
                burst.first_stream_symbol+=count;remaining-=count;
            }
            if(complete) {burst.bits.clear();burst.complete=true;return push(std::move(burst),std::move(diagnostics));}
            return result;
        }
        const auto id=std::pair{burst.stream_first_sample,burst.stream_first_symbol};
        auto it=states.find(id);
        if(it==states.end()) {
            if(states.size()>=StreamReceiver::candidate_limit) {
                Received unavailable;unavailable.error="receiver content candidate quota exhausted";
                unavailable.stream_complete=burst.complete;return unavailable;
            }
            it=states.emplace(id,std::make_unique<State>(options,burst)).first;
        }
        auto& state=*it->second;
        if(state.options.modem.symbol_burst.enabled() && state.options.modem.stream_phase_samples!=burst.stream_phase_samples)
            throw Error("Symbol_Burst reception changed its bound payload origin");
        state.options.modem.stream_phase_samples=burst.stream_phase_samples;
        state.result.diagnostics=std::move(diagnostics);state.result.diagnostics.pattern_score=burst.score;
        state.result.diagnostics.sample_offset=static_cast<std::size_t>(burst.stream_first_sample);
        if(burst.first_stream_symbol!=state.next_symbol && !burst.bits.empty()) {
            state.recovery_contiguous=false;fail(state,"noncontiguous received source");
        }
        auto& bits=burst.bits;
        std::size_t begin=0;
        for(std::size_t i=0;i<=bits.size();++i) {
            if(i<bits.size() && bits[i]!=modem::missing_pattern_bit)continue;
            if(i>begin) {
                auto context=state.options;context.content_limit=std::max(context.content_limit,i-begin);
                mask_binary_bits(std::span(bits).subspan(begin,i-begin),context,
                    static_cast<std::size_t>(burst.first_stream_symbol)+begin,nullptr,state.mask_addresses.get());
            }
            if(i<bits.size())++state.result.missing_symbols;
            begin=i+1;
        }
        if(bits.size()>std::numeric_limits<std::size_t>::max()-state.result.observed_bits)fail(state,"source observation counter exhausted");
        else state.result.observed_bits+=bits.size();
        if(bits.size()>std::numeric_limits<std::uint64_t>::max()-burst.first_stream_symbol)fail(state,"source symbol counter exhausted");
        else state.next_symbol=burst.first_stream_symbol+bits.size();
        const auto preview=std::min<std::size_t>(4096-state.result.raw_bits.size(),bits.size());
        for(std::size_t i=0;i<preview;++i)state.result.raw_bits.push_back(bits[i]==1?1:0);
        if(state.options.recovery_options.enabled && state.options.recovery_options.budget.count()>0 &&
           state.options.fec!=FecMode::off && !state.recovery_truncated) {
            const auto available=state.options.recovery_options.retained_bits-state.recovery_slots;
            if(bits.size()>available) {
                state.recovery_truncated=true;
                Bytes{}.swap(state.recovery_bits);
                std::vector<std::uint64_t>{}.swap(state.recovery_starts);
            } else {
                for(const auto bit:bits) {
                    if(state.recovery_slots%4==0)state.recovery_bits.push_back(0);
                    state.recovery_bits.back()|=static_cast<std::uint8_t>(bit<<(2*(state.recovery_slots%4)));
                    ++state.recovery_slots;
                }
            }
        }
        {
            try {
                const auto sink=[&](const boundary_sync::Interval& interval){this->interval(state,interval);};
                state.collector.push(bits,sink);
                if(burst.complete)state.collector.finish(true,sink);
            } catch(const Error& error){state.interval_failed=true;fail(state,error.what());}
        }
        state.result.stream_complete=burst.complete;
        // This fallback uses only the existing complete diagnostic prefix.
        // Never turn its zero placeholders into observations, retry failed
        // intervals, or let a codeword manufacture the physical end event.
        if(burst.complete && !state.failed && !state.aligned &&
           !state.collector.leading_marker_recognized() && !state.result.missing_symbols &&
           state.result.observed_bits==state.result.raw_bits.size()) {
            auto recovery_addresses=state.recovery_checkpoint?
                std::make_unique<BurstAddresses>(state.options,*state.recovery_checkpoint):nullptr;
            if(const auto recovered=recover_interval_alignment(state.result.raw_bits,state.options,
                    state.next_symbol-state.result.observed_bits,recovery_addresses.get())) {
                state.aligned=true;
                accept_interval(state,*recovered);
            }
        }
        // The sole decompression gate. No EOF, size, codeword or MAC can enter it.
        if(burst.complete && !state.failed && state.aligned && state.stored) {
            try {
                auto message=state.result.content.message;
                message.data=decode_source(state.source,
                    interval_data_bytes(options.fec,options.key.has_value()),options.compression,attachment::source_limit(options.content_limit));
                attachment::interpret(message,options.content_limit);
                // Application interpretation can still reject the decoded
                // source. Publish neither its bytes nor its name before that.
                state.result.content.message=std::move(message);
                state.result.content_validated=true;
            } catch(const Error& error){fail(state,error.what());}
        }
        // The fixed short dictionary is a bounded application interpretation,
        // never a framing or confidence test. It consumes the exact physical
        // endpoint and cannot pad an unfinished token or a missing symbol.
        // Equivalent clock origins can give an intact keyed stream a nonzero
        // local starting symbol; that coordinate does not imply a missing prefix.
        if(burst.complete && !state.failed && !state.aligned &&
           !state.collector.leading_marker_recognized() && !state.result.missing_symbols &&
           !state.result.raw_bits.empty() &&
           state.result.observed_bits==state.result.raw_bits.size() && state.result.observed_bits<=short_message_bits) {
            try {
                state.result.content.message.data=compression::decode_short_bits(
                    state.result.raw_bits,std::min(short_message_bytes,options.content_limit));
                state.result.short_text_decoded=true;
            } catch(const Error&) {} // Keep exact raw bits when no complete interpretation fits.
        }
        if(burst.complete) {
            // Seal only hard decisions and gap slots. No waveform or confidence
            // data crosses into exhaustive recovery, and no search runs here.
            const auto& recovery_options=state.options.recovery_options;
            if(recovery_options.enabled && recovery_options.budget.count()>0 &&
               state.options.fec!=FecMode::off && !state.result.content_validated &&
               !state.result.short_text_decoded && state.result.observed_bits>short_message_bits &&
               (state.interval_failed || !state.aligned)) {
                if(state.recovery_truncated || !state.recovery_contiguous ||
                   state.result.observed_bits!=state.recovery_slots) {
                    state.result.recovery_progress.state=RecoveryState::unavailable;
                } else {
                    RecoveryInput capture;
                    capture.bits.resize(state.recovery_slots);
                    for(std::size_t i=0;i<capture.bits.size();++i)
                        capture.bits[i]=static_cast<std::uint8_t>((state.recovery_bits[i/4]>>(2*(i%4)))&3);
                    capture.first_symbol=state.recovery_first;
                    capture.established_starts=std::move(state.recovery_starts);capture.fec=state.options.fec;
                    capture.interval_options=[value=state.options,checkpoint=state.recovery_checkpoint](std::uint64_t first) {
                        // Each worker owns a local cursor rooted at the bounded
                        // retained window, never a mutable shared cursor or a
                        // scan through discarded message history.
                        auto addresses=checkpoint?std::make_unique<BurstAddresses>(value,*checkpoint):nullptr;
                        return interval_options(value,first,addresses.get());
                    };
                    state.result.recovery=std::make_shared<RecoveryJob>(std::move(capture),recovery_options);
                    state.result.recovery_progress=state.result.recovery->progress();
                    state.result.recovery_source=std::make_shared<RecoverySource>(RecoverySource{
                        state.options,quota->limit-quota->used+state.stored});
                }
            }
            auto result=std::move(state.result);quota->used-=state.stored;quota->reserved-=state.source.capacity();states.erase(it);return result;
        }
        return state.result;
    }
};
StreamReceiver::StreamReceiver(Options options,std::uint64_t timestamp,std::shared_ptr<ReceiveStorageQuota> quota):impl_(std::make_unique<Impl>(std::move(options),timestamp,std::move(quota))){}
StreamReceiver::~StreamReceiver()=default;
StreamReceiver::StreamReceiver(StreamReceiver&&) noexcept=default;
StreamReceiver& StreamReceiver::operator=(StreamReceiver&&) noexcept=default;
Received StreamReceiver::push(modem::PatternBurst burst,modem::Diagnostics diagnostics) {return impl_->push(std::move(burst),std::move(diagnostics));}
void StreamReceiver::discard(std::uint64_t first_sample,std::uint64_t first_symbol) {
    auto& s=*impl_;const auto it=s.states.find({first_sample,first_symbol});
    if(it==s.states.end())return;
    s.quota->used-=it->second->stored;s.quota->reserved-=it->second->source.capacity();
    s.states.erase(it);
}
std::size_t StreamReceiver::working_bytes()const {
    auto bytes=sizeof(StreamReceiver)+sizeof(Impl)+impl_->options.receive_targets_db_hz.capacity()*sizeof(double);
    for(const auto& [id,state]:impl_->states) {
        const auto& result=state->result;const auto& message=result.content.message;
        // Include map links, the actual diagnostic/preview allocations and
        // bounded corrected source bytes retained in RAM until physical end.
        bytes+=sizeof(Impl::State)+sizeof(id)+sizeof(state)+4*sizeof(void*)+
            state->collector.working_bytes()+state->options.receive_targets_db_hz.capacity()*sizeof(double)+
            result.raw_bits.capacity()+result.diagnostics.constellation.capacity()*sizeof(std::complex<double>)+
            result.diagnostics.waveform.capacity()*sizeof(float)+result.error.capacity()+
            message.data.capacity()+message.filename.capacity()+message.callsign.capacity()+message.grid.capacity()+
            state->recovery_bits.capacity()+state->recovery_starts.capacity()*sizeof(std::uint64_t);
        bytes+=(static_cast<unsigned>(state->mask_addresses!=nullptr)+static_cast<unsigned>(state->interval_addresses!=nullptr))*sizeof(BurstAddresses);
        bytes+=state->source.capacity();
    }
    return bytes;
}
std::size_t StreamReceiver::source_buffer_bytes()const {
    std::size_t bytes=0;for(const auto& [id,state]:impl_->states)bytes+=state->source.capacity();
    return bytes;
}
Received interpret_pattern(modem::PatternBurst burst,const Options& options,std::uint64_t timestamp,modem::Diagnostics diagnostics) {
    StreamReceiver receiver(options,timestamp);return recover_received(receiver.push(std::move(burst),std::move(diagnostics)));
}

Received recover_received(Received received,std::stop_token stop) {
    if(!received.stream_complete || received.content_validated || !received.recovery || !received.recovery_source)
        return received;
    received.recovery->run(stop);
    received.recovery_progress=received.recovery->progress();
    if(received.recovery_progress.state==RecoveryState::recovered) {
        try {
            const auto& context=*received.recovery_source;
            const auto& options=context.options;
            const auto intervals=received.recovery->result();
            Bytes source;
            StreamContent content;content.message.local_id=received.content.message.local_id;
            content.authenticated=options.key.has_value();
            content.pre_fec_accuracy=StreamBitAccuracy{};
            const auto add_region=[](FecRegionStats& to,const FecRegionStats& from) {
                to.received_bits+=from.received_bits;to.corrected_bits+=from.corrected_bits;
                to.missing_bits+=from.missing_bits;to.corrected_bytes+=from.corrected_bytes;
                to.erased_bytes+=from.erased_bytes;to.repaired_bytes+=from.repaired_bytes;
            };
            for(const auto& interval:intervals) {
                if(source.size()>context.source_limit || interval.data.size()>context.source_limit-source.size())
                    throw Error("received source storage quota exhausted");
                source.insert(source.end(),interval.data.begin(),interval.data.end());
                content.corrected_bytes+=interval.corrected_bytes;content.consumed_bytes+=stream_interval_bytes;
                content.authenticated=content.authenticated && interval.authenticated;
                if(interval.pre_fec_accuracy) {
                    content.pre_fec_accuracy->received_data_bits+=interval.pre_fec_accuracy->received_data_bits;
                    content.pre_fec_accuracy->corrected_data_bits+=interval.pre_fec_accuracy->corrected_data_bits;
                    content.pre_fec_accuracy->missing_data_bits+=interval.pre_fec_accuracy->missing_data_bits;
                }
                add_region(content.fec_stats.data,interval.fec_stats.data);
                add_region(content.fec_stats.integrity,interval.fec_stats.integrity);
                add_region(content.fec_stats.parity,interval.fec_stats.parity);
            }
            if(intervals.empty())throw Error("recovery produced no source intervals");
            content.message.data=decode_source(source,interval_data_bytes(options.fec,options.key.has_value()),
                options.compression,attachment::source_limit(options.content_limit));
            attachment::interpret(content.message,options.content_limit);
            received.content=std::move(content);received.content_validated=true;received.error.clear();
        } catch(const Error& error) {
            received.error=std::string("recovered intervals failed source validation: ")+error.what();
        }
    }
    switch(received.recovery_progress.state) {
    case RecoveryState::recovered:case RecoveryState::exhausted:case RecoveryState::ambiguous:case RecoveryState::unavailable:
        received.recovery.reset();received.recovery_source.reset();break;
    default:break; // Preserve unfinished hard-bit work for explicit resumption.
    }
    return received;
}

std::vector<float> transmit(const Message& message, const Options& input_options, std::stop_token stop) {
    const auto options=effective_options(input_options);
    check_cancelled(stop);
    auto source=message_transmitter(message,options);
    if(source->total_samples()>options.modem.memory_limit/sizeof(float))throw Error("pattern waveform exceeds batch memory limit; use streaming output");
    std::vector<float> samples(static_cast<std::size_t>(source->total_samples()));
    std::size_t position=0;while(position<samples.size())position+=source->read(std::span(samples).subspan(position,std::min<std::size_t>(4096,samples.size()-position)),stop);
    return samples;
}

Received receive(std::span<const float> samples, const Options& input_options, Progress progress, std::stop_token stop) {
    const auto options=effective_options(input_options);
    check_cancelled(stop);
    validate(options);
    std::optional<Received> best;
    std::string last_error;
    auto profiles=options.automatic_receive_profiles?tuning::receive_profiles(options.modem,options.receive_targets_db_hz,options.receive_pattern_mode,options.key.has_value()):std::vector<modem::Config>{options.modem};
    for(const auto& profile:profiles)for(const auto origin:receive_origins(profile,options.timestamp,options.search_seconds,options.key.has_value())) {
        const auto epoch=origin.epoch;
        check_cancelled(stop);if(progress)progress(epoch);
        try {
        auto value=options;value.modem=profile;value.modem.stream_phase_samples=origin.phase;
        modem::PatternSearch search;search.allow_local_clock_fallback=true;
        if(profile.oscillator_search) {
            const auto declared=modem::oscillator_pattern_search(profile,effective_clock_sync(options)?modem::utc_transmit_rate_limit(profile):0);
            search.hypotheses=declared.hypotheses;
            search.admitted_frequency_half_width_hz=declared.frequency.requested_half_width_hz;
            search.admitted_clock_half_width_ppm=declared.requested_clock_half_width_ppm;
            search.complex_frequency_coverage=!declared.frequency.limited;
            search.complex_clock_coverage=declared.clock_half_width_ppm>=declared.requested_clock_half_width_ppm;
        }
        else search.expand_clock_search=true;
        search.start_offset_seconds=static_cast<double>(epoch)-options.capture_epoch.value_or(static_cast<double>(options.timestamp));
        if(profile.symbol_burst.enabled() && options.capture_epoch)
            *search.start_offset_seconds+=static_cast<double>(origin.phase)/profile.sample_rate;
        if(!options.capture_epoch)*search.start_offset_seconds+=(static_cast<double>(modem::training_sample_count(profile))+
            static_cast<double>(modem::pattern_pulse_padding_samples(profile)))/profile.sample_rate;
        search.bit_limit=pattern_bit_limit(value.content_limit);

        search.search_stream_phases=!profile.symbol_burst.enabled() && options.key.has_value();
        search.receiver_bank_count=modem::receiver_strategy_bank_count(profile,options.search_seconds,1,profiles.size());
        search.start_uncertainty_seconds=options.search_seconds+1.;
        modem::PatternReceiver decoder(seeded_config(value,epoch),options.dsp_workspace_bytes,search);
        StreamReceiver content(value,epoch);
        for(std::size_t offset=0;offset<samples.size();) {
            const auto count=std::min<std::size_t>(4096,samples.size()-offset);decoder.push(samples.subspan(offset,count),stop);offset+=count;
            for(auto& burst:decoder.take_bursts()){
                auto candidate=content.push(std::move(burst),decoder.diagnostics());
                if(!best || better_reception(candidate,*best))best=std::move(candidate);
            }
        }
        decoder.finish(stop);
        for(auto& burst:decoder.take_bursts()){
            auto candidate=content.push(std::move(burst),decoder.diagnostics());
                if(!best || better_reception(candidate,*best))best=std::move(candidate);
        }
        } catch(const Error& error){check_cancelled(stop);last_error=error.what();}
    }
    if(best) {
        const auto tail=samples.last(std::min<std::size_t>(2048,samples.size()));
        best->diagnostics.waveform.assign(tail.begin(),tail.end());
        return recover_received(std::move(*best),stop);
    }
    throw Error("no sufficiently confident pattern in timing search"+(last_error.empty()?std::string{}:": "+last_error));
}

Received simulate(const Message& message, const Options& input_options, const modem::ChannelConfig& channel,
                  Progress progress, std::stop_token stop) {
    const auto options=effective_options(input_options);
    check_cancelled(stop);
    validate_message(message,options);
    const auto config = seeded_config(options, options.timestamp);
    modem::validate_channel(config,channel);
    std::optional<Received> best;
    std::string last_error;
    const auto center=channel.receiver_timestamp.value_or(options.timestamp);
    const auto profiles=options.automatic_receive_profiles?tuning::receive_profiles(options.modem,options.receive_targets_db_hz,options.receive_pattern_mode,options.key.has_value()):std::vector<modem::Config>{options.modem};
    for(const auto& profile:profiles)for(const auto origin:receive_origins(profile,center,options.search_seconds,options.key.has_value())) {
        const auto epoch=origin.epoch;
        check_cancelled(stop);if(progress)progress(epoch);
        try {
        auto source=message_transmitter(message,options);auto value=options;value.modem=profile;value.modem.stream_phase_samples=origin.phase;
        modem::PatternSearch search;search.allow_local_clock_fallback=true;
        // This source supplies fixed-rate simulated PCM, without the hardware
        // UTC resampler. Retain its complete oscillator bank without charging
        // or searching an additional correction that cannot occur here.
        if(profile.oscillator_search) {
            const auto declared=modem::oscillator_pattern_search(profile);
            search.hypotheses=declared.hypotheses;
            search.admitted_frequency_half_width_hz=declared.frequency.requested_half_width_hz;
            search.admitted_clock_half_width_ppm=declared.requested_clock_half_width_ppm;
            search.complex_frequency_coverage=!declared.frequency.limited;
            search.complex_clock_coverage=declared.clock_half_width_ppm>=declared.requested_clock_half_width_ppm;
        }
        else search.expand_clock_search=true;
        search.start_offset_seconds=static_cast<double>(epoch)-static_cast<double>(center)+
            (static_cast<double>(modem::training_sample_count(profile))+
             static_cast<double>(modem::pattern_pulse_padding_samples(profile)))/profile.sample_rate;
        if(profile.symbol_burst.enabled())*search.start_offset_seconds+=
            (static_cast<double>(origin.phase)-static_cast<double>(options.modem.stream_phase_samples))/profile.sample_rate;
        search.bit_limit=pattern_bit_limit(value.content_limit);

        search.search_stream_phases=!profile.symbol_burst.enabled() && options.key.has_value();
        search.receiver_bank_count=modem::receiver_strategy_bank_count(profile,options.search_seconds,1,profiles.size());
        search.start_uncertainty_seconds=options.search_seconds+1.;
        modem::PatternReceiver decoder(seeded_config(value,epoch),options.dsp_workspace_bytes,search);
        StreamReceiver content(value,epoch);
        modem::SampledSimulationChannel impairments(config,channel);std::array<float,2048> samples{};
        std::size_t preview_count=0;
        const auto harvest=[&] {
            for(auto& burst:decoder.take_bursts()){
                auto candidate=content.push(std::move(burst),decoder.diagnostics());
                if(!best || better_reception(candidate,*best))best=std::move(candidate);
                best->diagnostics.waveform.assign(samples.begin(),samples.begin()+static_cast<std::ptrdiff_t>(preview_count));
            }
        };
        while(const auto count=impairments.read(*source,samples,stop)){preview_count=count;decoder.push(std::span(samples).first(count),stop);harvest();}
        auto trailing=modem::pattern_absence_capture_samples(profile)+profile.sample_rate+2*modem::pattern_pulse_padding_samples(profile);
        while(trailing) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(trailing,samples.size()));
            auto tail=std::span(samples).first(count);impairments.read_noise(tail,stop);preview_count=count;decoder.push(tail,stop);trailing-=count;harvest();
        }
        decoder.finish(stop);harvest();
        }catch(const Error& error){check_cancelled(stop);last_error=error.what();}
    }
    if(best)return recover_received(std::move(*best),stop);
    throw Error("no sufficiently confident pattern in simulated timing search"+(last_error.empty()?std::string{}:": "+last_error));
}
}

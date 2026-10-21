#pragma once
// Diagnostic overlay only. Never included by the ordinary production source.
#include "pattern_correlator_batch.hpp"
#include <atomic>
#include <cfenv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>

namespace datapump::modem::detail::correlator_tone_numeric {
inline constexpr std::size_t capacity=4096;
// kind: 0=Impl::accumulate, 1=batch accumulate_lane, 2=batch/owner binding.
// identity is an opaque integer only; no recorded pointer is dereferenced.
struct Context {
    unsigned kind=0;
    std::uintptr_t identity=0,owner=0;
    std::uint64_t batch_first=0,batch_end=0;
    std::uint64_t epoch=0,symbol_samples=0,chip_samples=0,index=0;
    std::uint64_t phase_lower=0,phase_upper=0,block_first=0,block_count=0;
    std::uint64_t observed_start=0,observed=0,until=0,chip=0;
    std::size_t frequency=0,rate_index=0,tone_bank_base=0,bank=0,group=0;
    unsigned bit=0;
    std::uint32_t sample_rate=0;
    double carrier_hz=0,frequency_offset=0,bank_frequency=0;
    long double origin=0,rate=0;
};
struct Record {
    Context context;
    long double within=0;
    double fraction=0,tone=0;
    std::complex<double> raw_phase{},corrected_phase{};
    CorrelationProjection projection;
    CorrelationFit before,after;
    int rounding=0;
};
struct Slot {Record value;std::atomic<bool> ready{false};};

struct State {
    std::array<Slot,capacity> slots{};
    std::atomic<std::size_t> next{0},dropped{0};
    char path[4096]{};
    bool enabled=false,requested=false,path_error=false;
    State() noexcept {
        const auto* value=std::getenv("DATAPUMP_CORRELATOR_TONE_TRACE");
        if(!value || !*value)return;
        requested=true;
        const auto size=std::strlen(value);
        if(value[0]!='/' || size>=sizeof(path)){path_error=true;return;}
        std::memcpy(path,value,size+1);enabled=true;
    }
    void append(const Record& value) noexcept {
        const auto position=next.fetch_add(1,std::memory_order_relaxed);
        if(position>=capacity){dropped.fetch_add(1,std::memory_order_relaxed);return;}
        slots[position].value=value;
        slots[position].ready.store(true,std::memory_order_release);
    }
    static void projection(std::FILE* out,const CorrelationProjection& p) noexcept {
        std::fprintf(out,"[\"%a\",\"%a\",\"%a\",\"%a\",\"%a\",\"%a\"]",p.xc,p.xs,p.cc,p.ss,p.cs,p.energy);
    }
    static void fit(std::FILE* out,const CorrelationFit& f) noexcept {
        std::fprintf(out,"{\"fields\":[\"%a\",\"%a\",\"%a\",\"%a\",\"%a\",\"%a\"],\"count\":%" PRIu64 "}",
            f.xc,f.xs,f.cc,f.ss,f.cs,f.energy,f.count);
    }
    ~State() noexcept {
        if(!requested)return;
        if(path_error){std::fputs("CORRELATOR_TONE_TRACE_ERROR invalid absolute output path\n",stderr);return;}
        // Exclusive creation preserves previous captures. No hot-loop I/O.
        auto* out=std::fopen(path,"wbx");
        if(!out){std::fputs("CORRELATOR_TONE_TRACE_ERROR output open failed\n",stderr);return;}
        const auto reserved=next.load(std::memory_order_acquire);
        const auto retained=std::min(capacity,reserved);
        std::size_t pieces=0,bindings=0,unpublished=0;
        std::fprintf(out,"{\"type\":\"header\",\"schema\":1,\"capacity\":%zu,\"arena_bytes\":%zu,"
            "\"filter\":{\"tone\":true,\"sample_rate\":256,\"symbol_samples\":2080,\"carrier_hz\":\"0x1p+6\","
            "\"group\":0,\"bit\":1,\"index\":0,\"frequency\":2,\"observed_start\":12,"
            "\"origin_open_interval\":[11,12],\"rate_less_than\":1,\"block_first_less_than\":2095,\"observed_less_than\":2095},"
            "\"field_order\":[\"xc\",\"xs\",\"cc\",\"ss\",\"cs\",\"energy\"],"
            "\"polar_argument\":\"unobserved; reconstructable from observed,tau,tone,sample_rate; do not assume recomputation is actual execution\","
            "\"ordering\":\"join by owner, lane identity, observed, until; reservation order is not thread order\"}\n",capacity,sizeof(State));
        for(std::size_t i=0;i<retained;++i) {
            if(!slots[i].ready.load(std::memory_order_acquire)){++unpublished;continue;}
            const auto& r=slots[i].value;const auto& c=r.context;
            if(c.kind==2) {
                ++bindings;
                std::fprintf(out,"{\"type\":\"batch_binding\",\"slot\":%zu,\"identity\":\"0x%" PRIxPTR "\","
                    "\"owner\":\"0x%" PRIxPTR "\",\"batch_first\":%" PRIu64 ",\"batch_end\":%" PRIu64 "}\n",
                    i,c.identity,c.owner,c.batch_first,c.batch_end);
                continue;
            }
            ++pieces;
            std::fprintf(out,"{\"type\":\"piece\",\"slot\":%zu,\"source\":\"%s\",\"identity\":\"0x%" PRIxPTR "\","
                "\"owner\":\"0x%" PRIxPTR "\",\"batch_first\":%" PRIu64 ",\"batch_end\":%" PRIu64 ","
                "\"epoch\":%" PRIu64 ",\"symbol_samples\":%" PRIu64 ",\"chip_samples\":%" PRIu64 ",\"index\":%" PRIu64 ","
                "\"phase_lower\":%" PRIu64 ",\"phase_upper\":%" PRIu64 ",\"block_first\":%" PRIu64 ",\"block_count\":%" PRIu64 ","
                "\"observed_start\":%" PRIu64 ",\"observed\":%" PRIu64 ",\"until\":%" PRIu64 ",\"chip\":%" PRIu64 ","
                "\"frequency\":%zu,\"rate_index\":%zu,\"tone_bank_base\":%zu,\"bank\":%zu,\"group\":%zu,\"bit\":%u,\"sample_rate\":%u,"
                "\"carrier_hz\":\"%a\",\"frequency_offset\":\"%a\",\"bank_frequency\":\"%a\","
                "\"origin\":\"%La\",\"rate\":\"%La\",\"within\":\"%La\",\"fraction\":\"%a\",\"tone\":\"%a\","
                "\"raw_phase\":[\"%a\",\"%a\"],\"corrected_phase\":[\"%a\",\"%a\"],\"rounding\":%d,\"projection\":",
                i,c.kind==0?"impl":"batch",c.identity,c.owner,c.batch_first,c.batch_end,
                c.epoch,c.symbol_samples,c.chip_samples,c.index,c.phase_lower,c.phase_upper,c.block_first,c.block_count,
                c.observed_start,c.observed,c.until,c.chip,c.frequency,c.rate_index,c.tone_bank_base,c.bank,c.group,c.bit,c.sample_rate,
                c.carrier_hz,c.frequency_offset,c.bank_frequency,c.origin,c.rate,r.within,r.fraction,r.tone,
                r.raw_phase.real(),r.raw_phase.imag(),r.corrected_phase.real(),r.corrected_phase.imag(),r.rounding);
            projection(out,r.projection);std::fputs(",\"fit_before\":",out);fit(out,r.before);
            std::fputs(",\"fit_after\":",out);fit(out,r.after);std::fputs("}\n",out);
        }
        const auto lost=dropped.load(std::memory_order_acquire);
        const bool write_error=std::ferror(out)!=0;
        const bool complete=pieces!=0 && lost==0 && unpublished==0 && !write_error;
        std::fprintf(out,"{\"type\":\"seal\",\"reserved\":%zu,\"pieces\":%zu,\"bindings\":%zu,\"dropped\":%zu,"
            "\"unpublished\":%zu,\"error\":%s,\"complete\":%s,\"zero_matching_inconclusive\":%s}\n",
            reserved,pieces,bindings,lost,unpublished,write_error?"true":"false",complete?"true":"false",pieces?"false":"true");
        const bool flush_error=std::fflush(out)!=0;
        const bool close_error=std::fclose(out)!=0;
        if(write_error || flush_error || close_error)
            std::fputs("CORRELATOR_TONE_TRACE_ERROR output write/flush/close failed; disregard seal completeness\n",stderr);
    }
};
static_assert(sizeof(State)<=2*1024*1024,"diagnostic arena exceeds 2 MiB");
// One inline instance is shared across both translation units, including a zero-match exit.
inline State global_state;
inline State& state() noexcept {return global_state;}
inline bool selected(bool tone,std::uint32_t sample_rate,std::uint64_t symbol_samples,double carrier_hz,
        std::size_t group,unsigned bit,std::uint64_t index,std::size_t frequency,
        std::uint64_t observed_start,long double origin,long double rate,
        std::uint64_t block_first,std::uint64_t observed) noexcept {
    return tone && sample_rate==256 && symbol_samples==2080 && carrier_hz==64 && group==0 && bit==1 &&
        index==0 && frequency==2 && observed_start==12 && origin>11 && origin<12 && rate<1 &&
        block_first<2095 && observed<2095 && state().enabled;
}
inline void piece(const Context& context,long double within,double fraction,double tone,
        std::complex<double> raw_phase,std::complex<double> corrected_phase,
        const CorrelationProjection& projection,const CorrelationFit& before,const CorrelationFit& after) noexcept {
    Record r;r.context=context;r.within=within;r.fraction=fraction;r.tone=tone;
    r.raw_phase=raw_phase;r.corrected_phase=corrected_phase;r.projection=projection;r.before=before;r.after=after;
    r.rounding=std::fegetround();state().append(r);
}
inline void bind_batch(const void* owner,const CorrelationBatch& batch) noexcept {
    const auto& g=batch.geometry;
    if(!g.tone || g.sample_rate!=256 || g.symbol_samples!=2080 || g.carrier_hz!=64 ||
       batch.blocks.empty() || batch.blocks.front().sample>=2095 || !state().enabled)return;
    Record r;r.context.kind=2;r.context.identity=reinterpret_cast<std::uintptr_t>(batch.blocks.data());
    r.context.owner=reinterpret_cast<std::uintptr_t>(owner);r.context.batch_first=batch.blocks.front().sample;
    r.context.batch_end=batch.blocks.back().sample+batch.blocks.back().count;state().append(r);
}
} // namespace datapump::modem::detail::correlator_tone_numeric

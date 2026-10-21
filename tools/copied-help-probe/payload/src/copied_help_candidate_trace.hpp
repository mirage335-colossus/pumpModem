#pragma once

// Opt-in diagnostic overlay for copied Help candidate/arbitration tracing.
// This header is included only by the private copied-source overlay.
#include <array>
#include <atomic>
#include <bit>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace datapump::diagnostic::copied_help_candidate {
inline constexpr std::size_t capacity = 4096;
inline constexpr std::size_t raw_prefix_capacity = 256;
inline constexpr std::size_t chunk_capacity = 64;

struct Record {
    std::uint64_t ordinal=0;
    std::uint64_t generation=0, input_revision=0, captured_samples=0;
    std::uint64_t bank_identity=0, receiver_identity=0, config_identity=0, epoch=0, family=0, sample_origin=0;
    std::uint64_t candidate_id=0, prior_signal_id=0, signal_id=0, winner_revision=0;
    std::uint64_t physical_first=0, physical_end=0, symbol_samples=0, first_stream_symbol=0;
    std::uint64_t stream_phase_samples=0, observed_bits=0, missing_symbols=0;
    std::uint64_t raw_count=0, chunk_first_symbol=0, chunk_count=0, chunk_prefix_count=0, chunk_unknown_count=0;
    double score=0, support_samples=0;
    bool complete=false, content_validated=false, short_text_decoded=false;
    bool decision_selected=false, decision_limited=false, add_signal_called=false;
    bool add_signal_accepted=false;
    std::array<char,raw_prefix_capacity> raw_prefix{};
    std::array<char,chunk_capacity> chunk_bits{}; // '?': observed as unknown, never a guessed zero.
    std::array<std::uint64_t,64> superseded{};
    std::size_t superseded_count=0;
};
struct Slot { Record record{}; std::atomic<bool> ready{false}; };
struct State {
    std::array<Slot,capacity> slots{};
    std::atomic<std::size_t> next{0}, dropped{0}, candidates_seen{0}, capped_records{0}, missing_presentations{0};
    char path[4096]{};
    bool requested=false, enabled=false, path_error=false;
    State() noexcept {
        const auto* value=std::getenv("DATAPUMP_COPIED_HELP_CANDIDATE_TRACE");
        if(!value || !*value)return;
        requested=true;
        const auto size=std::strlen(value);
        if(value[0]!='/' || size>=sizeof(path)){path_error=true;return;}
        std::memcpy(path,value,size+1);enabled=true;
    }
    std::uint64_t candidate_seen() noexcept { return candidates_seen.fetch_add(1,std::memory_order_relaxed)+1; }
    void append(const Record& record) noexcept {
        const auto position=next.fetch_add(1,std::memory_order_relaxed);
        if(position>=capacity){dropped.fetch_add(1,std::memory_order_relaxed);return;}
        slots[position].record=record;
        slots[position].ready.store(true,std::memory_order_release);
    }
    ~State() noexcept {
        if(!requested)return;
        if(path_error){std::fputs("COPIED_HELP_CANDIDATE_TRACE_ERROR invalid absolute output path\n",stderr);return;}
        auto* out=std::fopen(path,"wbx");
        if(!out){std::fputs("COPIED_HELP_CANDIDATE_TRACE_ERROR output open failed\n",stderr);return;}
        const auto reserved=next.load(std::memory_order_acquire);
        const auto retained=reserved<capacity?reserved:capacity;
        std::size_t unpublished=0, written=0;
        const auto seen=candidates_seen.load(std::memory_order_acquire);
        const auto lost=dropped.load(std::memory_order_acquire);
        const auto missing=seen>reserved?seen-reserved:0;
        std::fprintf(out,"{\"type\":\"header\",\"schema\":1,\"capacity\":%zu,\"arena_bytes\":%zu,"
            "\"raw_prefix_capacity_bits\":%zu,\"chunk_capacity_bits\":%zu,"
            "\"raw_unknown_semantics\":\"raw_prefix preserves StreamReceiver placeholders with observed_bits/missing_symbols; chunk_bits uses '?' for unknown PatternBurst value 2\","
            "\"selection_semantics\":\"every selected and rejected observe result is recorded; add_signal result is from its one actual call\"}\n",
            capacity,sizeof(State),raw_prefix_capacity,chunk_capacity);
        for(std::size_t i=0;i<retained;++i) {
            if(!slots[i].ready.load(std::memory_order_acquire)){++unpublished;continue;}
            const auto& r=slots[i].record;++written;
            std::fprintf(out,"{\"type\":\"candidate\",\"slot\":%zu,\"ordinal\":%" PRIu64 ","
                "\"generation\":%" PRIu64 ",\"input_revision\":%" PRIu64 ",\"captured_samples\":%" PRIu64 ","
                "\"bank_identity\":\"0x%" PRIx64 "\",\"receiver_identity\":\"0x%" PRIx64 "\",\"config_identity\":\"0x%" PRIx64 "\","
                "\"epoch\":%" PRIu64 ",\"family\":%" PRIu64 ",\"sample_origin\":%" PRIu64 ","
                "\"candidate_id\":%" PRIu64 ",\"prior_signal_id\":%" PRIu64 ","
                "\"physical_first\":%" PRIu64 ",\"physical_end\":%" PRIu64 ","
                "\"symbol_samples\":%" PRIu64 ",\"first_stream_symbol\":%" PRIu64 ","
                "\"stream_phase_samples\":%" PRIu64 ",\"score\":\"%a\",\"support_samples\":\"%a\","
                "\"raw_prefix_count\":%" PRIu64 ",\"raw_prefix\":\"%.*s\","
                "\"observed_bits\":%" PRIu64 ",\"missing_symbols\":%" PRIu64 ","
                "\"complete\":%s,\"content_validated\":%s,\"short_text_decoded\":%s,"
                "\"chunk_first_symbol\":%" PRIu64 ",\"chunk_count\":%" PRIu64 ","
                "\"chunk_prefix_count\":%" PRIu64 ",\"chunk_unknown_count\":%" PRIu64 ",\"chunk_bits\":\"%.*s\","
                "\"selected\":%s,\"limited\":%s,\"winner_signal_id\":%" PRIu64 ","
                "\"winner_revision\":%" PRIu64 ",\"superseded_ids\":[",
                i,r.ordinal,r.generation,r.input_revision,r.captured_samples,r.bank_identity,r.receiver_identity,r.config_identity,
                r.epoch,r.family,r.sample_origin,r.candidate_id,r.prior_signal_id,r.physical_first,r.physical_end,
                r.symbol_samples,r.first_stream_symbol,r.stream_phase_samples,r.score,r.support_samples,
                r.raw_count,static_cast<int>(r.raw_count),r.raw_prefix.data(),r.observed_bits,r.missing_symbols,
                r.complete?"true":"false",r.content_validated?"true":"false",r.short_text_decoded?"true":"false",
                r.chunk_first_symbol,r.chunk_count,r.chunk_prefix_count,r.chunk_unknown_count,static_cast<int>(r.chunk_prefix_count),r.chunk_bits.data(),
                r.decision_selected?"true":"false",r.decision_limited?"true":"false",r.signal_id,r.winner_revision);
            for(std::size_t n=0;n<r.superseded_count;++n)
                std::fprintf(out,"%s%" PRIu64,n?",":"",r.superseded[n]);
            std::fprintf(out,"],\"add_signal_called\":%s,\"add_signal_accepted\":%s}\n",
                r.add_signal_called?"true":"false",r.add_signal_accepted?"true":"false");
        }
        const bool write_error=std::ferror(out)!=0;
        const auto capped=capped_records.load(std::memory_order_acquire);
        const auto missing_presentation=missing_presentations.load(std::memory_order_acquire);
        const bool complete=enabled && seen!=0 && written==seen && !lost && !missing && !capped && !missing_presentation && !unpublished && !write_error;
        std::fprintf(out,"{\"type\":\"seal\",\"candidates_seen\":%" PRIu64 ",\"reserved\":%zu,"
            "\"records\":%zu,\"dropped\":%zu,\"capped_records\":%zu,\"missing_publications\":%" PRIu64 ","
            "\"unpublished\":%zu,\"missing_presentations\":%zu,\"write_error\":%s,\"zero_candidates_inconclusive\":%s,\"complete\":%s}\n",
            seen,reserved,written,lost,capped,missing,unpublished,missing_presentation,write_error?"true":"false",seen?"false":"true",complete?"true":"false");
        const bool flush_error=std::fflush(out)!=0;
        const bool close_error=std::fclose(out)!=0;
        if(write_error||flush_error||close_error)
            std::fputs("COPIED_HELP_CANDIDATE_TRACE_ERROR output write/flush/close failed; disregard seal completeness\n",stderr);
    }
};
static_assert(sizeof(State)<=8*1024*1024,"copied Help candidate trace exceeds 8 MiB");
inline State global_state;
inline State& state() noexcept { return global_state; }
inline std::atomic<bool> armed{false};
inline void arm() noexcept { armed.store(true,std::memory_order_release); }
inline bool active() noexcept { return state().enabled && armed.load(std::memory_order_acquire); }

inline std::uint64_t config_identity(std::uint64_t epoch,std::uint64_t family,std::uint64_t sample_origin,
        std::uint64_t symbol_samples,std::uint32_t sample_rate,std::uint64_t stream_phase,
        std::uint64_t profile_flags,std::uint64_t key_tag_hash) noexcept {
    std::uint64_t h=1469598103934665603ULL;
    const std::uint64_t fields[]{epoch,family,sample_origin,symbol_samples,sample_rate,stream_phase,profile_flags,key_tag_hash};
    for(const auto field:fields)for(unsigned i=0;i<8;++i){h^=(field>>(i*8))&0xffU;h*=1099511628211ULL;}
    return h;
}
} // namespace datapump::diagnostic::copied_help_candidate

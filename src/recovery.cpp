#include "datapump/recovery.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/speculation.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <limits>
#include <mutex>
#include <thread>

namespace datapump::transfer {
namespace {
constexpr std::size_t coded_bits=stream_interval_bytes*8;
constexpr std::size_t cadence=boundary_sync::marker_bits+coded_bits;
constexpr std::size_t last_initial=boundary_sync::marker_bits+boundary_sync::maximum_slip_bits;
constexpr std::size_t batch_size=16;
constexpr std::size_t worker_scratch=255*256+16384;
using Clock=std::chrono::steady_clock;
unsigned ceil_log2(std::uint64_t n) { return n<=1?0:std::bit_width(n-1); }
bool terminal(RecoveryState s) {
    return s==RecoveryState::recovered || s==RecoveryState::exhausted ||
        s==RecoveryState::ambiguous || s==RecoveryState::unavailable || s==RecoveryState::none;
}
struct Packed {
    Bytes bytes=Bytes(stream_interval_bytes),masks=Bytes(stream_interval_bytes);
    std::vector<std::size_t> erased;
    unsigned missing=0;
};
Packed pack(const Bytes& bits,std::size_t start) {
    if(start>=bits.size())throw Error("Invalid recovery coding start");
    datapump_speculation_barrier();
    Packed out;
    for(std::size_t bit=0;bit<coded_bits;++bit) {
        const auto mask=static_cast<std::uint8_t>(1U<<(7-bit%8));
        const auto position=datapump_index_nospec(start+bit,bits.size());
        if(bit>=bits.size()-start || bits[position]==2) {
            if(!out.masks[bit/8])out.erased.push_back(bit/8);
            out.masks[bit/8]|=mask;++out.missing;
        } else if(bits[position])out.bytes[bit/8]|=mask;
    }
    return out;
}
// Recompute observations from the original capture, never from guessed bits.
void original_statistics(DecodedInterval& decoded,const Packed& original,const Bytes& corrected,std::size_t parity) {
    decoded.fec_stats={};decoded.corrected_bytes=0;decoded.erased_bytes=original.erased.size();
    for(std::size_t i=0;i<corrected.size();++i) {
        auto& region=i<decoded.data.size()?decoded.fec_stats.data:
            i<stream_interval_bytes-parity?decoded.fec_stats.integrity:decoded.fec_stats.parity;
        const auto missing=std::popcount(original.masks[i]);
        const auto changed=static_cast<std::uint8_t>(original.bytes[i]^corrected[i]);
        region.received_bits+=8-missing;region.missing_bits+=missing;
        region.corrected_bits+=std::popcount(static_cast<std::uint8_t>(changed&~original.masks[i]));
        region.corrected_bytes+=changed!=0;region.erased_bytes+=missing!=0;
        region.repaired_bytes+=changed!=0 || missing!=0;decoded.corrected_bytes+=changed!=0;
    }
    const auto& data=decoded.fec_stats.data;
    decoded.pre_fec_accuracy=StreamBitAccuracy{data.received_bits,data.corrected_bits,data.missing_bits};
}
}

struct RecoveryJob::Impl {
    struct Match { Bytes coded;DecodedInterval decoded; };
    struct Plan {
        std::size_t start=0;
        std::uint64_t trials=0;
        unsigned marker_evidence=0;
        unsigned guess_count=0;
        std::unique_ptr<Match> match;
        bool ambiguous=false;
    };
    RecoveryInput input;
    RecoveryOptions options;
    mutable std::mutex mutex;
    std::mutex run_mutex;
    RecoveryProgress status;
    std::vector<Plan> plans;
    std::vector<DecodedInterval> accepted;
    Bytes marker;
    std::size_t parity=0,prepare_cursor=0,next_plan=0;
    std::uint64_t next_assignment=0;
    std::optional<std::size_t> anchor;
    unsigned required_evidence=0;
    bool initialized=false,prepared=false;
    bool conflicting=false;
    std::optional<std::size_t> matched_phase;
    std::size_t bytes=0,workspace=0;
    unsigned worker_limit=0;
    Clock::time_point began{};
    std::chrono::milliseconds prior_elapsed{0};

    Impl(RecoveryInput value,RecoveryOptions settings):input(std::move(value)),options(settings) {
        status.state=options.enabled?RecoveryState::ready:RecoveryState::none;
        bytes=sizeof(*this)+input.bits.capacity()+input.established_starts.capacity()*sizeof(std::uint64_t);
        const auto count=input.established_starts.empty()?input.bits.size():input.bits.size()/cadence+1;
        const auto matches=input.bits.size()/cadence+1;
        const auto per_match=sizeof(Match)+256+2*sizeof(DecodedInterval)+256;
        const auto maximum=std::numeric_limits<std::size_t>::max();
        if(bytes>maximum-2048)return;
        if(count>(maximum-bytes-2048)/(2*sizeof(Plan)))return;
        auto base=bytes+2048+count*2*sizeof(Plan);
        if(matches>(maximum-base)/per_match)return;
        base+=matches*per_match;
        if(base>options.workspace_bytes)return;
        const auto available=std::max(1U,std::thread::hardware_concurrency());
        const auto requested=std::max(1U,std::min(available,options.workers?options.workers:available));
        worker_limit=static_cast<unsigned>(std::min<std::size_t>(requested,(options.workspace_bytes-base)/worker_scratch));
        if(!worker_limit)return;
        workspace=base+worker_limit*worker_scratch;
    }
    void set_state(RecoveryState s) {std::lock_guard lock(mutex);status.state=s;}
    bool initialize() {
        if(!worker_limit || input.bits.empty() || input.bits.size()>options.retained_bits || !input.interval_options ||
           options.budget.count()<0 || input.first_symbol>std::numeric_limits<std::uint64_t>::max()-input.bits.size() ||
           std::any_of(input.bits.begin(),input.bits.end(),[](auto b){return b>2;}))return false;
        datapump_speculation_barrier();
        parity=interval_parity_bytes(input.fec);
        if(!parity)return false;
        if(input.bits.size()>std::numeric_limits<std::uint64_t>::max()/(boundary_sync::marker_bits+1))return false;
        // Charge every retained start and every possible marker suffix. Unknown
        // assignments are already covered by projecting codewords onto the
        // original known bits; they never acquire independent evidence.
        required_evidence=128+ceil_log2(input.bits.size()*(boundary_sync::marker_bits+1));
        if(!input.established_starts.empty()) {
            const auto first=*std::min_element(input.established_starts.begin(),input.established_starts.end());
            if(first<input.first_symbol)return false;
            const auto offset=first-input.first_symbol;
            if(offset>=input.bits.size())return false;
            anchor=static_cast<std::size_t>(offset%cadence);
            // A longer leading fragment can be the end of an earlier payload.
            // Do not turn it into an apparently complete later source.
            if(*anchor>last_initial)return false;
            for(const auto start:input.established_starts)
                if(start<input.first_symbol || start-input.first_symbol>=input.bits.size() ||
                   (start-input.first_symbol)%cadence!=*anchor)return false;
            prepare_cursor=*anchor;
        }
        marker=boundary_sync::insert(Bytes(coded_bits));marker.resize(boundary_sync::marker_bits);
        {
            std::lock_guard lock(mutex);bytes+=marker.capacity();
            // Allocate the complete, validated plan table once. Geometric
            // growth can transiently retain both the old and new allocations.
            const auto count=anchor?(input.bits.size()-1-*anchor)/cadence+1:input.bits.size();
            plans.reserve(count);bytes+=plans.capacity()*sizeof(Plan);
        }
        initialized=true;return true;
    }
    unsigned marker_support(std::size_t start) const {
        unsigned observed=0;
        for(std::size_t i=0;i<std::min(start,marker.size());++i) {
            const auto bit=input.bits[start-i-1];
            if(bit==2)continue;
            if(bit!=marker[marker.size()-i-1])break;
            ++observed;
        }
        return observed;
    }
    std::vector<std::uint16_t> guessed_positions(const Packed& original) const {
        const auto reserved=std::min<std::size_t>(parity,2ULL*options.extra_errors);
        const auto remaining=parity-reserved;
        auto sorted=original.erased;
        std::stable_sort(sorted.begin(),sorted.end(),[&](auto a,auto b) {
            return std::popcount(original.masks[a])<std::popcount(original.masks[b]);
        });
        const auto guessed_bytes=sorted.size()>remaining?sorted.size()-remaining:0;
        std::vector<std::uint16_t> guesses;
        for(std::size_t j=0;j<guessed_bytes;++j)for(unsigned bit=0;bit<8;++bit)
            if(original.masks[sorted[j]]&(1U<<(7-bit)))
                guesses.push_back(static_cast<std::uint16_t>(sorted[j]*8+bit));
        return guesses;
    }
    bool add_plan(std::size_t start) {
        auto original=pack(input.bits,start);
        Plan plan;plan.start=start;plan.marker_evidence=marker_support(start);
        const auto maximum=static_cast<int>(plan.marker_evidence+8*parity)-static_cast<int>(original.missing);
        if(maximum>=static_cast<int>(required_evidence)) {
            const auto guesses=guessed_positions(original);
            if(guesses.size()>=64)return false;
            plan.guess_count=static_cast<unsigned>(guesses.size());
            plan.trials=std::uint64_t{1}<<plan.guess_count;
        }
        std::lock_guard lock(mutex);
        if(plan.trials>std::numeric_limits<std::uint64_t>::max()-status.total)return false;
        const auto old_capacity=plans.capacity();
        status.total+=plan.trials;plans.push_back(std::move(plan));
        bytes+=(plans.capacity()-old_capacity)*sizeof(Plan);
        return true;
    }
    bool prepare(std::stop_token stop,Clock::time_point deadline) {
        if(!initialized && !initialize()){set_state(RecoveryState::unavailable);return false;}
        while(prepare_cursor<input.bits.size()) {
            if(stop.stop_requested() || Clock::now()>=deadline)return false;
            if(!add_plan(prepare_cursor)){set_state(RecoveryState::unavailable);return false;}
            prepare_cursor+=anchor?cadence:1;
        }
        prepared=true;return true;
    }
    std::optional<Match> evaluate(const Plan& plan,const Packed& original,const IntervalOptions& configured,
                                  std::span<const std::uint16_t> guesses,
                                  std::uint64_t assignment) const {
        auto word=original.bytes,masks=original.masks;
        for(std::size_t i=0;i<guesses.size();++i) {
            const auto bit=guesses[i];const auto byte=bit/8;
            const auto mask=static_cast<std::uint8_t>(1U<<(7-bit%8));
            masks[byte]&=static_cast<std::uint8_t>(~mask);
            if((assignment>>i)&1U)word[byte]|=mask;
        }
        std::vector<std::size_t> erasures;
        for(std::size_t i=0;i<masks.size();++i)if(masks[i])erasures.push_back(i);
        try {
            // Use the unchanged RS primitive, then the unchanged interval
            // verifier. Retain the corrected full word to check original known
            // bits inside partially missing bytes and deduplicate hypotheses.
            fec::rs_correct(word,parity,erasures);
            unsigned errors=0;
            for(std::size_t i=0;i<word.size();++i) {
                const auto changed=static_cast<std::uint8_t>(word[i]^original.bytes[i]);
                if(original.masks[i]) {
                    if(changed&static_cast<std::uint8_t>(~original.masks[i]))return {};
                } else errors+=changed!=0;
            }
            const auto evidence=static_cast<int>(plan.marker_evidence+8*parity)-
                static_cast<int>(original.missing+15*errors+ceil_log2(errors+1));
            if(evidence<static_cast<int>(required_evidence))return {};
            auto decoded=decode_interval(word,configured);
            original_statistics(decoded,original,word,parity);
            return Match{std::move(word),std::move(decoded)};
        } catch(const Error&) {return {};}
    }
    void merge(Plan& plan,Match match) {
        std::lock_guard lock(mutex);
        if(conflicting)return;
        if(!anchor) {
            const auto phase=plan.start%cadence;
            if(matched_phase && *matched_phase!=phase){conflicting=true;return;}
            matched_phase=phase;
        }
        if(plan.match) {
            if(plan.match->coded!=match.coded){plan.ambiguous=true;conflicting=true;}
        } else {
            bytes+=sizeof(Match)+match.coded.capacity()+match.decoded.data.capacity();
            plan.match=std::make_unique<Match>(std::move(match));
        }
    }
    void finish_search() {
        std::lock_guard lock(mutex);
        if(conflicting || std::any_of(plans.begin(),plans.end(),[](const auto& p){return p.ambiguous;})) {
            status.state=RecoveryState::ambiguous;return;
        }
        const auto reserve_source=[&](std::size_t count) {
            accepted.reserve(count);bytes+=accepted.capacity()*sizeof(DecodedInterval);
        };
        const auto append_source=[&](const DecodedInterval& decoded) {
            accepted.push_back(decoded);bytes+=accepted.back().data.capacity();
        };
        if(anchor) {
            for(auto& plan:plans) {
                if(!plan.match){status.state=RecoveryState::exhausted;return;}
            }
            reserve_source(plans.size());
            for(auto& plan:plans)append_source(plan.match->decoded);
        } else {
            std::optional<std::size_t> phase;
            for(const auto& plan:plans)if(plan.match) {
                const auto current=plan.start%cadence;
                if(phase && *phase!=current){status.state=RecoveryState::ambiguous;return;}
                phase=current;
            }
            if(!phase || *phase>last_initial){status.state=RecoveryState::exhausted;return;}
            // All retained starts have one plan, including impossible ones.
            // A source can only start within the supported leading geometry.
            for(auto start=*phase;start<input.bits.size();start+=cadence) {
                const auto& plan=plans[start];
                if(!plan.match){status.state=RecoveryState::exhausted;return;}
            }
            reserve_source((input.bits.size()-1-*phase)/cadence+1);
            for(auto start=*phase;start<input.bits.size();start+=cadence)
                append_source(plans[start].match->decoded);
        }
        status.state=RecoveryState::recovered;
    }
    void run(std::stop_token stop) {
        std::unique_lock serial(run_mutex,std::try_to_lock);
        if(!serial.owns_lock())return;
        {
            std::lock_guard lock(mutex);
            if(terminal(status.state))return;
            prior_elapsed=status.elapsed;began=Clock::now();status.state=RecoveryState::running;
        }
        try {
            // Check conversion and time-point addition separately: an API
            // caller can supply a millisecond budget much wider than the
            // steady clock's native duration representation.
            if(options.budget.count()<0 || options.budget>
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::duration::max()))
                throw Error("Recovery deadline exceeds clock range");
            const auto duration=std::chrono::duration_cast<Clock::duration>(options.budget);
            if(began>Clock::time_point::max()-duration)
                throw Error("Recovery deadline exceeds clock range");
            const auto deadline=began+duration;
            if(!prepared)prepare(stop,deadline);
            if(prepared) {
                std::uint64_t batches=0;
                for(std::size_t i=next_plan;i<plans.size();++i) {
                    const auto remaining=plans[i].trials-(i==next_plan?next_assignment:0);
                    batches+=remaining/batch_size+(remaining%batch_size!=0);
                }
                const auto workers=static_cast<unsigned>(std::min<std::uint64_t>(worker_limit,std::max<std::uint64_t>(1,batches)));
                std::atomic<bool> failed{false};
                {std::lock_guard lock(mutex);bytes+=workers*worker_scratch;}
                auto work=[&] {
                    try {
                        while(!failed.load(std::memory_order_relaxed) && !stop.stop_requested() && Clock::now()<deadline) {
                            std::size_t index=0;std::uint64_t first=0,count=0;
                            {
                                std::lock_guard lock(mutex);
                                if(conflicting)return;
                                while(next_plan<plans.size() && next_assignment==plans[next_plan].trials) {
                                    ++next_plan;next_assignment=0;
                                }
                                if(next_plan==plans.size())return;
                                index=next_plan;first=next_assignment;
                                count=std::min<std::uint64_t>(batch_size,plans[index].trials-first);
                                next_assignment+=count;
                            }
                            auto& plan=plans[index];
                            const auto original=pack(input.bits,plan.start);
                            const auto guesses=guessed_positions(original);
                            const auto configured=input.interval_options(input.first_symbol+plan.start);
                            if(configured.fec!=input.fec)throw Error("Recovery FEC profile mismatch");
                            for(std::uint64_t i=0;i<count;++i)
                                if(auto match=evaluate(plan,original,configured,guesses,first+i))merge(plan,std::move(*match));
                            {std::lock_guard lock(mutex);status.attempts+=count;}
                        }
                    } catch(...) {failed.store(true,std::memory_order_relaxed);}
                };
                {
                    std::vector<std::jthread> threads;threads.reserve(workers);
                    try {for(unsigned i=0;i<workers;++i)threads.emplace_back(work);}
                    catch(...) {failed.store(true,std::memory_order_relaxed);}
                    for(auto& thread:threads)thread.join();
                }
                {std::lock_guard lock(mutex);bytes-=workers*worker_scratch;}
                if(failed.load(std::memory_order_relaxed))set_state(RecoveryState::unavailable);
                else {
                    bool complete;
                    {std::lock_guard lock(mutex);complete=conflicting || status.attempts==status.total;}
                    if(complete)finish_search();
                }
            }
        } catch(...) {set_state(RecoveryState::unavailable);}
        std::lock_guard lock(mutex);
        status.elapsed=prior_elapsed+std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-began);
        if(status.state==RecoveryState::running)
            status.state=stop.stop_requested()?RecoveryState::cancelled:RecoveryState::incomplete;
    }
};

RecoveryJob::RecoveryJob(RecoveryInput input,RecoveryOptions options):impl_(std::make_unique<Impl>(std::move(input),options)) {}
RecoveryJob::~RecoveryJob()=default;
RecoveryProgress RecoveryJob::progress() const {
    std::lock_guard lock(impl_->mutex);auto result=impl_->status;
    if(result.state==RecoveryState::running)
        result.elapsed=impl_->prior_elapsed+std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-impl_->began);
    return result;
}
void RecoveryJob::run(std::stop_token stop) {impl_->run(stop);}
std::vector<DecodedInterval> RecoveryJob::result() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->status.state==RecoveryState::recovered?impl_->accepted:std::vector<DecodedInterval>{};
}
std::size_t RecoveryJob::working_bytes() const {std::lock_guard lock(impl_->mutex);return impl_->bytes;}
std::size_t RecoveryJob::workspace_bound() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->workspace?impl_->workspace:std::max(impl_->bytes,impl_->options.workspace_bytes);
}
}

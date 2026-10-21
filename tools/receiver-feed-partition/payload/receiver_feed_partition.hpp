#pragma once
// Private diagnostic overlay. One source-thread writer; seal only after Controller joins.
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace datapump::receiver_feed_partition {
enum class Owner : unsigned {
    bank_admission, bank_owner_prepare, bank_feed, bank_pending, bank_drain,
    bank_publish, input_binning, process_residual, input_fft_prepare, fft_score,
    track_continue, candidate_admit, candidate_publish, count
};
inline constexpr std::size_t owner_count=static_cast<std::size_t>(Owner::count);
inline constexpr std::size_t max_blocks=512,max_receivers=32;
inline constexpr const char* names[owner_count]={
    "bank_admission","bank_owner_prepare","bank_feed","bank_pending","bank_drain",
    "bank_publish","input_binning","process_residual","input_fft_prepare","fft_score",
    "track_continue","candidate_admit","candidate_publish"};
struct Stage {std::uint64_t calls=0,wall_ns=0,thread_cpu_ns=0;};
struct OwnerMeta {
    std::uint64_t receiver_index=0,epoch=0,family=0,sample_origin=0,spreading_factor=0,chip_samples=0;
    std::uint64_t frontier_before=0;bool frontier_before_available=false;
};
struct OwnerRow {
    OwnerMeta meta{};bool initialized=false,frontier_after_available=false;
    std::uint64_t frontier_after=0;
    std::array<Stage,owner_count> stages{};
};
struct BlockRow {
    std::uint64_t serial=0,tid=0,input_samples=0,generated_samples=0,source_generated_samples=0,transmitted_samples=0;
    std::uint64_t bank_samples_before=0,bank_samples_after=0,receiver_count_before=0,receiver_count_after=0;
    std::uint64_t begin_wall_ns=0,end_wall_ns=0,begin_thread_cpu_ns=0,end_thread_cpu_ns=0;
    std::uint64_t total_wall_ns=0,total_thread_cpu_ns=0,residual_wall_ns=0,residual_thread_cpu_ns=0;
    std::array<Stage,owner_count> stages{};
    std::array<OwnerRow,max_receivers> owners{};
    std::size_t owner_rows=0;bool closed=false,feed_finished=false;
};
struct State {
    std::array<BlockRow,max_blocks> blocks{};
    std::size_t count=0;
    std::uint64_t dropped_blocks=0,dropped_owner_rows=0,clock_failures=0,closure_failures=0,accounting_failures=0,identity_changes=0;
    std::uint64_t cpu_clock_reads=0,wall_clock_reads=0,scope_calls=0,active_scopes=0,active_blocks=0;
    bool sealed=false;
};
inline State state{};
static_assert(sizeof(State)<=16*1024*1024,"diagnostic recorder exceeds fixed storage bound");
struct Active {BlockRow* row=nullptr;const void* token=nullptr;int stage=-1,slot=-1;std::uint64_t child_cpu=0,child_wall=0;};
inline thread_local Active active{};
inline bool recording() noexcept {return active.row!=nullptr;}
inline std::uint64_t cpu_raw(bool& ok) noexcept {
    timespec t{};ok=clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)==0;
    return ok?std::uint64_t(t.tv_sec)*1000000000ULL+std::uint64_t(t.tv_nsec):0;
}
inline std::uint64_t cpu_now() noexcept {
    ++state.cpu_clock_reads;bool ok=false;const auto value=cpu_raw(ok);if(!ok)++state.clock_failures;return value;
}
inline std::uint64_t wall_raw() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline std::uint64_t wall_now() noexcept {++state.wall_clock_reads;return wall_raw();}
inline std::uint64_t delta(std::uint64_t end,std::uint64_t begin) noexcept {
    if(end<begin){++state.clock_failures;return 0;}return end-begin;
}
inline std::uint64_t exclusive(std::uint64_t total,std::uint64_t children) noexcept {
    if(children>total){++state.accounting_failures;return 0;}return total-children;
}
inline void bind_owner(int slot,const OwnerMeta& meta) noexcept {
    if(!recording())return;
    if(slot<0 || std::size_t(slot)>=max_receivers){++state.dropped_owner_rows;return;}
    auto& row=active.row->owners[std::size_t(slot)];
    if(!row.initialized){row.meta=meta;row.initialized=true;++active.row->owner_rows;}
    else if(row.meta.epoch!=meta.epoch || row.meta.family!=meta.family || row.meta.sample_origin!=meta.sample_origin ||
            row.meta.spreading_factor!=meta.spreading_factor || row.meta.chip_samples!=meta.chip_samples)++state.identity_changes;
}
inline void frontier_after(int slot,bool available,std::uint64_t value) noexcept {
    if(!recording() || slot<0 || std::size_t(slot)>=max_receivers)return;
    auto& row=active.row->owners[std::size_t(slot)];row.frontier_after_available=available;row.frontier_after=value;
}
class Block {
    BlockRow* row_=nullptr;Active prior_{};int exceptions_=std::uncaught_exceptions();
public:
    Block(bool enabled,std::uint64_t serial,std::uint64_t input,std::uint64_t generated,std::uint64_t source_generated,
          std::uint64_t transmitted,std::uint64_t bank_samples,std::uint64_t receivers) noexcept {
        if(!enabled)return;
        if(state.sealed || recording()){++state.closure_failures;return;}
        if(state.count==max_blocks){++state.dropped_blocks;return;}
        prior_=active;row_=&state.blocks[state.count++];auto& r=*row_;
        r.serial=serial;r.input_samples=input;r.generated_samples=generated;r.source_generated_samples=source_generated;
        r.transmitted_samples=transmitted;r.bank_samples_before=bank_samples;r.receiver_count_before=receivers;
        static thread_local const auto tid=std::uint64_t(syscall(SYS_gettid));r.tid=tid;
        active={row_,this,-1,-1,0,0};++state.active_blocks;
        r.begin_thread_cpu_ns=cpu_now();r.begin_wall_ns=wall_now();
    }
    void finish(std::uint64_t bank_samples,std::uint64_t receivers) noexcept {
        if(row_){row_->bank_samples_after=bank_samples;row_->receiver_count_after=receivers;row_->feed_finished=true;}
    }
    ~Block() noexcept {
        if(!row_)return;
        if(active.row!=row_ || active.token!=this || state.active_scopes){++state.closure_failures;}
        row_->end_thread_cpu_ns=cpu_now();row_->end_wall_ns=wall_now();
        std::uint64_t accounted_cpu=0,accounted_wall=0;
        for(const auto& s:row_->stages){accounted_cpu+=s.thread_cpu_ns;accounted_wall+=s.wall_ns;}
        row_->total_thread_cpu_ns=delta(row_->end_thread_cpu_ns,row_->begin_thread_cpu_ns);row_->total_wall_ns=delta(row_->end_wall_ns,row_->begin_wall_ns);
        row_->residual_thread_cpu_ns=exclusive(row_->total_thread_cpu_ns,accounted_cpu);
        row_->residual_wall_ns=exclusive(row_->total_wall_ns,accounted_wall);
        row_->closed=true;if(!row_->feed_finished || std::uncaught_exceptions()>exceptions_)++state.closure_failures;
        --state.active_blocks;active=prior_;
    }
    Block(const Block&)=delete;Block& operator=(const Block&)=delete;
};
class Scope {
    Active parent_{};std::uint64_t cpu_=0,wall_=0;std::size_t stage_=0;int slot_=-1;bool enabled_=false;
public:
    explicit Scope(Owner stage,int slot=-2,const OwnerMeta* meta=nullptr,bool enabled=true) noexcept {
        if(!enabled || !recording())return;
        stage_=std::size_t(stage);if(stage_>=owner_count){++state.accounting_failures;return;}
        parent_=active;slot_=slot==-2?parent_.slot:slot;if(meta)bind_owner(slot_,*meta);
        cpu_=cpu_now();wall_=wall_now();enabled_=true;++state.active_scopes;++state.scope_calls;
        active={parent_.row,this,int(stage_),slot_,0,0};
    }
    ~Scope() noexcept {
        if(!enabled_)return;
        const auto cpu=cpu_now(),wall=wall_now();
        if(active.row!=parent_.row || active.token!=this){++state.closure_failures;active=parent_;--state.active_scopes;return;}
        const auto total_cpu=delta(cpu,cpu_),total_wall=delta(wall,wall_);
        const auto own_cpu=exclusive(total_cpu,active.child_cpu),own_wall=exclusive(total_wall,active.child_wall);
        auto& stage=parent_.row->stages[stage_];++stage.calls;stage.thread_cpu_ns+=own_cpu;stage.wall_ns+=own_wall;
        if(slot_>=0 && std::size_t(slot_)<max_receivers){auto& s=parent_.row->owners[std::size_t(slot_)].stages[stage_];++s.calls;s.thread_cpu_ns+=own_cpu;s.wall_ns+=own_wall;}
        active=parent_;if(active.stage>=0){active.child_cpu+=total_cpu;active.child_wall+=total_wall;}--state.active_scopes;
    }
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
// Caller must have joined every Controller/Session worker. No concurrent recording is allowed.
inline bool seal(const std::string& directory) noexcept {
    if(state.sealed)return false;state.sealed=true;
    try {
        const auto path=std::filesystem::path(directory);
        if(std::filesystem::exists(path/"receiver-feed.jsonl") || std::filesystem::exists(path/"receiver-feed-summary.json"))return false;
        constexpr std::uint64_t trials=4096;
        volatile std::uint64_t sink=0;bool ok0=false,ok1=false;
        const auto bw=wall_raw(),bc=cpu_raw(ok0);for(std::uint64_t i=0;i<trials;++i)sink=i;
        const auto baseline_cpu=delta(cpu_raw(ok1),bc),baseline_wall=delta(wall_raw(),bw);
        bool calibration_ok=ok0&&ok1;
        const auto cw=wall_raw(),cc=cpu_raw(ok0);calibration_ok&=ok0;
        for(std::uint64_t i=0;i<trials;++i){bool ok=false;const auto c=cpu_raw(ok),w=wall_raw();sink=c^w;calibration_ok&=ok;}
        const auto calibration_cpu=delta(cpu_raw(ok1),cc),calibration_wall=delta(wall_raw(),cw);calibration_ok&=ok1;(void)sink;
        if(!calibration_ok)++state.clock_failures;
        std::size_t owners=0,empty_blocks=0,unclosed_blocks=0;
        std::ofstream out(path/"receiver-feed.jsonl");
        const auto stages=[&](const auto& values,std::size_t block,int slot){
            for(std::size_t i=0;i<owner_count;++i){const auto& s=values[i];if(!s.calls)continue;
                out<<"{\"type\":\""<<(slot<0?"stage":"receiver_stage")<<"\",\"block\":"<<block<<",\"receiver_index\":"<<slot
                   <<",\"stage\":\""<<names[i]<<"\",\"calls\":"<<s.calls<<",\"wall_ns\":"<<s.wall_ns<<",\"thread_cpu_ns\":"<<s.thread_cpu_ns<<"}\n";
            }
        };
        for(std::size_t b=0;b<state.count;++b){const auto& r=state.blocks[b];owners+=r.owner_rows;empty_blocks+=!r.owner_rows;unclosed_blocks+=!r.closed;
            out<<"{\"type\":\"block\",\"index\":"<<b<<",\"serial\":"<<r.serial<<",\"tid\":"<<r.tid
               <<",\"begin_wall_ns\":"<<r.begin_wall_ns<<",\"end_wall_ns\":"<<r.end_wall_ns
               <<",\"begin_thread_cpu_ns\":"<<r.begin_thread_cpu_ns<<",\"end_thread_cpu_ns\":"<<r.end_thread_cpu_ns
               <<",\"input_samples\":"<<r.input_samples<<",\"generated_samples\":"<<r.generated_samples
               <<",\"source_generated_samples\":"<<r.source_generated_samples<<",\"transmitted_samples\":"<<r.transmitted_samples
               <<",\"bank_samples_before\":"<<r.bank_samples_before<<",\"bank_samples_after\":"<<r.bank_samples_after
               <<",\"receiver_count_before\":"<<r.receiver_count_before<<",\"receiver_count_after\":"<<r.receiver_count_after
               <<",\"total_wall_ns\":"<<r.total_wall_ns<<",\"total_thread_cpu_ns\":"<<r.total_thread_cpu_ns
               <<",\"residual_wall_ns\":"<<r.residual_wall_ns<<",\"residual_thread_cpu_ns\":"<<r.residual_thread_cpu_ns
               <<",\"owner_rows\":"<<r.owner_rows<<",\"closed\":"<<(r.closed?"true":"false")<<",\"feed_finished\":"<<(r.feed_finished?"true":"false")<<"}\n";
            stages(r.stages,b,-1);
            for(std::size_t i=0;i<max_receivers;++i){const auto& o=r.owners[i];if(!o.initialized)continue;const auto& m=o.meta;
                out<<"{\"type\":\"receiver\",\"block\":"<<b<<",\"receiver_index\":"<<m.receiver_index<<",\"epoch\":"<<m.epoch
                   <<",\"family\":"<<m.family<<",\"sample_origin\":"<<m.sample_origin<<",\"spreading_factor\":"<<m.spreading_factor<<",\"chip_samples\":"<<m.chip_samples
                   <<",\"frontier_before_available\":"<<(m.frontier_before_available?"true":"false")<<",\"frontier_before\":"<<m.frontier_before
                   <<",\"frontier_after_available\":"<<(o.frontier_after_available?"true":"false")<<",\"frontier_after\":"<<o.frontier_after
                   <<",\"local_input_samples_available\":false}\n";
                stages(o.stages,b,int(i));
            }
        }
        out.close();const bool events_ok=bool(out);
        const bool complete=state.count && owners && !empty_blocks && !unclosed_blocks && !state.active_blocks && !state.active_scopes &&
            !state.dropped_blocks && !state.dropped_owner_rows && !state.clock_failures && !state.closure_failures &&
            !state.accounting_failures && !state.identity_changes && calibration_cpu && events_ok;
        std::ofstream summary(path/"receiver-feed-summary.json");
        summary<<"{\"schema\":1,\"blocks\":"<<state.count<<",\"max_blocks\":"<<max_blocks<<",\"max_receivers\":"<<max_receivers<<",\"fixed_storage_bytes\":"<<sizeof(State)
               <<",\"owner_rows\":"<<owners<<",\"empty_blocks\":"<<empty_blocks<<",\"unclosed_blocks\":"<<unclosed_blocks
               <<",\"dropped_blocks\":"<<state.dropped_blocks<<",\"dropped_owner_rows\":"<<state.dropped_owner_rows
               <<",\"clock_failures\":"<<state.clock_failures<<",\"closure_failures\":"<<state.closure_failures
               <<",\"accounting_failures\":"<<state.accounting_failures<<",\"identity_changes\":"<<state.identity_changes
               <<",\"unclosed_scopes\":"<<state.active_scopes<<",\"active_blocks\":"<<state.active_blocks
               <<",\"scope_calls\":"<<state.scope_calls<<",\"cpu_clock_reads\":"<<state.cpu_clock_reads<<",\"wall_clock_reads\":"<<state.wall_clock_reads
               <<",\"calibration_trials\":"<<trials<<",\"baseline_cpu_ns\":"<<baseline_cpu<<",\"baseline_wall_ns\":"<<baseline_wall
               <<",\"clock_pair_cpu_ns\":"<<calibration_cpu<<",\"clock_pair_wall_ns\":"<<calibration_wall
               <<",\"clock_cost_is_partial_estimate\":true,\"worker_cpu_included\":false,\"files_ok\":"<<(events_ok?"true":"false")
               <<",\"complete\":"<<(complete?"true":"false")<<"}\n";
        summary.close();const bool result=complete&&bool(summary);
        if(!result)std::fputs("RECEIVER_FEED_OUTPUT_ERROR incomplete or write failure\n",stderr);return result;
    } catch(...) {std::fputs("RECEIVER_FEED_OUTPUT_ERROR\n",stderr);return false;}
}
}

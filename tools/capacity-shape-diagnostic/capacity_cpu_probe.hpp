#pragma once
// Private diagnostic only. No detector, scheduling or input decision is made here.
#include "audio_trace.hpp"
#include "scheduled_work_profile.hpp"
#include "datapump/pattern_receiver.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <utility>

namespace datapump::diagnostic::capacity_cpu {
namespace profile=::datapump::detail::cpu_profile;
inline bool enabled() noexcept {static const bool value=[] {const auto* p=std::getenv("DATAPUMP_CAPACITY_CPU_DIAGNOSTIC");return p&&std::strcmp(p,"1")==0;}();return value;}
inline constexpr std::size_t owner_cap=1024,point_cap=512,pcm_cap=1048576,partition_cap=8192;
#define DATAPUMP_CPU_COUNTERS(X) \
 X(input_transforms) X(template_jobs) X(exact_reused_template_jobs) X(fft_jobs) X(direct_jobs) \
 X(skipped_batches) X(skipped_start_trials) X(retained_start_trials) X(scored_start_trials) \
 X(threshold_trials) X(native_fp32_transforms) X(tabulated_fp64_transforms) X(eof_replays) X(arithmetic_fallback_jobs)
struct Work {
#define FIELD(n) std::uint64_t n=0;
 DATAPUMP_CPU_COUNTERS(FIELD)
#undef FIELD
};
inline Work read_work(const modem::PatternFftWork& w) noexcept {Work out;
#define COPY(n) out.n=w.n;
 DATAPUMP_CPU_COUNTERS(COPY)
#undef COPY
 return out;}
inline void add(Work& target,const Work& v,bool& overflow) noexcept {
#define ADD(n) if(v.n>UINT64_MAX-target.n)overflow=true;else target.n+=v.n;
 DATAPUMP_CPU_COUNTERS(ADD)
#undef ADD
}
template<class Fields>void fields(Fields& f,const Work& w,const char* prefix) {
#define WRITE(n) f.add((std::string(prefix)+#n).c_str(),w.n);
 DATAPUMP_CPU_COUNTERS(WRITE)
#undef WRITE
}
struct Partition {std::uint64_t first=0,count=0,file_sample_offset=0,captured_count=0,case_sequence=0;unsigned case_id=0;bool noise=false,partial=false;};
struct State {
 std::mutex mutex;std::map<const void*,Work> active;Work destroyed;
 std::uint64_t observations=0,destroyed_owners=0,points=0,unknown_fallback=0;
 bool overflow=false,storage_failed=false,point_limited=false,finalized=false;
 std::FILE* pcm=nullptr;std::string pcm_path;bool pcm_open_attempted=false,pcm_failed=false,pcm_limited=false,pcm_gap=false;
 std::uint64_t pcm_samples=0,pcm_next=0,pcm_first=0,partial_reads=0;
 std::array<Partition,partition_cap> partitions{};std::size_t partition_count=0;
 bool partition_limited=false;long double observer_wall=0,observer_cpu=0;bool observer_cpu_valid=true;
 std::uint64_t observer_clock_reads=0,case_sequence=0;unsigned case_id=0;std::string label="before_raw";
};
inline State state;
class Observer {
 std::chrono::steady_clock::time_point wall_=std::chrono::steady_clock::now();
 profile::Tick cpu_=profile::native_thread_cpu(nullptr);
public:
 ~Observer(){const auto c=profile::native_thread_cpu(nullptr);const auto w=std::chrono::steady_clock::now();
  std::lock_guard lock(state.mutex);state.observer_clock_reads+=2;
  state.observer_wall+=std::chrono::duration<long double>(w-wall_).count();
  if(cpu_.valid&&c.valid&&c.ns>=cpu_.ns)state.observer_cpu+=(c.ns-cpu_.ns)*1e-9L;else state.observer_cpu_valid=false;}
};
template<class Impl>Work impl_work(const Impl& s) noexcept {auto w=s.fft_workload?*s.fft_workload:modem::PatternFftWork{};w.threshold_trials=s.trials;if(modem::search_arithmetic_is_int8(s.config.search_arithmetic))w.arithmetic_fallback_jobs=w.template_jobs-w.int8_jobs;return read_work(w);}
template<class Impl>void observe(const Impl& s) noexcept {
 if(!enabled())return;Observer cost;try {std::lock_guard lock(state.mutex);if(state.finalized)return;
  ++state.observations;if(s.fallback){++state.unknown_fallback;return;}
  auto it=state.active.find(&s);if(it==state.active.end()&&state.active.size()==owner_cap){state.storage_failed=true;return;}
  state.active[&s]=impl_work(s);
 }catch(...){std::lock_guard lock(state.mutex);state.storage_failed=true;}
}
template<class Impl>void destroyed(const Impl& s) noexcept {
 if(!enabled())return;Observer cost;try {std::lock_guard lock(state.mutex);if(state.finalized)return;
  if(s.fallback)++state.unknown_fallback;else {add(state.destroyed,impl_work(s),state.overflow);++state.destroyed_owners;}
  state.active.erase(&s);
 }catch(...){std::lock_guard lock(state.mutex);state.storage_failed=true;}
}
inline void work_point(const char* boundary) noexcept {
 if(!enabled())return;Observer cost;try {std::lock_guard lock(state.mutex);if(state.finalized)return;
  if(state.points==point_cap){state.point_limited=true;return;}++state.points;
  Work active;for(const auto& [key,w]:state.active)add(active,w,state.overflow);Work total=state.destroyed;add(total,active,state.overflow);
  audio::trace::record("capacity_cpu_work",[&](auto& f){f.add("schema",1);f.add("case",state.label);f.add("boundary",boundary);
   f.add("active_snapshot_owners",state.active.size());f.add("destroyed_owners",state.destroyed_owners);f.add("observations",state.observations);
   f.add("snapshot_scope","latest completed push/finish per active native owner plus exact destructor counters; current in-flight push may be absent");
   f.add("storage_failed",state.storage_failed);f.add("overflow",state.overflow);f.add("unknown_fallback_observations",state.unknown_fallback);
   fields(f,active,"active_");fields(f,state.destroyed,"destroyed_");fields(f,total,"total_");});
 }catch(...){std::lock_guard lock(state.mutex);state.storage_failed=true;}
}
inline void capture(std::span<const float> input,std::uint64_t first,bool noise,bool partial) noexcept {
 if(!enabled()||input.empty())return;Observer cost;try {std::lock_guard lock(state.mutex);if(state.finalized)return;
  if(!state.pcm_open_attempted){state.pcm_open_attempted=true;const auto* p=std::getenv("DATAPUMP_CAPACITY_CPU_PCM");
   if(!p||!*p||std::strlen(p)>1024||sizeof(float)!=4||std::endian::native!=std::endian::little||!std::numeric_limits<float>::is_iec559){state.pcm_failed=true;return;}
   state.pcm_path=p;state.pcm=std::fopen(p,"wbx");if(!state.pcm){state.pcm_failed=true;return;}state.pcm_first=state.pcm_next=first;}
  if(first!=state.pcm_next)state.pcm_gap=true;state.pcm_next=first+input.size();if(partial)++state.partial_reads;
  const auto remaining=pcm_cap/sizeof(float)-state.pcm_samples;const auto count=std::min<std::uint64_t>(remaining,input.size());
  if(count<input.size())state.pcm_limited=true;
  Partition* partition=nullptr;
  if(state.partition_count==partition_cap)state.partition_limited=true;
  else {partition=&state.partitions[state.partition_count++];*partition={first,input.size(),state.pcm_samples,0,state.case_sequence,state.case_id,noise,partial};}
  if(state.pcm&&count){const auto n=std::fwrite(input.data(),sizeof(float),count,state.pcm);state.pcm_samples+=n;if(partition)partition->captured_count=n;if(n!=count)state.pcm_failed=true;}
 }catch(...){std::lock_guard lock(state.mutex);state.pcm_failed=true;}
}
template<class Fn>decltype(auto) generate(Fn&& work) {profile::Context cpu(profile::Stage::ordinary_source_generation);return std::forward<Fn>(work)();}
template<class Channel,class Source>std::size_t read(Channel& channel,Source& source,std::span<float> output,std::stop_token stop) {
 const auto first=channel.received_samples();std::size_t count=0;
 try{{profile::Context cpu(profile::Stage::ordinary_source_generation);count=channel.read(source,output,stop);}}
 catch(...){const auto used=std::min<std::uint64_t>(output.size(),channel.received_samples()-first);capture(output.first(used),first,false,true);throw;}
 capture(output.first(count),first,false,false);return count;
}
template<class Channel>void read_noise(Channel& channel,std::span<float> output,std::stop_token stop={}) {
 const auto first=channel.received_samples();
 try{{profile::Context cpu(profile::Stage::ordinary_source_generation);channel.read_noise(output,stop);}}
 catch(...){const auto used=std::min<std::uint64_t>(output.size(),channel.received_samples()-first);capture(output.first(used),first,true,true);throw;}
 capture(output,first,true,false);
}
inline void case_marker(const char* label,const char* phase) noexcept {
 if(!enabled())return;std::uint64_t sequence=0;try{{std::lock_guard lock(state.mutex);state.label=label;state.case_id=std::strcmp(label,"raw")==0?1:std::strcmp(label,"short")==0?2:std::strcmp(label,"framed")==0?3:0;sequence=++state.case_sequence;}
  audio::trace::record("capacity_cpu_case_cpu",[&](auto& f){f.add("schema",1);f.add("case",label);f.add("phase",phase);f.add("case_sequence",sequence);
   f.add("scope","cumulative case sequence marker; nonquiescent snapshots are not exact per-case deltas");profile::write_fields(f,profile::global().snapshot());});
  work_point(phase);
 }catch(...){}
}
inline void finalize() noexcept {
 if(!enabled())return;
 try {
  // Source/task joins have completed; all first positive Session modems are destroyed.
  const auto snap=profile::global().snapshot();std::array<long double,6> wall_cost{},cpu_cost{};bool calibration_valid=true;
  constexpr std::size_t repetitions=6,reads=10000;
  for(std::size_t k=0;k<repetitions;++k){const auto w0=std::chrono::steady_clock::now();const auto c0=profile::native_thread_cpu(nullptr);
   for(std::size_t i=0;i<reads;++i)if(!profile::native_thread_cpu(nullptr).valid)calibration_valid=false;
   const auto c1=profile::native_thread_cpu(nullptr);const auto w1=std::chrono::steady_clock::now();
   wall_cost[k]=std::chrono::duration<long double>(w1-w0).count()/reads;
   if(c0.valid&&c1.valid&&c1.ns>=c0.ns)cpu_cost[k]=(c1.ns-c0.ns)*1e-9L/reads;else calibration_valid=false;}
  std::sort(wall_cost.begin(),wall_cost.end());std::sort(cpu_cost.begin(),cpu_cost.end());
  std::lock_guard lock(state.mutex);if(state.finalized)return;state.finalized=true;
  if(state.pcm&&std::fclose(state.pcm)!=0)state.pcm_failed=true;state.pcm=nullptr;
  if(!state.pcm_path.empty()) {
   const auto path=state.pcm_path+".partitions.json";auto* file=std::fopen(path.c_str(),"wx");
   if(!file)state.pcm_failed=true;
   else {if(std::fprintf(file,"{\"schema\":1,\"float_format\":\"IEEE754 binary32 little endian\",\"scope\":\"single positive Session continuous channel; generated before RX delivery; case id0=before raw,1=raw,2=short,3=framed; sequence is most recent test case marker at capture\",\"partitions\":[")<0)state.pcm_failed=true;
    for(std::size_t i=0;i<state.partition_count;++i){const auto& p=state.partitions[i];if(std::fprintf(file,"%s{\"first\":%llu,\"count\":%llu,\"file_sample_offset\":%llu,\"captured_count\":%llu,\"case_id\":%u,\"case_sequence\":%llu,\"noise\":%s,\"partial\":%s}",i?",":"",static_cast<unsigned long long>(p.first),static_cast<unsigned long long>(p.count),static_cast<unsigned long long>(p.file_sample_offset),static_cast<unsigned long long>(p.captured_count),p.case_id,static_cast<unsigned long long>(p.case_sequence),p.noise?"true":"false",p.partial?"true":"false")<0)state.pcm_failed=true;}
    if(std::fprintf(file,"]}\n")<0)state.pcm_failed=true;if(std::fclose(file)!=0)state.pcm_failed=true;}
  }
  audio::trace::record("capacity_cpu_summary",[&](auto& f){f.add("schema",1);f.add("finalized",true);
   f.add("scope","first positive Session after all task joins; final native work includes cancelled/partial execution; whole-run profiling is cumulative including raw/short/framed and cleanup");
   f.add("active_snapshot_owners",state.active.size());f.add("destroyed_owners",state.destroyed_owners);f.add("work_observations",state.observations);f.add("work_points",state.points);
   f.add("work_complete",state.active.empty()&&!state.storage_failed&&!state.overflow&&!state.unknown_fallback);f.add("storage_failed",state.storage_failed);f.add("overflow",state.overflow);
   f.add("unknown_fallback_observations",state.unknown_fallback);f.add("work_point_limited",state.point_limited);fields(f,state.destroyed,"total_");
   f.add("pcm_path",state.pcm_path);f.add("pcm_bytes",state.pcm_samples*sizeof(float));f.add("pcm_samples",state.pcm_samples);f.add("pcm_first",state.pcm_first);f.add("pcm_next_generated",state.pcm_next);
   f.add("pcm_complete",state.pcm_open_attempted&&!state.pcm_failed&&!state.pcm_limited&&!state.pcm_gap&&!state.partition_limited);f.add("pcm_failed",state.pcm_failed);f.add("pcm_limited",state.pcm_limited);f.add("pcm_gap",state.pcm_gap);
   f.add("pcm_partitions",state.partition_count);f.add("pcm_partition_limited",state.partition_limited);f.add("partial_reads",state.partial_reads);
   f.add("observer_wall_seconds",state.observer_wall);f.add("observer_thread_cpu_seconds",state.observer_cpu);f.add("observer_cpu_valid",state.observer_cpu_valid);f.add("observer_clock_reads",state.observer_clock_reads);
   f.add("observer_scope","work-map/counter and PCM capture helpers only; excludes finalization, calibration, trace writer, category scope overhead and generation bodies; may already be included in parent profile categories, never add to profile total");
   f.add("clock_calibration_valid",calibration_valid);f.add("clock_calibration_repetitions",repetitions);f.add("clock_calibration_reads_per_repeat",reads);
   f.add("clock_calibration_cpu_per_read_median_seconds",(cpu_cost[2]+cpu_cost[3])/2);f.add("clock_calibration_wall_per_read_median_seconds",(wall_cost[2]+wall_cost[3])/2);
   f.add("profile_clock_read_cpu_estimate_seconds",(cpu_cost[2]+cpu_cost[3])/2*snap.clock_reads);
   f.add("clock_estimate_scope","post-join clock-only calibration times actual profiler clock reads; estimate not rigorous bound and excludes other instrumentation work");
   profile::write_fields(f,snap);});
 }catch(...){try{std::lock_guard lock(state.mutex);state.storage_failed=true;}catch(...){}}
}
#undef DATAPUMP_CPU_COUNTERS
} // namespace datapump::diagnostic::capacity_cpu

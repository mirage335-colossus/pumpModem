#pragma once
// PRIVATE diagnostic overlay. Synthetic test_gui_controller EU fixture only.
#include "audio_trace.hpp"
#include "scheduled_work_profile.hpp"
#include "datapump/channel.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/fhss_schedule.hpp"
#include <optional>
#include <string_view>
#include <openssl/evp.h>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <span>
#include <vector>
#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>
#endif
namespace datapump::gui_stop_current {
using Clock=std::chrono::steady_clock;
using U=std::pair<const char*,std::uint64_t>;
using D=std::pair<const char*,double>;
inline constexpr std::size_t record_limit=8192,partition_limit=4096,pcm_limit=4*1024*1024;
inline const bool enabled=[] {const auto* s=std::getenv("DATAPUMP_GUI_STOP_CURRENT");return s&&std::string_view(s)=="1";}();
struct Stamp {std::uint64_t wall=0,tid=0;std::int64_t cpu=-1;};
struct Row {const char* name=nullptr;Stamp stamp;std::array<U,40> u{};std::array<D,8> d{};};
struct Partition {std::uint64_t read=0,channel=0,serial=0,generation=0,revision=0,bank=0,before=0,after=0,count=0,source_first=0,source_end=0,offset=0,stored=0;bool committed=false,unwinding=false;};
struct State {
 std::atomic<bool> active{false},requested{false},read_active{false};
 std::atomic<std::uint64_t> clock_reads{0},clock_errors{0},errors{0},calls{0},request_count{0},next_call{0};
 std::atomic<std::uint64_t> latest_read{0},published_read{0},latest_generated{0},latest_transmitted{0},latest_endpoint{0},latest_read_wall{0};
 Clock::time_point emit_deadline{};
 std::mutex mutex;std::array<Row,record_limit> rows{};std::size_t count=0,overflow=0;
 // Critical CPU boundaries have reserved storage: later receiver/configuration
 // details must never crowd out the joined ledger. All overflow remains fatal.
 std::array<std::string,256> details{};std::size_t detail_count=0,detail_overflow=0;
 std::array<std::string,16> cpu_ledgers{};std::size_t cpu_ledger_count=0,cpu_ledger_overflow=0;
 std::uint64_t detail_first_overflow_wall=0,cpu_ledger_first_overflow_wall=0;
 std::array<Partition,partition_limit> partitions{};std::size_t partition_count=0,partition_overflow=0;
 std::vector<float> pcm;std::size_t stored_samples=0;std::uint64_t committed_samples=0,uncertain_feeds=0;
 EVP_MD_CTX* digest=nullptr;bool digest_error=false,pcm_truncated=false,ack_seen=false;
 std::uint64_t observer_wall=0,observer_cpu=0,observer_invalid=0,hash_wall=0,hash_cpu=0;
 std::uint64_t status_count=0,status_poll_calls=0,status_last=0,status_final_count=0;
 std::uint64_t poll_count=0,poll_wall=0,poll_cpu=0,poll_invalid=0,poll_max=0,poll_last=0,poll_max_interval=0;
 ~State(){if(digest)EVP_MD_CTX_free(digest);}
};
inline State state;
inline std::uint64_t wall()noexcept {++state.clock_reads;return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();}
inline std::int64_t cpu()noexcept {
 ++state.clock_reads;
#if defined(__linux__)
 timespec t{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)==0)return std::int64_t(t.tv_sec)*1000000000+t.tv_nsec;
#endif
 ++state.clock_errors;return -1;
}
inline std::uint64_t tid()noexcept {
#if defined(__linux__)
 return static_cast<std::uint64_t>(syscall(SYS_gettid));
#else
 return 0;
#endif
}
inline Stamp stamp()noexcept {return {wall(),tid(),cpu()};}
inline bool active()noexcept{return enabled&&state.active.load(std::memory_order_acquire);}
inline void account(const Stamp& start)noexcept {
 const auto end_cpu=cpu();const auto end_wall=wall();
 state.observer_wall+=end_wall-start.wall;
 if(start.cpu>=0&&end_cpu>=start.cpu)state.observer_cpu+=end_cpu-start.cpu;else ++state.observer_invalid;
}
inline Row make_row(const char* name,std::initializer_list<U> us={},std::initializer_list<D> ds={})noexcept {
 Row r;r.name=name;std::size_t i=0;for(auto v:us){if(i==r.u.size()){++state.errors;break;}r.u[i++]=v;}
 i=0;for(auto v:ds){if(i==r.d.size()){++state.errors;break;}r.d[i++]=v;}return r;
}
inline Stamp push(Row row)noexcept {
 if(!active())return {};
 const auto start=stamp();if(!row.stamp.wall)row.stamp=start;
 try {std::lock_guard lock(state.mutex);++state.calls;
  if(state.count<record_limit)state.rows[state.count++]=row;else ++state.overflow;
  account(start);
 }catch(...){++state.errors;}
 return start;
}
inline Stamp point(const char* name,std::initializer_list<U> u={},std::initializer_list<D> d={})noexcept{return push(make_row(name,u,d));}
template<class F>inline void detail(const char* name,F fill,bool critical=false)noexcept {
 if(!active())return;const auto started=stamp();
 try {audio::trace::Fields f;f.add("observed_wall_ns",started.wall);f.add("observed_thread_cpu_ns",started.cpu);f.add("tid",started.tid);fill(f);
  auto text="{\"event\":"+audio::trace::quoted(std::string("gui_stop_current_")+name)+f.out.str()+"}";
  std::lock_guard lock(state.mutex);
  if(critical) {
   if(text.size()>15000 || state.cpu_ledger_count==state.cpu_ledgers.size()) {
    if(!state.cpu_ledger_overflow)state.cpu_ledger_first_overflow_wall=started.wall;
    ++state.cpu_ledger_overflow;
   }else state.cpu_ledgers[state.cpu_ledger_count++]=std::move(text);
  }else if(text.size()>15000 || state.detail_count==state.details.size()) {
   if(!state.detail_overflow)state.detail_first_overflow_wall=started.wall;
   ++state.detail_overflow;
  }else state.details[state.detail_count++]=std::move(text);
  account(started);
 }catch(...){++state.errors;}
}
// Process-cumulative completed Contexts, including workers; never reset. A
// request/strict snapshot may cut active Contexts and is not an atomic interval.
inline void cpu_ledger(const char* boundary)noexcept {
 detail("cpu_ledger",[&](auto& f){f.add("boundary",boundary);f.add("request_ordinal",state.request_count.load());::datapump::detail::cpu_profile::write_fields(f,::datapump::detail::cpu_profile::global().snapshot());},true);
}
// Leaf timing uses one inverse per 257 calls, per kind and physical thread.
// The odd prime stride avoids systematically selecting only one bit of a pair.
// Shape records are deduplicated, not emitted for every transform. Only the
// executor inverse scope is eligible; unrelated transforms stay outside it.
inline constexpr std::uint64_t fft_sample_period=257;
inline bool fft_sample(std::size_t,unsigned kind)noexcept {
 if(!active()||!state.requested||kind>2||
    ::datapump::detail::cpu_profile::current_stage()!=::datapump::detail::cpu_profile::Stage::native_fft_inverse)return false;
 static thread_local std::array<std::uint64_t,3> counts{};
 return ++counts[kind]%fft_sample_period==0;
}
inline void fft_shape(std::size_t n,unsigned kind,bool vectorized,bool inverse)noexcept {
 if(!active()||!state.requested||!inverse||kind>2)return;
 using Stage=::datapump::detail::cpu_profile::Stage;
 const auto stage=::datapump::detail::cpu_profile::current_stage();
 if(stage!=Stage::native_fft_inverse&&stage!=Stage::borrowed_fft_checks&&stage!=Stage::legacy_fft_setup)return;
 struct Shape {std::size_t n;std::uint64_t request;unsigned kind;bool vectorized;};
 static thread_local std::array<Shape,64> seen{};
 static thread_local std::size_t count=0;
 const Shape key{n,state.request_count.load(),kind,vectorized};
 for(std::size_t i=0;i<count;++i)if(seen[i].n==key.n&&seen[i].request==key.request&&
    seen[i].kind==key.kind&&seen[i].vectorized==key.vectorized)return;
 if(count==seen.size()){++state.errors;return;}
 seen[count++]=key;
 detail("fft_shape",[&](auto& f){f.add("request_ordinal",key.request);f.add("transform",n);f.add("kind",kind);f.add("vectorized",vectorized);f.add("inverse",inverse);f.add("sample_period",fft_sample_period);f.add("sample_scope","per-kind per-thread inverses inside executor; sparse exact thread CPU, no extrapolated wall latency");});
}
inline std::string hex(std::span<const unsigned char> bytes){constexpr char h[]="0123456789abcdef";std::string s;for(auto b:bytes){s+=h[b>>4];s+=h[b&15];}return s;}
inline void config(const char* role,const modem::Config& c)noexcept {
 detail("config",[&](auto& f){f.add("role",role);f.add("sample_rate",c.sample_rate);f.add("bandwidth_hz",c.bandwidth_hz);f.add("carrier_hz",c.carrier_hz);
  f.add("stream_epoch",c.stream_epoch);f.add("stream_phase_samples",c.stream_phase_samples);f.add("spreading_factor",c.spreading_factor);
  f.add("scramble",c.scramble);f.add("dsss",c.dsss);f.add("dsss_factor",c.dsss_factor);f.add("pulse_shaping",c.pulse_shaping);
  f.add("spreading_seed",hex(c.spreading_seed));f.add("dsss_seed",hex(c.dsss_seed));
  if(c.data_key){const auto m=c.data_key->mac(Bytes{'g','u','i','-','s','t','o','p','-','c','u','r','r','e','n','t'});f.add("key_mac",hex(m));}
  f.add("raw_key_material_captured",false);
  if(c.fake_hopping){const auto& p=*c.fake_hopping;f.add("channels",p.channels);f.add("dwell",p.dwell_seconds);f.add("offset",p.offset_seconds);
   f.add("clock_error",p.clock_error_seconds);f.add("peer_clock_error",p.peer_clock_error_seconds);f.add("region",p.region_seconds);f.add("audio_error",p.audio_error_seconds);
   f.add("matched_gaps",p.matched_gaps);f.add("smooth_ramps",p.smooth_ramps);f.add("power_ramps",p.power_ramps);f.add("defer_symbols",p.defer_symbols);
   std::ostringstream shifts;shifts.imbue(std::locale::classic());shifts<<std::hexfloat;for(unsigned i=0;i<p.channels;++i)shifts<<p.shifts[i]<<',';f.add("shifts_hex",shifts.str());}
 });
}
struct Interval {
 Row row;Stamp start{};int exceptions=std::uncaught_exceptions();bool on=false;
 Interval(const char* begin,const char* end,std::initializer_list<U> u={}):row(make_row(end,u)),on(active()) {if(on)start=point(begin,u);}
 ~Interval()noexcept {if(!on)return;auto end=stamp();row.stamp=end;
  std::size_t i=0;while(i<row.u.size()&&row.u[i].first)++i;
  if(i+4>row.u.size()){++state.errors;return;}
  row.u[i++]={"elapsed_wall_ns",end.wall-start.wall};row.u[i++]={"elapsed_cpu_ns",start.tid==end.tid&&start.cpu>=0&&end.cpu>=start.cpu?std::uint64_t(end.cpu-start.cpu):0};
  row.u[i++]={"cpu_valid",start.tid==end.tid&&start.cpu>=0&&end.cpu>=start.cpu};row.u[i++]={"unwinding",std::uncaught_exceptions()>exceptions};
  // push's observation stamp is the end marker; elapsed excludes final insertion.
  push(row);
 }
};
struct LastRead {std::uint64_t id=0,channel=0,serial=0,first=0,end=0,count=0;};
inline thread_local LastRead last_read;
inline thread_local std::uint64_t serial=0;
struct Read {
 modem::SampledSimulationChannel* channel;modem::StreamingTransmitter* tx;std::span<float> output;std::uint64_t first=0,id=0;bool on=false;int exceptions=std::uncaught_exceptions();
 std::optional<Interval> interval;
 Read(modem::SampledSimulationChannel& c,modem::StreamingTransmitter* source,std::span<float> out):channel(&c),tx(source),output(out),on(active()) {
  if(!on)return;
  try {first=c.received_samples();id=++state.next_call;
   {std::lock_guard lock(state.mutex);state.read_active=true;state.latest_read=id;}
   // Scope storage is inline; no per-read allocation or hot-path file I/O.
   interval.emplace("source_read_begin","source_read_end",std::initializer_list<U>{{"read_id",id},{"channel",reinterpret_cast<std::uintptr_t>(&c)},{"serial",tx?serial:0},{"source_first",first},{"capacity",out.size()}});
  }catch(...){++state.errors;on=false;}
 }
 ~Read()noexcept {
  if(!on)return;try {const auto end=channel->received_samples();const auto count=end>=first?end-first:0;
   last_read={id,reinterpret_cast<std::uintptr_t>(channel),tx?serial:0,first,end,count};
   const auto generated=tx?tx->samples_emitted():0,transmitted=tx?channel->transmitted_samples():0,endpoint=tx?tx->total_samples():0;
   point("source_read_result",{{"read_id",id},{"channel",last_read.channel},{"serial",last_read.serial},{"source_first",first},{"source_end",end},{"count",count},{"generated",generated},{"transmitted",transmitted},{"endpoint",endpoint},{"unwinding",std::uncaught_exceptions()>exceptions},{"capacity",output.size()},{"source_present",tx!=nullptr}});
   {std::lock_guard lock(state.mutex);state.published_read=id;state.latest_generated=generated;state.latest_transmitted=transmitted;state.latest_endpoint=endpoint;state.latest_read_wall=wall();state.read_active=false;}
   if(tx&&output.size()&&count==0&&std::uncaught_exceptions()==exceptions)point("natural_eof",{{"read_id",id},{"serial",serial},{"generated",generated},{"transmitted",transmitted},{"endpoint",endpoint}});
  }catch(...){++state.errors;state.read_active=false;}
 }
};
struct Feed {
 std::span<const float> input;const std::uint64_t &bank_samples,&revision;std::uint64_t before,rev,generation,bank;LastRead read;bool on;int exceptions=std::uncaught_exceptions();std::optional<Interval> interval;
 Feed(std::span<const float> data,const std::uint64_t& samples,const std::uint64_t& r,std::uint64_t g,const void* identity):input(data),bank_samples(samples),revision(r),before(samples),rev(r),generation(g),bank(reinterpret_cast<std::uintptr_t>(identity)),read(last_read),on(active()) {
  if(on)try{interval.emplace("rx_feed_begin","rx_feed_end",std::initializer_list<U>{{"read_id",read.id},{"serial",read.serial},{"revision",rev},{"generation",g},{"bank",bank},{"bank_before",before},{"count",input.size()}});}catch(...){++state.errors;}
 }
 ~Feed()noexcept {
  if(!on)return;const auto started=stamp();try{std::lock_guard lock(state.mutex);
   const bool unwind=std::uncaught_exceptions()>exceptions;
   const bool committed=!unwind&&revision==rev&&input.size()<=std::numeric_limits<std::uint64_t>::max()-before&&bank_samples==before+input.size();
   Partition p{read.id,read.channel,read.serial,generation,rev,bank,before,bank_samples,input.size(),read.first,read.end,state.committed_samples,0,committed,unwind};
   if(committed){
    if(!state.digest||EVP_DigestUpdate(state.digest,input.data(),input.size_bytes())!=1)state.digest_error=true;
    const auto available=state.pcm.size()-state.stored_samples;p.stored=std::min(available,input.size());
    if(p.stored)std::memcpy(state.pcm.data()+state.stored_samples,input.data(),p.stored*sizeof(float));state.stored_samples+=p.stored;
    if(p.stored!=input.size())state.pcm_truncated=true;state.committed_samples+=input.size();
   }else ++state.uncertain_feeds;
   if(state.partition_count<partition_limit)state.partitions[state.partition_count++]=p;else ++state.partition_overflow;
   const auto end_cpu=cpu();const auto end_wall=wall();state.hash_wall+=end_wall-started.wall;if(started.cpu>=0&&end_cpu>=started.cpu)state.hash_cpu+=end_cpu-started.cpu;else ++state.observer_invalid;
   account(started);
  }catch(...){++state.errors;}
 }
};
inline void request_before()noexcept {
 if(!active())return;const auto entered=stamp();state.requested=true;const auto request=++state.request_count;
 try {Row r;
  {std::lock_guard lock(state.mutex);r=make_row("request_before_lock",{{"request",request},{"current_read_id",state.latest_read.load()},{"published_read_id",state.published_read.load()},{"read_inflight",state.read_active.load()},{"published_generated",state.latest_generated.load()},{"published_transmitted",state.latest_transmitted.load()},{"published_endpoint",state.latest_endpoint.load()},{"source_witness_wall_ns",state.latest_read_wall.load()},{"source_snapshot_wall_ns",wall()}});r.stamp=entered;}
  push(r);cpu_ledger("request");
 }catch(...){++state.errors;}
}
// Coarse copies of fields from the already-polled GUI Snapshot only. Hardware
// disposition fields remain raw witnesses and are INAPPLICABLE in simulation;
// zero cannot prove an idle/advanced simulated decoder. No owner scan is added.
template<class Snapshot>inline void ack(const Snapshot& s,bool final=false)noexcept {
 if(!active()||!state.requested)return;
 try {bool emit=false,status=false,first=false,last=false;const auto now=wall();
  {std::lock_guard lock(state.mutex);++state.status_poll_calls;
   if(!s.transmitting&&!state.ack_seen){state.ack_seen=true;emit=true;}
   first=state.status_count==0;last=final||emit;
   status=first||last||now-state.status_last>=200000000;
   if(status){++state.status_count;state.status_last=now;if(last)++state.status_final_count;}
  }
  if(status)point("snapshot_status",{{"serial",s.transmission_id},{"snapshot_sequence",s.sequence},{"status_hook_wall_ns",now},{"first",first},{"final",last},{"simulation",s.simulation},
   {"transmitting",s.transmitting},{"finished",s.transmission_finished},{"stopping",s.transmission_stopping},{"transmission_failed",s.transmission_failed},
   {"input_revision",s.receiver_input_revision},{"samples_received",s.samples_received},{"processed_physical_samples",s.processed_physical_samples},
   {"queued_physical_samples",s.queued_physical_samples},{"decoding_physical_samples",s.decoding_physical_samples},{"buffered_samples",s.buffered_samples},{"decoding_samples",s.decoding_samples},
   {"receiver_instances",s.receiver_instances},{"receiver_behind",s.receiver_behind},{"simulation_receiving_tail",s.simulation_receiving_tail},{"dsp_buffered_bytes",s.dsp_buffered_bytes},
   {"health_failed",s.receiver_health.failed()},{"input_overruns",s.receiver_health.input_overruns},{"dropped_samples",s.receiver_health.dropped_samples},
   {"input_interrupted",s.receiver_health.input_interrupted},{"search_limited",s.receiver_health.search_limited},{"receiver_reset",s.receiver_health.receiver_reset},
   {"error_present",!s.error.empty()},{"receiver_error_present",!s.receiver_error.empty()},
   {"hardware_frontier_applicable",!s.simulation},{"hardware_queues_applicable",!s.simulation},{"bank_processed_frontier_available",0}},
   {{"published_source_seconds",s.transmission_seconds},{"transmission_fraction",s.transmission_fraction},{"receiver_backlog_seconds",s.receiver_backlog_seconds},
    {"receiver_oldest_input_seconds",s.receiver_oldest_input_seconds},{"simulation_compute_seconds",s.simulation_compute_seconds}});
  if(emit)point("gui_ack",{{"serial",s.transmission_id},{"finished",s.transmission_finished},{"stopping",s.transmission_stopping},{"failed",s.transmission_failed},{"input_revision",s.receiver_input_revision},{"samples_received",s.samples_received},{"health_failed",s.receiver_health.failed()},{"dropped_samples",s.receiver_health.dropped_samples}},{{"published_source_seconds",s.transmission_seconds}});
 }catch(...){++state.errors;}
}
struct Poll {
 Stamp start{};bool on=active();Poll(){if(on)start=stamp();}
 ~Poll()noexcept {if(!on)return;const auto end=stamp();try{std::lock_guard lock(state.mutex);++state.poll_count;state.poll_wall+=end.wall-start.wall;
  state.poll_max=std::max(state.poll_max,end.wall-start.wall);if(state.poll_last)state.poll_max_interval=std::max(state.poll_max_interval,start.wall-state.poll_last);state.poll_last=start.wall;
  if(start.tid==end.tid&&start.cpu>=0&&end.cpu>=start.cpu)state.poll_cpu+=end.cpu-start.cpu;else ++state.poll_invalid;}catch(...){++state.errors;}}
};
inline std::string output(const char* suffix){const auto* root=std::getenv("DATAPUMP_GUI_STOP_CURRENT_OUT");return root?std::string(root)+suffix:std::string();}
inline bool drain()noexcept {try{while(auto s=audio::trace::active.load()){if(s->queued()<128)return true;if(Clock::now()>=state.emit_deadline){++state.errors;return false;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}}catch(...){++state.errors;}return false;}
struct Case {
 bool on=false;std::unique_ptr<audio::trace::Scope> trace;
 explicit Case(bool target,const std::filesystem::path& keyfile):on(enabled&&target) {
  if(!on)return;
  if(output("").empty()){++state.errors;on=false;return;}
  if(state.active.exchange(true))throw std::runtime_error("overlapping current GUI diagnostic");
  try{state.pcm.resize(pcm_limit/sizeof(float));state.digest=EVP_MD_CTX_new();if(!state.digest||EVP_DigestInit_ex(state.digest,EVP_sha256(),nullptr)!=1)state.digest_error=true;
   std::filesystem::copy_file(keyfile,output("/synthetic-keyfile"),std::filesystem::copy_options::none);
   trace=std::make_unique<audio::trace::Scope>(output("/trace.jsonl"),false);
  }catch(...){++state.errors;}
  point("case_begin",{{"synthetic_fixture_only",1}},{{"system_unix_seconds",std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()}});
  cpu_ledger("case_begin");
 }
 ~Case()noexcept {
  if(!on)return;point("case_joined",{{"request_count",state.request_count.load()}});cpu_ledger("case_joined");state.active=false;
  // Case is declared before Controller, so every Controller/Session task has
  // joined before any digest/file emission. No hot-path file I/O occurs here.
  try {
   unsigned char digest[32]{};unsigned n=0;if(!state.digest||EVP_DigestFinal_ex(state.digest,digest,&n)!=1||n!=32)state.digest_error=true;
   if(auto* file=std::fopen(output("/committed-input.f32").c_str(),"wbx")){const auto bytes=state.stored_samples*sizeof(float);if(std::fwrite(state.pcm.data(),1,bytes,file)!=bytes)++state.errors;if(std::fclose(file))++state.errors;}else ++state.errors;
   state.emit_deadline=Clock::now()+std::chrono::seconds(10);
   for(std::size_t i=0;i<state.count;++i){const auto& r=state.rows[i];if(!drain())break;audio::trace::record((std::string("gui_stop_current_")+r.name).c_str(),[&](auto& f){f.add("capture_index",i);f.add("observed_wall_ns",r.stamp.wall);f.add("observed_thread_cpu_ns",r.stamp.cpu);f.add("tid",r.stamp.tid);for(const auto& u:r.u)if(u.first)f.add(u.first,u.second);for(const auto& d:r.d)if(d.first)f.add(d.first,d.second);});}
   for(std::size_t i=0;i<state.detail_count;++i){if(!drain())break;audio::trace::record("gui_stop_current_detail",[&](auto& f){f.add("detail_json",state.details[i]);});}
   for(std::size_t i=0;i<state.cpu_ledger_count;++i){if(!drain())break;audio::trace::record("gui_stop_current_detail",[&](auto& f){f.add("detail_json",state.cpu_ledgers[i]);});}
   for(std::size_t i=0;i<state.partition_count;++i){const auto& p=state.partitions[i];if(!drain())break;audio::trace::record("gui_stop_current_pcm_partition",[&](auto& f){f.add("ordinal",i);f.add("read_id",p.read);f.add("channel",p.channel);f.add("serial",p.serial);f.add("generation",p.generation);f.add("revision",p.revision);f.add("bank",p.bank);f.add("bank_before",p.before);f.add("bank_after",p.after);f.add("count",p.count);f.add("source_first",p.source_first);f.add("source_end",p.source_end);f.add("sample_offset",p.offset);f.add("stored_count",p.stored);f.add("committed",p.committed);f.add("unwinding",p.unwinding);});}
   drain();audio::trace::record("gui_stop_current_summary",[&](auto& f){f.add("schema",3);f.add("float_bytes",sizeof(float));f.add("native_little_endian",std::endian::native==std::endian::little);f.add("unmatched",true);f.add("qualification",false);f.add("records",state.count);f.add("record_limit",record_limit);f.add("record_overflow",state.overflow);f.add("details",state.detail_count+state.cpu_ledger_count);f.add("generic_details",state.detail_count);f.add("detail_limit",state.details.size());f.add("detail_overflow",state.detail_overflow);f.add("detail_first_overflow_wall_ns",state.detail_first_overflow_wall);f.add("cpu_ledger_records",state.cpu_ledger_count);f.add("cpu_ledger_limit",state.cpu_ledgers.size());f.add("cpu_ledger_overflow",state.cpu_ledger_overflow);f.add("cpu_ledger_first_overflow_wall_ns",state.cpu_ledger_first_overflow_wall);f.add("partitions",state.partition_count);f.add("partition_overflow",state.partition_overflow);f.add("errors",state.errors.load());f.add("clock_reads",state.clock_reads.load());f.add("clock_errors",state.clock_errors.load());f.add("observer_wall_sum_ns",state.observer_wall);f.add("observer_thread_cpu_sum_ns",state.observer_cpu);f.add("observer_invalid",state.observer_invalid);f.add("hash_wall_ns",state.hash_wall);f.add("hash_thread_cpu_ns",state.hash_cpu);f.add("snapshot_status_count",state.status_count);f.add("snapshot_status_poll_calls",state.status_poll_calls);f.add("snapshot_status_final_count",state.status_final_count);f.add("snapshot_status_period_ns",200000000);f.add("poll_calls",state.poll_count);f.add("poll_wall_ns",state.poll_wall);f.add("poll_thread_cpu_ns",state.poll_cpu);f.add("poll_invalid",state.poll_invalid);f.add("poll_max_wall_ns",state.poll_max);f.add("poll_max_interval_ns",state.poll_max_interval);f.add("committed_samples",state.committed_samples);f.add("pcm_stored_samples",state.stored_samples);f.add("pcm_truncated",state.pcm_truncated);f.add("uncertain_feeds",state.uncertain_feeds);f.add("digest_error",state.digest_error);f.add("committed_pcm_sha256",hex(std::span(digest,n)));f.add("worker_cpu_attribution","unavailable; use separate proc sampler, never add wall sums");f.add("overhead_scope","observer sums include point/detail/hash insertion and overlap; exclude interval object construction, snapshot throttle/locking, clock-call self cost, writer, serialization, proc sampler, and changed scheduling; not matched overhead");});
  }catch(...){std::fputs("GUI_STOP_CURRENT_CAPTURE_ERROR: finalization failed\n",stderr);}
  trace.reset();
 }
};
}

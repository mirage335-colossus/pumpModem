#!/usr/bin/env python3
"""Pinned TEMP diagnostic only; never qualification or a production patch."""
if not __debug__:raise RuntimeError('optimized Python is unsupported for diagnostic evidence validation')
from pathlib import Path
import argparse,datetime,difflib,hashlib,json,os,re,shlex,subprocess,sys,time

HEADER='#pragma once\n// TEMP diagnostic only. This file is never part of qualification source.\n#include "audio_trace.hpp"\n#include "datapump/crypto.hpp"\n#include "datapump/fhss_schedule.hpp"\n#include <openssl/evp.h>\n#include <algorithm>\n#include <array>\n#include <atomic>\n#include <cstdlib>\n#include <fstream>\n#include <functional>\n#include <iostream>\n#include <memory>\n#include <mutex>\n#include <sstream>\n#include <stdexcept>\n#include <thread>\n#include <cfenv>\n#include <cfloat>\n#include <stop_token>\n#include <exception>\n#if defined(__SSE2__)\n#include <xmmintrin.h>\n#endif\n#if defined(__linux__)\n#include <time.h>\n#endif\nnamespace datapump::stop_differential {\nusing Clock=std::chrono::steady_clock;\ninline std::string env(const char* name){const auto* p=std::getenv(name);return p?p:"";}\ninline const std::string group=env("DATAPUMP_STOP_GROUP");\ninline const bool detailed=env("DATAPUMP_STOP_TRACE")=="1";\ninline const bool only_target=env("DATAPUMP_STOP_ONLY_TARGET")=="1";\ninline const bool enabled=group=="original"||group=="eu"||group=="fcc"||group=="eu-offset"||group=="eu-guard"||group=="eu-position";\ninline constexpr std::uint64_t epoch=1700000000;\ninline const std::uint64_t target_block=group=="eu-position"?120:60;\ninline double arrival_offset_seconds(){return group=="eu-offset"?1.282:2.564;}\ninline const char* offset_text(){return group=="eu-offset"?"1282ms":"2564ms";}\ninline const char* audio_error_text(){return group=="eu-guard"?"0ms":"10ms";}\ninline std::atomic<bool> active{false},ready{false},fired{false},missed{false},target_done{false};\ninline std::atomic<std::uint64_t> blocks{0},request_ns{0},request_transmitted{0},request_generated{0},identity{0},noise_creations{0},output_samples{0};\ninline Clock::time_point origin;\ninline std::mutex output_mutex;\ninline std::ofstream witness;\ninline std::uint64_t witness_records=0,witness_bytes=0;\ninline bool witness_failed=false;\ninline constexpr std::uint64_t witness_cap=8*1024*1024;\ninline std::uint64_t wall(){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();}\ninline std::int64_t cpu(){\n#if defined(__linux__)\n timespec t{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)==0)return std::int64_t(t.tv_sec)*1000000000+t.tv_nsec;\n#endif\n return -1;\n}\ninline bool primary(){return active.load(std::memory_order_acquire)&&!target_done.load(std::memory_order_acquire);}\ninline bool controlled(){return primary()&&group!="original";}\n// TEMP bounded component accounting. No detector state, call order, or worker policy changes.\nenum class CostKind:std::size_t {refresh,constructor,push,pending,history,process,tracks,templates,collect,measure,legacy_fft,prepared_fft,count};\nstruct CostSlot {std::atomic<std::uint64_t> calls{0},wall_ns{0},cpu_ns{0},invalid{0};};\ninline std::array<CostSlot,static_cast<std::size_t>(CostKind::count)> cost_slots;\ninline std::atomic<bool> cost_active{false};\ninline std::atomic<unsigned> cost_scopes{0};\nstruct CostScope {\n CostKind kind;std::uint64_t start=0;std::int64_t cpu_start=-1;\n explicit CostScope(CostKind k):kind(k){if(cost_active.load(std::memory_order_acquire)){++cost_scopes;start=wall();cpu_start=cpu();}}\n ~CostScope(){if(start){const auto end=cpu();auto& s=cost_slots[static_cast<std::size_t>(kind)];++s.calls;s.wall_ns.fetch_add(wall()-start,std::memory_order_relaxed);\n  if(cpu_start>=0&&end>=cpu_start)s.cpu_ns.fetch_add(static_cast<std::uint64_t>(end-cpu_start),std::memory_order_relaxed);else{++s.invalid;missed=true;}\n  --cost_scopes;}}\n CostScope(const CostScope&)=delete;CostScope& operator=(const CostScope&)=delete;\n};\n\ntemplate<class F>void event(const char* name,F fields,bool minimal=false,bool after_target=false){\n if((!primary()&&!after_target)||(!minimal&&!detailed))return;\n audio::trace::Fields f;f.add("wall_ns",wall());f.add("thread_cpu_ns",cpu());\n std::ostringstream tid;tid<<std::this_thread::get_id();f.add("thread",tid.str());fields(f);\n const auto payload=f.out.str();\n if(minimal){std::lock_guard lock(output_mutex);\n  const auto line="{\\"event\\":"+audio::trace::quoted(name)+",\\"witness_sequence\\":"+std::to_string(++witness_records)+payload+"}\\n";\n  if(witness_records>10000||witness_bytes+line.size()>witness_cap)witness_failed=true;\n  else{witness.write(line.data(),line.size());witness_bytes+=line.size();if(!witness)witness_failed=true;}\n }\n if(detailed)audio::trace::record(name,[&](auto& out){out.out<<payload;});\n}\n// TEMP: preserve one fully observed idle prefix; gate only between source iterations.\ninline std::atomic<bool> idle_ready{false},idle_tx_requested{false},idle_gate_done{false};\ninline std::atomic<std::uint64_t> idle_prefix{0};\ninline void idle_fed(std::size_t count,unsigned rate,std::uint64_t bank_samples){\n if(!controlled()||idle_gate_done.load())return;\n if(idle_ready.load()){missed=true;throw std::runtime_error("duplicate pre-TX idle prefix");}\n idle_prefix=count;\n event("idle_prefix_fed",[&](auto& f){f.add("count",count);f.add("sample_rate",rate);f.add("bank_samples",bank_samples);f.add("fully_fed",true);},true);\n idle_ready.store(true,std::memory_order_release);\n}\ntemplate<class Poll>void await_idle(Poll poll){\n if(!controlled())return;\n const auto deadline=Clock::now()+std::chrono::seconds(5);\n while(!idle_ready.load(std::memory_order_acquire)){\n  if(Clock::now()>=deadline){missed=true;throw std::runtime_error("diagnostic idle prefix was not fully fed");}\n  poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));\n }\n}\ninline void tx_requested(){\n if(!controlled())return;\n event("idle_tx_requested",[](auto& f){f.add("prefix",idle_prefix.load());},true);idle_tx_requested.store(true,std::memory_order_release);\n}\ntemplate<class Ready>bool before_source_iteration(std::stop_token stop,Ready prepared){\n if(!controlled()||!idle_ready.load()||idle_gate_done.load())return true;\n const auto start=wall();const auto deadline=Clock::now()+std::chrono::seconds(5);\n // The preceding iteration\'s source_active guard has already been released.\n while(!(idle_tx_requested.load(std::memory_order_acquire)&&prepared())){\n  if(stop.stop_requested())return false;\n  if(Clock::now()>=deadline){missed=true;event("idle_gate_failed",[](auto& f){f.add("reason","TX preparation did not become ready");},true);return false;}\n  std::this_thread::sleep_for(std::chrono::milliseconds(1));\n }\n idle_gate_done=true;event("idle_gate_released",[&](auto& f){f.add("prefix",idle_prefix.load());f.add("wait_wall_ns",wall()-start);f.add("prepared_ready",true);},true);\n return true;\n}\n\n// TEMP: source-thread overload/direction/geometry counts; other-thread observed windows require boundary exclusion; sparse same-thread phase samples.\ninline thread_local bool source_feed_thread=false;\ninline std::atomic<std::uint64_t> fft_block{0};\ninline std::array<std::atomic<std::uint64_t>,36> fft_calls{};\nenum class FftKind:unsigned {borrowed,float_plan,double_plan};\nenum class FftPhase:unsigned {preflight,guard,permutation,first_stage,later_stages,normalization};\ninline constexpr std::array<const char*,6> fft_phase_names{"preflight","guard","permutation","first_stage","later_stages","normalization"};\ninline std::size_t fft_index(unsigned lane,unsigned kind,unsigned inverse,unsigned size){return ((lane*3+kind)*2+inverse)*3+size;}\nstruct FftSample {\n unsigned lane=0,size=0;std::size_t n=0;bool inverse=false,vectorized=false,gate_seen=false,completed=false,unwinding=false;\n std::uint64_t block=0,ordinal=0,wall_start=0,total_wall_start=0,clock_reads=0;\n std::int64_t cpu_start=-1,total_cpu_start=-1;\n unsigned phase=0;std::array<std::uint64_t,6> phase_wall{};std::array<std::int64_t,6> phase_cpu{};\n int rounding=std::fegetround();unsigned mxcsr=0;\n FftSample(unsigned l,unsigned s,std::size_t length,bool backwards,std::uint64_t order):lane(l),size(s),n(length),inverse(backwards),block(fft_block.load()),ordinal(order){\n#if defined(__SSE2__)\n  mxcsr=_mm_getcsr();\n#endif\n  begin();total_wall_start=wall_start;total_cpu_start=cpu_start;\n }\n void begin(){cpu_start=cpu();wall_start=wall();clock_reads+=2;}\n void finish(){const auto end_cpu=cpu();const auto end_wall=wall();clock_reads+=2;\n  phase_wall[phase]+=end_wall-wall_start;\n  if(cpu_start<0||end_cpu<cpu_start){phase_cpu[phase]=-1;missed=true;}\n  else if(phase_cpu[phase]>=0)phase_cpu[phase]+=end_cpu-cpu_start;\n }\n void mark(FftPhase next){const auto index=static_cast<unsigned>(next);if(index==phase)return;finish();phase=index;begin();}\n void publish(){finish();const auto end_cpu=cpu();const auto end_wall=wall();clock_reads+=2;\n  event("borrowed_fft_sample",[&](auto& f){f.add("block",block);f.add("source_thread",lane==0);f.add("n",n);f.add("inverse",inverse);f.add("ordinal",ordinal);f.add("period",4096);f.add("vectorized",vectorized);f.add("gate_seen",gate_seen);f.add("completed",completed);f.add("unwinding",unwinding);f.add("rounding",rounding);f.add("FLT_EVAL_METHOD",FLT_EVAL_METHOD);f.add("mxcsr",mxcsr);f.add("clock_reads",clock_reads);f.add("whole_wall_ns",end_wall-total_wall_start);f.add("whole_cpu_ns",total_cpu_start>=0&&end_cpu>=total_cpu_start?end_cpu-total_cpu_start:-1);\n   for(unsigned i=0;i<fft_phase_names.size();++i){const auto name=std::string(fft_phase_names[i]);f.add((name+"_wall_ns").c_str(),phase_wall[i]);f.add((name+"_cpu_ns").c_str(),phase_cpu[i]);}\n   f.add("scope","one borrowed FFT per4096 calls per feed/lane/direction/geometry; phase gaps include instrumentation bookkeeping; no inter-thread CPU subtraction");});\n }\n};\nstruct FftProbe {\n std::unique_ptr<FftSample> sample;const int uncaught_start=std::uncaught_exceptions();\n FftProbe(FftKind kind,std::size_t n,bool inverse){\n  if(!detailed||!primary()||!fired.load()||!cost_active.load())return;\n  const unsigned lane=source_feed_thread?0:1,size=n==256?0:n==512?1:2;\n  const auto ordinal=fft_calls[fft_index(lane,static_cast<unsigned>(kind),inverse,size)].fetch_add(1,std::memory_order_relaxed);\n  if(kind==FftKind::borrowed && ordinal%4096==0)sample=std::make_unique<FftSample>(lane,size,n,inverse,ordinal);\n }\n void mark(FftPhase next){if(sample)sample->mark(next);}\n void gate(bool enabled){if(sample){sample->vectorized=enabled;sample->gate_seen=true;}}\n void complete(){if(sample)sample->completed=true;}\n ~FftProbe(){if(sample)try{sample->unwinding=std::uncaught_exceptions()>uncaught_start;sample->publish();}catch(...){missed=true;}}\n FftProbe(const FftProbe&)=delete;FftProbe& operator=(const FftProbe&)=delete;\n};\n\nstruct FeedCosts {\n bool enabled=false;std::uint64_t block=0;\n explicit FeedCosts(std::uint64_t b):block(b){if(!(primary()&&detailed))return;\n  if(cost_active.load(std::memory_order_acquire)||cost_scopes.load()){missed=true;return;}\n  for(auto& s:cost_slots){s.calls=0;s.wall_ns=0;s.cpu_ns=0;s.invalid=0;}\n  for(auto& count:fft_calls)count=0;fft_block=b;source_feed_thread=true;enabled=true;cost_active.store(true,std::memory_order_release);}\n ~FeedCosts(){if(!enabled)return;cost_active=false;source_feed_thread=false;if(cost_scopes.load())missed=true;\n  event("feed_cost_summary",[&](auto& f){f.add("block",block);f.add("active_scopes",cost_scopes.load());f.add("other_thread_attribution","observed admission window only; cross-boundary entries cannot support exact per-feed attribution");\n   f.add("scope","per-callback wall sums may overlap; CPU sums use each callback\'s thread; constructor is nested inside refresh");\n   constexpr std::array<const char*,12> names{"refresh","constructor","push","pending","history","process","tracks","templates","collect","measure","legacy_fft","prepared_fft"};\n   for(std::size_t i=0;i<names.size();++i){const auto p=std::string(names[i]);const auto& s=cost_slots[i];\n    f.add((p+"_calls").c_str(),s.calls.load());f.add((p+"_wall_ns").c_str(),s.wall_ns.load());f.add((p+"_cpu_ns").c_str(),s.cpu_ns.load());f.add((p+"_invalid").c_str(),s.invalid.load());}\n   constexpr std::array<const char*,3> kinds{"borrowed","float_plan","double_plan"};\n   constexpr std::array<const char*,3> sizes{"n256","n512","other"};\n   for(unsigned lane=0;lane<2;++lane)for(unsigned kind=0;kind<3;++kind)for(unsigned inverse=0;inverse<2;++inverse)for(unsigned size=0;size<3;++size){\n    const auto name=std::string("fft_")+(lane==0?"source_":"other_")+kinds[kind]+(inverse?"_inverse_":"_forward_")+sizes[size]+"_calls";\n    f.add(name.c_str(),fft_calls[fft_index(lane,kind,inverse,size)].load());}});}\n FeedCosts(const FeedCosts&)=delete;FeedCosts& operator=(const FeedCosts&)=delete;\n};\nstruct GuiPollStats {\n bool enabled=primary()&&detailed;std::uint64_t calls=0,wall_ns=0,cpu_ns=0,max_wall_ns=0,max_interval_ns=0,first=0,last=0,invalid=0;\n ~GuiPollStats(){if(enabled)event("gui_stop_poll_summary",[&](auto& f){f.add("calls",calls);f.add("poll_wall_ns",wall_ns);f.add("poll_thread_cpu_ns",cpu_ns);f.add("max_call_wall_ns",max_wall_ns);f.add("max_start_interval_ns",max_interval_ns);f.add("first_poll_ns",first);f.add("last_poll_ns",last);f.add("invalid",invalid);f.add("scope","original strict 5s and 10s loops only; excludes natural observation after failure");},true,true);}\n};\nstruct GuiPollScope {\n GuiPollStats& stats;std::uint64_t start=0;std::int64_t cpu_start=-1;\n explicit GuiPollScope(GuiPollStats& s):stats(s){if(!s.enabled)return;start=wall();cpu_start=cpu();if(s.last)s.max_interval_ns=std::max(s.max_interval_ns,start-s.last);else s.first=start;s.last=start;}\n ~GuiPollScope(){if(!start)return;const auto end=cpu();const auto elapsed=wall()-start;++stats.calls;stats.wall_ns+=elapsed;stats.max_wall_ns=std::max(stats.max_wall_ns,elapsed);\n  if(cpu_start>=0&&end>=cpu_start)stats.cpu_ns+=static_cast<std::uint64_t>(end-cpu_start);else{++stats.invalid;missed=true;}}\n};\n\nstruct Case {\n explicit Case(bool target){\n  if(!enabled||!target)return;\n  witness_records=0;witness_bytes=0;witness_failed=false;\n  const auto path=env("DATAPUMP_STOP_WITNESS_PATH");if(path.empty())throw std::runtime_error("missing diagnostic witness path");\n  witness.open(path,std::ios::binary|std::ios::trunc);if(!witness)throw std::runtime_error("diagnostic witness open failed");\n  idle_ready=false;idle_tx_requested=false;idle_gate_done=false;idle_prefix=0;\n  ready=false;fired=false;missed=false;target_done=false;blocks=0;request_ns=0;identity=0;noise_creations=0;output_samples=0;origin=Clock::now();active=true;\n  event("case_begin",[](auto& f){f.add("group",group);f.add("trace",detailed);f.add("epoch_policy",group=="original"?"ordinary":"E0+elapsed_steady_wall");f.add("E0",epoch);f.add("real_unix_seconds",std::chrono::duration<long double>(std::chrono::system_clock::now().time_since_epoch()).count());},true);\n }\n ~Case(){if(active){\n  event("case_end",[](auto& f){f.add("fired",fired.load());f.add("missed",missed.load());f.add("blocks",blocks.load());f.add("target_disarmed",target_done.load());},true,true);active=false;\n  std::lock_guard lock(output_mutex);witness.flush();const bool flush_ok=bool(witness);witness.close();const bool close_ok=!witness.fail();\n  std::ofstream seal(env("DATAPUMP_STOP_WITNESS_PATH")+".seal.json",std::ios::binary|std::ios::trunc);\n  seal<<"{\\"event\\":\\"witness_finalization\\",\\"records\\":"<<witness_records<<",\\"bytes\\":"<<witness_bytes<<",\\"failed\\":"<<(witness_failed?"true":"false")<<",\\"flush_ok\\":"<<(flush_ok?"true":"false")<<",\\"close_ok\\":"<<(close_ok?"true":"false")<<"}\\n";seal.flush();seal.close();\n }}\n};\ninline std::function<double()> epoch_clock(){\n if(!controlled())return {};\n const auto base=origin;return [base]{return double(epoch)+std::chrono::duration<double>(Clock::now()-base).count();};\n}\ninline std::string hex(std::span<const std::uint8_t> b){std::string s;for(auto x:b){constexpr char digits[]="0123456789abcdef";s+=digits[x>>4];s+=digits[x&15];}return s;}\ninline Crypto synthetic(unsigned base){std::array<std::uint8_t,32> b{};for(unsigned i=0;i<b.size();++i)b[i]=static_cast<std::uint8_t>(base+i);return Crypto(b);}\ninline std::string fingerprint(const Crypto& key){const std::array<std::uint8_t,8> probe{\'D\',\'I\',\'A\',\'G\',\'K\',\'E\',\'Y\',\'1\'};return hex(key.mac(probe));}\ninline std::string file_hash(const std::filesystem::path& path){\n std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("diagnostic keyfile open failed");\n std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);\n if(!ctx||EVP_DigestInit_ex(ctx.get(),EVP_sha256(),nullptr)!=1)throw std::runtime_error("diagnostic digest init failed");\n std::array<char,65536> buffer{};while(in){in.read(buffer.data(),buffer.size());if(in.gcount()&&EVP_DigestUpdate(ctx.get(),buffer.data(),static_cast<std::size_t>(in.gcount()))!=1)throw std::runtime_error("diagnostic digest update failed");}\n if(!in.eof())throw std::runtime_error("diagnostic keyfile read failed");\n std::array<std::uint8_t,EVP_MAX_MD_SIZE> digest{};unsigned n=0;if(EVP_DigestFinal_ex(ctx.get(),digest.data(),&n)!=1||n!=32)throw std::runtime_error("diagnostic digest final failed");return hex(std::span(digest).first(n));\n}\ninline void loaded_keys(std::vector<KeyEntry>& keys,const std::filesystem::path& path){\n if(!primary())return;if(keys.size()!=1)throw std::runtime_error("diagnostic target must load exactly one key");\n const auto before=wall();const auto hash=file_hash(path);\n if(controlled())keys.front().key=synthetic(0x40);\n event("effective_rx_key",[&](auto& f){f.add("file_sha256",hash);f.add("effective_key_mac",fingerprint(keys.front().key));f.add("semantic_override",controlled());f.add("hash_wall_ns",wall()-before);},true);\n}\ninline Crypto noise_key(){\n if(noise_creations.fetch_add(1)!=0)throw std::runtime_error("diagnostic target created more than one noise root");\n auto key=synthetic(0xa0);event("effective_noise_key",[&](auto& f){f.add("effective_key_mac",fingerprint(key));},true);return key;\n}\ntemplate<class T>std::string serialized(const T& value){std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(std::numeric_limits<long double>::max_digits10)<<value;return s.str();}\ninline std::string exact(long double v){std::ostringstream s;s.imbue(std::locale::classic());s<<std::hexfloat<<v;return s.str();}\ninline void plan(const char* role,const modem::FhssPlan* p){\n event("resolved_plan",[&](auto& f){f.add("role",role);f.add("present",p!=nullptr);if(!p)return;\n  f.add("channels",p->channels);f.add("dwell",p->dwell_seconds);f.add("power_ramps",p->power_ramps);f.add("defer_symbols",p->defer_symbols);f.add("mandatory_ramps",p->mandatory_ramps);f.add("matched_gaps",p->matched_gaps);f.add("smooth_ramps",p->smooth_ramps);f.add("remote_guard_covered",p->remote_guard_covered);f.add("ramp_seconds",p->ramp_seconds);\n  f.add("clock_error",p->clock_error_seconds);f.add("peer_clock_error",p->peer_clock_error_seconds);f.add("region",p->region_seconds);f.add("offset",p->offset_seconds);f.add("audio_error",p->audio_error_seconds);f.add("fallback_reason",p->power_fallback_reason);\n  std::ostringstream shifts;shifts.imbue(std::locale::classic());shifts<<std::hexfloat;\n  for(unsigned i=0;i<p->channels&&i<modem::FhssPlan::capacity;++i){if(i)shifts<<\',\';shifts<<p->shifts[i];}f.add("shifts_hex",shifts.str());\n },true);\n}\nstruct Interval {\n const char* name;std::uint64_t begin=0;std::int64_t cpu_begin=-1;\n explicit Interval(const char* n):name(n){if(primary()&&detailed){begin=wall();cpu_begin=cpu();}}\n ~Interval(){if(begin)event(name,[&](auto& f){const auto end=cpu();f.add("wall_elapsed_ns",wall()-begin);f.add("thread_cpu_elapsed_ns",end>=0&&cpu_begin>=0?end-cpu_begin:-1);});}\n};\n}\n'
PREIMAGES={'tests/test_gui_controller.cpp': '3730678e53ef37e295adc319b757b527ffb6a6505bc5424955958658df56ce0f', 'src/gui/controller.cpp': '035744048b5532f367d565c20511f768a49951d6a7cc7e5e4b842ff7061d7891', 'src/live.cpp': '789735690478e9c7331487740c475e862293df69a49ce7a44b6793a3909415ac', 'src/pattern_code.cpp': '3e837e010ee937136b0cca72e2200b2c3de938bec344e0292478d5e1cde05cd4', 'src/channel.cpp': '1d484160f58b0955a4683096dc756fdd3086a28197dcb54ab2ce6861384c2515', 'src/pattern_receiver.cpp': 'a5860a8130a9e6d851c7b8c68bced746c2c12a9a6ad7c0bc15c8062b6ecd52c8', 'src/pattern_fft_batch.cpp': '14cd5c72e43900497538bafb83a8fef3bf55d30d2342ee1e9880286409a7a7ae', 'src/search_fft.cpp': '1740668a7f04dfa20c1f73c35709cb5d9696513b9dc047b31409072c134feebc'}
READ_PINS={'include/datapump/symbol_burst.hpp': 'fa9b107d82f161f345a07cc1093693451db8758042040162d88b8c82a7d41cf5', 'include/datapump/simulation_estimate.hpp': '182611c646c30ad8ec49c69a5e9385594c584addbffa566aebef941ae3ef33c1', 'include/datapump/pattern_code.hpp': '3f42d8076a1cab65ebc27760313d3e577dae84696b21dd172f9abdcdbb6f80e4', 'include/datapump/streaming_modem.hpp': '8daf14164e75b427dae0f9a1337d00398b4b4164003a86d79d8aa8cb8492e20f', 'include/datapump/pattern_receiver.hpp': '7da6a7ab2aa261fd65b5be1a31f81c3b22f6efc9517cd819dd2907fefe022b13', 'include/datapump/pattern_correlator.hpp': 'b170b63bd2c351d2c87f094fa8609e206edaf5c7da0a44df96cb8122a6b79eae', 'include/datapump/scheduled_complex_search.hpp': 'b8823edaccc0d6e1d23b233d68ca3d6fc9cb68684795310090aa0d158e028f9f', 'include/datapump/audio_provider_timing.hpp': 'a2952a611c6f32112c25c09c7b8edd76d48c3d58f0424b1d532ad0dac3430098', 'include/datapump/scheduled_symbol_calendar.hpp': 'e117f888b05b12d272c0a1556b853d4a63c5690ebe5e52777fd869b323c4a9dd', 'include/datapump/sched_hop.hpp': '46a324da63279d58c87e9200d32862a240239b3eb1bab70fc20a1210f8de2346', 'include/datapump/receiver_strategy.hpp': 'd7555b4ad294ab312b07c350fda56833f607128f6a0a6524bd1479fdf5c54ccd', 'include/datapump/fhss_schedule.hpp': 'dd37559ca3bbd769477b3a28b5cc5fc7f8ec862eec8e07b9214036cb84af1f9c', 'include/datapump/audio.hpp': '9f6bb632f65c360a49a686cc8d2fb4b30d3250482aa166ec4c46fb27e68a8514', 'include/datapump/utc_timing.hpp': '1f19f14821277b58fcb2526d5a896dfcce2f3ad2c34b59f856bd008bc50610a4', 'include/datapump/clock_sync.hpp': 'dae640fd90eaad12afb5ca33ac36109da13db10d684929978df799ad4c7f761e', 'include/datapump/host/sandbox.hpp': '42355b5e09fa83c7c7c636bf69225e5acc317c85be9c73936b0acc135ebc5aa0', 'include/datapump/host/protocol.hpp': '3009d12885f9f2386fedf0dd0456fd49baf3110d5c93277938114058e803b6e9', 'include/datapump/host/audio.hpp': 'a3dbfa5fa33cc26a94c2c63ff24864d756aaf4274bff05f1d55381be3c402155', 'include/datapump/execution.hpp': 'f73931d528b57d9f23cceaa41a1043c0891a9d34a178651c4a6f1a2f0bad8d50', 'include/datapump/live.hpp': '3ce71a65f29a380b4b11f84666897157a17bce363df9a48d1b36fdfd5e2fd266', 'include/datapump/speculation.h': 'a258af6df4ba690e828e3331070663d27f0c3c15bc5fffa37cbe2aec26a418a6', 'include/datapump/received_text.hpp': 'cb2915e00c6f23874068dcce1173770a8329a69d31f978b3766680bc10ad875d', 'include/datapump/legacy/session.hpp': '1d299c0571ecba55de8a609516b493e16b19db9a7d84920be302784eca25f23a', 'include/datapump/legacy/modem.hpp': '55e9eb9a73ede31b93c2c32a2b1ce2a9841e486b760df1a5a65815b7ca9b54e0', 'include/datapump/fast/session.hpp': '7cf7b0d9928b6fdbdb6a131601876e836e28d19541356618e02210212a834db5', 'include/datapump/fast/attachment.hpp': '8e45f2a07a1142520548190f994c18db868657e3569530b5f102132648073e4c', 'include/datapump/fast/compression.hpp': '7a9de3138e3ca32fd96a1924b40f8adf26a8623fb7fe430d6a840f11918a0591', 'include/datapump/fast/preset.hpp': 'f77100aba7b90dde598a1c9727c00a66db08de002b35fe6126e8f9eb080361c1', 'include/datapump/fast/outer_rs.hpp': 'f8760161fe49fe8603ccc020d7fea2bc405314c3c7344ff56369faa412175adf', 'include/datapump/fast/ldpc.hpp': '4343cc6c08c6a98721e707b6bf2112bc6ad811630d334017660d0b391b0bcd63', 'include/datapump/fast/telemetry.hpp': '830e9b06e18f9c90f32ba223a2c6ce3d43c0c7abad1ef6f933743f52e7b824d2', 'include/datapump/fast/cli.hpp': '11b4d23e4d73e325c39eb00210d80727432ddb4797876540996fca4788262afc', 'include/datapump/fast/file_transfer.hpp': '8cb869df13ac9d767b8b34d0684aebe340169a7379b1e0cffa56d20818e573de', 'include/datapump/fast/modem.hpp': '94dafd504f6c9afcd9dc0940f7bf6edd5b8b1a5374e5273944382f98ddde9f54', 'include/datapump/fast/codec.hpp': 'a758d9f20fc5e31aa82170611ad5579f65bc4583b74f8d67f99560bab076ee6a', 'include/datapump/fast/profile.hpp': '85253d5fe880dbb5ca22f9de82125791ad15cea1b6de6607115e6c7a89a5a5e4', 'include/datapump/lpi_estimate.hpp': '40096bdfc9fd8e44398379c4979c45ac7c9a83eebd5a5db0964937fe47ffcc94', 'include/datapump/correlation_experiment.hpp': 'c4a57e5fa7b37ab1bfeb012640fda17f2192a70f3c973dee24b9b562bf03e08c', 'include/datapump/pattern_search.hpp': 'b4cd227945349a17f61a06ff40f8601e5570c4e211372e5a29820256e67385d1', 'include/datapump/recovery.hpp': '412b8364f5169071c7fca8937ceae58fb3b4928da8ebbcb89ece9f2af9695313', 'include/datapump/transmit_trace.hpp': '3a4355c447ef328567d16f12a340d68ff03bcfcd1f61fe00eda9cb62864f2a9e', 'include/datapump/attachment.hpp': 'b4abe989f47fb43a6373a2319188ca0c1546951d14bf48279127c9cb3f9e29e6', 'include/datapump/stream_codec.hpp': 'f5db9dc5e2bb897f253c1802dc47311f09fa389c56aca08666a5d26cebe69064', 'include/datapump/symbol_schedule.hpp': '89c48559591a0c594e881eca1685acad1032ac392cfddd9453455535e89476a4', 'include/datapump/pattern_pulse.hpp': 'f3053ed41fa5cbca182ae4dcf5ff4796b030a3cb15527f617d7a765e7f989312', 'include/datapump/boundary_sync.hpp': '308185d95b0b742aa4bcd574ca1a64e0efbb81d9ca678bdb3ae926c4280d5eab', 'include/datapump/compression.hpp': 'df2d76fce444f831ddd3eef53b1dc1c908cc31dc6f155ab97db04a83a369ce71', 'include/datapump/channel.hpp': '69c588c2285a9275a22148068b5a39fcc2fed04f1d65a17d272aa195911ced37', 'include/datapump/resampler.hpp': '47f0f7845e529cfeef7aa26649d7c4f8bc45d95eb2f5d84fb0f4675a223a529c', 'include/datapump/tuning.hpp': '96a4fcb20ec64e2f2f7f826addbd5305083419e004491935cb6c0e45448a1804', 'include/datapump/transfer.hpp': '877f15741329ce37b0f9ffe218ea8facdc5f7a7233017a03e7c685f3b88d9506', 'include/datapump/qr.hpp': '37a2931eee0b44ca4c930336fb7929e37c552ffbd2e37a87ae71768ca3baf2ea', 'include/datapump/modem.hpp': '68fd185bca9f4fc53e9b6a2236926f44516fdf30e7739408cd71515bc1405105', 'include/datapump/crypto.hpp': '14e681ae6637b453b05fae4125b300e6bc27ec2ec4c2ee449fa3b2ab97214b74', 'include/datapump/runtime.hpp': 'aab4bd434aeb3549b733d84e050fd6e0fbff71856644a89abdb73b0ba0fc87d9', 'include/datapump/types.hpp': '351fc7936aa83b2f2b7df25d54ecccb0b4b9471edd4376b3b842e7ffef10212a', 'src/pattern_fft_batch.hpp': 'b1a0c6f4653216b1ab04a1df5e123282abb94f46333ba270a98817212ddb5dbb', 'src/search_fft.hpp': '97451e9d1e2a83be320f1f36ad7a67ea212900298a8821665504ab1c614fb2e9', 'src/amplitude_probability.hpp': 'cf70b23a0c2e1a2b9bec54799be7496fff24deece6bea3294569a8a610f4965d', 'src/eme_probability.hpp': '90b059903a2f00998b1f007344952957525d93c7d19ca8ddf4600205b1a08010', 'src/scheduled_fp32_coherent_prefit.hpp': 'dc5b2f193b8ba6903df6fcc3a11bf068d74cd29ca2187d4537cbb75e842c0503', 'src/scheduled_native_cohort.hpp': '9b462bf898a4844e319be55ffc8fa3e53ff45571dfa68a976b3dd183c30c4da1', 'src/scheduled_exact_source_points.hpp': 'b87aae8c65c3396df985c6cfcc8a4de8856084e50785e5e4ec9817c953cf8283', 'src/scheduled_exact_source_samples.hpp': 'd31bd9056927085140f01fb9739268274f84d06d96865077ec44be17d0fe7dcf', 'src/live_receiver_layout.hpp': 'b50432b93c87b6a7467a74f745f84a74e0ae780c187c1eed1083bdd761bf261c', 'src/scheduled_source_binding_types.hpp': 'da33f066d9d482ad39499591d226a092aa78af40939ed71f9ea0206766a3186f', 'src/pattern_observation_layout.hpp': '9db9e8a902133c5e4554fffe7d1f1b282e23443538374e3a7fcd09797616065b', 'src/live_receiver_admission.hpp': 'e83e43e14d1d0e248eb82a417a46bb7c86eb9bc1ca252014c2a8889c19b37196', 'src/scheduled_original_pulse_row.hpp': '5b85bfa57beed1b6d5ab0e3a311540ec9789a9ee11bd49f21a22cbb109601af2', 'src/scheduled_default_point_diagnostics.hpp': '35c56034b878e3f068a8ad1656955752c60f80ec0f75c6c5084c874fccf94695', 'src/scheduled_default_point_guard_archive_rebuild.hpp': 'a56af3afdacb2f6ec972d39969725361658978e1e87a399a2e0edb26a2074260', 'src/scheduled_default_linear_runtime.hpp': '3b93e951fdbdcbccf11c0cf4d1714c51bd9a218177b82f142b76c98b1c8f5919', 'src/scheduled_default_point_bulk.hpp': 'c5b1f6a20db345b7dbc3beda3744dcf5ecb61d7988a5cf3acc488cd9965b734a', 'src/scheduled_default_point_fft_runtime.hpp': '5f29a9ec6674414ce030c83026cf912997f37faa49cec7e76389188f7166d667', 'src/scheduled_default_point_reference_recipe.hpp': 'c3fb64c737655af5c9516ced156a537f43868e48078e1997944afd7a7b0fe6b0', 'src/scheduled_default_compact_chords.hpp': '3d523884e24b52a17416b2fb5cf19b78f4099194910973281a4fd9462dabe391', 'src/scheduled_default_point_guard_summary.hpp': 'd4c042543c296e9d4507790fa7f3647dfcc0731a38c093b8e9cef762c39ad727', 'src/scheduled_default_point_guard_archive_runtime.hpp': 'cc68ec6ff52868e58381f3d337b026959c6caef5a9662bbcc9427cadb76b3721', 'src/scheduled_default_point_family.hpp': '2dc7bf688b396a7cc6c6cedbf4239d9f3b42c7b9f57a8fd243482fdf58b74e72', 'src/scheduled_default_prepared_events.hpp': '0e3c9cc5d1193c02d14b5f8973527bb991ea65066bb35962d8169179ccdebb87', 'src/scheduled_default_point_guard_strip.hpp': '4e43c2847669607520b34735e45733b725c85b02d7e7f78dc6140456947e059c', 'src/scheduled_default_point_fft.hpp': '98ed78e60a8e2f970a89bb3024ac375cfa01b47b632867f6451928ce34c1bb96', 'src/scheduled_default_point_template_cache.hpp': 'd8fa97589fa7a95842e9b926c85f21f61055e71b0681135c4000c224572f31a1', 'src/scheduled_default_point_guard_archive.hpp': '7c31f1f8d77b74e54d91ea486b158ed08979f538bb42613d4da04828fae7b26e', 'src/scheduled_point_source_nodes.hpp': '69f8748f548757e0aed5808519e3c26649eb9c2cf77e93e1d47d190eb84e287f', 'src/scheduled_default_point_piece_cache.hpp': 'e8032df047ebf434e4ac6a451dde0183afe2a9f47a87758303f553c5b617bffb', 'src/scheduled_default_point_observation.hpp': '9b653f6c7bfca78947d0b86fb228d76c8d1a5559eeb7cefe63b2d460a9c73ce6', 'src/scheduled_default_guard_cohort.hpp': '7856dbc6e10229f36b3da26ba48051d23ad87de4923a7941dabfd1b8987e413a', 'src/scheduled_default_linear_events.hpp': 'd77161d06db1b78734e00d829a071c0a8642d434e98c6e83522caec988ba27de', 'src/scheduled_default_linear_cellstats.hpp': 'ad43d179231307498f65d531380b6edfebf44aa86908cf6240dc8bcf5f8a3721', 'src/scheduled_diagnostic_journal.hpp': 'bc9696bef2f86fef296925050a3b97bc3fe92730b7e1fce88e118d3692b164d0', 'src/scheduled_linear_fragment.hpp': '64d70123273794cd32417b9054f0a5fd24a7e85557f50126dc668db9ac64ee3d', 'src/scheduled_linear_family.hpp': 'a0052c8664310ecb77a84c39bbf9649151a97651c1cca39b563c9e43438ba13e', 'src/scheduled_linear_gain_cells.hpp': '294bfde133b667a50851edb6efec31a8cd8eff65c4b9d4d2508e4b5166d6199a', 'src/scheduled_linear_native_bound.hpp': '90e6710ce664ebe36893f7d2ba833dafd0bc7fed8ece8a3bbbcb79f5b8d08333', 'src/coarse_pulse_cells.hpp': 'b879d8d1f19a5968bb234c148bf518b6a60d87e9712115849d9f3a74df28c68a', 'src/scheduled_zero_guard.hpp': '2dcde8752f3a18bff686631d935a765d2c677c4d89ce37d5cf12c6cbe62d3a03', 'src/linear_grid_moments.hpp': 'b4811cc826e0a7bd09b8d1374184b487b5b96f00aadf4b5102900c9d204a6de2', 'src/streaming_linear_grid_fft.hpp': '07f18e8a28643b40fda030b9a08fca4f2675030cc32e0804289e431c098e2b92', 'src/scheduled_linear_grid_fft.hpp': 'abd2108b13755c2753d2334444c965570cc1529c5da5cfcaa8f983cb6e82b45d', 'src/scheduled_template_cpu_attribution.hpp': 'd7be2fefc6c0d569f7614bc6417103b54a46d90b7355ffc76ee1f865d0f2d1ac', 'src/scheduled_pulse_derivative_chord_certificate.hpp': '5ecdf8fe57f6f431425d402383df0eba9fc42b03fede6e7646ef91a72525a89f', 'src/scheduled_origin_gram.hpp': 'e15e18c3e03016b9e9bb6ff6fe71e659a76ece57fbe0a419bb046fbdb35f6488', 'src/scheduled_gain_chord_certificate.hpp': 'a75523827acd0752ba0d3d9eb54837f97d69761011e85a2542767d80c471146c', 'src/scheduled_owned_moment_runtime.hpp': 'd23d19324ff86846fd688db036dab8f10855a362a2d520dd3402589a95d8f00d', 'src/scheduled_owned_moment_correction.hpp': '54e07271521f5c88df15367e59da295565a739942a87fd27810ddf750a332f90', 'src/scheduled_sparse_carrier.hpp': '45ce15cdb771a8fc3e8bcedd8401d52ee4bd58bb901bb806e47aa57654ef628c', 'src/scheduled_owned_fft_window_filter.hpp': '534ebd456e72353342be0e42fa928cbd9454d12a32e5c758e4a4ee0ce8e31b51', 'src/scheduled_lazy_family_runtime.hpp': 'b7143fe4440730bb429fcc440374d8eb0edbb51635701f3a15ad408270bc8d82', 'src/scheduled_frequency_moments.hpp': '06246cae46dddf22c2a4cee8029d4b973fb85c2eec507337a7aa07a27994f261', 'src/scheduled_polynomial_chip_moments.hpp': '6f653fd93219a8f7eab7df3b1854029b780164d7275d4f3c5c4aa7d2bc606caf', 'src/scheduled_polynomial_chip_runtime.hpp': '66f0d8916bd6c269ca2e2aa29aa072b9a85d40b25878b8f232d7633d100cac76', 'src/scheduled_owned_field_errors.hpp': 'ef15a41cca67c9945c25ce4177cf63ec261ca974051cf9fabde8849082182ab4', 'src/scheduled_sparse_owned_spans.hpp': 'a1628a3f663d75dc09001807ca2edc5e5b44e37878fbfd35dbfe3a69ce5a726a', 'src/scheduled_original_boundary_partition.hpp': 'a3907872766905b19faa16f19fb744bf34897855677fd01f6ecce5b2d0d785cc', 'src/scheduled_acquisition_tile.hpp': '5133ba98eab1eaf7a66b237bba87909434a6c1d942bf73f45bb17bf5b1ebef4b', 'src/scheduled_origin_gram_contract.hpp': '611ab2787edf7a08a060d58dadd9cb18cdc8da1f78bd845f7c2fb006e65d019e', 'src/scheduled_pulse_chord_certificate.hpp': 'bfc6e2bc7a6f1b2dde2d568364d59e6005b3b50124d208c7be7d4004a0dd13cc', 'src/scheduled_fractional_basis.hpp': 'd74f362e51fe1a286159eefd18fe47c6284af81b3db5b00bc2c4b1325a6c5888', 'src/scheduled_cold_owned_proxy.hpp': '683628f06ca257afc84436874c63afc5244da9a14191997aa4f0d18a7482df8c', 'src/scheduled_cold_colored_source_rows.hpp': 'a6f56f90af5c5bd90fc5931e6e57e0fbc59e259948de2cad7d27e0574f9d5ac3', 'src/scheduled_cold_colored_certificate.hpp': '9585b0fc8fad14adca41e3366fd6f5675c6cffddf8c493a4623e8963fecfbe03', 'src/scheduled_partial_colored.hpp': 'b9654f7fbb1dd2c470fb5cc81842d481edb0dad27b369b5bb484bb0ffb97fe3b', 'src/scheduled_partial_colored_runtime.hpp': '0fd7a286e4e7ff782f5fdb1c116e09c29d291fd9453c22b19234c4b37481ca1b', 'src/scheduled_acquisition_policy.hpp': '855c1454cde021e7761473122b279a074ddf563c77350f5daef35288baa2d389', 'src/scheduled_polynomial_chip_fft.hpp': 'c3d42ae80416f5038ef23c84c26dd7ff935bf1aebd960b4dfd1b3f73fc256b81', 'src/scheduled_residual_prefix.hpp': 'f5f6aa9ae682595a88b29353e60be02fd9847f7e098cd26dde4953ad343ac5fe', 'src/scheduled_acquisition_certificate.hpp': '2dd75eb9bbc99fee9f4a1e588afc3abc444ff81d5eb64d6a5c1654aa6f0b9e36', 'src/scheduled_moment_synthesis.hpp': '6b7e7435ad4f2b8d48fbd188b91d55bf1be2ad3e4eeea2c28901a3feeeec58a7', 'src/scheduled_spectral_products.hpp': '211369545b24fa908f5dd56aa048c83e6764c955e51631fb85e8b05efc47f705', 'src/scheduled_parallel_preparation.hpp': '7042b79bb57e28d793c316ec8c1ba68a1753bb19a8aa2f759e47958b5106f7b0', 'src/scheduled_cold_colored_proxy_runtime.hpp': '251de3617daf14f45126754f6a1e941060f9c13a2f253978f2211ff7275f266d', 'src/live_deferred_pattern_history.hpp': '2edb4c2011354d77b679299868cbf0695ca8fc9eecac1759bbac326c6abadc7c', 'src/scheduled_shared_input.hpp': 'a14ee40d794976920630048c79a64fb31fbb273d411090c44c6f9c2993f04bcf', 'src/stable_owner_store.hpp': '7b6c934c2cfb52eba174d8b47c2184c3bcaca94cc2a9b89de8c5c861601fcb2a', 'src/scheduled_acquisition_quarters.hpp': 'c0b86d3e6d254750b540150cfb78ba08b1f47e048f13c82584e7e1f6d934b4fd', 'src/shared_scheduled_capture.hpp': 'd1ee36d88ac68be293dae78941369cb19252e46682140ea25968f8ecc4d22558', 'src/scheduled_template_proxy_certificate.hpp': 'c73d8771c376f5230327d95a18c67df7fd1ab1b37fb857157f444396f77a4df2', 'src/scheduled_resume_state.hpp': '43b613797a17b026e02bb9b709174aafde064ef7766688dad51a6440ac0dc7cd', 'src/scheduled_processing_journal.hpp': 'd3795101d763ea84db48f98438be76615f0a2bcd447dca287b9c70bab5a5e0b9', 'src/scheduled_original_projection_cache.hpp': '3f7fed2c4afce5d592ee37ea54dc871a2fd95bad65bcdb0a5eb9e3a35d86f210', 'src/scheduled_frequency_recurrence.hpp': '51ffcf4aff0c65a60dbb1f96d0cd595db65eee5857e6befcd2ae6f8747f0a1a3', 'src/scheduled_frequency_postinverse.hpp': '27574b038ca638e30bf2efebbc228ce28a2e2d2531224efa15cc536581196a33', 'src/scheduled_float_spectral_accumulator.hpp': '2488bc70174db5a756262c0fe372f8b053c66353d004b02384a289c65d702dd3', 'src/scheduled_cold_colored_runtime.hpp': 'd739b3fe1b9c8a437b2aa88e00fa657908627145e39d7b61f62b0406942805b2', 'src/scheduled_certificate_diagnostics.hpp': 'cd93f62598871b07bc530a580d802784fd949875384512f88c50b377675db9c8', 'src/scheduled_carrier_restart.hpp': 'b5ad6a8c99f66a1f38383329a551066755d4991505500ffb1fbfa1e0c1ca440a', 'src/scheduled_acquisition_memory.hpp': '9474d70ad9a94858299e2ed2a934ec5b34076c7ae5c8b2b029822cb1fc9a636c', 'src/live_deferred_input.hpp': 'b12ce3ee6babc7fea48914cc549633832eaf5da71dbf1cf634234a2117b75986', 'src/scheduled_acquisition_catalog.hpp': '9b56bde57c9848a862a09228a1808b59c14dc8fa55b4700eda8bf834a66e2809', 'src/scheduled_private_batch.hpp': '02cdd072e16b5c3e606a06c2a933f26587769a1d496c75d5ccc93a558d1b27ac', 'src/scheduled_frequency_piece.hpp': 'c7d1275bb04fda284586fc8dbfa2d10289d945825ce250e77a3001af5e6b1f0d', 'src/scheduled_ordered_pulse_candidates.hpp': 'e73fe0fc09d5660f5735087ccca23b0dc6336227a77cd0eb890c20ccf1ef0fc1', 'src/scheduled_borrowed_plan_prepare.hpp': '27c20c637f893f352e0b17e6430de47a65b3c361f607d8f24e6547d1f1ff2101', 'src/scheduled_outer_public_replay.hpp': 'd5e2907205c74876d3a5768a01641f6dd343bec056c5e6293caf72797771e98c', 'src/scheduled_fp32_pulse_tile.hpp': 'df13f3c4a1d00bcb6352446145cce1cf7788036c38b73e796790f2c85efe33db', 'src/scheduled_temporal_templates.hpp': 'b0c0ba7299c0e0fdb469833ff53889b666daa1871b5d0d148e5d5c1888248700', 'src/scheduled_temporal_fp32_execution.hpp': 'd0a2a79d9b37bf87798031333ac63d44f6cb3c8ea6f0adfa039edc97dc14d248', 'src/scheduled_shared_temporal_plans.hpp': 'c9923232cbae877ab1bd2f17538fb87b87e189ee6ec3bdc6950fad9cd2774efb', 'src/scheduled_temporal_plan_cache.hpp': '2f86db8a7ecb5d391373389fe212387b60485d6fe2d232f5b147de2a063c2bc8', 'src/scheduled_flat_export.hpp': 'f712c95d549d8567216a3228ac3b9b650c69e7952d392eddd13e6d0cada319ee', 'src/scheduled_r1_export.hpp': '029576de5947ceb32c02898ad578f6373c035d0f3f57f7a9940730cac6042da8', 'src/scheduled_work_profile.hpp': '87a701a4ed853685e4250805a8665fdd0f10b15347ab0f4f8be1fcc8148b7acf', 'src/pattern_permutation_cache.hpp': '77ec79cfeafc3336816d921392028a0865ff9435b7c4417cd437474436fc9e43', 'src/pattern_coarse_cache.hpp': '91de6e4d5791959dd1db1f659a5626b6ab5c8016b91904e3af56d557ed30819e', 'src/pattern_projection_cache.hpp': '5ec96963e9c284028105f3af4330bddf309f2f5c570bf39ce7a56cd15dab6ae3', 'src/scheduled_temporal_normalization.hpp': 'a59177fa8c7326014cc6333901b6947e3c3afbe831cab07257a1c067c33fc1fb', 'src/pattern_physical_moment_cache.hpp': '49e8309e1162970ea17d6766a9833687070bbf939c08a29f259d8dfed89bb1e6', 'src/scheduled_mapped_piece_groups.hpp': 'a0c609358f42dda39ad2550ba6ac0ca3368ef63076f7cf8dff5ab47856e71e9f', 'src/scheduled_float_gram.hpp': 'bb02b4edcd95def9746b1573616a4089f971add555d38c5f155e64db746aa464', 'src/scheduled_common_simd.hpp': '0bb248351fbf32a7770edf863249a475cea438d1ed52879e1ee9f8c44e94727d', 'src/family_simd.hpp': '2ff8f02f8311786e3bdf8174dd92febde8fb7f4694b851d39739bd4bb404443a', 'src/scheduled_float_signal_moments.hpp': 'bd5286afe432db9156a0704dc76590f469e40726f319ad27f22e8d48cc6f5b40', 'src/scheduled_pulse_moments.hpp': '216687165c9e869b65d1a8ccd1770bef9f3281b758a26d87ceb7fbab57b2ee20', 'src/pattern_shared_source_curve.hpp': 'e75ec3011212d5a44f7c773881023bb29c867d0dc7a6dcc5280e13a73710d958', 'src/scheduled_frequency_fusion.hpp': 'c958eef9a4470c196ce3b61722dac3034405ceb15a1e2f85066701673546f294', 'src/sched_capture.hpp': '721b2bb77782acf31a8a5618984f8ab0c43d3ee6b79a30b7cec0c8479f6f3346', 'src/pattern_envelope.hpp': '75e660d1933ad3bb54c5def1b6737018e1bc911cbfdee9ed28e3ad5627fa3aaa', 'src/audio_trace.hpp': '2e476953b87dbb555e5734357a7de68873709070a97986b6b48fa82397bc52d9', 'src/audio_realtime.hpp': '6029e60cdc97673eb8d1543c21c4266ea534b5ddec58e2f3621705c2be5f7ce0', 'src/live_scheduling.hpp': '97b2a6c3a54105ba8fb54f621307c64d0169ef0932598cb21aa98bf008a4f539', 'src/pulse_playback.hpp': 'eba7d1bd97749344a55a70232416d9f86477d95262d067de27761a0a3f4ac964', 'src/search_arithmetic.hpp': 'e9bd83c4a37e05c19f82d411c310e6424f5107255887cf7391a45b3621666714', 'src/estimate_cancellation.hpp': 'bee57d243de5f2b4f6b82c9198cab915e3ecd0e02138a86137d358b92df5de21', 'src/lpi_hopping.hpp': '280c66cdeac9bdac39e8917f423c1634c013187738ad4d24d329a32e86af775a', 'src/pattern_fft_window.hpp': '9a57f5bc828663c4e48501c31083a8e88587ce90e7fe74af33267f723b4940ef', 'src/pattern_start_geometry.hpp': 'b8da96d6f5f7d4646a74acd7c8e90c5c6479007c614862378395bd70a1b2d42c', 'src/frequency_parse.hpp': '298e793324da84d78422944f10696f6abed79f21b6bdf6517c6d4c611d1517a7', 'tests/test_environment.hpp': 'a931e0c148d9684e3888140f11f4876e7c7198cb05f6d19e62de460f7f7c7e32', 'tests/scheduled_default_linear_cellstats_reference.hpp': 'ca7122def3109998fd9e70ea5dbe4dbb976f2a5289add830113d652e974a5c5c', 'tests/scheduled_linear_family_cell_reference.hpp': '1a993d8c881dd48817c5874ec5e26cc52f47d1de7799be0160e48c9a262024a0', 'tests/scheduled_lazy_test_fixture.hpp': '4e4b4abb1204f05f016a65adf46fe48d4c1881888738bb341e1d6c466e7d703b', 'tests/alsa_stub.hpp': '2479ae9c3f5045bdf4f9bf56c79f8a836ca4eb33b26b3ac0e9ec741fac878ee5', 'tests/cpu_work_budget.hpp': 'dda76f51613e23b71d8cf838c12c1a2e05fde348bfb9d85ab0bfc56767702ba3', 'tests/gui_extension_fixture.hpp': '687708993c74d9694aeb243d61f45cce2d502829f0f27c05fdda96ac3847838c', 'src/host/sandbox_policy.hpp': '682f9f27462b83824843ccc610548c7541f324fe5d1a2ced2cbe15e752ef15c9', 'src/host/runtime.hpp': '663a7b4a083ec022968a54fc90b8ac68ba548e58ce4491d26e4595e76a44b37a', 'src/probability_random.hpp': '2ca740f7cb3a6f6feeb8013626cbc227d6ee5b80bdfe7d7595a8c723d9c36580', 'src/pattern_carrier.hpp': 'f3506de31ed523aed3dbed3eff505fde71f3a9f4c677793c32e6052c6860332c', 'src/alsa_plugin_path.hpp': '8ed3a5ebe28d2ff2c2b676dea57bdf3157144fe13a4e0e705151debdc6465b1b', 'src/transmit_epoch_guard.hpp': 'a53e43ab1ea4df7ec6d0bd89bf6bcb697579564b1a91a6f2e053c4d194ee6586', 'tests/legacy_simulation.hpp': '81de812d84868196494708af5d58d0f5c6379b78009b839456370023d57497cd', 'tests/estimate_warning_fixture.hpp': 'f5eccd05ffbafa0b9531dd6dfa69fbdb0dbd11d3f9dbbe5f55cacc72459a8b0f', 'tests/overlay_fixture.hpp': '328323f60b1a41ebf0dc5f24aade9184f811b3923609d2b05a98131ac83eb520', 'tests/document_geometry_fixture.hpp': 'ee37586b7dd252ae7116e3e3eecc35373b88b7ac92a69cd0e180a97004d57e72', 'src/legacy/olivia_codec.hpp': 'e607f3ace721cb190d1fb8996022367c200fb1e04c28386bed71711172fb4e88', 'tests/rev_event_stub/windows.h': 'b2057306ad38e1a9e2f3fa97ab281af017edc13418b8a211e8819dcc99f6b431', 'src/fast/filter_history.hpp': '34a2bf856ee719fd46a9b07c6a7a08942e59151f740e97bceb140aa6f971adfa', 'src/fast/session_state.hpp': '78a6d74d93d60f452b2e24a8d7d10d2fb6b2747a8fd42cb2deb09f8bd30d61c2', 'src/fast/acoustic_ofdm.hpp': '9374123f0e9bf434749677e9cf3ad46375dd2c94b636c30f72ca84e67cec650d', 'src/pattern_differential.hpp': 'cf5194f100f5e7e3c6a996a424a10c53a46b9e4a96cce389525d70f24eefa71a', 'src/receiver_probability.hpp': '030a9c7e8ee1cea83fe674278deb40cc4cdf71d8fc904d4455857a57ab5e634a', 'src/pattern_drift.hpp': 'bfc9d395e44692c73866e42544a615619192ca8a098454c80d8c9216a3469e83', 'src/pattern_correlator_batch.hpp': 'd973d9a8c825f19920c0bbf4f108127bc2fa92ebdbeb60ab2b30f198dbd23509', 'src/search_parallel.hpp': '4da8151e4dc0c030100c2d2aecfe17bfc4f3e9ca50b72c46b58c6de7a3096887', 'src/live_receptions.hpp': 'b8a5cc917e20fc48670f53fb43f0db457a41c6f8670e52a1e10a658c7984c9d5', 'src/live_pattern_scores.hpp': '50a576a5ce25b326f471bc5c983365e5ebd2e25e13baee2de269ed3b6522b77f', 'src/transmit_timing.hpp': '329745c3abc30e2a4ed06b6a68cdede46ea3ea22deeeb9d0d833602f87dded35', 'src/signal_view.hpp': '7de061c907442183fc9c9e12c1d0079095eaef1c9a014421afc22bf61ccae97c', 'tests/winmm_stub/mmsystem.h': 'aca8843249b382056adeb0df254189a41064c31dc57e3566e4a50e4175568e3e', 'tests/winmm_stub/windows.h': 'dc10472193822d3a16f6e2a7c8818d1d81b941655b3646833a0ddf851b6d7df2', 'src/gui/theme.hpp': '519a29f075f07cc20ee1b8889fe94fe52e5fcb3f5ddf7ba6dc6637df0b093017', 'src/gui/theme_fltk.hpp': '74fc163c5f7605a4c5f8bc8fdf8b9e2a867d0632f55392250f2ff7f6b5d82a69', 'src/gui/inspection_model.hpp': '60389795d70905e95509318c0fb193fb16f4572e5da16198f7fe65ed3e643095', 'src/gui/plot_data.hpp': '28c00a451cadfd432548de5ae12d045fa8c04efd4f330b345acd568ba679cecf', 'src/gui/plot_render.hpp': 'ec0983d3b0c7f37a86a3a65327c827256280229f9a8cecefadc7e884411e799d', 'src/gui/bitmap.hpp': 'f75d45a1f628d01632acb06733a3797552a5c00fba7812699fe20a65725a80c9', 'src/gui/rev_platform.hpp': 'a9c31753602d87c8f85c403cd5948a94dac4e4cd7cbaa87e36879ab8c3615b43', 'src/gui/link_planner_model.hpp': '5ba50225282272e09a975afb2cf1169bc403c31e09a907eb45e1e00a97820c08', 'src/gui/presentation_palette.hpp': '1ec0b262c69ecbbd8c8f3d9d5f7964f70523e9214ad75e1891e5645b2740915f', 'src/gui/receiver_timing_advice.hpp': 'c315bdfb16742abb09e143eafddc669d9f37d8c7bfa6d215d8c4c2510b960f84', 'src/gui/pattern_space.hpp': 'f87da60bc8cdd8c3fc1e8625152ecb2e09219a6b28d1f37bf4a46dadcdbbc99e', 'src/gui/audio_diagnostic.hpp': 'e8a6195b793d8e3f721b24a9975898ac8dafc859bbff2a3e5b31b14853d20ef8', 'src/gui/debug_live.hpp': 'e7a71a69f6b2fad205b47a6a5b5e01a37fa7f903bd6fa5309549aa5062412456', 'src/gui/fhss_profile.hpp': '420ca4daa86f37152bde9c53428b2e8c541dc6649f9b310ebbe1a69dc15ff816', 'src/gui/receiver_health.hpp': 'eff1373c9ef31d94db70e454aeb6f50c3fdb96230567160b06e049392c3c0dd0', 'src/gui/ui_surface.hpp': '63b1a2acfeb9f1cf9033147fe7fe4745647765dbaaad7adc8994fadee3f2d7df', 'src/gui/terminal_record_layout.hpp': 'c3d9b42ccea0522a255ba5c0a9c46de0a4229817685fad1fe4626f2a112b940e', 'src/gui/backend_sdl_input.hpp': 'c5146c681380e4ba7c6df1f709df3b9a6407afc90bdbeaedc702d2996d8cf635', 'src/gui/cell_layout.hpp': 'c73f5b63e7294077ee0563b83d35135d41fc6a3464b4c24ea4a060db48d14c4e', 'src/gui/web_pixels.hpp': '0f7e94258490242213fd75ad8c9c50da1f4a662f7a933a6a529a681fbb59cea9', 'src/gui/web_bridge.hpp': '6abd25a3fee5cb27d678b0e6bf427f5a2ca000b18f3032f2e98172cae36dd340', 'src/gui/terminal_bitmap.hpp': 'a04e69cd8042b3b3de5d7306cac0490f9e52221203cabf92ef3a2682eb00d429', 'src/gui/framebuffer_font.hpp': '33bc78099f80e338b2edea37166efd4a0bfde2583e7acf16f775b4db867c37b2', 'src/gui/framebuffer_ui.hpp': '8b8836912ce7041633cb5db517f396cf064963cd6a013753caf18d6d078cb1c0', 'src/gui/terminal_ui.hpp': 'adbd94eb13ec58984277661d1d4bcee4c7fd630e90a97af73c956d23c9645367', 'src/gui/backend_ncurses.hpp': '70aa1e23442a5beee6103d1d105d08ec0ec5da4fb48646716599c3bf780bbb1f', 'src/gui/framebuffer.hpp': '18308b5d3cf693585d0f319c2638b034e2c4c865d23f94cead13ebda2370e039', 'src/gui/gui_smoke_clock.hpp': '74f90c356a44aff78f696b5cc5b11c5eb4339d6369b55b1f6d9dfb89c1646349', 'src/gui/gui_smoke_budget.hpp': '4f7e8a2abc2b59073328cf1d1ad294568fd44e439a684e322f47c3f553f80113', 'src/gui/gui_smoke.hpp': '4c5f31bb28e8f54bf804c063de36f73738243c272d6746704c8c9d48672cfea5', 'src/gui/controller.hpp': '720ed78afbc0b6ad00ec1a7e27f019641ff65f235db565d82dd8f39a0ca9bd4c', 'src/gui/bitmap_sources.hpp': '2e3f62426d9e90c5f5955c7359ba5b4cff5b81a66d827c6ec1c623ded2def009', 'src/gui/ui_contract.hpp': '366a92ca85664c08e0784f42cd3be45fbc2144660d53cebdda50e4c18e8338a2', 'src/gui/desktop_layout.hpp': 'f1a08690cf7001abff82cccc35e79396ae74ec477efc7680f029da6507ce824f', 'src/gui/audio_controls.hpp': '2aa745a903f538090a5ebb7047b576c1e2d6b3d3b3578e885dbeaa2b7922710f', 'src/gui/state.hpp': '0b579ec6228e6a907d0b9d5948219a558e430d8b95a7d50100bb559625648edc', 'src/gui/gui_smoke_pending.hpp': '2433d94fbebf602342963f8b4410167c0cee3d41c7beb44051a79c5a8cfb223f', 'src/gui/gui_smoke_replay.hpp': '24b485a35241da189dc47611a9b9c155a417670a451ef82c27a478612075b016', 'src/gui/service_queue.hpp': '778e15a422082d9ee9e904327bd0b9d0a3dee93108e7bf5fd2de9b2a602a4288', 'src/gui/record_scroll.hpp': 'eb3a3e5a2f9c00cd7ea41b5d03ecc99183d3bfe3e1eb6105c59a3fb1848cb118', 'src/gui/control_binding.hpp': '2264ef08fa1321e3f7a153f4d49eb52267b93a7465a5e7ec36c93fea32e0f5ed', 'src/gui/record_interactions.hpp': '1145aa5c22bf5f34c44e6f8c80e030c303cac29a569c1b54021c4e770c3aa495', 'src/gui/control_interactions.hpp': '26ff302dcc26e36e15e826128bb37bd95ee18a94463c135552f413c40ec2d2ab', 'src/gui/document_layout.hpp': 'f3dfbb924d1b7601689051a41406ac22acac01f31a34e7ac7699319631eedfe1', 'src/gui/utf8_policy.hpp': '3f404109a0ab4410938766e1c55165b422f25a3272e4e2bb964a7b94af934c29', 'src/gui/text_policy.hpp': '10828e6f966a9f79e9c79612169643d92d3ff97ef23ad8cf3ca4cc633c854970', 'src/gui/record_presentations.hpp': 'd9f3a15a440aa3e14ea5cfbac676fadfe5a552e355e915060adfc041f2824a54', 'src/gui/backend_fltk_document.hpp': '045a98e923489a624413868ef4a71afef72ecd9d4481bc3dd76b476138fcedf3', 'src/gui/application.hpp': '25140ed4541a4fb7baf30e409ebeb2cb5ea04b3e6dd698a3cbba15e85848fe2b', 'src/gui/control_layout.hpp': '4d9180c9264372d1648be93aadc6b8e1f7e582994780bfa6eee28f09bee516f5', 'src/gui/backend_rev_document.hpp': '8e496a81f472e3109ba0b7e0787c9fdba5a40dfe3b623a5d2cd13130364be52a', 'src/gui/ui_document.hpp': 'a94e3c960e03e7a9e32d3014b9f7eee2e6b8a621d40e350cbadf2cd395c42c0b', 'src/gui/inspection_page.hpp': '7ee739ce7aeb31b1722c4d46d70ada9a1adfdca481ed00c7183712ea1baa5a9f', 'src/gui/bitmap_fltk.hpp': '50467f081b2e83954e523458bfcf16f98f10b33c57f34153ad01f4f3446525da', 'src/gui/overlay.hpp': 'f0eab3d37ead705652b8c43e4eea1ef5e4b722fbe7bc5ba35155bdc76a539ffa', 'src/gui/binary_editor.hpp': '7f6d1c52abd47a8c81c0de3c163c00ec1f42402ed9a017bfeac85724d2893fa6', 'src/gui/document_presentation.hpp': '4088b7961aaf1f2956411902ee23f9d8e7309accf24590cd3616e30131204130', 'src/gui/backend_rev_theme.hpp': '14b00a35ca5f2282486d495ed3a879ff4f59d6378c29f3d91990a03ae91ecaae', 'src/gui/chrome_layout.hpp': 'f4de9dc562fa15ce81050cfd83323c2beb449d528d697d610ca870e613dca4b1', 'src/gui/record_reconciliation.hpp': 'e35171f622fb5eefa8572fbcf831a0dc3d483fc1d6aa2cb98d09e9d90bc29960', 'src/gui/binding_state.hpp': '092ce898f606a893fab8bdac98cb6d2c9b6a1d6ecf797ded7e7017e45d4a1707', 'src/gui/document_actions.hpp': '699590496f50992baebe524da66e92c1040c1563daa27ea6e5791dcb77e85d7d', 'src/gui/pattern_score_view.hpp': '75b3c195927dc7e40976bd92792c08971c7c3af4ab49c3caf9177e91ee794470', 'src/gui/profile_reference.hpp': '0f012df74a40b596b33c97d9cbc76ba8d219919e5520daa994d841012aaa5299', 'src/gui/transmit_scope.hpp': '7a9e90eb41675356e3fdd4a7942952af481f6665d7d4c9421b360803ba5f2048', 'src/gui/screen_overlay.hpp': '031a380810d7e656df9f0bebfed1eefef674e4c43b74e3f77c8279c6b9edf43c', 'src/gui/launch_command.hpp': '6167787716aa037b768f83038f3bd7f3e28f1247f30ea58b987eecfb8a469d44', 'src/gui/link_planner_page.hpp': '2cb4951006aef97fba40c240bf9115a1370f4ca23ea7d113d1f2c6aaccd3076c', 'src/gui/legacy/controller.hpp': '47035fb595c2c4da8cd34c5688a0d6007e5bae66b72384ad64b2233c600b3124', 'src/gui/legacy/plots.hpp': 'b4f2895014be87d7d08452a119621e06927780798e5347fce1a68cc6210547e4', 'src/gui/legacy/presentation.hpp': 'c58f185e36c8dc7b9ef757fcef5f7d4dd05e753eef484de08f3aa53f9bf91439', 'src/gui/legacy/screen.hpp': 'bd14cd07bdad7876b5d95cc1df4832f07d4cb365c60ea00ff6928889c7e1bd8d', 'src/gui/fast/controller.hpp': 'f9a3391aa35fec17d8dec5bc42a397bb3d02880251d3590597b1ed884752a541', 'src/gui/fast/plots.hpp': '52ef4e7a940ad518fa04fe5b46f116e4ef5af30c7962328de8748f8a55da9971', 'src/gui/fast/presentation.hpp': '030223c0520f89c55e60c543002b3f26a9949173ea0ffbace137072f52db7815', 'src/gui/fast/screen.hpp': 'f7ab33f4af184aa9228c4a5f8c98150a62316eb97672a859f7730d7de16ce8e0'}
BASE='789fb91e351f9c2ecd3ef72b9881f74d7a4796fd'
FOCUSED_GROUPS={'eu-kernel-o1-san-on','eu-kernel-o1-san-off','eu-focus-o1-san-on','eu-focus-o0-san-on','eu-focus-o1-san-off','eu-focus-o0-san-off'}
GROUPS=('original','eu','fcc','eu-offset','eu-guard','eu-position','eu-o0-san-on','eu-o1-san-off','eu-o0-san-off','eu-focus-o1-san-on','eu-focus-o0-san-on','eu-focus-o1-san-off','eu-focus-o0-san-off','eu-kernel-o1-san-on','eu-kernel-o1-san-off')
SOURCE_FACTORS={'eu-offset','eu-guard','eu-position'}
NEW_GROUPS=set(GROUPS)-{'original','eu','fcc'}
CMAKE_PREIMAGES={'CMakeLists.txt':'f3973f79ef40c0521d6e1bdf58bac8a4e5de87797c7a3d4333264f202e274995','cmake/GuiShared.cmake':'daa982886aeecf9c4ee55a78f28ba3b7722157c4be84da63cf54fc0889683480','cmake/VendoredLzma.cmake':'f1bf58a5242757974b969bf7f6e720404248028c7514607976af4f0f1356437f'}
def group_spec(group):
 if group not in GROUPS:raise ValueError('unknown diagnostic group')
 return {'group':group,'optimization':0 if '-o0-' in group else 1,'sanitizers':not group.endswith('-san-off'),'instrumented_fixture':1,'new_overlay':group in NEW_GROUPS,'focused_only':group in FOCUSED_GROUPS}

sha=lambda b:hashlib.sha256(b).hexdigest()
def decode(text):
 def pairs(items):
  d={}
  for k,v in items:
   if k in d:raise ValueError('duplicate JSON key '+k)
   d[k]=v
  return d
 def finite(value):
  result=float(value)
  if not -float('inf')<result<float('inf'):raise ValueError('nonfinite JSON number')
  return result
 value=json.loads(text,object_pairs_hook=pairs,parse_float=finite,parse_constant=lambda x:(_ for _ in ()).throw(ValueError(x)))
 if not isinstance(value,dict):raise ValueError('expected JSON object')
 return value
def load(p):return decode(p.read_text())
def write_json(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def changed(original,group):
 d=dict(original)
 def replace(p,a,b):
  if d[p].count(a)!=1:raise ValueError('nonunique preimage '+p+' '+a[:90])
  d[p]=d[p].replace(a,b)
 for p in PREIMAGES:
  include='#include "../stop_differential.hpp"\n' if p=='src/gui/controller.cpp' else '#include "stop_differential.hpp"\n' if p.startswith('src/') else '#include "../src/stop_differential.hpp"\n'
  d[p]=include+d[p]
 p='src/gui/controller.cpp'
 replace(p,'session({},value.replay_clock)','session(datapump::stop_differential::epoch_clock(),value.replay_clock)')
 replace(p,'            key_failed=false; keys=std::move(result.keys); key_path=result.path;',
  '            datapump::stop_differential::loaded_keys(result.keys,result.path);\n            key_failed=false; keys=std::move(result.keys); key_path=result.path;')
 p='src/pattern_code.cpp'
 replace(p,'    config.data_key=Crypto::random();','    config.data_key=datapump::stop_differential::controlled()?datapump::stop_differential::noise_key():Crypto::random();')
 p='src/channel.cpp'
 replace(p,'    std::uint64_t received=0,origin=0,total=0,buffer_start=0;',
  '    std::uint64_t received=0,origin=0,total=0,buffer_start=0;\n    std::uint64_t differential_begins=0;')
 marker='        burst_rotation=phase_rotation(tau*config.carrier_hz*rate*(static_cast<long double>(origin)+startup)/config.sample_rate);'
 replace(p,marker,marker+'''
        ++differential_begins;
        namespace D=datapump::stop_differential;
        D::event("channel_state",[&](auto& f){
            f.add("begin",differential_begins);f.add("received",received);f.add("origin",origin);
            f.add("startup_hex",D::exact(startup));f.add("rate_hex",D::exact(rate));
            f.add("oscillator_real_hex",D::exact(oscillator.real()));f.add("oscillator_imag_hex",D::exact(oscillator.imag()));
            f.add("rotation_real_hex",D::exact(burst_rotation.real()));f.add("rotation_imag_hex",D::exact(burst_rotation.imag()));
            f.add("step_real_hex",D::exact(step.real()));f.add("step_imag_hex",D::exact(step.imag()));
            f.add("sample_rate",config.sample_rate);f.add("carrier_hz",config.carrier_hz);
            f.add("seed",channel.seed);f.add("snr_db",channel.snr_db);f.add("clock_error_ppm",channel.clock_error_ppm);
            f.add("frequency_offset_hz",channel.frequency_offset_hz);f.add("phase_noise",channel.phase_noise_degrees_per_sqrt_second);f.add("delay_samples",channel.delay_samples);
        },true);
        const auto witness=[&](const char* name,const auto& engine,const std::string& distribution){
            D::event("channel_rng",[&](auto& f){f.add("begin",differential_begins);f.add("engine",name);f.add("state",D::serialized(engine));f.add("distribution",distribution);},true);
        };
        if(D::primary()){witness("startup",startup_random,"");witness("phase",phase_random,D::serialized(phase_normal));witness("noise",noise_random,D::serialized(noise_normal));}
''')
 p='src/live.cpp'
 helper='''    bool differential_finish_locked() {
        if(source_pcm_started && current.transmitting && !current.simulation_receiving_tail && !finish_requested) {
            finish_requested=true;current.transmission_stopping=true;
            queued.clear();
            current.status="Finishing echo protection and pulse tails; cancel again to abort immediately";
            changed.notify_all();return true;
        }
        return false;
    }
'''
 replace(p,'    void prepare_source_read(Prepared& wave) {',helper+'    void prepare_source_read(Prepared& wave) {')
 finish='''        if(impl_->source_pcm_started && impl_->current.transmitting && !impl_->current.simulation_receiving_tail && !impl_->finish_requested) {
            impl_->finish_requested=true;impl_->current.transmission_stopping=true;
            impl_->queued.clear();
            impl_->current.status="Finishing echo protection and pulse tails; cancel again to abort immediately";
            impl_->changed.notify_all();return;
        }'''
 replace(p,finish,'''        if(impl_->differential_finish_locked()) {
            if(datapump::stop_differential::primary()) {
                datapump::stop_differential::identity=impl_->tx_serial;
                datapump::stop_differential::fired=true;
                datapump::stop_differential::event("request_installed",[&](auto& f){f.add("transmission",impl_->tx_serial);f.add("request_ns",datapump::stop_differential::request_ns.load());f.add("requester","GUI");f.add("PUBLIC_snapshot_source_seconds",impl_->current.transmission_seconds);f.add("PUBLIC_settings_sample_rate",impl_->settings.transfer.modem.sample_rate);},true);
            }
            return;
        }''')
 replace(p,'void Session::finish_transmit() {','''void Session::finish_transmit() {
    if(datapump::stop_differential::primary()) {
        datapump::stop_differential::request_ns=datapump::stop_differential::wall();
        datapump::stop_differential::event("request_before_lock",[](auto& f){f.add("requester","GUI");},true);
    }''')
 endpoint='''        if(finish_requested && !wave.graceful) {
            wave.transmitter->request_finish();wave.graceful=true;
        }'''
 replace(p,endpoint,'''        if(finish_requested && !wave.graceful) {
            const auto differential_before=wave.transmitter->total_samples();
            wave.transmitter->request_finish();wave.graceful=true;
            datapump::stop_differential::event("endpoint_installed",[&](auto& f){
                f.add("transmission",wave.serial);f.add("generated",wave.transmitter->samples_emitted());f.add("transmitted",wave.transmitted_samples);
                f.add("endpoint_before",differential_before);f.add("endpoint_after",wave.transmitter->total_samples());f.add("sample_rate",wave.modem.sample_rate);f.add("stop_requested",wave.stop.stop_requested());
            },true);
        }''')
 replace(p,'                    value.transfer.timestamp=static_cast<std::uint64_t>(current_epoch());',
  '''                    value.transfer.timestamp=datapump::stop_differential::controlled()?datapump::stop_differential::epoch:static_cast<std::uint64_t>(current_epoch());
                datapump::stop_differential::event("tx_prepare_config",[&](auto& f){f.add("noise",noise);f.add("TX_epoch",value.transfer.timestamp);f.add("sample_rate",value.transfer.modem.sample_rate);f.add("carrier_hz",value.transfer.modem.carrier_hz);f.add("symbol_samples",modem::symbol_sample_count(value.transfer.modem));f.add("pulse_padding",modem::pattern_pulse_padding_samples(value.transfer.modem));},true);
                datapump::stop_differential::plan("actual_TX",value.transfer.modem.fake_hopping.get());''')
 first='''                        prepare_source_read(*wave);
                        const auto count = simulation_channel->read(*wave->transmitter,
                            std::span(samples).first(capacity), wave->stop);
                        wave->transmitted_samples = simulation_channel->transmitted_samples();
                        if (!count) { complete_tx(*wave); wave.reset(); continue; }
                        const auto input_samples = std::span(samples).first(count);
                        account(count, version); progress(*wave, value);
                        plot_window.push(input_samples);
                        feed_samples(*simulation_bank,input_samples,value,version,wave->stop,nullptr,processing_token);
                        publish(plot_window,transmit_modem,version,last_plot,false,
                            wave->transmitter.get(),wave->serial);'''
 replace(p,first,'''                        namespace D=datapump::stop_differential;
                        std::size_t count=0;
                        {D::Interval timer("source_read");prepare_source_read(*wave);
                         count=simulation_channel->read(*wave->transmitter,std::span(samples).first(capacity),wave->stop);}
                        wave->transmitted_samples=simulation_channel->transmitted_samples();
                        if(!count) {
                            D::event("natural_eof",[&](auto& f){f.add("transmission",wave->serial);f.add("generated",wave->transmitter->samples_emitted());f.add("transmitted",wave->transmitted_samples);f.add("endpoint",wave->transmitter->total_samples());f.add("graceful",wave->graceful);f.add("stop_requested",wave->stop.stop_requested());},true);
                            complete_tx(*wave);
                            if(D::active&&wave->serial==D::identity.load())
                                D::event("completion_ack",[&](auto& f){f.add("transmission",wave->serial);f.add("complete_tx_returned",true);},true,true);
                            wave.reset();continue;
                        }
                        const auto ordinal=D::primary()?++D::blocks:0;
                        if(D::primary())D::output_samples+=count;
                        const auto input_samples=std::span(samples).first(count);
                        {D::Interval timer("account_progress");account(count,version);progress(*wave,value);}
                        plot_window.push(input_samples);
                        D::event("feed_begin",[&](auto& f){f.add("block",ordinal);f.add("transmission",wave->serial);f.add("count",count);f.add("generated",wave->transmitter->samples_emitted());f.add("transmitted",wave->transmitted_samples);f.add("bank_samples",simulation_bank->samples);f.add("revision",simulation_bank->revision);f.add("resident",simulation_bank->receivers.size());f.add("admissions",simulation_bank->admissions);f.add("retirements",simulation_bank->retirements);f.add("builds",simulation_bank->builds);f.add("workspace",simulation_bank->working_bytes);});
                        {D::Interval timer("receiver_feed");feed_samples(*simulation_bank,input_samples,value,version,wave->stop,nullptr,processing_token);}
                        D::event("feed_end",[&](auto& f){f.add("block",ordinal);f.add("bank_samples",simulation_bank->samples);f.add("resident",simulation_bank->receivers.size());f.add("admissions",simulation_bank->admissions);f.add("retirements",simulation_bank->retirements);f.add("builds",simulation_bank->builds);f.add("workspace",simulation_bank->working_bytes);});
                        {D::Interval timer("publication");publish(plot_window,transmit_modem,version,last_plot,false,wave->transmitter.get(),wave->serial);}''')
 pacing='''                        std::unique_lock lock(mutex);
                        changed.wait_until(lock,stop,deadline,[&] {
                            return !current.running || generation != version || wave->stop.stop_requested();
                        });
                        continue;'''
 replace(p,pacing,'''                        const auto differential_lock_start=D::wall();
                        std::unique_lock lock(mutex);
                        D::event("pacing_lock_acquired",[&](auto& f){f.add("elapsed_ns",D::wall()-differential_lock_start);});
                        {D::Interval timer("pacing_wait");changed.wait_until(lock,stop,deadline,[&] {
                            return !current.running || generation != version || wave->stop.stop_requested();
                        });}
                        lock.unlock();
                        if(D::controlled() && ordinal==D::target_block) {
                            if(!D::ready || count!=720 || D::output_samples!=D::target_block*720 || wave->modem.sample_rate!=14400 ||
                               simulation_channel->startup_offset_samples()>=4320.95 ||
                               double(wave->transmitted_samples)/wave->modem.sample_rate<=D::arrival_offset_seconds()) {
                                D::missed=true;D::event("setup_rejected",[](auto& f){f.add("reason","boundary/readiness/geometry");},true);
                                throw Error("diagnostic matched stop boundary rejected");
                            }
                            D::request_ns=D::wall();D::event("request_before_lock",[](auto& f){f.add("requester","source_boundary");},true);
                            std::lock_guard finish_lock(mutex);
                            if(!differential_finish_locked())throw Error("diagnostic first finish was not eligible");
                            D::identity=wave->serial;D::request_transmitted=wave->transmitted_samples;D::request_generated=wave->transmitter->samples_emitted();
                            D::event("request_installed",[&](auto& f){f.add("request_ns",D::request_ns.load());f.add("requester","source_boundary");f.add("block",ordinal);f.add("transmission",wave->serial);f.add("transmitted",wave->transmitted_samples);f.add("generated",wave->transmitter->samples_emitted());f.add("sample_rate",wave->modem.sample_rate);f.add("receiver_timestamp",value.transfer.timestamp);f.add("epoch_now",current_epoch());},true);
                            D::fired=true;
                        }
                        continue;''')
 p='tests/test_gui_controller.cpp'
 replace(p,'controller.edit(F::clock_offset,"2564ms");controller.edit(F::audio_error,"10ms");','controller.edit(F::clock_offset,datapump::stop_differential::offset_text());controller.edit(F::audio_error,datapump::stop_differential::audio_error_text());')
 if d[p].count('fhss_arrival_offset_seconds==2.564')!=2:raise ValueError('expected exactly two original offset comparisons')
 d[p]=d[p].replace('fhss_arrival_offset_seconds==2.564','fhss_arrival_offset_seconds==datapump::stop_differential::arrival_offset_seconds()')
 replace(p,'void fake_hop_display() {','''void fake_hop_display() {
    namespace D=datapump::stop_differential;
    std::unique_ptr<datapump::audio::trace::Scope> differential_trace;
    if(D::enabled&&D::detailed)differential_trace=std::make_unique<datapump::audio::trace::Scope>(D::env("DATAPUMP_STOP_TRACE_PATH"),false);''')
 replace(p,'    for(const auto* preference:{"fake-fcc","fake-eu","fake-list"}) {\n    Controller controller({true,true});',
  '    for(const auto* preference:{"fake-fcc","fake-eu","fake-list"}) {\n    if(D::only_target&&std::string_view(preference)!="fake-eu")continue;\n    D::Case differential_case(std::string_view(preference)=="fake-eu");\n    Controller controller({true,true});')
 replace(p,'    controller.select(F::fhss,preference);','    controller.select(F::fhss,D::controlled()?(D::group=="fcc"?"fake-fcc":"fake-eu"):preference);')
 replace(p,'    const auto prior_transmission_id=controller.snapshot().transmission_id;','''    const auto prior_transmission_id=controller.snapshot().transmission_id;
    D::event("effective_fixture",[&](auto& f){
        f.add("fixture_scope",D::only_target?"fake-eu-only":"full-main");f.add("admission_clock",D::controlled()&&D::env("DATAPUMP_STOP_SAMPLE_CLOCK")=="1"?"sampled":"wall");f.add("logical_ordinal","fake-eu");f.add("selected_preference",controller.field(F::fhss).selected);f.add("profile",controller.field(F::fhss).display_text);
        f.add("clock_accuracy",controller.field(F::clock_accuracy).text);f.add("clock_region",controller.field(F::clock_region).text);f.add("clock_offset",controller.field(F::clock_offset).text);f.add("audio_error",controller.field(F::audio_error).text);
        f.add("receiver_timestamp",controller.settings().transfer.timestamp);f.add("simulation_seed",controller.settings().simulation_seed);f.add("variant",D::group);f.add("expected_arrival_offset",D::arrival_offset_seconds());f.add("target_block",D::target_block);
    },true);
    D::plan("receiver_settings",controller.settings().transfer.modem.fake_hopping.get());''')

 stop='''    const auto stop_source_seconds=controller.snapshot().transmission_seconds;
    const auto protected_tail_seconds=double(modem::transmit_stop_sample_count(stop_config))/stop_config.sample_rate;
    controller.activate(C::cancel);'''
 replace(p,stop,'''    const auto protected_tail_seconds=double(modem::transmit_stop_sample_count(stop_config))/stop_config.sample_rate;
    if(D::controlled()) {
        D::ready=true;const auto boundary_limit=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(!D::fired&&!D::missed&&std::chrono::steady_clock::now()<boundary_limit){controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        check(D::fired&&!D::missed,"diagnostic matched source boundary was unavailable");
        controller.poll();
    }
    const auto stop_source_seconds=D::controlled()?double(D::request_transmitted.load())/stop_config.sample_rate:controller.snapshot().transmission_seconds;
    if(!D::controlled())controller.activate(C::cancel);''')
 replace(p,'    const auto requested_source_seconds=controller.snapshot().transmission_seconds;',
  '    const auto requested_source_seconds=D::controlled()?double(D::request_transmitted.load())/stop_config.sample_rate:controller.snapshot().transmission_seconds;')
 replace(p,'    const auto stop_started=std::chrono::steady_clock::now();',
  '    const auto stop_started=D::active?std::chrono::steady_clock::time_point(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::nanoseconds(D::request_ns.load()))):std::chrono::steady_clock::now();')
 assertion='''    check((stopped_within_deadline||!short_list)&&!controller.snapshot().transmitting&&
          controller.snapshot().transmission_finished&&!controller.snapshot().transmission_failed&&
          !controller.snapshot().transmission_stopping&&controller.snapshot().error.empty()&&
          controller.field(F::carrier).disabled_text.empty()&&
          controller.field(F::rf_shift).disabled_text.empty()&&controller.field(F::carrier).text==carrier&&
          controller.field(F::rf_shift).text==shift&&controller.field(F::fhss).display_text.find("Fake: hop ")==std::string::npos,
          "Ending fake FHSS did not restore the configured frequencies and idle label");'''
 replace(p,assertion,'''    try {
'''+assertion+'''
    } catch(...) {
        if(D::active) {
            D::event("strict_stop_failed",[&](auto& f){f.add("transmission",controller.snapshot().transmission_id);f.add("elapsed_ns",D::wall()-D::request_ns.load());},true);
            const auto observation_end=stop_started+std::chrono::seconds(45);
            while(controller.snapshot().transmitting&&std::chrono::steady_clock::now()<observation_end){controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
            const auto& final=controller.snapshot();
            D::event("observation_end_before_cleanup",[&](auto& f){
                f.add("transmission",final.transmission_id);f.add("same_identity",final.transmission_id==D::identity.load());f.add("transmitting",final.transmitting);f.add("stopping",final.transmission_stopping);
                f.add("healthy",!final.transmission_failed&&final.error.empty()&&final.receiver_error.empty()&&!final.receiver_health.failed()&&!final.receiver_health.dropped_samples);
                f.add("source_seconds",final.transmission_seconds);f.add("source_since_request",final.transmission_seconds-requested_source_seconds);f.add("source_allowance",source_allowance);
                f.add("restored",controller.field(F::carrier).disabled_text.empty()&&controller.field(F::rf_shift).disabled_text.empty()&&controller.field(F::carrier).text==carrier&&controller.field(F::rf_shift).text==shift&&controller.field(F::fhss).display_text.find("Fake: hop ")==std::string::npos);
                f.add("elapsed_ns",D::wall()-D::request_ns.load());
            },true);
        }
        if(D::active)D::event("known_strict_failure_terminal",[](auto& f){f.add("retained_original_failure",true);f.add("omitted_scope","post-failure traversed proof, unkeyed tail, later fixtures remain unexecuted as on original failure");},true);
        throw;
    }
    D::event("strict_stop_passed",[&](auto& f){f.add("transmission",controller.snapshot().transmission_id);f.add("elapsed_ns",D::wall()-D::request_ns.load());},true);''')
 replace(p,'    controller.select(F::key,"none");\n    check(!controller.field(F::fhss).enabled',
  '    if(D::active){D::event("target_instrumentation_disarmed",[](auto& f){f.add("reason","original unkeyed regression follows");},true);D::target_done=true;}\n    controller.select(F::key,"none");\n    check(!controller.field(F::fhss).enabled')
 p='src/live.cpp'
 replace(p,'    void refresh_bank(Bank& bank, const Settings& value) {','    void refresh_bank(Bank& bank, const Settings& value) {\n        datapump::stop_differential::CostScope differential_cost(datapump::stop_differential::CostKind::refresh);')
 replace(p,'                    const auto construct=[&] {','                    const auto construct=[&] {\n                        datapump::stop_differential::CostScope differential_cost(datapump::stop_differential::CostKind::constructor);')
 replace(p,'            rx_cpu::Scope cpu_candidates(rx_cpu::Stage::publication);','            datapump::stop_differential::CostScope differential_cost(datapump::stop_differential::CostKind::history);\n            rx_cpu::Scope cpu_candidates(rx_cpu::Stage::publication);')
 replace(p,'                            receiver.modem->process_pending(stop);','                            {datapump::stop_differential::CostScope differential_cost(datapump::stop_differential::CostKind::pending);receiver.modem->process_pending(stop);}')
 replace(p,'                receiver.push(raw,mixed,stop,cache);},wave);','                datapump::stop_differential::CostScope differential_cost(datapump::stop_differential::CostKind::push);receiver.push(raw,mixed,stop,cache);},wave);')
 replace(p,'{D::Interval timer("receiver_feed");feed_samples(*simulation_bank,input_samples,value,version,wave->stop,nullptr,processing_token);}','{D::FeedCosts costs(ordinal);D::Interval timer("receiver_feed");feed_samples(*simulation_bank,input_samples,value,version,wave->stop,nullptr,processing_token);}')
 replace(p,'                        Bank initial;\n                        if(value.transfer.modem.symbol_burst.enabled()) {','                        Bank initial;\n                        if(!value.transfer.modem.symbol_burst.enabled() && datapump::stop_differential::controlled() && datapump::stop_differential::env("DATAPUMP_STOP_SAMPLE_CLOCK")=="1")initial.observed_epoch=current_epoch();\n                        if(value.transfer.modem.symbol_burst.enabled()) {')
 replace(p,'                        simulation_bank = make_bank(value,std::move(initial));','''                        simulation_bank = make_bank(value,std::move(initial));
                        datapump::stop_differential::event("receiver_clock_mapping",[&](auto& f){
                            f.add("sample_anchor",simulation_bank->observed_epoch.has_value());f.add("anchor_epoch",simulation_bank->observed_epoch.value_or(0.));
                            f.add("bank_samples",simulation_bank->samples);f.add("observed_sample",simulation_bank->observed_sample);f.add("revision",simulation_bank->revision);
                            f.add("receiver_timestamp",value.transfer.timestamp);f.add("symbol_burst",value.transfer.modem.symbol_burst.enabled());
                        },true);''')
 p='tests/test_gui_controller.cpp'
 anchor='    const auto stop_started=D::active?'
 if d[p].count(anchor)!=1:raise ValueError('GUI stop timer anchor')
 d[p]=d[p].replace(anchor,'    D::GuiPollStats differential_gui_polls;\n'+anchor)
 anchor='do {controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}'
 if d[p].count(anchor)!=2:raise ValueError('strict GUI loop count')
 d[p]=d[p].replace(anchor,'do {{D::GuiPollScope differential_poll(differential_gui_polls);controller.poll();}std::this_thread::sleep_for(std::chrono::milliseconds(5));}')
 if group in NEW_GROUPS:
  opt='-O'+str(group_spec(group)['optimization'])
  anchors={'CMakeLists.txt':'target_compile_options(datapump PUBLIC -O1 -fsanitize=address,undefined',
   'cmake/GuiShared.cmake':'target_compile_options(datapump_gui_application INTERFACE\n      -O1 -fsanitize=address,undefined',
   'cmake/VendoredLzma.cmake':'target_compile_options(liblzma PRIVATE -O1 -fsanitize=address,undefined'}
  for q,anchor in anchors.items():
   if d[q].count(anchor)!=1:raise ValueError('SAN optimization target preimage '+q)
   d[q]=d[q].replace(anchor,anchor.replace('-O1',opt))
  project='project(DataPump VERSION 1.0.0 LANGUAGES C CXX)'
  if d['CMakeLists.txt'].count(project)!=1:raise ValueError('project preimage')
  d['CMakeLists.txt']=d['CMakeLists.txt'].replace(project,project+'\n# TEMP diagnostic: independent optimization/SAN factors; assertions remain enabled.\nadd_compile_options('+opt+')\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)')
  marker='  add_test(NAME gui_controller COMMAND test_gui_controller)'
  if d['CMakeLists.txt'].count(marker)!=1:raise ValueError('controller macro preimage')
  d['CMakeLists.txt']=d['CMakeLists.txt'].replace(marker,'  target_compile_definitions(test_gui_controller PRIVATE DATAPUMP_INSTRUMENTED_TEST=1)\n'+marker)

 # TEMP downstream kernel probe: aggregate only; nested wall/CPU buckets are not additive.
 p='src/pattern_receiver.cpp'
 replace(p,'    void process(std::stop_token stop,bool final=false) {','    void process(std::stop_token stop,bool final=false) {\n    datapump::stop_differential::CostScope differential_kernel_cost(datapump::stop_differential::CostKind::process);')
 replace(p,'    void prepare_templates(std::uint64_t index,std::stop_token stop) {','    void prepare_templates(std::uint64_t index,std::stop_token stop) {\n    datapump::stop_differential::CostScope differential_kernel_cost(datapump::stop_differential::CostKind::templates);')
 replace(p,'    void continue_tracks(std::stop_token stop,bool final=false,\n                         std::uint64_t latest_start=std::numeric_limits<std::uint64_t>::max()) {','    void continue_tracks(std::stop_token stop,bool final=false,\n                         std::uint64_t latest_start=std::numeric_limits<std::uint64_t>::max()) {\n    datapump::stop_differential::CostScope differential_kernel_cost(datapump::stop_differential::CostKind::tracks);')
 replace(p,'    PatternEvidence measure(std::uint64_t start,std::uint64_t index,std::size_t f,std::uint64_t observed_before,\n                            std::span<const Complex> carrier_phases,std::stop_token stop) {','    PatternEvidence measure(std::uint64_t start,std::uint64_t index,std::size_t f,std::uint64_t observed_before,\n                            std::span<const Complex> carrier_phases,std::stop_token stop) {\n    datapump::stop_differential::CostScope differential_kernel_cost(datapump::stop_differential::CostKind::measure);')
 replace(p,'    template<class Score> void collect_selected(const detail::PatternFftStartSelection& selected,\n            std::size_t count,std::uint64_t index,std::uint64_t phase,std::size_t f,Score score,bool startup_guide=false) {','    template<class Score> void collect_selected(const detail::PatternFftStartSelection& selected,\n            std::size_t count,std::uint64_t index,std::uint64_t phase,std::size_t f,Score score,bool startup_guide=false) {\n    datapump::stop_differential::CostScope differential_kernel_cost(datapump::stop_differential::CostKind::collect);')
 p='src/pattern_fft_batch.cpp'
 replace(p,'void pattern_fft(std::span<FftComplex> a, bool inverse, std::stop_token stop) {','void pattern_fft(std::span<FftComplex> a, bool inverse, std::stop_token stop) {\n    datapump::stop_differential::CostScope differential_kernel_cost(datapump::stop_differential::CostKind::legacy_fft);')
 p='src/search_fft.cpp'
 replace(p,'void fft(std::span<std::complex<double>> a,bool inverse,const DoubleTableView& view,std::stop_token stop){','void fft(std::span<std::complex<double>> a,bool inverse,const DoubleTableView& view,std::stop_token stop){\n    datapump::stop_differential::CostScope differential_kernel_cost(datapump::stop_differential::CostKind::prepared_fft);')
 replace(p,'void fft(std::span<std::complex<float>> a,bool inverse,const FloatPlan& p,std::stop_token stop,Kernel k){','void fft(std::span<std::complex<float>> a,bool inverse,const FloatPlan& p,std::stop_token stop,Kernel k){\n    datapump::stop_differential::CostScope differential_kernel_cost(datapump::stop_differential::CostKind::prepared_fft);')
 replace(p,'void fft(std::span<std::complex<double>> a,bool inverse,const DoublePlan& p,std::stop_token stop,Kernel k){','void fft(std::span<std::complex<double>> a,bool inverse,const DoublePlan& p,std::stop_token stop,Kernel k){\n    datapump::stop_differential::CostScope differential_kernel_cost(datapump::stop_differential::CostKind::prepared_fft);')
 # TEMP phase probe and idle-prefix handshake. No original statements removed.
 p='src/live.cpp'
 replace(p,'        while (!stop.stop_requested()) {\n            execution::checkpoint();\n            Settings value; std::uint64_t version; std::stop_token processing_token;',
  '        while (!stop.stop_requested()) {\n            if(!datapump::stop_differential::before_source_iteration(stop,[this]{std::lock_guard lock(mutex);return static_cast<bool>(ready);}))break;\n            execution::checkpoint();\n            Settings value; std::uint64_t version; std::stop_token processing_token;')
 replace(p,'                        feed_samples(*simulation_bank,idle,value,version,processing_token);\n                        std::unique_lock lock(mutex);',
  '                        feed_samples(*simulation_bank,idle,value,version,processing_token);\n                        datapump::stop_differential::idle_fed(count,value.transfer.modem.sample_rate,simulation_bank->samples);\n                        std::unique_lock lock(mutex);')
 p='tests/test_gui_controller.cpp'
 replace(p,'    controller.start();controller.activate(C::transmit_noise);\n    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);',
  '    controller.start();\n    D::await_idle([&]{controller.poll();});\n    controller.activate(C::transmit_noise);D::tx_requested();\n    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);')
 p='src/search_fft.cpp'
 for signature,kind in [
  ('void fft(std::span<std::complex<double>> a,bool inverse,const DoubleTableView& view,std::stop_token stop){','borrowed'),
  ('void fft(std::span<std::complex<float>> a,bool inverse,const FloatPlan& p,std::stop_token stop,Kernel k){','float_plan'),
  ('void fft(std::span<std::complex<double>> a,bool inverse,const DoublePlan& p,std::stop_token stop,Kernel k){','double_plan')]:
  anchor=signature+'\n    datapump::stop_differential::CostScope differential_kernel_cost(datapump::stop_differential::CostKind::prepared_fft);'
  replace(p,anchor,anchor+'\n    datapump::stop_differential::FftProbe differential_fft_probe(datapump::stop_differential::FftKind::'+kind+',a.size(),inverse);')
 first=d[p].index('void fft(std::span<std::complex<double>> a,bool inverse,const DoubleTableView& view,')
 last=d[p].index('void fft(std::span<std::complex<float>> a,bool inverse,const FloatPlan& p,',first)
 body=d[p][first:last]
 def phase_insert(anchor,addition):
  nonlocal body
  if body.count(anchor)!=1:raise ValueError('FFT phase anchor '+anchor)
  body=body.replace(anchor,addition+anchor)
 phase_insert('#if defined(DATAPUMP_SEARCH_FFT_X86)\n    const bool vectorized=',
  '    differential_fft_probe.mark(datapump::stop_differential::FftPhase::guard);\n')
 phase_insert('    auto* const values=a.data();',
  '#if defined(DATAPUMP_SEARCH_FFT_X86)\n    differential_fft_probe.gate(vectorized);\n#else\n    differential_fft_probe.gate(false);\n#endif\n    differential_fft_probe.mark(datapump::stop_differential::FftPhase::permutation);\n')
 phase_insert('        cancelled(stop);const auto half=length/2;',
  '        differential_fft_probe.mark(length==2?datapump::stop_differential::FftPhase::first_stage:datapump::stop_differential::FftPhase::later_stages);\n')
 phase_insert('    if(inverse)for(std::size_t i=0;i<n;++i){',
  '    differential_fft_probe.mark(datapump::stop_differential::FftPhase::normalization);\n')
 if not body.endswith('}\n'):raise ValueError('borrowed FFT completion anchor')
 body=body[:-2]+'    differential_fft_probe.complete();\n}\n'
 d[p]=d[p][:first]+body+d[p][last:]
 return d

def prepare(root,report,group='original'):
 provenance={'kind':'exact-pinned-source-copy','source_base':BASE}
 if (root/'.git').exists():
  head=subprocess.check_output(['git','-c','safe.directory='+str(root),'rev-parse','HEAD'],cwd=root,text=True).strip()
  paths=subprocess.check_output(['git','-c','safe.directory='+str(root),'diff','--name-only',BASE,'--'],cwd=root,text=True).splitlines()
  allowed={'.github/workflows/native-linux.yml','tools/gui-stop-differential.py'}
  if set(paths)-allowed:raise ValueError('unapproved sourcebase delta '+repr(sorted(set(paths)-allowed)))
  if subprocess.run(['git','-c','safe.directory='+str(root),'diff','--quiet'],cwd=root).returncode or subprocess.run(['git','-c','safe.directory='+str(root),'diff','--cached','--quiet'],cwd=root).returncode:raise ValueError('tracked dirty source at preparation')
  provenance={'kind':'Git metadata-only bridge','source_base':BASE,'actual_checkout_HEAD':head,'changed_from_base':paths,'allowed_changes':sorted(allowed)}
 spec=group_spec(group);pins=dict(PREIMAGES)
 if spec['new_overlay']:pins.update(CMAKE_PREIMAGES)
 original={p:(root/p).read_text() for p in pins}
 for p,v in pins.items():
  if sha((root/p).read_bytes())!=v:raise ValueError('stale source pin '+p)
 for p,v in READ_PINS.items():
  if sha((root/p).read_bytes())!=v:raise ValueError('stale read pin '+p)
 if (root/'src/stop_differential.hpp').exists():raise ValueError('private header already exists')
 variant=changed(original,group)
 if report.exists():raise ValueError('report must be a fresh directory')
 report.mkdir(parents=True)
 (report/'originals').mkdir()
 for p,text in original.items():
  q=report/'originals'/p;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes((root/p).read_bytes())
 manifest={'prepare_provenance':provenance,'source':BASE,'qualification':False,'originals':pins,'prepared_group':group,'configuration':spec,'postimages':{p:sha(t.encode()) for p,t in variant.items()},'read_pins':READ_PINS,'header_sha256':sha(HEADER.encode()),'tool_sha256':sha(Path(__file__).read_bytes()),'state':'prepared','matching':'Semantic syntheticRX key after ordinary validated random keyfile load; fileSHA retained separately. Moving E0+wall Session clock, fixed localTX epoch; actual channel state and request coordinates must match. No reset/reseed/retarget.'}
 (report/'source-variant.patch').write_text(''.join(''.join(difflib.unified_diff(original[p].splitlines(True),variant[p].splitlines(True),fromfile='a/'+p,tofile='b/'+p)) for p in original))
 write_json(report/'manifest.json',manifest)
 for p,text in variant.items():(root/p).write_text(text)
 (root/'src/stop_differential.hpp').write_text(HEADER)
 for p,v in manifest['postimages'].items():assert sha((root/p).read_bytes())==v

def restore(root,report):
 m=load(report/'manifest.json')
 states={}
 for p,v in m['postimages'].items():
  digest=sha((root/p).read_bytes())
  if digest not in (v,m['originals'][p]):raise ValueError('unknown source bytes '+p)
  states[p]='post' if digest==v else 'pre'
 header=root/'src/stop_differential.hpp'
 if header.exists() and sha(header.read_bytes())!=m['header_sha256']:raise ValueError('changed private header')
 for p,v in m['originals'].items():
  if sha((report/'originals'/p).read_bytes())!=v:raise ValueError('changed original '+p)
 for p in m['originals']:(root/p).write_bytes((report/'originals'/p).read_bytes())
 if header.exists():header.unlink()
 assert all(sha((root/p).read_bytes())==v for p,v in m['originals'].items())
 write_json(report/'restoration.json',{'restored':True,'originals':m['originals'],'private_header_removed':True,'recognized_initial_states':states,'partial_detected':any(v=='pre' for v in states.values()),'cleanup_does_not_override_preparation_or_test_failure':True})

def trace_seal(path,stderr):
 if not path.is_file() or path.stat().st_size>256*1024*1024:raise ValueError('missing/oversize trace')
 data=path.read_bytes();rows=[decode(x) for x in data.splitlines()]
 if not rows:raise ValueError('empty trace')
 foot=rows[-1];begin=rows[0]
 seals=[decode(x) for x in stderr.splitlines() if x.startswith('{"event":"trace_finalization"')]
 if len(seals)!=1:raise ValueError('missing/duplicate external finalization')
 s=seals[0]
 if foot.get('event')!='trace_end' or begin.get('event')!='trace_begin' or s.get('result')!='complete':raise ValueError('trace not complete')
 for field in ['token','attempted','dropped']:
  if foot.get(field)!=s.get(field):raise ValueError('footer/seal mismatch '+field)
 if begin.get('token')!=s.get('token'):raise ValueError('begin token mismatch')
 if type(s.get('attempted')) is not int or s['attempted']!=len(rows)-1 or type(s.get('dropped')) is not int or s['dropped']!=0 or s.get('size_limited') is not False or s.get('flush_ok') is not True or s.get('close_ok') is not True or s.get('prior_io_failed') is not False or s.get('path')!=str(path) or foot.get('finalization_required') is not True or foot.get('size_limited') is not False or foot.get('prior_io_failed') is not False:raise ValueError('trace seal rejected')
 sequences=[x.get('sequence') for x in rows[:-1]]
 if not all(type(x) is int for x in sequences) or sorted(sequences)!=list(range(1,len(rows))):raise ValueError('missing/duplicate sequence')
 return {'sha256':sha(data),'records':len(rows),'seal':s}

def witness_file(path):
 if not path.is_file() or path.stat().st_size>8*1024*1024:raise ValueError('missing/oversize witness')
 data=path.read_bytes();events=[decode(line) for line in data.splitlines()]
 sealpath=Path(str(path)+'.seal.json');seal=load(sealpath)
 if not events or len(events)>10000 or seal.get('event')!='witness_finalization' or seal.get('failed') is not False or seal.get('flush_ok') is not True or seal.get('close_ok') is not True or type(seal.get('records')) is not int or seal['records']!=len(events) or type(seal.get('bytes')) is not int or seal['bytes']!=len(data):raise ValueError('incomplete witness finalization')
 if not all(type(e.get('witness_sequence')) is int for e in events) or [e.get('witness_sequence') for e in events]!=list(range(1,len(events)+1)):raise ValueError('invalid witness sequence')
 return events,{'sha256':sha(data),'bytes':len(data),'records':len(events),'seal':seal,'seal_sha256':sha(sealpath.read_bytes())}

STRICT_MESSAGE='Ending fake FHSS did not restore the configured frequencies and idle label'
def one(events,name):
 values=[e for e in events if e.get('event')==name]
 if len(values)!=1:raise ValueError('missing/duplicate '+name)
 return values[0]
def signature(events):
 rx=one(events,'effective_rx_key');noise=one(events,'effective_noise_key');tx=one(events,'tx_prepare_config');fixture=one(events,'effective_fixture');request=one(events,'request_installed')
 states=[e for e in events if e.get('event')=='channel_state']
 if not states:raise ValueError('missing channel state')
 begins=[e['begin'] for e in states]
 if len(begins)!=len(set(begins)):raise ValueError('duplicate channel begin')
 state=max(states,key=lambda x:x['begin']);rng=[e for e in events if e.get('event')=='channel_rng' and e.get('begin')==state['begin']]
 if len(rng)!=3 or {e.get('engine') for e in rng}!={'startup','phase','noise'}:raise ValueError('incomplete channel RNG witness')
 if any(not isinstance(e.get('state'),str) or len(e['state'].encode())>12000 or not isinstance(e.get('distribution'),str) or len(e['distribution'].encode())>1000 for e in rng):raise ValueError('invalid channel witness size')
 if type(fixture.get('target_block')) is not int or fixture['target_block'] not in (60,120) or type(request.get('block')) is not int or request['block']!=fixture['target_block'] or type(request.get('sample_rate')) is not int or request['sample_rate']!=14400 or not all(type(request.get(k)) is int for k in ('transmitted','generated')):raise ValueError('unmatched boundary coordinates')
 if fixture.get('receiver_timestamp')!=0 or request.get('receiver_timestamp')!=0:raise ValueError('RX timestamp was changed')
 strip=lambda e:{k:v for k,v in e.items() if k not in ('wall_ns','thread_cpu_ns','thread','witness_sequence')}
 plans=[e for e in events if e.get('event')=='resolved_plan']
 if len(plans)!=2 or {e.get('role') for e in plans}!={'receiver_settings','actual_TX'} or not all(type(e.get('present')) is int and e.get('present')==1 and e.get('channels')==200 and isinstance(e.get('shifts_hex'),str) and len(e['shifts_hex'].encode())<8000 for e in plans):raise ValueError('incomplete resolved plan witnesses')
 plan_fields={'dwell','power_ramps','defer_symbols','mandatory_ramps','matched_gaps','smooth_ramps','remote_guard_covered','ramp_seconds','clock_error','peer_clock_error','region','offset','audio_error','fallback_reason'}
 if any(not plan_fields.issubset(e) or len(e['shifts_hex'].split(','))!=200 for e in plans):raise ValueError('missing resolved plan geometry')
 if any(any(type(e[k]) is not int or e[k] not in (0,1) for k in ('power_ramps','defer_symbols','mandatory_ramps','matched_gaps','smooth_ramps','remote_guard_covered')) for e in plans):raise ValueError('invalid resolved plan flags')
 result={'plans':{e['role']:strip(e) for e in plans},'rx_key_mac':rx['effective_key_mac'],'noise_key_mac':noise['effective_key_mac'],'tx':strip(tx),'channel':strip(state),'rng':{e['engine']:{'state':e['state'],'distribution':e['distribution']} for e in rng},'fixture':{k:v for k,v in strip(fixture).items() if k not in ('selected_preference','profile')},'request':{k:request[k] for k in ('block','generated','transmitted','sample_rate')}}
 return result
def classify(code,events,stderr,controlled):
 if code==124:return 'fatal_timeout'
 if any(s in stderr for s in ('AddressSanitizer','LeakSanitizer','UndefinedBehaviorSanitizer','runtime error:')):return 'fatal_sanitizer'
 try:
  one(events,'case_begin');one(events,'case_end');one(events,'effective_fixture');one(events,'effective_rx_key');one(events,'tx_prepare_config');one(events,'request_before_lock');one(events,'request_installed');one(events,'endpoint_installed')
  if any(e.get('event')=='setup_rejected' or e.get('event')=='case_end' and e.get('missed') for e in events):return 'fatal_setup_rejected'
  if any(type(e.get('thread_cpu_ns')) is not int or e['thread_cpu_ns']<0 for e in events):return 'fatal_thread_cpu_unavailable'
  verdict=[e for e in events if e.get('event') in ('strict_stop_failed','strict_stop_passed')]
  if len(verdict)!=1:raise ValueError('missing/duplicate strict result')
  terminal=[e for e in events if e.get('event')=='known_strict_failure_terminal']
  errors=[s for s in stderr.splitlines() if s.strip() and not s.startswith('{"event":"trace_finalization"')]
  if controlled:signature(events)
  if code==1 and verdict[0]['event']=='strict_stop_failed' and len(terminal)==1 and terminal[0].get('retained_original_failure')==1 and errors==[STRICT_MESSAGE]:
   observation=one(events,'observation_end_before_cleanup')
   if type(observation.get('transmitting')) is not int or observation.get('transmitting')!=0:return 'fatal_natural_continuation_stalled'
   if any(type(observation.get(k)) is not int or observation[k]!=1 for k in ('healthy','same_identity','restored')):return 'fatal_continuation_physical_or_identity'
   one(events,'natural_eof');one(events,'completion_ack')
   return 'strict_stop_failed_retained_tail_omitted'
  if code==0 and verdict[0]['event']=='strict_stop_passed' and not terminal and not errors:
   if type(verdict[0].get('elapsed_ns')) is not int or verdict[0]['elapsed_ns']>10_000_000_000:return 'fatal_late_stop_observation'
   one(events,'target_instrumentation_disarmed');one(events,'natural_eof');one(events,'completion_ack');return 'full_executable_pass'
  return 'fatal_distinct_assertion_or_exit'
 except (KeyError,TypeError,ValueError):return 'fatal_incomplete_or_malformed_evidence'

def compile_proof(root,spec,report=None):
 path=root/'build/compile_commands.json'
 if not path.is_file() or path.stat().st_size>64*1024*1024:raise ValueError('missing/oversize compile_commands')
 def pairs(items):
  result={}
  for key,value in items:
   if key in result:raise ValueError('duplicate compile JSON key '+key)
   result[key]=value
  return result
 def finite(value):
  result=float(value)
  if not -float('inf')<result<float('inf'):raise ValueError('nonfinite compile JSON number')
  return result
 entries=json.loads(path.read_text(),object_pairs_hook=pairs,parse_float=finite,parse_constant=lambda value:(_ for _ in ()).throw(ValueError(value)))
 if not isinstance(entries,list) or not entries:raise ValueError('invalid compile_commands inventory')
 selected={k:[] for k in ('core','gui','controller','lzma')};outputs=set()
 for entry in entries:
  if not isinstance(entry,dict):raise ValueError('invalid compile command object')
  args=entry.get('arguments')
  if args is None:
   if not isinstance(entry.get('command'),str):raise ValueError('invalid compile command string')
   args=shlex.split(entry['command'])
  if not isinstance(args,list) or not args or any(not isinstance(x,str) for x in args):raise ValueError('invalid compile argv')
  if any(x.startswith('@') for x in args):raise ValueError('response-file flags are not supported')
  if not isinstance(entry.get('directory'),str) or not entry['directory'] or not isinstance(entry.get('file'),str) or not entry['file']:raise ValueError('invalid compile directory/source')
  directory=Path(entry['directory']);file=Path(entry['file'])
  if not directory.is_absolute():raise ValueError('nonabsolute compile directory')
  source=(file if file.is_absolute() else directory/file).resolve()
  if '-o' not in args or args.count('-o')!=1 or args.index('-o')+1==len(args):raise ValueError('missing/duplicate compile output')
  output=args[args.index('-o')+1].replace('\\','/')
  kind=None
  for label,target in [('core','datapump'),('gui','datapump_gui_application'),('controller','test_gui_controller'),('lzma','liblzma')]:
   if 'CMakeFiles/'+target+'.dir/' in output:kind=label;break
  if kind is None:continue
  out=(directory/output).resolve() if not Path(output).is_absolute() else Path(output).resolve()
  if str(out) in outputs:raise ValueError('duplicate target compile output')
  outputs.add(str(out))
  opts=[x for x in args if x.startswith('-O')]
  if any(x not in ('-O','-O0','-O1','-O2','-O3','-Os','-Oz','-Og','-Ofast') for x in opts):raise ValueError('unsupported optimization flag')
  if not opts or opts[-1]!='-O'+str(spec['optimization']):raise ValueError('effective optimization mismatch '+str(source))
  definitions=[]
  for i,x in enumerate(args):
   if x=='-D':
    if i+1==len(args):raise ValueError('missing separate definition')
    definitions.append(args[i+1])
   elif x.startswith('-D'):definitions.append(x[2:])
   if x=='-U' and i+1<len(args) and args[i+1] in ('DATAPUMP_INSTRUMENTED_TEST','DATAPUMP_ORDERED_PULSE_CONTRACT_OFF') or any(x=='-U'+name for name in ('DATAPUMP_INSTRUMENTED_TEST','DATAPUMP_ORDERED_PULSE_CONTRACT_OFF')):raise ValueError('fixture/FP macro undefinition')
  if any(x=='NDEBUG' or x.startswith('NDEBUG=') for x in definitions) or '/DNDEBUG' in args:raise ValueError('NDEBUG changes assertion policy')
  if not any(x=='-g' or re.fullmatch('-g[1-3]',x) for x in args):raise ValueError('missing Debug symbols')
  if any(x.startswith('-flto') and x!='-fno-lto' for x in args):raise ValueError('LTO is unsupported')
  enabled=set()
  for x in args:
   if x.startswith('-fsanitize='):enabled.update(x.split('=',1)[1].split(','))
   if x.startswith('-fno-sanitize='):raise ValueError('sanitizer disable override')
  if (enabled!={'address','undefined'} if spec['sanitizers'] else bool(enabled)):raise ValueError('effective sanitizer mismatch '+str(source))
  if '-ffast-math' in args or '-Ofast' in args or any(x.startswith('-ffp-contract=') and x!='-ffp-contract=off' for x in args):raise ValueError('changed FP arithmetic policy')
  if kind!='lzma' and ('-fno-fast-math' not in args or '-ffp-contract=off' not in args):raise ValueError('missing exact FP policy')
  if kind=='controller' and ('DATAPUMP_INSTRUMENTED_TEST=1' not in definitions or any(x.startswith('DATAPUMP_INSTRUMENTED_TEST=') and x!='DATAPUMP_INSTRUMENTED_TEST=1' for x in definitions)):raise ValueError('instrumented fixture policy mismatch')
  if kind!='lzma' and 'DATAPUMP_ORDERED_PULSE_CONTRACT_OFF=1' not in definitions:raise ValueError('missing ordered pulse arithmetic policy')
  selected[kind].append({'source':str(source),'output':str(out),'effective_optimization':opts[-1],'sanitizers':sorted(enabled),'arguments':args})
 if any(not rows for rows in selected.values()):raise ValueError('missing required target compile inventory')
 required={'src/live.cpp','src/pattern_receiver.cpp','src/pattern_fft_batch.cpp','src/search_fft.cpp','src/pattern_code.cpp','src/channel.cpp'}
 core={str(Path(row['source']).relative_to(root)) for row in selected['core']}
 if not required.issubset(core):raise ValueError('missing core object coverage')
 if str(root/'src/gui/controller.cpp') not in {r['source'] for r in selected['gui']} or len(selected['controller'])!=1 or selected['controller'][0]['source']!=str(root/'tests/test_gui_controller.cpp'):raise ValueError('wrong GUI/controller compile inventory')
 # Ninja generates build.ninja rather than Makefile-generator link.txt files.
 # This tool mode prints commands without executing application/build commands.
 plan=subprocess.run(['ninja','-C',str(root/'build'),'-t','commands','test_gui_controller'],cwd=root,capture_output=True,timeout=30)
 if plan.returncode or len(plan.stdout)>16*1024*1024 or plan.stderr:raise ValueError('missing/invalid Ninja link command proof')
 candidates=[]
 for line in plan.stdout.decode().splitlines():
  args=shlex.split(line)
  if '-o' in args and args.count('-o')==1 and args.index('-o')+1<len(args) and Path(args[args.index('-o')+1]).name=='test_gui_controller':candidates.append((line,args))
 if len(candidates)!=1:raise ValueError('missing/duplicate controller link command')
 linkline,linkargs=candidates[0];san=set()
 for x in linkargs:
  if x.startswith('-fsanitize='):san.update(x.split('=',1)[1].split(','))
  if x.startswith('-fno-sanitize='):raise ValueError('link sanitizer disable override')
  if x.startswith('-flto'):raise ValueError('link LTO is unsupported')
 if (san!={'address','undefined'} if spec['sanitizers'] else bool(san)):raise ValueError('link sanitizer mismatch')
 if report is not None:
  (report/'compile_commands.json').write_bytes(path.read_bytes())
  (report/'ninja-controller-commands.txt').write_bytes(plan.stdout)
 return {'compile_commands_sha256':sha(path.read_bytes()),'ninja_commands_sha256':sha(plan.stdout),'link_line':linkline,'configuration':spec,'objects':selected,'link_arguments':linkargs,'qualification':False}

def factor_comparison(baseline,factor,group):
 if not isinstance(baseline,dict) or not isinstance(factor,dict):raise ValueError('missing factor signature')
 def flatten(value,path=''):
  if isinstance(value,dict):
   result={}
   for k,v in value.items():result.update(flatten(v,path+'.'+k if path else k))
   return result
  return {path:value}
 a,b=flatten(baseline),flatten(factor);diffs=[{'path':p,'baseline':a.get(p),'factor':b.get(p)} for p in sorted(set(a)|set(b)) if a.get(p)!=b.get(p)]
 allowed={'fixture.variant'};derived=set();expected={}
 if group=='eu-offset':
  allowed|={'fixture.clock_offset','fixture.expected_arrival_offset','plans.receiver_settings.offset','plans.actual_TX.offset'}
  expected={'fixture.expected_arrival_offset':(2.564,1.282),'plans.receiver_settings.offset':(2.564,1.282),'plans.actual_TX.offset':(2.564,1.282)}
 elif group=='eu-guard':
  allowed|={'fixture.audio_error','plans.receiver_settings.audio_error','plans.actual_TX.audio_error'}
  expected={'plans.receiver_settings.audio_error':(.01,0),'plans.actual_TX.audio_error':(.01,0)}
 elif group=='eu-position':
  allowed|={'fixture.target_block','request.block'};derived={'request.generated','request.transmitted'}
  expected={'fixture.target_block':(60,120),'request.block':(60,120)}
  unexplained_position=type(a.get('request.transmitted')) is not int or type(b.get('request.transmitted')) is not int or b['request.transmitted']-a['request.transmitted']!=60*720
  for signature in (baseline,factor):
   request=signature.get('request',{});g=request.get('generated');t=request.get('transmitted')
   if type(g) is not int or type(t) is not int or not 0<=g-t<=1026:unexplained_position=True
 else:raise ValueError('unknown source factor')
 unexplained=[d for d in diffs if d['path'] not in allowed|derived]
 if group=='eu-position' and unexplained_position:unexplained.append({'path':'request.generated/request.transmitted','reason':'later transmitted stop position or bounded source lookahead mismatch'})
 for p,values in expected.items():
  if (a.get(p),b.get(p))!=values:unexplained.append({'path':p,'reason':'factor did not have its declared exact values','expected':values,'baseline':a.get(p),'factor':b.get(p)})
 if a.get('fixture.variant')!='eu' or b.get('fixture.variant')!=group:unexplained.append({'path':'fixture.variant','reason':'wrong finite factor preset'})
 return {'factor':group,'all_differences':diffs,'permitted_input_paths':sorted(allowed),'derived_output_paths_reported_not_proven':sorted(derived),'unexplained_differences':unexplained,'causal_conclusion':False,'qualification':False}

def effective_clock_mapping(events,expected_sampled):
 mapping=one(events,'receiver_clock_mapping')
 if type(mapping.get('sample_anchor')) is not int or mapping['sample_anchor']!=int(expected_sampled):raise ValueError('actual sample-anchor policy mismatch')
 if any(type(mapping.get(k)) is not int for k in ('bank_samples','observed_sample','revision','receiver_timestamp','symbol_burst')) or mapping['bank_samples']!=0 or mapping['observed_sample']!=0 or mapping['receiver_timestamp']!=0 or mapping['symbol_burst']!=0 or mapping['revision']<0:raise ValueError('unexpected clock-bank birth coordinates')
 value=mapping.get('anchor_epoch')
 if type(value) not in (int,float) or not -float('inf')<value<float('inf') or (value<=0 if expected_sampled else value!=0):raise ValueError('invalid actual clock anchor epoch')
 return mapping

def clock_identity(mapping):
 return {k:mapping.get(k) for k in ('sample_anchor','anchor_epoch','bank_samples','observed_sample','revision','receiver_timestamp','symbol_burst')} if isinstance(mapping,dict) else None

def sample_clock_comparison(baseline,anchored,baseline_mapping=None,anchored_mapping=None):
 if not isinstance(baseline,dict) or not isinstance(anchored,dict):return {'matched':False,'reason':'missing source/input witness','qualification':False}
 a=json.loads(json.dumps(baseline));b=json.loads(json.dumps(anchored));left=a.get('fixture',{}).pop('admission_clock',None);right=b.get('fixture',{}).pop('admission_clock',None)
 actual=False
 if isinstance(baseline_mapping,dict) and isinstance(anchored_mapping,dict):
  try:
   x=clock_identity(effective_clock_mapping([baseline_mapping],False));y=clock_identity(effective_clock_mapping([anchored_mapping],True))
   actual=x.pop('sample_anchor')==0 and y.pop('sample_anchor')==1 and x.pop('anchor_epoch')==0 and y.pop('anchor_epoch')>0 and x==y
  except (KeyError,TypeError,ValueError):actual=False
 return {'matched':left=='wall' and right=='sampled' and a==b and actual,'baseline_clock':left,'factor_clock':right,'only_permitted_difference':'one-time observed_epoch at bank creation; source/input signatures same except declared fixture.admission_clock','actual_mapping_verified':actual,'qualification':False,'causal_conclusion':False}

def run(root,report,group):
 m=load(report/'manifest.json');binary=root/'build/test_gui_controller'
 if m.get('prepared_group')!=group or m.get('configuration')!=group_spec(group):raise ValueError('prepared group/config mismatch')
 if m['source']!=BASE or not binary.is_file():raise ValueError('unprepared source/binary')
 if m['tool_sha256']!=sha(Path(__file__).read_bytes()):raise ValueError('tool identity mismatch')
 for p,v in m['postimages'].items():
  if sha((root/p).read_bytes())!=v:raise ValueError('postimage mismatch '+p)
 if sha((root/'src/stop_differential.hpp').read_bytes())!=m['header_sha256']:raise ValueError('private header mismatch')
 for p,v in m['read_pins'].items():
  if sha((root/p).read_bytes())!=v:raise ValueError('changed read dependency '+p)
 cache=root/'build/CMakeCache.txt';cachetext=cache.read_text()
 expected_san='ON' if m['configuration']['sanitizers'] else 'OFF'
 if 'CMAKE_BUILD_TYPE:STRING=Debug' not in cachetext.splitlines() or 'DATAPUMP_SANITIZERS:BOOL='+expected_san not in cachetext.splitlines():raise ValueError('expected exact Debug/SAN configuration')
 configuration_proof=compile_proof(root,m['configuration'],report) if group in NEW_GROUPS else {'old_group':True}
 actual_head=subprocess.check_output(['git','-c','safe.directory='+str(root),'rev-parse','HEAD'],cwd=root,text=True).strip()
 if m['prepare_provenance'].get('actual_checkout_HEAD')!=actual_head:raise ValueError('checkout HEAD changed since preparation')
 compiler_lines=[s.split('=',1)[1] for s in cachetext.splitlines() if s.startswith(('CMAKE_CXX_COMPILER:STRING=','CMAKE_CXX_COMPILER:FILEPATH='))]
 if len(compiler_lines)!=1:raise ValueError('missing compiler identity')
 compiler=subprocess.check_output([compiler_lines[0],'--version'],cwd=root,text=True)
 common={'actual_checkout_HEAD':actual_head,'source_base':BASE,'variant_manifest_sha256':sha((report/'manifest.json').read_bytes()),'CMakeCache_sha256':sha(cache.read_bytes()),'compiler_path':compiler_lines[0],'compiler_version':compiler,'runner':{k:os.environ.get(k) for k in ('RUNNER_NAME','RUNNER_OS','RUNNER_ARCH','GITHUB_RUN_ID','GITHUB_RUN_ATTEMPT','GITHUB_JOB','GITHUB_SHA')},'tool_sha256':sha(Path(__file__).read_bytes()),'configuration_proof':configuration_proof}
 if group in SOURCE_FACTORS:cells=[('eu',False,'baseline-plain'),('eu',True,'baseline-trace'),(group,True,'factor-trace')]
 elif group in NEW_GROUPS:cells=[('eu',False,'baseline-plain'),('eu',True,'baseline-trace')]
 else:cells=[(group,t,group+('-trace' if t else '-plain')) for t in ([True] if group=='original' else [False,True] if group=='eu' else [True,False])]
 if group=='eu-focus-o1-san-on':cells.append(('eu',True,'sample-clock-trace'))
 outputs=[];group_binary_sha256=sha(binary.read_bytes())
 for variant,trace,name in cells:
  out=report/name
  if out.exists():raise ValueError('run output exists')
  out.mkdir();tracepath=out/'trace.jsonl';witnesspath=out/'witness.jsonl';env=dict(os.environ,DATAPUMP_STOP_SAMPLE_CLOCK='1' if name=='sample-clock-trace' else '0',DATAPUMP_STOP_ONLY_TARGET='1' if group in FOCUSED_GROUPS else '0',DATAPUMP_STOP_GROUP=variant,DATAPUMP_STOP_TRACE='1' if trace else '0',DATAPUMP_STOP_TRACE_PATH=str(tracepath),DATAPUMP_STOP_WITNESS_PATH=str(witnesspath))
  binary_before=sha(binary.read_bytes());start=time.monotonic()
  with (out/'stdout.log').open('wb') as stdout,(out/'stderr.log').open('wb') as stderr:
   try:result=subprocess.run([str(binary)]+(['--fake-hop-display'] if group in FOCUSED_GROUPS else []),cwd=root,env=env,stdout=stdout,stderr=stderr,timeout=1800 if group in NEW_GROUPS else 480);code=result.returncode
   except subprocess.TimeoutExpired:code=124
  text=(out/'stdout.log').read_text(errors='replace');err=(out/'stderr.log').read_text(errors='replace');events=[];parse_error=None
  witness_result=None
  try:events,witness_result=witness_file(witnesspath)
  except (OSError,KeyError,TypeError,ValueError) as error:parse_error=str(error)
  record={'name':name,'prepared_group':group,'runtime_variant':variant,'source':BASE,'binary_sha256':sha(binary.read_bytes()),'exit':code,'seconds':time.monotonic()-start,'command':[str(binary)]+(['--fake-hop-display'] if group in FOCUSED_GROUPS else []),'fixture_scope':'fake-eu-only' if group in FOCUSED_GROUPS else 'full-main','omitted_scope':['all main fixtures outside fake_hop_display','fake-fcc and fake-list iterations'] if group in FOCUSED_GROUPS else [],'environment':{k:env[k] for k in ['DATAPUMP_STOP_SAMPLE_CLOCK','DATAPUMP_STOP_ONLY_TARGET','DATAPUMP_STOP_GROUP','DATAPUMP_STOP_TRACE','DATAPUMP_STOP_TRACE_PATH','DATAPUMP_STOP_WITNESS_PATH']},'variant_postimages':m['postimages'],'header_sha256':m['header_sha256'],'qualification':False,'events':events,'stdout_sha256':sha((out/'stdout.log').read_bytes()),'stderr_sha256':sha((out/'stderr.log').read_bytes())}
  record['witness']=witness_result
  record.update(common);record['classification']=classify(code,events,err,variant!='original')
  if parse_error is not None:record['event_parse_error']=parse_error;record['classification']='fatal_evidence_setup'
  if record['binary_sha256']!=binary_before or binary_before!=group_binary_sha256:record['classification']='fatal_binary_changed'
  try:
   if trace:record['trace']=trace_seal(tracepath,err)
   elif tracepath.exists():raise ValueError('traceoff unexpectedly created trace')
   record['effective_clock_mapping']=effective_clock_mapping(events,env['DATAPUMP_STOP_SAMPLE_CLOCK']=='1')
   if variant!='original':record['matching_signature']=signature(events)
  except (OSError,KeyError,TypeError,ValueError) as error:record['evidence_error']=str(error);record['classification']='fatal_evidence_setup'
  write_json(out/'result.json',record);outputs.append(record)
  if record['classification'].startswith('fatal'):break
 paired=group=='original' or len(outputs)>=2 and outputs[0].get('matching_signature')==outputs[1].get('matching_signature') and outputs[0].get('matching_signature') is not None and clock_identity(outputs[0].get('effective_clock_mapping'))==clock_identity(outputs[1].get('effective_clock_mapping'))
 factor=factor_comparison(outputs[0].get('matching_signature'),outputs[2].get('matching_signature'),group) if group in SOURCE_FACTORS and len(outputs)==3 else None
 clock_comparison=sample_clock_comparison(outputs[0].get('matching_signature'),outputs[2].get('matching_signature'),outputs[0].get('effective_clock_mapping'),outputs[2].get('effective_clock_mapping')) if group=='eu-focus-o1-san-on' and len(outputs)==3 else None
 result={'source':BASE,'group':group,'qualification':False,'processes':outputs,'planned_cells':[name for _,_,name in cells],'unexecuted_cells':[name for _,_,name in cells[len(outputs):]],'within_group_inputs_matched':paired,'factor_comparison':factor,'sample_clock_comparison':clock_comparison,'strict_exit':1 if not paired or len(outputs)!=len(cells) or factor is not None and bool(factor['unexplained_differences']) or clock_comparison is not None and not clock_comparison['matched'] or any(x['exit'] or x['classification']!='full_executable_pass' for x in outputs) else 0,'matching_required':'Crossgroup signature comparison remains root-owned; mismatch is unmatched evidence, never preference-only cause. Distinct failures fatal; known retained strict failure explicitly omits subsequent original assertions.'}
 write_json(report/('group-'+group+'.json'),result)
 return result['strict_exit']

def main():
 p=argparse.ArgumentParser();p.add_argument('action',choices=['prepare','run','restore']);p.add_argument('--root',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--group',choices=GROUPS);a=p.parse_args();root=a.root.resolve();report=a.report.resolve()
 if a.action=='prepare':prepare(root,report,a.group or 'original')
 elif a.action=='restore':restore(root,report)
 else:
  if not a.group:p.error('--group required')
  return run(root,report,a.group)
 return 0
if __name__=='__main__':raise SystemExit(main())

#include "raw32_context.hpp"
#include "gui/state.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <cstdio>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr auto timeout = 120s;
constexpr std::size_t output_limit = 8u * 1024u * 1024u;
const std::vector<std::uint8_t> help_bits{
    0,1,0,0,1,0,0,0, 0,1,1,0,0,1,0,1,
    0,1,1,0,1,1,0,0, 0,1,1,1,0,0,0,0,
};

std::string quote(std::string_view s) {
    std::string out="\"";
    constexpr char hex[]="0123456789abcdef";
    for (unsigned char c:s) {
        if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
        else if(c=='\n')out+="\\n";
        else if(c=='\r')out+="\\r";
        else if(c=='\t')out+="\\t";
        else if(c<0x20){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=char(c);
    }
    out+='"';return out;
}

class Journal {
    std::FILE* file_=nullptr;
    std::size_t bytes_=0;
    bool flush_ok_=false,close_ok_=false;
public:
    explicit Journal(const std::filesystem::path& p) {
        file_=std::fopen(p.c_str(),"wbx");
        if(!file_)throw std::runtime_error("output JSONL must be new and creatable");
    }
    ~Journal(){finish();}
    void event(const std::string& line) {
        timespec cpu{};
        const bool cpu_ok=clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu)==0;
        const auto wall=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
        const auto stamped=line.substr(0,line.size()-1)+",\"monotonic_ns\":"+std::to_string(wall)+
            ",\"poll_thread_cpu_ns\":"+(cpu_ok?std::to_string(std::uint64_t(cpu.tv_sec)*1000000000+cpu.tv_nsec):"null")+"}";
        if(!file_ || stamped.size()+1>output_limit-bytes_)throw std::runtime_error("JSONL output limit exceeded/closed");
        if(std::fwrite(stamped.data(),1,stamped.size(),file_)!=stamped.size() || std::fputc('\n',file_)==EOF)
            throw std::runtime_error("JSONL write failed");
        bytes_+=stamped.size()+1;
    }
    bool finish() noexcept {
        if(!file_)return flush_ok_&&close_ok_;
        auto* f=file_;file_=nullptr;
        flush_ok_=std::fflush(f)==0;close_ok_=std::fclose(f)==0;
        return flush_ok_&&close_ok_;
    }
    bool flush_ok()const{return flush_ok_;}
    bool close_ok()const{return close_ok_;}
};
std::string byte_array(const datapump::Bytes& bytes) {
    std::string out="[";for(auto value:bytes){if(out.size()>1)out+=',';out+=std::to_string(value);}return out+"]";
}
std::string bits(const datapump::Bytes& bytes) {
    std::string out;for(auto value:bytes)out+=value<2?char('0'+value):'?';return out;
}

std::size_t thread_count() {
    std::size_t n=0;std::error_code ec;
    for(std::filesystem::directory_iterator i("/proc/self/task",ec),end;!ec&&i!=end;i.increment(ec))++n;
    return n;
}
std::uint64_t cpu_usec() {
    rusage u{};if(getrusage(RUSAGE_SELF,&u)!=0)return 0;
    return std::uint64_t(u.ru_utime.tv_sec+u.ru_stime.tv_sec)*1000000u+
           std::uint64_t(u.ru_utime.tv_usec+u.ru_stime.tv_usec);
}
long peak_rss_kib() { rusage u{};return getrusage(RUSAGE_SELF,&u)==0?u.ru_maxrss:0; }

std::string signal_key(const datapump::live::Snapshot& s) {
    std::ostringstream o;
    for(const auto& x:s.signals)o<<x.id<<':'<<x.revision<<':'<<x.received_bits<<':'<<x.expected_bits
      <<':'<<x.complete<<':'<<x.validated<<':'<<x.binary<<':'<<x.text<<':'<<x.raw_bits<<';';
    o<<"|received="<<s.received.size();
    for(const auto& r:s.received)o<<':'<<datapump::gui::id_label(r.content.message)<<':'<<r.stream_complete<<':'
      <<r.content_validated<<':'<<r.content.message.data.size();
    return o.str();
}
std::size_t accepted_bits(const datapump::live::Snapshot& s) {
    std::size_t n=0;for(const auto& x:s.signals)n=std::max(n,x.received_bits);return n;
}
std::string waveform_prefix(const datapump::live::Snapshot& s) {
    std::string result="[";
    const auto n=std::min<std::size_t>(s.waveform.size(),16);
    for(std::size_t i=0;i<n;++i) {
        if(i)result+=',';
        result+=std::isfinite(s.waveform[i])?std::to_string(s.waveform[i]):"null";
    }
    result+=']';return result;
}
}

int main(int argc,char** argv) {
    using namespace datapump;
    std::filesystem::path keyfile,output;
    if(argc!=5||std::string_view(argv[1])!="--keyfile"||std::string_view(argv[3])!="--output") {
        std::cerr<<"usage: copied_help_session --keyfile ABS --output NEW_JSONL\n";return 2;
    }
    keyfile=argv[2];output=argv[4];
    if(!keyfile.is_absolute()||!output.is_absolute()||!std::filesystem::is_directory(output.parent_path())) {
        std::cerr<<"keyfile and output must be absolute; output parent must exist\n";return 2;
    }
    Journal journal(output);
    auto started=Clock::now();auto cpu0=cpu_usec();std::unique_ptr<live::Session> session;
    bool journal_ok=false;std::string failure;bool saw_idle=false,submitted=false,copy_ok=false;std::uint64_t submission_prior_tx=0;
    std::size_t polls=0,last_bits=0;std::uint64_t last_tx=0,last_trace_revision=0;
    std::string last_key,last_model_key,last_health;
    std::unique_ptr<gui::Inbox> inbox;std::unique_ptr<gui::Signals> signals;
    try {
        auto context=diagnostic::copied_help_session::make_context(keyfile);
        diagnostic::copied_help_session::arm_probes();
        auto replay_ms=std::make_shared<std::atomic<std::int64_t>>(0);
        session=std::make_unique<live::Session>(live::Session::EpochClock{},[replay_ms] {
            return Clock::time_point{}+std::chrono::milliseconds(replay_ms->load(std::memory_order_relaxed));
        });
        inbox=std::make_unique<gui::Inbox>(context.settings.content_limit);
        signals=std::make_unique<gui::Signals>();
        session->start(context.settings);
        journal.event("{\"event\":\"identity\",\"context\":"+context.identity_json+"}");
        const auto deadline=started+timeout;
        auto next_progress=Clock::now();
        while(Clock::now()<deadline) {
            auto snapshot=session->snapshot();++polls;
            if(Clock::now()>=next_progress) {
                next_progress=Clock::now()+200ms;
                journal.event("{\"event\":\"progress\",\"poll\":"+std::to_string(polls)+
                    ",\"samples_received\":"+std::to_string(snapshot.samples_received)+
                    ",\"processed_physical_samples\":"+std::to_string(snapshot.processed_physical_samples)+
                    ",\"queued_physical_samples\":"+std::to_string(snapshot.queued_physical_samples)+
                    ",\"decoding_physical_samples\":"+std::to_string(snapshot.decoding_physical_samples)+
                    ",\"transmission_id\":"+std::to_string(snapshot.transmission_id)+
                    ",\"generated_bits\":"+std::to_string(snapshot.transmit_trace.generated_bits)+
                    ",\"generated_chips\":"+std::to_string(snapshot.transmit_trace.generated_chips)+
                    ",\"transmission_fraction\":"+std::to_string(snapshot.transmission_fraction)+
                    ",\"transmission_finished\":"+(snapshot.transmission_finished?"true":"false")+
                    ",\"receiving_tail\":"+(snapshot.simulation_receiving_tail?"true":"false")+
                    ",\"replay\":"+(snapshot.simulation_replay?"true":"false")+"}");
            }
            std::ostringstream health_state;
            health_state<<snapshot.receiver_health.input_overruns<<':'<<snapshot.receiver_health.dropped_samples
              <<':'<<snapshot.receiver_health.input_interrupted<<':'<<snapshot.receiver_health.search_limited
              <<':'<<snapshot.receiver_health.receiver_reset;
            if(health_state.str()!=last_health) {
                last_health=health_state.str();
                journal.event("{\"event\":\"receiver_health\",\"poll\":"+std::to_string(polls)+
                  ",\"input_overruns\":"+std::to_string(snapshot.receiver_health.input_overruns)+
                  ",\"dropped_samples\":"+std::to_string(snapshot.receiver_health.dropped_samples)+
                  ",\"input_interrupted\":"+(snapshot.receiver_health.input_interrupted?"true":"false")+
                  ",\"search_limited\":"+(snapshot.receiver_health.search_limited?"true":"false")+
                  ",\"receiver_reset\":"+(snapshot.receiver_health.receiver_reset?"true":"false")+"}");
            }
            if(snapshot.samples_received) {
                if(!saw_idle)
                    journal.event("{\"event\":\"sampled_input\",\"poll\":"+std::to_string(polls)+
                      ",\"samples_received\":"+std::to_string(snapshot.samples_received)+
                      ",\"waveform_sample_rate\":"+std::to_string(snapshot.waveform_sample_rate)+
                      ",\"waveform_prefix\":"+waveform_prefix(snapshot)+
                      ",\"input_overruns\":"+std::to_string(snapshot.receiver_health.input_overruns)+
                      ",\"dropped_samples\":"+std::to_string(snapshot.receiver_health.dropped_samples)+
                      ",\"input_interrupted\":"+(snapshot.receiver_health.input_interrupted?"true":"false")+"}");
            }
            if(snapshot.samples_received&&!submitted)saw_idle=true;
            if(snapshot.receiver_health.failed()||snapshot.receiver_health.dropped_samples)
                throw std::runtime_error("receiver health failure before success");
            if(snapshot.transmission_failed)throw std::runtime_error("raw32 transmission failed: "+snapshot.error);
            if(snapshot.transmission_id!=last_tx||snapshot.transmit_trace.generated_bits!=last_bits||
               snapshot.transmit_trace.revision!=last_trace_revision) {
                const auto& t=snapshot.transmit_trace;
                journal.event("{\"event\":\"transmit\",\"poll\":"+std::to_string(polls)+
                  ",\"id\":"+std::to_string(snapshot.transmission_id)+
                  ",\"generated_bits\":"+std::to_string(t.generated_bits)+
                  ",\"total_wire_bits\":"+std::to_string(t.total_wire_bits)+
                  ",\"trace_revision\":"+std::to_string(t.revision)+
                  ",\"trace_active\":"+(t.active?"true":"false")+
                  ",\"trace_raw\":"+(t.raw?"true":"false")+
                  ",\"source_available\":"+(t.source_available?"true":"false")+
                  ",\"compressed_available\":"+(t.compressed_available?"true":"false")+
                  ",\"wire_bits\":"+quote(bits(t.wire_bits))+
                  ",\"transmitting\":"+(snapshot.transmitting?"true":"false")+
                  ",\"finished\":"+(snapshot.transmission_finished?"true":"false")+
                  ",\"failed\":"+(snapshot.transmission_failed?"true":"false")+
                  ",\"error\":"+quote(snapshot.error)+"}");
                last_tx=snapshot.transmission_id;last_bits=t.generated_bits;last_trace_revision=t.revision;
            }
            const auto sk=signal_key(snapshot);
            const bool has_delta=!snapshot.signals.empty()||!snapshot.received.empty();
            if(has_delta) {
                last_key=sk;
                journal.event("{\"event\":\"reception_poll\",\"poll\":"+std::to_string(polls)+
                  ",\"sampled_input\":"+std::to_string(snapshot.samples_received)+
                  ",\"signal_count\":"+std::to_string(snapshot.signals.size())+
                  ",\"received_count\":"+std::to_string(snapshot.received.size())+
                  ",\"max_received_bits\":"+std::to_string(accepted_bits(snapshot))+
                  ",\"signal_state\":"+quote(sk)+"}");
                for(std::size_t i=0;i<snapshot.signals.size();++i) {
                    const auto& x=snapshot.signals[i];
                    journal.event("{\"event\":\"signal_delta\",\"poll\":"+std::to_string(polls)+
                      ",\"id\":"+std::to_string(x.id)+",\"revision\":"+std::to_string(x.revision)+
                      ",\"received_bits\":"+std::to_string(x.received_bits)+
                      ",\"expected_bits\":"+std::to_string(x.expected_bits)+
                      ",\"complete\":"+(x.complete?"true":"false")+
                      ",\"validated\":"+(x.validated?"true":"false")+
                      ",\"binary\":"+(x.binary?"true":"false")+
                      ",\"text\":"+quote(x.text)+",\"raw_bits\":"+quote(x.raw_bits)+",\"missing_symbols\":"+std::to_string(x.missing_symbols)+",\"superseded_ids\":"+[&]{std::string ids="[";for(auto id:x.superseded_ids){if(ids.size()>1)ids+=',';ids+=std::to_string(id);}return ids+"]";}()+"}");
                }
                for(std::size_t i=0;i<snapshot.received.size();++i) {
                    const auto& x=snapshot.received[i];
                    journal.event("{\"event\":\"received_delta\",\"poll\":"+std::to_string(polls)+
                      ",\"index\":"+std::to_string(i)+
                      ",\"stream_complete\":"+(x.stream_complete?"true":"false")+
                      ",\"content_validated\":"+(x.content_validated?"true":"false")+
                      ",\"kind\":"+std::to_string(static_cast<int>(x.content.message.kind))+
                      ",\"data_bytes\":"+std::to_string(x.content.message.data.size())+",\"data\":"+byte_array(x.content.message.data)+",\"raw_bits\":"+quote(bits(x.raw_bits))+"}");
                }
            }
            // Journal the candidate events and counters above, then consume
            // this poll exactly once through production arbitration.
            gui::apply_receptions(*inbox,*signals,snapshot);
            if(has_delta) {
                last_model_key=sk;
                for(std::size_t i=0;i<signals->lines().size();++i) {
                    const auto& line=signals->lines()[i];
                    const auto text=signals->copy_text(i);
                    const auto bytes=signals->copy_bytes(i);
                    const auto bits=signals->copy_bits(i);
                    journal.event("{\"event\":\"gui_model_row\",\"poll\":"+std::to_string(polls)+
                      ",\"row_id\":"+std::to_string(line.id)+",\"revision\":"+std::to_string(line.revision)+
                      ",\"complete\":"+(line.complete?"true":"false")+
                      ",\"validated\":"+(line.validated?"true":"false")+
                      ",\"binary\":"+(line.binary?"true":"false")+
                      ",\"received_bits\":"+std::to_string(line.received_bits)+
                      ",\"expected_bits\":"+std::to_string(line.expected_bits)+
                      ",\"copy_text\":"+(text?quote(*text):"null")+
                      ",\"copy_bytes_size\":"+(bytes?std::to_string(bytes->size()):"null")+
                      ",\"copy_bits\":"+(bits?quote(*bits):"null")+"}");
                }
                journal.event("{\"event\":\"gui_model_totals\",\"poll\":"+std::to_string(polls)+
                  ",\"inbox_items\":"+std::to_string(inbox->items().size())+
                  ",\"inbox_bytes\":"+std::to_string(inbox->size_bytes())+
                  ",\"signal_rows\":"+std::to_string(signals->lines().size())+"}");
            }
            if(!submitted&&saw_idle) {
                submission_prior_tx=snapshot.transmission_id;
                session->transmit_bits(help_bits);
                submitted=true;
                journal.event("{\"event\":\"submitted\",\"poll\":"+std::to_string(polls)+
                  ",\"bit_count\":32,\"bits\":\"01001000011001010110110001110000\"}");
            }
            if(submitted&&snapshot.transmission_id>submission_prior_tx&&snapshot.transmission_finished&&
               !snapshot.transmitting&&!snapshot.simulation_replay) {
                const auto& trace=snapshot.transmit_trace;
                if(!trace.active||!trace.raw||trace.generated_bits!=32||trace.total_wire_bits!=32||
                   trace.wire_plain_bits!=help_bits||trace.wire_bits.size()!=32||trace.data_key_bits.size()<32||
                   snapshot.simulation_receiving_tail||snapshot.transmission_fraction!=1)
                    throw std::runtime_error("completed replay has incorrect raw32 source/tail identity");
                for(std::size_t bit=0;bit<32;++bit)if(trace.wire_bits[bit]!=(trace.wire_plain_bits[bit]^trace.data_key_bits[bit]))
                    throw std::runtime_error("raw32 source Data XOR identity mismatch");
                if(!inbox->items().empty())throw std::runtime_error("raw32 unexpectedly acquired a stream identity");
                for(std::size_t i=0;i<signals->lines().size();++i) {
                    const auto text=signals->copy_text(i);
                    const auto bytes=signals->copy_bytes(i);
                    const auto bits=signals->copy_bits(i);
                    if(text&&*text=="Help"&&bytes&&*bytes==Bytes({'H','e','l','p'})&&!bits)copy_ok=true;
                }
                if(copy_ok) {
                    journal.event("{\"event\":\"copy_predicate\",\"poll\":"+std::to_string(polls)+
                      ",\"text\":\"Help\",\"bytes_hex\":\"48656c70\",\"bits_absent\":true}");
                    break;
                }
                throw std::runtime_error("original Help copy predicate failed at completed replay boundary");
            }
            std::this_thread::sleep_for(1ms);
            replay_ms->fetch_add(40,std::memory_order_relaxed);
        }
        if(!copy_ok)throw std::runtime_error("120 second fixture deadline expired");
    } catch(const std::exception& e) { failure=e.what(); }
    if(session) {
        try { session->stop(); } catch(const std::exception& e) { if(failure.empty())failure=std::string("stop failed: ")+e.what(); }
    }
    session.reset();
    const auto wall=std::chrono::duration<double>(Clock::now()-started).count();
    std::string result="{\"event\":\"case_result\",\"status\":"+quote(failure.empty()&&copy_ok?"passed":"failed")+
      ",\"reason\":"+quote(failure.empty()?(copy_ok?"complete":"copy predicate not met"):failure)+
      ",\"polls\":"+std::to_string(polls)+",\"saw_sampled_idle\":"+(saw_idle?"true":"false")+
      ",\"copy_predicate\":"+(copy_ok?"true":"false")+
      ",\"wall_seconds\":"+std::to_string(wall)+",\"process_cpu_usec\":"+std::to_string(cpu_usec()-cpu0)+
      ",\"threads\":"+std::to_string(thread_count())+",\"peak_rss_kib\":"+std::to_string(peak_rss_kib())+"}";
    try { journal.event(result);journal_ok=journal.finish(); } catch(...) { journal_ok=false; }
    std::cout<<"{\"event\":\"session_output_finalization\",\"flush_ok\":"<<(journal.flush_ok()?"true":"false")
      <<",\"close_ok\":"<<(journal.close_ok()?"true":"false")<<"}\n";
    std::cout.flush();

    return failure.empty()&&copy_ok&&journal_ok?0:1;
}

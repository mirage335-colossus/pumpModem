#pragma once
// Opt-in developer instrumentation. Never call from an audio process callback.
// Missing records/footer mean incomplete evidence, not a successful timing run.
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <array>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <cstdint>
#include <utility>

namespace datapump::audio::trace {
inline std::string quoted(const std::string& value) {
    std::string out="\"";
    for(unsigned char c:value) {
        if(c=='"'||c=='\\') {out+='\\';out+=c;}
        else if(c<32) {constexpr char hex[]="0123456789abcdef";out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=c;
    }
    return out+'"';
}
struct Fields {
    std::ostringstream out;
    Fields(){out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<long double>::max_digits10);}
    template<class T>void add(const char* name,T value) {
        out<<','<<quoted(name)<<':';
        if constexpr(std::is_floating_point_v<T>) {if(std::isfinite(value))out<<value;else out<<"null";}
        else out<<value;
    }
    void add(const char* name,const std::string& value){out<<','<<quoted(name)<<':'<<quoted(value);}
    void add(const char* name,const char* value){add(name,std::string(value));}
};
struct Finalization {
    bool prior_io_failed=false,flush_ok=false,close_ok=false;
    bool succeeded()const{return !prior_io_failed&&flush_ok&&close_ok;}
};
// An in-file footer cannot certify its own final flush/close. Publish a separate
// seal only afterward; never unlink a path which a user might have replaced.
inline Finalization finish_file(std::FILE*& file,bool prior_io_failed,
        int(*flush)(std::FILE*)=std::fflush,int(*close)(std::FILE*)=std::fclose) noexcept {
    if(!file)return {prior_io_failed,false,false};
    const bool flushed=flush(file)==0;
    auto* closing=std::exchange(file,nullptr);
    return {prior_io_failed,flushed,close(closing)==0};
}
inline std::string finalization_record(const std::string& token,const std::string& path,
        std::uint64_t attempted,std::uint64_t dropped,bool limited,Finalization result) {
    return "{\"event\":\"trace_finalization\",\"token\":"+quoted(token)+",\"path\":"+quoted(path)+
        ",\"attempted\":"+std::to_string(attempted)+",\"dropped\":"+std::to_string(dropped)+
        ",\"size_limited\":"+(limited?"true":"false")+",\"prior_io_failed\":"+(result.prior_io_failed?"true":"false")+
        ",\"flush_ok\":"+(result.flush_ok?"true":"false")+",\"close_ok\":"+(result.close_ok?"true":"false")+
        ",\"result\":"+quoted(result.succeeded()&&!dropped&&!limited?"complete":"incomplete")+"}\n";
}
struct State {
    static constexpr std::size_t capacity=256,maximum_record=16384;
    static constexpr std::uint64_t maximum_file_bytes=256ULL*1024*1024;
    std::uint64_t file_bytes=0;
    bool size_limited=false;
    bool receiver_probe=false;
    // Optional diagnostic selection happens before field formatting. Keep the
    // normal trace unchanged when no selector is supplied.
    bool (*event_filter)(const char*) noexcept=nullptr;
    std::FILE* file=nullptr;
    std::string token,path;
    std::mutex mutex;
    std::condition_variable changed,finished;
    struct Slot {std::atomic<std::uint64_t> sequence{0};std::string line;};
    std::array<Slot,capacity> slots;
    std::atomic<std::uint64_t> enqueue{0},dequeue{0};
    std::atomic<std::uint64_t> attempted{0},dropped{0};
    std::atomic<unsigned> producers{0};
    std::atomic<bool> closing{false};
    bool stopping=false,done=false;
    std::atomic<bool> io_failed{false};
    State(){for(std::size_t i=0;i<capacity;++i)slots[i].sequence=i;}
    ~State(){if(file)std::fclose(file);}
    std::uint64_t queued()const{return enqueue.load()-dequeue.load();}
    bool pop(std::string& line) {
        const auto pos=dequeue.load();auto& slot=slots[pos%capacity];
        if(slot.sequence.load(std::memory_order_acquire)!=pos+1)return false;
        line=std::move(slot.line);slot.sequence.store(pos+capacity,std::memory_order_release);dequeue.store(pos+1);return true;
    }
    void push(std::string line) noexcept {
        try {
            if(line.size()>maximum_record){++dropped;return;}
            auto pos=enqueue.load();
            for(unsigned retry=0;retry<8;++retry) {
                auto& slot=slots[pos%capacity];const auto seq=slot.sequence.load(std::memory_order_acquire);
                if(seq<pos){++dropped;return;}
                if(seq==pos&&enqueue.compare_exchange_weak(pos,pos+1)) {
                    slot.line=std::move(line);slot.sequence.store(pos+1,std::memory_order_release);changed.notify_one();return;
                }
                pos=enqueue.load();
            }
            ++dropped;
        }catch(...){++dropped;}
    }
    bool write(const std::string& line,bool footer=false) {
        if(!footer&&file_bytes+line.size()>maximum_file_bytes-1024) {size_limited=true;return false;}
        if(std::fwrite(line.data(),1,line.size(),file)!=line.size()||std::fflush(file)!=0)io_failed=true;
        file_bytes+=line.size();return true;
    }
    void run() noexcept {
        try {
            for(;;) {
                std::string line;
                if(!pop(line)) {
                    std::unique_lock lock(mutex);
                    if(stopping&&producers==0&&queued()==0)break;
                    changed.wait_for(lock,std::chrono::milliseconds(20));continue;
                }
                if(!write(line))++dropped;
            }
            write("{\"event\":\"trace_end\",\"token\":"+datapump::audio::trace::quoted(token)+",\"finalization_required\":true,\"attempted\":"+std::to_string(attempted.load())+
                ",\"dropped\":"+std::to_string(dropped.load())+",\"size_limited\":"+(size_limited?"true":"false")+
                ",\"prior_io_failed\":"+(io_failed?"true":"false")+"}\n",true);
        }catch(...){io_failed=true;}
        const auto result=finish_file(file,io_failed);
        if(!result.succeeded())io_failed=true;
        try {
            const auto seal=finalization_record(token,path,attempted,dropped,size_limited,result);
            // Ordinary writer thread, never the process callback. A missing or
            // truncated external seal is incomplete evidence, including detach.
            std::fwrite(seal.data(),1,seal.size(),stderr);std::fflush(stderr);
        }catch(...){io_failed=true;}
        {std::lock_guard lock(mutex);done=true;}finished.notify_all();
    }
};
// The shared_ptr atomic free functions also support standard libraries without
// the C++20 atomic<shared_ptr> specialization (including the GCC 11 SDK build).
// Keep owning snapshots so delayed producers and detached writers retain State.
class StateSlot {
    std::shared_ptr<State> pointer_;
public:
    StateSlot()=default;
    StateSlot(const StateSlot&)=delete;
    StateSlot& operator=(const StateSlot&)=delete;
    std::shared_ptr<State> load(std::memory_order order=std::memory_order_seq_cst) const noexcept {
        return std::atomic_load_explicit(&pointer_,order);
    }
    void store(std::shared_ptr<State> desired,std::memory_order order=std::memory_order_seq_cst) noexcept {
        std::atomic_store_explicit(&pointer_,std::move(desired),order);
    }
    bool compare_exchange_strong(std::shared_ptr<State>& expected,std::shared_ptr<State> desired) noexcept {
        return std::atomic_compare_exchange_strong_explicit(&pointer_,&expected,std::move(desired),
            std::memory_order_seq_cst,std::memory_order_seq_cst);
    }
};
inline StateSlot active;
inline bool receiver_probe_enabled() noexcept {
    const auto state=active.load(std::memory_order_acquire);
    return state && state->receiver_probe;
}
inline std::atomic<std::uint64_t> next_identity{0};
inline std::uint64_t identity(){return ++next_identity;}
template<class F>void record(const char* event,F fields) noexcept {
    const auto state=active.load(std::memory_order_acquire);if(!state)return;
    if(state->event_filter && !state->event_filter(event))return;
    ++state->producers;
    // A delayed caller which loaded active before close may not register work
    // after the footer. Accepted producers finish/drop before the footer.
    if(state->closing) {--state->producers;state->changed.notify_one();return;}
    const auto sequence=state->attempted.fetch_add(1)+1;
    try {
        Fields f;f.add("sequence",sequence);
        f.add("steady",std::chrono::duration<long double>(std::chrono::steady_clock::now().time_since_epoch()).count());
        fields(f);state->push("{\"event\":"+quoted(event)+f.out.str()+"}\n");
    }catch(...){++state->dropped;}
    --state->producers;state->changed.notify_one();
}
class Scope {
    std::shared_ptr<State> state_;
    std::thread writer_;
    std::filesystem::path path_;
public:
    explicit Scope(std::filesystem::path path={},bool receiver_probe=false,
                   bool (*event_filter)(const char*) noexcept=nullptr) {
        if(path.empty())path="datapump-verbose-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+".jsonl";
        path_=std::filesystem::absolute(path);
        state_=std::make_shared<State>();
        state_->receiver_probe=receiver_probe;
        state_->event_filter=event_filter;
        const auto utf8=path_.u8string();state_->path.assign(utf8.begin(),utf8.end());
        state_->token=std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(identity());
#if defined(_WIN32) && !defined(__linux__)
        state_->file=_wfopen(path_.c_str(),L"wbx");
#else
        // Linux also builds the WinMM fixture with _WIN32 defined.
        state_->file=std::fopen(path_.string().c_str(),"wbx");
#endif
        if(!state_->file)throw std::runtime_error("Cannot create new verbose trace: "+path_.string());
        std::shared_ptr<State> empty;
        if(!active.compare_exchange_strong(empty,state_))throw std::runtime_error("A verbose audio trace is already active");
        try{writer_=std::thread([s=state_]{s->run();});}
        catch(...){active.store({});throw;}
        record("trace_begin",[&](Fields& f){f.add("schema",2);f.add("token",state_->token);f.add("maximum_queue_records",State::capacity);
            f.add("maximum_record_bytes",State::maximum_record);f.add("maximum_file_bytes",State::maximum_file_bytes);
            f.add("long_double_digits",std::numeric_limits<long double>::max_digits10);
            f.add("receiver_probe",receiver_probe);if(event_filter)f.add("event_filter",true);});
    }
    ~Scope(){
        state_->closing=true;
        active.store({},std::memory_order_release);
        std::unique_lock lock(state_->mutex);state_->stopping=true;state_->changed.notify_one();
        const bool joined=state_->finished.wait_for(lock,std::chrono::milliseconds(500),[&]{return state_->done;});lock.unlock();
        // A stalled regular-file write must not hold device teardown hostage.
        // Detached writer retains State; a missing footer is incomplete evidence.
        if(joined)writer_.join();else writer_.detach();
    }
    const std::filesystem::path& path()const{return path_;}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
}

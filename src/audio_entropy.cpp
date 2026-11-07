#include "datapump/audio_entropy.hpp"
#include "datapump/execution.hpp"
#ifdef DATAPUMP_AUDIO_ENTROPY_PROCESS
#include "audio_entropy_process.hpp"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <limits>
#include <mutex>
#include <utility>

namespace datapump::audio {
namespace detail {
namespace {
using Clock = execution::Clock;
struct Timing {
    std::chrono::milliseconds timeout{12000};
    std::chrono::milliseconds discard{1000};
    std::chrono::milliseconds minimum_sample_span{2500};
    std::function<Clock::time_point()> now=[] {return Clock::now();};
};
std::string capture_identity(std::string device) {
    return device.empty()?"default":std::move(device);
}
}

// A request never retains sample buffers. The mutex serializes the core sink
// and makes detaching its borrowed pointer a callback-drain barrier.
struct EntropyCollection {
    execution::Mutex mutex;
    execution::StopCondition changed;
    AudioEntropySink* sink;
    Timing timing;
    std::uint32_t rate=0;
    std::uint64_t discarded=0;
    std::optional<Clock::time_point> first_accepted;
    bool done=false,detached=false;
    std::exception_ptr error;

    EntropyCollection(AudioEntropySink& value,Timing options):sink(&value),timing(std::move(options)){}
    void fail(std::exception_ptr value) noexcept {
        try {
            std::lock_guard lock(mutex);
            if(done||detached)return;
            error=std::move(value);done=true;changed.notify_all();
        } catch(...) {
            // Synchronization failure cannot be converted into usable entropy.
            std::terminate();
        }
    }
    void fail(const char* reason) noexcept {
        try {fail(std::make_exception_ptr(AudioEntropyUnavailable(reason)));} catch(...) {std::terminate();}
    }
    void observe(std::span<const float> samples,std::uint32_t sample_rate) noexcept {
        try {
            std::lock_guard lock(mutex);
            if(done||detached||!sink)return;
            try {
                if(!sample_rate || samples.empty() || samples.size()>sample_rate)
                    throw AudioEntropyUnavailable("keyfile audio source supplied an invalid capture block");
                if(rate && rate!=sample_rate)throw AudioEntropyUnavailable("keyfile audio input format changed during collection");
                rate=sample_rate;
                const auto discard_frames=(static_cast<std::uint64_t>(rate)*
                    static_cast<std::uint64_t>(timing.discard.count())+999)/1000;
                const auto skip=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),
                    discard_frames-std::min(discarded,discard_frames)));
                discarded+=skip;samples=samples.subspan(skip);
                if(samples.empty())return;
                const auto now=timing.now();
                if(!first_accepted)first_accepted=now;
                sink->add_samples(samples,rate);
                if(sink->complete()) {
                    if(now<*first_accepted || now-*first_accepted<timing.minimum_sample_span)
                        throw AudioEntropyUnavailable("keyfile audio input did not span a fresh capture interval");
                    done=true;changed.notify_all();
                }
            } catch(...) {error=std::current_exception();done=true;changed.notify_all();}
        } catch(...) {std::terminate();}
    }
    bool finished() {
        std::lock_guard lock(mutex);return done||detached;
    }
    void detach() {
        std::lock_guard lock(mutex);sink=nullptr;detached=true;changed.notify_all();
    }
};

class EntropyCaptureOwner final {
    friend struct EntropyBroker;
    explicit EntropyCaptureOwner(std::shared_ptr<EntropyCollection> value):collection_(std::move(value)){}
    std::shared_ptr<EntropyCollection> collection_;
};

struct CaptureSession {
    std::string device;
    CaptureRoute route=CaptureRoute::selected_policy;
    bool exclusive=false,entropy_owned=false,active=true;
    std::weak_ptr<EntropyCollection> collection;
};
class BorrowedCapture final:public Error {public:BorrowedCapture():Error("keyfile audio uses the active capture"){};};
class CaptureOwnershipFailure final:public Error {public:using Error::Error;};

struct EntropyBroker {
    static constexpr std::size_t maximum_captures=16;
    execution::Mutex mutex;
    std::array<std::shared_ptr<CaptureSession>,maximum_captures> sessions{};
    std::shared_ptr<EntropyCollection> pending;
    std::string pending_device;
    bool pending_exclusive=false;
    std::atomic<bool> observing{false};

    std::shared_ptr<CaptureSession> begin_capture(const std::string& device,
        const std::shared_ptr<const EntropyCaptureOwner>& owner,bool exclusive,CaptureRoute route) {
        std::lock_guard lock(mutex);
        const auto identity=capture_identity(device);
        const bool matches_pending=identity==pending_device&&exclusive==pending_exclusive&&route==CaptureRoute::selected_policy;
        const bool own=owner && pending && owner->collection_==pending && matches_pending;
        if(owner&&!own)throw CaptureOwnershipFailure("invalid or expired keyfile audio capture ownership");
        // A helper which has not conclusively exited keeps its reservation,
        // even after its caller has detached the sink and timed out.
        if(!own)for(const auto& session:sessions)if(session&&session->active&&session->entropy_owned)
            throw Error("audio input is temporarily in use for keyfile generation");
        if(!own && pending && !matches_pending) {
            bool own_capture=false,any_capture=false;
            for(const auto& session:sessions)if(session&&session->active) {
                any_capture=true;own_capture=own_capture||session->entropy_owned;
            }
            if(!any_capture||own_capture)
                throw Error("wait for the short keyfile input capture before opening another input");
        }
        for(const auto& session:sessions)if(session&&session->active&&session->device==identity&&session->exclusive==exclusive&&session->route==route) {
            if(own) {
                // A receiver may have claimed the reserved input before the
                // short capture task ran. Borrow it without opening a device.
                if(session->collection.expired())session->collection=pending;
                throw BorrowedCapture();
            }
            if(session->entropy_owned)
                throw Error("audio input is temporarily in use for keyfile generation");
        }
        auto slot=std::find(sessions.begin(),sessions.end(),nullptr);
        if(slot==sessions.end())throw Error("too many active physical audio captures");
        auto session=std::make_shared<CaptureSession>();
        session->device=identity;session->exclusive=exclusive;session->route=route;session->entropy_owned=own;
        if(pending && matches_pending) {
            bool attached=false;
            for(const auto& other:sessions)if(other&&!other->collection.expired())attached=true;
            if(!attached)session->collection=pending;
        }
        *slot=session;return session;
    }
    void end_capture(const std::shared_ptr<CaptureSession>& session) noexcept {
        std::shared_ptr<EntropyCollection> collection;
        try {
            {
                std::lock_guard lock(mutex);session->active=false;collection=session->collection.lock();
                session->collection.reset();
                for(auto& slot:sessions)if(slot==session){slot.reset();break;}
            }
            if(collection)collection->fail("audio input ended before keyfile entropy collection completed");
        } catch(...) {std::terminate();}
    }
    std::shared_ptr<EntropyCollection> observer(const std::shared_ptr<CaptureSession>& session) {
        if(!observing.load(std::memory_order_acquire))return {};
        std::lock_guard lock(mutex);
        return session->active?session->collection.lock():std::shared_ptr<EntropyCollection>{};
    }
    std::shared_ptr<const EntropyCaptureOwner> begin_collection(const std::string& device,
        bool exclusive,const std::shared_ptr<EntropyCollection>& collection,bool& borrowed) {
        std::lock_guard lock(mutex);
        if(pending)throw AudioEntropyUnavailable("another keyfile audio collection is already in progress");
        const auto identity=capture_identity(device);bool active=false;borrowed=false;
        for(const auto& session:sessions)if(session&&session->active) {
            if(session->entropy_owned)
                throw AudioEntropyUnavailable("a previous keyfile audio capture has not finished shutting down");
            active=true;if(session->device==identity&&session->exclusive==exclusive&&session->route==CaptureRoute::selected_policy)borrowed=true;
        }
        // Backend aliases cannot reliably prove that two route names refer to
        // different physical devices. Never open a competing device while a
        // different selected capture is already active.
        if(active&&!borrowed)throw AudioEntropyUnavailable("selected keyfile audio input differs from the active input");
        pending=collection;pending_device=identity;pending_exclusive=exclusive;
        if(borrowed)for(const auto& session:sessions)if(session&&session->active&&session->device==pending_device&&session->exclusive==exclusive&&session->route==CaptureRoute::selected_policy) {
            session->collection=collection;break;
        }
        observing.store(true,std::memory_order_release);
        try {return std::shared_ptr<const EntropyCaptureOwner>(new EntropyCaptureOwner(collection));}
        catch(...) {
            for(auto& session:sessions)if(session)session->collection.reset();
            pending.reset();pending_device.clear();observing.store(false,std::memory_order_release);throw;
        }
    }
    void end_collection(const std::shared_ptr<EntropyCollection>& collection) noexcept {
        try {
            {
                std::lock_guard lock(mutex);
                for(auto& session:sessions)if(session&&session->collection.lock()==collection)session->collection.reset();
                if(pending==collection){pending.reset();pending_device.clear();observing.store(false,std::memory_order_release);}
            }
            collection->detach();
        } catch(...) {std::terminate();}
    }
};

namespace {
EntropyBroker& broker(){static EntropyBroker value;return value;}
struct CollectionLease {
    std::shared_ptr<EntropyCollection> collection;
    ~CollectionLease(){if(collection)broker().end_collection(collection);}
    void detach(){if(collection){broker().end_collection(collection);collection.reset();}}
};
KeyfileAudioSource make_source(std::string device,bool exclusive,Timing timing) {
    return [device=capture_identity(std::move(device)),exclusive,timing=std::move(timing)]
        (AudioEntropySink& sink,std::stop_token stop) {
        if(stop.stop_requested())throw Error("keyfile audio collection cancelled");
#ifdef DATAPUMP_AUDIO_ENTROPY_PROCESS
        reap_quarantined_entropy_process();
#endif
        auto collection=std::make_shared<EntropyCollection>(sink,timing);
        bool borrowed=false;
        auto owner=broker().begin_collection(device,exclusive,collection,borrowed);
        CollectionLease lease{collection};
        execution::Task capture_task;
        if(!borrowed) {
            capture_task=execution::Task([collection,owner,device,exclusive](std::stop_token capture_stop) {
                try {
#ifdef DATAPUMP_AUDIO_ENTROPY_PROCESS
                    capture_entropy_process(device,exclusive,owner,capture_stop);
#else
                    Options options;options.exclusive=exclusive;options.entropy_capture_owner=owner;
                    capture(48000,device,[collection](std::span<const float>) {
                        return !collection->finished();
                    },capture_stop,{},std::move(options));
#endif
                    if(!collection->finished())collection->fail("audio input ended before keyfile collection completed");
                } catch(const BorrowedCapture&) {
                    // Registry arbitration attached this request to the input
                    // that won the race. Its lifetime now controls completion.
                } catch(const CaptureOwnershipFailure&) {collection->fail(std::current_exception());}
                catch(const AudioEntropyUnavailable&) {collection->fail(std::current_exception());}
                catch(const Error& error) {
                    if(capture_stop.stop_requested())collection->fail(std::current_exception());
                    else collection->fail(std::make_exception_ptr(AudioEntropyUnavailable(
                        std::string("audio input unavailable: ")+error.what())));
                } catch(...) {collection->fail(std::current_exception());}
            });
        }
        std::exception_ptr failure;
        {
            std::unique_lock lock(collection->mutex);
            const auto deadline=Clock::now()+timing.timeout;
            const bool ready=collection->changed.wait_until(lock,stop,deadline,[&]{return collection->done;});
            if(stop.stop_requested())failure=std::make_exception_ptr(Error("keyfile audio collection cancelled"));
            else if(!ready)failure=std::make_exception_ptr(AudioEntropyUnavailable("audio input did not deliver usable fresh keyfile samples before the deadline"));
            else failure=collection->error;
        }
        // Unsubscribe and clear the borrowed sink before stopping/joining our
        // own capture. A borrowed receiver is never cancelled or consumed.
        lease.detach();
        if(capture_task.joinable()){capture_task.request_stop();capture_task.join();}
        if(failure)std::rethrow_exception(failure);
    };
}
}

struct CaptureEntropyRegistration::Impl {
    std::shared_ptr<CaptureSession> session;
    explicit Impl(std::shared_ptr<CaptureSession> value):session(std::move(value)){}
};
CaptureEntropyRegistration::CaptureEntropyRegistration(const std::string& device,
    std::shared_ptr<const EntropyCaptureOwner> owner,bool exclusive,CaptureRoute route) {
#ifdef DATAPUMP_AUDIO_ENTROPY_PROCESS
    reap_quarantined_entropy_process();
#endif
    auto session=broker().begin_capture(device,owner,exclusive,route);
    try {impl_=std::make_unique<Impl>(session);}
    catch(...) {broker().end_capture(session);throw;}
}
CaptureEntropyRegistration::~CaptureEntropyRegistration(){broker().end_capture(impl_->session);}
CaptureMonitor CaptureEntropyRegistration::monitor(CaptureMonitor existing)const {
    return [session=impl_->session,existing=std::move(existing)](std::span<const float> samples,std::uint32_t rate) {
        if(existing)existing(samples,rate);
        if(auto collection=broker().observer(session))collection->observe(samples,rate);
    };
}
CaptureDiscontinuity CaptureEntropyRegistration::discontinuity(CaptureDiscontinuity existing)const {
    return [session=impl_->session,existing=std::move(existing)] {
        if(auto collection=broker().observer(session))collection->fail("audio input discontinuity during keyfile entropy collection");
        if(existing)existing();else throw Error("audio capture discontinuity");
    };
}
void CaptureEntropyRegistration::failed(std::exception_ptr error)const noexcept {
    try {
        try {if(error)std::rethrow_exception(error);}
        catch(const AudioEntropyUnavailable&) {}
        catch(const Error& value) {
            error=std::make_exception_ptr(AudioEntropyUnavailable(std::string("audio input unavailable: ")+value.what()));
        }
        catch(...) {}
        if(auto collection=broker().observer(impl_->session))collection->fail(std::move(error));
    }
    catch(...) {std::terminate();}
}
} // namespace detail

KeyfileAudioSource keyfile_audio_source(std::string device,bool exclusive) {
    return detail::make_source(std::move(device),exclusive,{});
}
#ifdef DATAPUMP_AUDIO_ENTROPY_TESTING
namespace testing {
KeyfileAudioSource keyfile_audio_source(std::string device,bool exclusive,EntropyTiming timing) {
    detail::Timing options;options.timeout=timing.timeout;options.discard=timing.discard;
    options.minimum_sample_span=timing.minimum_sample_span;if(timing.now)options.now=std::move(timing.now);
    if(options.timeout<=std::chrono::milliseconds::zero() || options.discard< std::chrono::milliseconds::zero() ||
        options.minimum_sample_span<std::chrono::milliseconds::zero())throw Error("invalid test audio timing");
    return detail::make_source(std::move(device),exclusive,std::move(options));
}
}
#endif
} // namespace datapump::audio

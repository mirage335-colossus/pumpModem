#include "datapump/audio_entropy.hpp"
#include "datapump/execution.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

using namespace datapump;
namespace {
using Clock=std::chrono::steady_clock;
enum class Provider {normal,realtime,slow,gated,unavailable,internal_failure,short_input,rate_change,gap,silent,stall,retained};
std::atomic<Provider> provider{Provider::normal};
std::atomic<unsigned> physical_opens{0};
std::atomic<std::int64_t> audio_ticks{0};
std::atomic<unsigned> paths{0};
std::mutex opening_mutex;
std::condition_variable opening_changed;
bool opening_entered=false,opening_allowed=false;
std::unique_ptr<audio::detail::CaptureEntropyRegistration> retained_capture;
void check(bool ok,const char* message){if(!ok)throw Error(message);}
std::array<float,480> block(std::uint64_t& state) {
    std::array<float,480> result{};
    for(auto& sample:result) {
        state^=state<<13;state^=state>>7;state^=state<<17;
        sample=static_cast<float>(static_cast<int>(state&65535)-32768)/655360.0f;
    }
    return result;
}
struct Files {
    std::filesystem::path directory=std::filesystem::temp_directory_path()/
        ("datapump-audio-entropy-"+std::to_string(Clock::now().time_since_epoch().count())+"-"+std::to_string(paths++));
    Files(){check(std::filesystem::create_directory(directory),"audio test directory already exists");}
    ~Files(){std::error_code error;std::filesystem::remove_all(directory,error);}
};
audio::testing::EntropyTiming timing(std::chrono::milliseconds timeout=std::chrono::milliseconds(500)) {
    audio::testing::EntropyTiming result;result.timeout=timeout;
    result.now=[] {return Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(audio_ticks.load())));};
    return result;
}
KeyfileGenerationResult generate(const KeyfileAudioSource& source,bool fallback=false,std::stop_token stop={}) {
    Files files;KeyfileGenerationOptions options;options.audio=source;options.allow_audio_fallback=fallback;options.stop=stop;
    const auto result=testing::create_keyring(files.directory/"key",{"Audio"},testing::KeyfilePolicy{4096,8193},{},options);
    check(testing::load_keyring(files.directory/"key",testing::KeyfilePolicy{4096,8193}).size()==1,
          "audio-generated keyfile cannot be loaded");
    return result;
}
template<class Function>void unavailable(Function function) {
    bool failed=false;try{function();}catch(const AudioEntropyUnavailable&){failed=true;}
    check(failed,"unavailable audio did not fail explicitly");
}
template<class Predicate>void wait_for(Predicate ready,const char* message) {
    const auto deadline=Clock::now()+std::chrono::seconds(4);
    while(!ready()) {
        check(Clock::now()<deadline,message);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void rethrow(const std::exception_ptr& error){if(error)std::rethrow_exception(error);}

void owned_capture() {
    provider=Provider::normal;physical_opens=0;audio_ticks=0;
    const auto result=generate(audio::testing::keyfile_audio_source("selected",true,timing()));
    check(result.audio_used&&result.warning.empty(),"physical source was not reported used");
    check(physical_opens==1,"standalone source did not own exactly one capture");
    provider=Provider::realtime;physical_opens=0;
    const auto started=Clock::now();
    const auto production=generate(audio::keyfile_audio_source("selected"));
    check(production.audio_used&&physical_opens==1&&Clock::now()-started>=std::chrono::seconds(4),
          "production audio timing did not observe a fresh paced interval");
}
void fallback_and_internal_errors() {
    for(const auto mode:{Provider::unavailable,Provider::short_input,Provider::rate_change,Provider::gap,Provider::silent,Provider::stall}) {
        provider=mode;physical_opens=0;audio_ticks=0;
        auto source=audio::testing::keyfile_audio_source("selected",false,timing(std::chrono::milliseconds(100)));
        const auto result=generate(source,true);
        check(!result.audio_used&&!result.warning.empty(),"audio fallback omitted its warning or claimed samples");
    }
    provider=Provider::unavailable;
    unavailable([&]{generate(audio::testing::keyfile_audio_source("selected",false,timing()));});
    provider=Provider::internal_failure;bool internal=false;
    try {generate(audio::testing::keyfile_audio_source("selected",false,timing()),true);}
    catch(const std::bad_alloc&){internal=true;}
    check(internal,"internal source error was downgraded to an audio warning");
}
void fresh_interval_required() {
    provider=Provider::normal;audio_ticks=0;
    auto fast=timing();fast.now=[] {return Clock::time_point{};};
    const auto result=generate(audio::testing::keyfile_audio_source("selected",false,std::move(fast)),true);
    check(!result.audio_used&&!result.warning.empty(),"bulk replay passed the fresh interval check");
}
void active_capture_is_not_consumed() {
    provider=Provider::normal;physical_opens=0;audio_ticks=0;
    std::atomic<unsigned> original_calls{0};
    audio::detail::CaptureEntropyRegistration input("selected");
    auto monitor=input.monitor([&](std::span<const float>,std::uint32_t){++original_calls;});
    std::jthread producer([&](std::stop_token stop) {
        std::uint64_t state=0x358a39e90c49d2b1ULL;
        while(!stop.stop_requested()) {
            const auto samples=block(state);audio_ticks.fetch_add(10'000'000);
            monitor(samples,48000);std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    const auto result=generate(audio::testing::keyfile_audio_source("selected",false,timing(std::chrono::seconds(2))));
    check(result.audio_used&&physical_opens==0,"key generation opened a competing active input");
    const auto before=original_calls.load();std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(original_calls>before,"audio generation consumed or cancelled the existing monitor");
    producer.request_stop();producer.join();
    // Detached callbacks cannot access the destroyed core sink.
    std::uint64_t state=0x3234484587ULL;monitor(block(state),48000);
}
void mismatch_and_deadline() {
    physical_opens=0;
    audio::detail::CaptureEntropyRegistration input("active");
    const auto mismatch=generate(audio::testing::keyfile_audio_source("other",false,timing()),true);
    check(!mismatch.audio_used&&!mismatch.warning.empty()&&physical_opens==0,
          "a different active input was borrowed or a competing device opened");
    const auto sharing=generate(audio::testing::keyfile_audio_source("active",true,timing()),true);
    check(!sharing.audio_used&&!sharing.warning.empty()&&physical_opens==0,
          "an incompatible exclusive input route was silently borrowed");
    const auto stalled=generate(audio::testing::keyfile_audio_source("active",false,timing(std::chrono::milliseconds(50))),true);
    check(!stalled.audio_used&&!stalled.warning.empty()&&physical_opens==0,"active input deadline did not fail safely");
}
void direct_route_is_not_silently_shared() {
    physical_opens=0;
    audio::detail::CaptureEntropyRegistration direct("hw:0",{},false,audio::detail::CaptureRoute::direct_device);
    const auto result=generate(audio::testing::keyfile_audio_source("hw:0",false,timing()),true);
    check(!result.audio_used&&!result.warning.empty()&&physical_opens==0,
          "a direct hardware capture was confused with the selected shared route");
}
void cancellation_aborts() {
    physical_opens=0;std::stop_source cancel;cancel.request_stop();
    bool aborted=false;try {
        generate(audio::testing::keyfile_audio_source("selected",false,timing()),true,cancel.get_token());
    } catch(const AudioEntropyUnavailable&) {throw Error("cancellation was classified as unavailable audio");}
    catch(const Error&){aborted=true;}
    check(aborted&&physical_opens==0,"cancelled key generation opened input or succeeded");
}
void cancellation_during_owned_capture() {
    provider=Provider::slow;physical_opens=0;audio_ticks=0;
    std::stop_source cancel;std::exception_ptr failure;std::atomic<bool> accepted{false};
    auto options=timing(std::chrono::seconds(5));options.discard=std::chrono::milliseconds(0);
    options.now=[&]{accepted=true;return Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(audio_ticks.load())));};
    std::jthread keygen([&] {
        try {generate(audio::testing::keyfile_audio_source("selected",false,options),true,cancel.get_token());}
        catch(...){failure=std::current_exception();}
    });
    wait_for([&]{return accepted.load();},"owned capture did not deliver accepted samples");
    cancel.request_stop();keygen.join();
    check(static_cast<bool>(failure),"owned cancellation produced a keyfile");
    bool aborted=false;try{rethrow(failure);}catch(const AudioEntropyUnavailable&){throw Error("owned cancellation downgraded to fallback");}
    catch(const Error&){aborted=true;}
    check(aborted,"owned cancellation was not reported");
}
void cancellation_during_borrowed_capture() {
    provider=Provider::normal;physical_opens=0;audio_ticks=0;
    audio::detail::CaptureEntropyRegistration input("selected");
    std::atomic<unsigned> original_calls{0};auto monitor=input.monitor([&](std::span<const float>,std::uint32_t){++original_calls;});
    std::jthread producer([&](std::stop_token stop) {
        std::uint64_t state=0x538f203ea981cb17ULL;
        while(!stop.stop_requested()) {
            audio_ticks.fetch_add(10'000'000);monitor(block(state),48000);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    std::stop_source cancel;std::exception_ptr failure;std::atomic<bool> accepted{false};
    auto options=timing(std::chrono::seconds(5));
    options.now=[&]{accepted=true;return Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(audio_ticks.load())));};
    auto source=audio::testing::keyfile_audio_source("selected",false,options);
    std::jthread keygen([&]{try{generate(source,true,cancel.get_token());}catch(...){failure=std::current_exception();}});
    wait_for([&]{return accepted.load();},"borrowed collection did not deliver accepted samples");
    cancel.request_stop();keygen.join();
    bool aborted=false;try{rethrow(failure);}catch(const AudioEntropyUnavailable&){throw Error("borrowed cancellation downgraded to fallback");}
    catch(const Error&){aborted=true;}
    check(aborted&&physical_opens==0,"borrowed cancellation opened or completed a capture");
    const auto before=original_calls.load();std::this_thread::sleep_for(std::chrono::milliseconds(15));
    check(original_calls>before,"borrowed cancellation interrupted the receiver");
    producer.request_stop();producer.join();
}
void simultaneous_collections() {
    provider=Provider::stall;physical_opens=0;audio_ticks=0;
    std::stop_source cancel;std::exception_ptr first_error;
    auto source=audio::testing::keyfile_audio_source("selected",false,timing(std::chrono::seconds(5)));
    std::jthread first([&]{try{generate(source,true,cancel.get_token());}catch(...){first_error=std::current_exception();}});
    wait_for([]{return physical_opens.load()==1;},"first simultaneous capture did not start");
    const auto second=generate(source,true);
    check(!second.audio_used&&!second.warning.empty()&&physical_opens==1,
          "simultaneous requests opened two devices or hid fallback");
    cancel.request_stop();first.join();check(static_cast<bool>(first_error),"first request ignored cancellation");
}
void receiver_wins_opening_race() {
    provider=Provider::gated;physical_opens=0;audio_ticks=0;
    {std::lock_guard lock(opening_mutex);opening_entered=opening_allowed=false;}
    std::exception_ptr failure;KeyfileGenerationResult result;
    std::jthread keygen([&] {
        try {result=generate(audio::testing::keyfile_audio_source("selected",false,timing(std::chrono::seconds(3))));}
        catch(...){failure=std::current_exception();}
    });
    {
        std::unique_lock lock(opening_mutex);
        check(opening_changed.wait_for(lock,std::chrono::seconds(4),[]{return opening_entered;}),"opening race did not reach provider");
    }
    audio::detail::CaptureEntropyRegistration receiver("selected");auto monitor=receiver.monitor();
    std::jthread producer([&](std::stop_token stop) {
        std::uint64_t state=0x19829782ac78b7e1ULL;
        while(!stop.stop_requested()) {
            audio_ticks.fetch_add(10'000'000);monitor(block(state),48000);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    {std::lock_guard lock(opening_mutex);opening_allowed=true;opening_changed.notify_all();}
    keygen.join();producer.request_stop();producer.join();rethrow(failure);
    check(result.audio_used&&physical_opens==0,"opening race did not borrow the winning receiver");
}
void retained_capture_keeps_reservation() {
    provider=Provider::retained;physical_opens=0;audio_ticks=0;
    const auto timed_out=generate(audio::testing::keyfile_audio_source("selected",false,timing(std::chrono::milliseconds(30))),true);
    check(!timed_out.audio_used&&!timed_out.warning.empty()&&retained_capture,
          "retained capture did not report its timeout");
    const auto retry=generate(audio::testing::keyfile_audio_source("selected",false,timing()),true);
    check(!retry.audio_used&&!retry.warning.empty()&&physical_opens==1,
          "an unreleased entropy capture was borrowed or a second device opened");
    bool blocked=false;
    try {audio::detail::CaptureEntropyRegistration receiver("different alias");}
    catch(const Error&){blocked=true;}
    check(blocked,"an unreleased capture allowed a competing receiver route");
    retained_capture.reset();
    audio::detail::CaptureEntropyRegistration receiver("different alias");
}
}

// Link-time physical provider fixture. Production builds have no input mode
// selector or fake-sample flag; this source is linked only into this test.
namespace datapump::audio {
void capture(std::uint32_t,const std::string& device,const CaptureCallback& callback,std::stop_token stop,
    StreamFormatCallback,Options options) {
    if(provider==Provider::gated) {
        std::unique_lock lock(opening_mutex);opening_entered=true;opening_changed.notify_all();
        while(!opening_allowed&&!stop.stop_requested())opening_changed.wait_for(lock,std::chrono::milliseconds(10));
        if(stop.stop_requested())throw Error("audio operation cancelled");
    }
    if(provider==Provider::retained) {
        retained_capture=std::make_unique<detail::CaptureEntropyRegistration>(device,options.entropy_capture_owner,options.exclusive);
        ++physical_opens;
        while(!stop.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return;
    }
    detail::CaptureEntropyRegistration registration(device,options.entropy_capture_owner,options.exclusive);
    ++physical_opens;
    auto monitor=registration.monitor(std::move(options.capture_monitor));
    try {
        if(provider==Provider::unavailable)throw Error("test input unavailable");
        if(provider==Provider::internal_failure)throw std::bad_alloc();
        if(provider==Provider::stall) {
            while(!stop.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds(1));
            throw Error("audio operation cancelled");
        }
        std::uint64_t state=0xb20874719ced6921ULL;
        for(unsigned index=0;index<1000;++index) {
            if(stop.stop_requested())throw Error("audio operation cancelled");
            auto samples=block(state);
            if(provider==Provider::silent)samples.fill(0);
            audio_ticks.fetch_add(10'000'000);
            if(provider==Provider::realtime||provider==Provider::slow)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            monitor(samples,provider==Provider::rate_change&&index>110?44100:48000);
            if(provider==Provider::gap&&index==110)registration.discontinuity(options.capture_discontinuity)();
            if(provider==Provider::short_input&&index==110)return;
            if(!callback(samples))return;
        }
        throw Error("test capture exhausted its bounded source");
    } catch(...) {registration.failed(std::current_exception());throw;}
}
}

int main() {
    try {
        owned_capture();fallback_and_internal_errors();fresh_interval_required();
        active_capture_is_not_consumed();mismatch_and_deadline();direct_route_is_not_silently_shared();cancellation_aborts();
        cancellation_during_owned_capture();cancellation_during_borrowed_capture();
        simultaneous_collections();receiver_wins_opening_race();retained_capture_keeps_reservation();
        std::cout<<"bounded physical audio entropy broker tests passed\n";return 0;
    } catch(const std::exception& error){retained_capture.reset();std::cerr<<error.what()<<'\n';return 1;}
}

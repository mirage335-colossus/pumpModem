// Standalone physical audio capture, using the same API/S16 conversion as Fast.
// Input and output are raw little-endian mono float32, with no headers.
#include "datapump/audio.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using Clock=std::chrono::steady_clock;
namespace {
std::string quote(const std::string& value) {
    std::string result="\"";
    for(unsigned char c:value) {
        if(c=='\\'||c=='\"')result+='\\';
        if(c=='\n')result+="\\n";
        else if(c<32)result+=' ';
        else result+=static_cast<char>(c);
    }
    return result+'\"';
}
void format(const datapump::audio::StreamFormat& f) {
    std::cout<<"{\"logical_rate\":"<<f.logical_rate<<",\"hardware_rate\":"<<f.hardware_rate
        <<",\"usable_passband_hz\":"<<f.usable_passband_hz<<",\"workspace_bytes\":"<<f.workspace_bytes<<'}';
}
struct Level {
    double square=0,peak=0;
    std::uint64_t samples=0,near_fullscale=0,above_fullscale=0;
    void add(float value) {
        square+=double(value)*value;peak=std::max(peak,std::abs(double(value)));++samples;
        near_fullscale+=std::abs(value)>=.999;above_fullscale+=std::abs(value)>1;
    }
    void json()const {
        std::cout<<"{\"samples\":"<<samples<<",\"rms\":"<<(samples?std::sqrt(square/samples):0)
            <<",\"peak\":"<<peak<<",\"near_fullscale_samples\":"<<near_fullscale
            <<",\"above_fullscale_samples\":"<<above_fullscale<<'}';
    }
};
struct Options {
    std::filesystem::path input,output;
    std::string device="default";
    std::uint32_t rate=48000;
    bool stereo=true;
};
std::vector<float> read_input(const Options& o) {
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
    const auto bytes=std::filesystem::file_size(o.input);
    if(!bytes || bytes%4 || bytes>std::uint64_t(o.rate)*120*4)
        throw std::runtime_error("Input must contain nonempty aligned float32 PCM, at most 120 seconds");
    std::vector<float> samples(static_cast<std::size_t>(bytes/4));
    std::ifstream file(o.input,std::ios::binary);
    if(!file.read(reinterpret_cast<char*>(samples.data()),static_cast<std::streamsize>(bytes)))
        throw std::runtime_error("Cannot read input PCM");
    if constexpr(std::endian::native!=std::endian::little) {
        for(auto& value:samples) {
            auto word=std::bit_cast<std::uint32_t>(value);
            word=((word&0xff)<<24)|((word&0xff00)<<8)|((word>>8)&0xff00)|(word>>24);
            value=std::bit_cast<float>(word);
        }
    }
    for(auto value:samples)if(!std::isfinite(value))throw std::runtime_error("Input contains nonfinite PCM");
    return samples;
}
void save_output(const std::filesystem::path& path,std::vector<float>& samples) {
    if constexpr(std::endian::native!=std::endian::little) {
        for(auto& value:samples) {
            auto word=std::bit_cast<std::uint32_t>(value);
            word=((word&0xff)<<24)|((word&0xff00)<<8)|((word>>8)&0xff00)|(word>>24);
            value=std::bit_cast<float>(word);
        }
    }
    // Exclusive creation: a diagnostic must not replace an existing capture.
    auto* file=std::fopen(path.string().c_str(),"wbx");
    if(!file)throw std::runtime_error("Cannot exclusively create output PCM");
    const bool written=std::fwrite(samples.data(),sizeof(float),samples.size(),file)==samples.size();
    const auto closed=std::fclose(file);
    if(!written||closed)throw std::runtime_error("Cannot write output PCM");
}
int run(const Options& o) {
    if(std::filesystem::exists(o.output))throw std::runtime_error("Output already exists");
    auto input=read_input(o);
    Level input_level,capture_level;
    for(auto value:input)input_level.add(value);
    constexpr double preroll_seconds=.75,postroll_seconds=1.;
    const double input_seconds=double(input.size())/o.rate;
    const double maximum_seconds=preroll_seconds+input_seconds+postroll_seconds+10;
    const auto maximum_samples=static_cast<std::size_t>(std::ceil(maximum_seconds*o.rate));
    std::vector<float> recorded;recorded.reserve(maximum_samples);
    std::atomic<std::uint64_t> captured{0},stop_after_sample{std::numeric_limits<std::uint64_t>::max()};
    std::atomic<bool> capture_done{false},timed_out{false};
    std::stop_source stop;
    datapump::audio::StreamFormat capture_format{},playback_format{};
    std::string capture_error,playback_error;
    bool sample_bound_reached=false;
    std::uint64_t playback_start_sample=0,playback_format_sample=0,playback_done_sample=0;
    const auto started=Clock::now();
    double playback_start_seconds=0,playback_done_seconds=0;
    std::mutex watchdog_mutex;std::condition_variable watchdog_cv;bool watchdog_finished=false;
    std::thread watchdog([&] {
        std::unique_lock lock(watchdog_mutex);
        if(!watchdog_cv.wait_until(lock,started+std::chrono::duration<double>(maximum_seconds),[&]{return watchdog_finished;})) {
            timed_out=true;stop.request_stop();
        }
    });
    std::thread capture_thread([&] {
        try {
            datapump::audio::capture(o.rate,o.device,[&](std::span<const float> chunk) {
                const auto count=std::min(chunk.size(),maximum_samples-recorded.size());
                // Capacity was reserved before opening hardware; this only
                // copies bounded PCM, without DSP, filesystem I/O or logging.
                recorded.insert(recorded.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(count));
                captured.store(recorded.size());
                if(recorded.size()>=maximum_samples){sample_bound_reached=true;return false;}
                return recorded.size()<stop_after_sample.load();
            },stop.get_token(),[&](const auto& f){capture_format=f;});
        } catch(const std::exception& e){capture_error=e.what();stop.request_stop();}
        capture_done=true;
    });
    try {
        const auto startup_deadline=started+std::chrono::seconds(10);
        while(captured.load()<std::ceil(preroll_seconds*o.rate)) {
            if(capture_done.load())throw std::runtime_error("Capture ended before playback");
            if(Clock::now()>=startup_deadline)throw std::runtime_error("Capture startup exceeded 10 seconds");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        playback_start_sample=captured.load();
        playback_start_seconds=std::chrono::duration<double>(Clock::now()-started).count();
        datapump::audio::play(input,o.rate,o.device,stop.get_token(),[&](const auto& f) {
            playback_format=f;playback_format_sample=captured.load();
        },!o.stereo);
        playback_done_sample=captured.load();
        playback_done_seconds=std::chrono::duration<double>(Clock::now()-started).count();
        stop_after_sample=playback_done_sample+static_cast<std::uint64_t>(std::ceil(postroll_seconds*o.rate));
    } catch(const std::exception& e){playback_error=e.what();stop.request_stop();}
    capture_thread.join();
    {
        std::lock_guard lock(watchdog_mutex);watchdog_finished=true;watchdog_cv.notify_all();
    }
    watchdog.join();
    const auto elapsed=std::chrono::duration<double>(Clock::now()-started).count();
    for(auto value:recorded)capture_level.add(value);
    save_output(o.output,recorded);
    const bool success=capture_error.empty()&&playback_error.empty()&&!timed_out&&!sample_bound_reached
        && playback_done_sample && recorded.size()>=stop_after_sample.load();
    std::cout<<std::setprecision(12)<<"{\"success\":"<<(success?"true":"false")
        <<",\"input\":"<<quote(o.input.string())<<",\"output\":"<<quote(o.output.string())
        <<",\"device\":"<<quote(o.device)<<",\"stereo\":"<<(o.stereo?"true":"false")
        <<",\"logical_rate\":"<<o.rate<<",\"input_samples\":"<<input.size()
        <<",\"input_seconds\":"<<input_seconds<<",\"capture_samples\":"<<recorded.size()
        <<",\"capture_seconds\":"<<double(recorded.size())/o.rate<<",\"elapsed_seconds\":"<<elapsed
        <<",\"playback_start_capture_sample\":"<<playback_start_sample
        <<",\"playback_format_capture_sample\":"<<playback_format_sample
        <<",\"playback_done_capture_sample\":"<<playback_done_sample
        <<",\"playback_start_seconds\":"<<playback_start_seconds
        <<",\"playback_done_seconds\":"<<playback_done_seconds
        <<",\"playback_marker_definition\":\"Capture count immediately before play invocation; format callback follows device open; neither is a calibrated analog latency\""
        <<",\"capture_error\":"<<quote(capture_error)<<",\"playback_error\":"<<quote(playback_error)
        <<",\"timed_out\":"<<(timed_out?"true":"false")<<",\"sample_bound_reached\":"<<(sample_bound_reached?"true":"false")
        <<",\"capture_format\":";format(capture_format);
    std::cout<<",\"playback_format\":";format(playback_format);
    std::cout<<",\"input_level\":";input_level.json();std::cout<<",\"capture_level\":";capture_level.json();
    std::cout<<"}\n";
    return success?0:2;
}
}
int main(int argc,char** argv) {try {
    Options o;
    for(int i=1;i<argc;++i) {
        const std::string option=argv[i];
        if(option=="--help") {
            std::cout<<"cable_audio_capture --input mono-f32le --output new-mono-f32le [--rate 48000] [--device default] [--stereo|--mono]\n"
                <<"Actual production audio API, including its S16 conversion; both outputs by default. Maximum input 120 seconds. Capture starts at least 0.75 s before play invocation and continues 1 s after playback drains. Existing output files are never overwritten.\n";
            return 0;
        }
        if(option=="--stereo"){o.stereo=true;continue;}
        if(option=="--mono"){o.stereo=false;continue;}
        if(i+1==argc)throw std::runtime_error("Missing option value: "+option);
        const std::string value=argv[++i];
        if(option=="--input")o.input=value;
        else if(option=="--output")o.output=value;
        else if(option=="--device")o.device=value;
        else if(option=="--rate") {
            std::size_t end=0;const auto rate=std::stoul(value,&end);
            if(end!=value.size()||rate<44100||rate>192000)throw std::runtime_error("Rate must be 44100..192000 Hz");
            o.rate=static_cast<std::uint32_t>(rate);
        } else throw std::runtime_error("Unknown option: "+option);
    }
    if(o.input.empty()||o.output.empty())throw std::runtime_error("Both --input and --output are required");
    return run(o);
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}

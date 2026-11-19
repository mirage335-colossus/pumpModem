#pragma once
// Lab-only dated audio messages carried in DPW1 frames. The standard worker
// never sees these types. See _zLab/README.md before changing their meaning.
#include "datapump/audio.hpp"
#include "datapump/host/protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace datapump::lab::audio_wire {
namespace wire = datapump::host::protocol;
inline constexpr std::uint32_t max_packet_frames = 4096;
inline constexpr std::size_t max_reason = 512;
enum class Kind : std::uint32_t {
    capture_open=200, capture_stop=201, playback_open=202, playback_schedule=203,
    playback_pcm=204, playback_end=205, playback_cancel=206, capture_credit=207, capture_cutoff=208,
    capture_format=220, capture_pcm=221, capture_end=222, playback_format=223,
    playback_scheduled=224, playback_credit=225, playback_drain=226,
    playback_flushed=227, error=228, capture_cutoff_ack=229
};
struct Message {
    Kind kind{};
    std::uint64_t stream=0, first_frame=0, utc_seconds=0, workspace=0;
    std::uint32_t logical_rate=0, hardware_rate=0, channel=0, exclusive=0;
    std::uint32_t quality=0, declared_clock=0, count=0, status=0, window_frames=0, prebuffer_frames=0;
    double passband=0, uncertainty=0, seconds_per_frame=0, rate_uncertainty=0;
    double clock_extrapolation=0, clock_phase=0, provider_floor=0, utc_fraction=0;
    double clock_halfwidth=0, clock_slew=0, gain=0, max_utc_error=0, lag=0, epoch=0;
    std::vector<float> pcm;
    std::string reason;
};
inline bool laboratory_type(std::uint32_t kind) noexcept {
    return (kind>=200 && kind<=208) || (kind>=220 && kind<=229);
}
inline void check(bool ok,const char* text) {if(!ok)throw datapump::Error(text);}
inline void finite(double x) {check(std::isfinite(x),"nonfinite lab audio field");}
inline void check_count(std::uint32_t count) {check(count>0 && count<=max_packet_frames,"invalid lab audio packet count");}
inline void check_quality(std::uint32_t q) {check(q<=2,"invalid lab audio timing quality");}
inline void check_bool(std::uint32_t v) {check(v<=1,"invalid lab audio boolean");}
inline void put_pcm(wire::Writer& w,const Message& m) {
    check(m.pcm.size()==m.count,"lab audio PCM size mismatch");check_count(m.count);w.u32(m.count);
    for(float v:m.pcm){check(std::isfinite(v) && std::abs(v)<=8,"invalid lab audio PCM");w.f32(v);}
}
inline void get_pcm(wire::Reader& r,Message& m) {
    m.count=r.u32();check_count(m.count);m.pcm.resize(m.count);
    for(auto& v:m.pcm){v=r.f32();check(std::isfinite(v) && std::abs(v)<=8,"invalid lab audio PCM");}
}
inline void write_reason(wire::Writer& w,const std::string& reason) {
    check(reason.size()<=max_reason,"lab audio reason too long");w.text(reason);
}
inline void check_clock(const Message& m) {
    finite(m.clock_halfwidth);finite(m.clock_slew);finite(m.clock_phase);
    check(m.clock_halfwidth>=0 && m.clock_halfwidth<1 && m.clock_slew>=0 &&
          m.clock_slew<=.001 && m.clock_phase>=0,"invalid lab audio clock policy");
}
inline void check_rate(std::uint32_t r) {check(r>=64 && r<=120000000,"invalid lab audio sample rate");}
inline void check_format(const Message& m) {
    check_rate(m.logical_rate);check_rate(m.hardware_rate);finite(m.passband);finite(m.uncertainty);
    check_quality(m.quality);check(m.passband>=0 && m.uncertainty>=0,"invalid lab audio format");
}
inline void check_stamp(const Message& m) {
    finite(m.utc_fraction);finite(m.uncertainty);finite(m.seconds_per_frame);finite(m.rate_uncertainty);
    finite(m.clock_extrapolation);finite(m.clock_phase);finite(m.provider_floor);check_quality(m.quality);check_bool(m.declared_clock);
    check(m.utc_seconds<4294967296ULL && m.utc_fraction>=0 && m.utc_fraction<1 &&
          m.uncertainty>=0 && m.seconds_per_frame>0 && m.rate_uncertainty>=0 &&
          m.clock_extrapolation>=0 && m.clock_phase>=0 && m.provider_floor>=0,
          "invalid lab audio capture date");
}
inline wire::Frame encode(const Message& m) {
    check(m.stream!=0,"invalid lab audio stream");wire::Writer w;w.u64(m.stream);
    switch(m.kind) {
    case Kind::capture_open:
        check_rate(m.logical_rate);check_bool(m.exclusive);check_clock(m);
        w.u32(m.logical_rate);w.u32(m.exclusive);w.f64(m.clock_halfwidth);w.f64(m.clock_slew);w.f64(m.clock_phase);break;
    case Kind::playback_open:
        check_rate(m.logical_rate);check_bool(m.exclusive);check(m.channel<=2,"invalid lab output channel");check_clock(m);
        finite(m.gain);finite(m.max_utc_error);finite(m.lag);
        check(m.gain>=0 && m.max_utc_error>=0 && m.lag>=0,"invalid lab output settings");
        w.u32(m.logical_rate);w.u32(m.channel);w.u32(m.exclusive);w.f64(m.gain);w.f64(m.max_utc_error);w.f64(m.lag);
        w.f64(m.clock_halfwidth);w.f64(m.clock_slew);w.f64(m.clock_phase);break;
    case Kind::playback_schedule: finite(m.epoch);check(m.epoch>=0,"invalid lab output epoch");w.f64(m.epoch);break;
    case Kind::capture_cutoff: w.u64(m.first_frame);break;
    case Kind::playback_pcm: w.u64(m.first_frame);put_pcm(w,m);break;
    case Kind::capture_pcm:
        check_stamp(m);w.u64(m.first_frame);w.u64(m.utc_seconds);w.f64(m.utc_fraction);w.f64(m.uncertainty);
        w.f64(m.seconds_per_frame);w.u32(m.quality);w.f64(m.rate_uncertainty);w.f64(m.clock_extrapolation);
        w.u32(m.declared_clock);w.f64(m.clock_phase);w.f64(m.provider_floor);put_pcm(w,m);break;
    case Kind::capture_format: case Kind::playback_format:
        check_format(m);w.u32(m.logical_rate);w.u32(m.hardware_rate);w.f64(m.passband);w.u64(m.workspace);
        w.u32(m.quality);w.f64(m.uncertainty);
        if(m.kind==Kind::playback_format){
            check(m.window_frames>0 && m.window_frames<=131072 && m.prebuffer_frames>0 && m.prebuffer_frames<=m.window_frames,
                  "invalid lab playback buffer");
            w.u32(m.window_frames);w.u32(m.prebuffer_frames);
        }break;
    case Kind::capture_credit: case Kind::playback_credit:
        check_count(m.count);w.u32(m.count);break;
    case Kind::capture_end: case Kind::playback_scheduled: case Kind::capture_cutoff_ack:
        check_bool(m.status);w.u32(m.status);write_reason(w,m.reason);break;
    case Kind::error: write_reason(w,m.reason);break;
    case Kind::playback_end: case Kind::playback_drain: w.u64(m.first_frame);break;
    case Kind::capture_stop: case Kind::playback_cancel: case Kind::playback_flushed: break;
    default:throw datapump::Error("unknown lab audio frame");
    }
    return {static_cast<wire::Type>(m.kind),std::move(w.bytes)};
}
inline Message decode(const wire::Frame& frame) {
    const auto kind=static_cast<std::uint32_t>(frame.type);check(laboratory_type(kind),"not a lab audio frame");
    wire::Reader r(frame.payload);Message m;m.kind=static_cast<Kind>(kind);m.stream=r.u64();check(m.stream!=0,"invalid lab audio stream");
    switch(m.kind) {
    case Kind::capture_open:
        m.logical_rate=r.u32();m.exclusive=r.u32();m.clock_halfwidth=r.f64();m.clock_slew=r.f64();m.clock_phase=r.f64();
        check_rate(m.logical_rate);check_bool(m.exclusive);check_clock(m);break;
    case Kind::playback_open:
        m.logical_rate=r.u32();m.channel=r.u32();m.exclusive=r.u32();m.gain=r.f64();m.max_utc_error=r.f64();m.lag=r.f64();
        m.clock_halfwidth=r.f64();m.clock_slew=r.f64();m.clock_phase=r.f64();
        check_rate(m.logical_rate);check_bool(m.exclusive);check(m.channel<=2,"invalid lab output channel");check_clock(m);
        finite(m.gain);finite(m.max_utc_error);finite(m.lag);check(m.gain>=0 && m.max_utc_error>=0 && m.lag>=0,"invalid lab output settings");break;
    case Kind::playback_schedule:m.epoch=r.f64();finite(m.epoch);check(m.epoch>=0,"invalid lab output epoch");break;
    case Kind::capture_cutoff:m.first_frame=r.u64();break;
    case Kind::playback_pcm:m.first_frame=r.u64();get_pcm(r,m);break;
    case Kind::capture_pcm:
        m.first_frame=r.u64();m.utc_seconds=r.u64();m.utc_fraction=r.f64();m.uncertainty=r.f64();m.seconds_per_frame=r.f64();
        m.quality=r.u32();m.rate_uncertainty=r.f64();m.clock_extrapolation=r.f64();m.declared_clock=r.u32();
        m.clock_phase=r.f64();m.provider_floor=r.f64();check_stamp(m);get_pcm(r,m);break;
    case Kind::capture_format:case Kind::playback_format:
        m.logical_rate=r.u32();m.hardware_rate=r.u32();m.passband=r.f64();m.workspace=r.u64();m.quality=r.u32();m.uncertainty=r.f64();check_format(m);
        if(m.kind==Kind::playback_format){m.window_frames=r.u32();m.prebuffer_frames=r.u32();
            check(m.window_frames>0 && m.window_frames<=131072 && m.prebuffer_frames>0 && m.prebuffer_frames<=m.window_frames,
                  "invalid lab playback buffer");}break;
    case Kind::capture_credit:case Kind::playback_credit:m.count=r.u32();check_count(m.count);break;
    case Kind::capture_end:case Kind::playback_scheduled:case Kind::capture_cutoff_ack:m.status=r.u32();check_bool(m.status);m.reason=r.text(max_reason);break;
    case Kind::error:m.reason=r.text(max_reason);break;
    case Kind::playback_end:case Kind::playback_drain:m.first_frame=r.u64();break;
    case Kind::capture_stop:case Kind::playback_cancel:case Kind::playback_flushed:break;
    default:throw datapump::Error("unknown lab audio frame");
    }
    r.end();return m;
}
inline void set_timestamp(Message& m,const datapump::audio::TimePrediction& stamp) {
    check(std::isfinite(stamp.utc_seconds) && stamp.utc_seconds>=0 && stamp.utc_seconds<4294967296.L,"lab audio date outside calendar");
    const auto whole=std::floor(stamp.utc_seconds);m.utc_seconds=static_cast<std::uint64_t>(whole);
    m.utc_fraction=static_cast<double>(stamp.utc_seconds-whole);
    if(m.utc_fraction>=1){++m.utc_seconds;m.utc_fraction=0;}
    const auto recovered=static_cast<long double>(m.utc_seconds)+static_cast<long double>(m.utc_fraction);
    const auto error=std::abs(recovered-stamp.utc_seconds);
    m.uncertainty=std::nextafter(stamp.uncertainty_seconds+static_cast<double>(error),std::numeric_limits<double>::infinity());
    m.seconds_per_frame=stamp.seconds_per_frame;m.quality=static_cast<std::uint32_t>(stamp.quality);
    m.rate_uncertainty=stamp.rate_uncertainty_fraction;m.clock_extrapolation=stamp.clock_extrapolation_seconds;
    m.declared_clock=stamp.declared_clock_policy;m.clock_phase=stamp.system_clock_phase_uncertainty_seconds;
    m.provider_floor=stamp.provider_uncertainty_floor_seconds;check_stamp(m);
}
inline datapump::audio::TimePrediction timestamp(const Message& m) {
    check_stamp(m);datapump::audio::TimePrediction out;
    out.utc_seconds=static_cast<long double>(m.utc_seconds)+static_cast<long double>(m.utc_fraction);
    out.uncertainty_seconds=m.uncertainty;out.seconds_per_frame=m.seconds_per_frame;
    out.quality=static_cast<datapump::audio::TimingQuality>(m.quality);out.rate_uncertainty_fraction=m.rate_uncertainty;
    out.clock_extrapolation_seconds=m.clock_extrapolation;out.declared_clock_policy=m.declared_clock!=0;
    out.system_clock_phase_uncertainty_seconds=m.clock_phase;out.provider_uncertainty_floor_seconds=m.provider_floor;return out;
}
}

#include "datapump/fast/telemetry.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::fast {
namespace {
std::atomic<std::uint64_t> next_stream{1};
constexpr double pi=std::numbers::pi;
std::uint64_t sample_hash(std::uint64_t value) noexcept {
    value+=0x9e3779b97f4a7c15ULL;
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;
    return value^(value>>31);
}
void sample_batch(std::array<std::complex<float>,512>& points,std::size_t& count,
                  std::uint64_t& seen,std::complex<float> value,std::uint64_t salt) noexcept {
    // OFDM emits ascending-frequency bins in a burst. Reservoir sampling keeps
    // observations across that burst instead of only its highest frequencies.
    // Fixed pseudorandom choices make display sampling reproducible and O(1).
    if(!seen)count=0;
    if(seen==std::numeric_limits<std::uint64_t>::max())return;
    ++seen;
    if(count<points.size())points[count++]=value;
    else {
        const auto slot=sample_hash(seen^salt)%seen;
        if(slot<points.size())points[static_cast<std::size_t>(slot)]=value;
    }
}
void spectrum(Diagnostics& frame) noexcept {
    if(frame.waveform_count<512)return;
    std::array<std::complex<double>,512> transformed{};
    double window_sum=0;
    const auto start=frame.waveform_count-transformed.size();
    for(std::size_t i=0;i<transformed.size();++i) {
        const auto window=.5-.5*std::cos(2*pi*static_cast<double>(i)/static_cast<double>(transformed.size()-1));
        transformed[i]=static_cast<double>(frame.waveform[start+i])*window;window_sum+=window;
    }
    for(std::size_t i=1,j=0;i<transformed.size();++i) {
        auto bit=transformed.size()/2;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;
        if(i<j)std::swap(transformed[i],transformed[j]);
    }
    for(std::size_t length=2;length<=transformed.size();length*=2) {
        const auto step=std::polar(1.,-2*pi/static_cast<double>(length));
        for(std::size_t start_at=0;start_at<transformed.size();start_at+=length) {
            std::complex<double> phase=1;
            for(std::size_t i=0;i<length/2;++i) {
                const auto even=transformed[start_at+i],odd=transformed[start_at+i+length/2]*phase;
                transformed[start_at+i]=even+odd;transformed[start_at+i+length/2]=even-odd;phase*=step;
            }
        }
    }
    for(std::size_t bin=0;bin<frame.spectrum_db.size();++bin) {
        const auto amplitude=(bin?2.:1.)*std::abs(transformed[bin])/window_sum;
        frame.spectrum_db[bin]=static_cast<float>(20*std::log10(std::max(1e-6,amplitude)));
    }
    frame.spectrum_valid=true;
}
}
std::uint64_t next_diagnostics_stream_id() noexcept {
    auto id=next_stream.fetch_add(1,std::memory_order_relaxed);
    if(!id)id=next_stream.fetch_add(1,std::memory_order_relaxed);
    return id;
}
std::shared_ptr<const Diagnostics> initial_diagnostics(const Profile& p,bool tx,std::uint64_t id) noexcept {
    try {
        auto frame=std::make_shared<Diagnostics>();
        frame->stream_id=id;frame->sample_rate=p.sample_rate;frame->constellation=p.constellation;frame->transmitting=tx;
        frame->acoustic_ofdm=p.acoustic_ofdm;
        frame->spectrum_db.fill(-120);
        return frame;
    }catch(...) {return {};}
}
Telemetry::Telemetry(const Profile& p,bool tx,std::uint64_t id) noexcept:
    stream_id_(id),sample_rate_(p.sample_rate),constellation_(p.constellation),transmitting_(tx),acoustic_ofdm_(p.acoustic_ofdm){}
void Telemetry::record_samples(std::span<const float> values) noexcept {
    for(const auto value:values) {
        waveform_[waveform_position_]=std::isfinite(value)?value:0;
        waveform_position_=(waveform_position_+1)%waveform_.size();
        waveform_count_=std::min(waveform_count_+1,waveform_.size());++samples_;
    }
}
void Telemetry::record_symbol(std::complex<float> value) noexcept {
    if(!std::isfinite(value.real())||!std::isfinite(value.imag()))return;
    point_sample_=samples_;
    if(acoustic_ofdm_)sample_batch(points_,point_count_,point_batch_seen_,value,0x73796d626f6cULL);
    else {
        points_[point_position_]=value;point_position_=(point_position_+1)%points_.size();
        point_count_=std::min(point_count_+1,points_.size());
    }
}
void Telemetry::record_input(std::complex<float> value) noexcept {
    if(!std::isfinite(value.real())||!std::isfinite(value.imag()))return;
    input_sample_=samples_;
    if(acoustic_ofdm_)sample_batch(input_,input_count_,input_batch_seen_,value,0x696e707574ULL);
    else {
        input_[input_position_]=value;input_position_=(input_position_+1)%input_.size();
        input_count_=std::min(input_count_+1,input_.size());
    }
}
std::shared_ptr<const Diagnostics> Telemetry::publish(bool acquired,Clock::time_point now) noexcept {
    if(published_ && now-last_publication_<std::chrono::milliseconds(100))return {};
    try {
        auto frame=std::make_shared<Diagnostics>();
        frame->stream_id=stream_id_;frame->revision=revision_+1;frame->samples=samples_;
        frame->sample_rate=sample_rate_;frame->constellation=constellation_;frame->transmitting=transmitting_;frame->acquired=acquired;
        frame->acoustic_ofdm=acoustic_ofdm_;frame->constellation_sample=point_sample_;frame->input_sample=input_sample_;
        frame->waveform_count=waveform_count_;frame->constellation_count=point_count_;frame->input_count=input_count_;
        frame->spectrum_db.fill(-120);
        const auto first_sample=(waveform_position_+waveform_.size()-waveform_count_)%waveform_.size();
        double power=0;
        for(std::size_t i=0;i<waveform_count_;++i) {
            const auto value=waveform_[(first_sample+i)%waveform_.size()];frame->waveform[i]=value;
            power+=static_cast<double>(value)*value;frame->waveform_peak=std::max(frame->waveform_peak,std::abs(value));
        }
        if(waveform_count_)frame->waveform_rms=static_cast<float>(std::sqrt(power/static_cast<double>(waveform_count_)));
        const auto first_point=acoustic_ofdm_?0:(point_position_+points_.size()-point_count_)%points_.size();
        for(std::size_t i=0;i<point_count_;++i)frame->constellation_points[i]=points_[(first_point+i)%points_.size()];
        const auto first_input=acoustic_ofdm_?0:(input_position_+input_.size()-input_count_)%input_.size();
        for(std::size_t i=0;i<input_count_;++i)frame->input_points[i]=input_[(first_input+i)%input_.size()];
        spectrum(*frame);
        // Empty later publications retain these points. The next observation
        // starts a new batch, without mixing in the preceding frequency sweep.
        point_batch_seen_=0;input_batch_seen_=0;
        ++revision_;published_=true;last_publication_=now;
        return frame;
    }catch(...) {return {};}
}
std::size_t Telemetry::workspace_bytes() const noexcept {return sizeof(*this);}
} // namespace datapump::fast

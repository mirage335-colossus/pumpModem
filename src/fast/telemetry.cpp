#include "telemetry.hpp"
#include "datapump/fast/preset.hpp"
#include "search_fft.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <numbers>
#include <numeric>
#include <vector>

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
void measure_waveform(Diagnostics& frame) noexcept {
    double power=0;
    frame.waveform_peak=0;
    for(auto& value:std::span(frame.waveform).first(frame.waveform_count)) {
        if(!std::isfinite(value))value=0;
        power+=static_cast<double>(value)*value;
        frame.waveform_peak=std::max(frame.waveform_peak,std::abs(value));
    }
    if(frame.waveform_count)
        frame.waveform_rms=static_cast<float>(std::sqrt(power/static_cast<double>(frame.waveform_count)));
    frame.spectrum_db.fill(-120);
    spectrum(frame);
}
Diagnostics metadata(const Profile& profile,bool tx,std::uint64_t stream) {
    Diagnostics result;
    result.stream_id=stream;result.sample_rate=profile.sample_rate;result.constellation=profile.constellation;
    result.transmitting=tx;result.acoustic_ofdm=profile.acoustic_ofdm;
    return result;
}
}
std::uint64_t next_diagnostics_stream_id() noexcept {
    auto id=next_stream.fetch_add(1,std::memory_order_relaxed);
    if(!id)id=next_stream.fetch_add(1,std::memory_order_relaxed);
    return id;
}
std::shared_ptr<const Diagnostics> initial_diagnostics(const Profile& p,bool tx,std::uint64_t id) noexcept {
    try {
        auto frame=std::make_shared<Diagnostics>(metadata(p,tx,id));
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
        const auto first_sample=(waveform_position_+waveform_.size()-waveform_count_)%waveform_.size();
        for(std::size_t i=0;i<waveform_count_;++i)
            frame->waveform[i]=waveform_[(first_sample+i)%waveform_.size()];
        const auto first_point=acoustic_ofdm_?0:(point_position_+points_.size()-point_count_)%points_.size();
        for(std::size_t i=0;i<point_count_;++i)frame->constellation_points[i]=points_[(first_point+i)%points_.size()];
        const auto first_input=acoustic_ofdm_?0:(input_position_+input_.size()-input_count_)%input_.size();
        for(std::size_t i=0;i<input_count_;++i)frame->input_points[i]=input_[(first_input+i)%input_.size()];
        measure_waveform(*frame);
        // Empty later publications retain these points. The next observation
        // starts a new batch, without mixing in the preceding frequency sweep.
        point_batch_seen_=0;input_batch_seen_=0;
        ++revision_;published_=true;last_publication_=now;
        return frame;
    }catch(...) {return {};}
}
std::size_t Telemetry::workspace_bytes() const noexcept {return sizeof(*this);}

struct detail::CaptureTelemetry::NoiseMeasurement {
    static constexpr std::size_t length=2048;
    static constexpr std::size_t history_capacity=110;
    struct Power {
        std::uint64_t end=0;
        double occupied=0,reference=0;
    };
    // Filled by the capture worker before the watchdog can see a complete
    // baseline. That worker never modifies it after filling it.
    std::vector<float> preroll;
    bool preroll_complete=false;
    const double bin_hz,low,high;
    const SnrReferenceBand reference;
    modem::detail::search_fft::FloatPlan fft{length};
    std::array<float,length> hann{};
    double window_energy=0;
    bool calibrated=false;
    double noise_reference=0,noise_occupied=0,noise_deviation=0;
    std::array<Power,history_capacity> history{};
    std::size_t next=0;
    std::optional<LoopbackSnrEstimate> last;

    NoiseMeasurement(const Profile& p,std::size_t preroll_samples):
        preroll(preroll_samples),bin_hz(static_cast<double>(p.sample_rate)/length),
        low(occupied_lower_hz(p)),high(occupied_upper_hz(p)),reference(expected_snr_reference_band(p.channel)) {
        for(std::size_t i=0;i<length;++i) {
            hann[i]=static_cast<float>(.5-.5*std::cos(2*pi*static_cast<double>(i)/static_cast<double>(length-1)));
            window_energy+=static_cast<double>(hann[i])*hann[i];
        }
    }

    bool usable() const noexcept {
        // A band below the frequency resolution of the measurement, or one
        // outside the declared reference, cannot give a comparable estimate.
        return high-low>=4*bin_hz && low>=reference.lower_hz && high<=reference.upper_hz;
    }

    std::optional<Power> power(std::span<const float,length> input) const {
        // Remove local DC and linear drift before tapering. Microphone bias
        // can move rapidly just after the capture device opens.
        constexpr double middle=(length-1)*.5;
        constexpr double index_energy=length*(static_cast<double>(length)*length-1)/12;
        double sum=0,trend=0;
        for(std::size_t i=0;i<length;++i) {
            const double x=input[i];
            if(!std::isfinite(x)||std::abs(x)>=.999)return std::nullopt;
            sum+=x;
            trend+=x*(static_cast<double>(i)-middle);
        }
        std::array<std::complex<float>,length> bins;
        const auto mean=sum/length,slope=trend/index_energy;
        for(std::size_t i=0;i<length;++i)
            bins[i]={static_cast<float>((input[i]-mean-slope*(static_cast<double>(i)-middle))*hann[i]),0};
        modem::detail::search_fft::fft(bins,false,fft);
        Power result;
        const auto overlap=[&](double frequency,double lower,double upper) {
            return std::clamp((std::min(upper,frequency+bin_hz*.5)-std::max(lower,frequency-bin_hz*.5))/bin_hz,0.,1.);
        };
        for(std::size_t k=1;k<bins.size()/2;++k) {
            const auto hz=k*bin_hz;
            if(hz>reference.upper_hz+bin_hz*.5)break;
            const auto contribution=2.*std::norm(bins[k])/(length*window_energy);
            result.occupied+=contribution*overlap(hz,low,high);
            result.reference+=contribution*overlap(hz,reference.lower_hz,reference.upper_hz);
        }
        return result;
    }

    void calibrate() {
        calibrated=true;
        if(!usable())return;
        // Skip the first quarter of the preroll. Overlap by half a window
        // to average several tapered observations within the existing quiet
        // capture budget, without delaying playback.
        unsigned count=0;
        double minimum=std::numeric_limits<double>::infinity(),maximum=0,occupied_sq=0;
        for(auto start=preroll.size()/4;start+length<=preroll.size();start+=length/2) {
            const auto measured=power(std::span<const float>(preroll).subspan(start,length).first<length>());
            if(!measured) {
                noise_reference=0;
                return;
            }
            noise_reference+=measured->reference;
            noise_occupied+=measured->occupied;
            occupied_sq+=measured->occupied*measured->occupied;
            minimum=std::min(minimum,measured->reference);
            maximum=std::max(maximum,measured->reference);
            ++count;
        }
        // Discard digital silence and abrupt changes in the reference band.
        // The floor is a local estimate of the room before this test.
        if(count<3 || minimum<=1e-12 || maximum>4*minimum) {
            noise_reference=0;
            return;
        }
        noise_reference/=count;
        noise_occupied/=count;
        noise_deviation=std::sqrt(std::max(0.,occupied_sq/count-noise_occupied*noise_occupied));
    }

    void observe(const Window& window) {
        if(noise_reference<=0 || window.count<length || window.end<preroll.size()+length)return;
        if(const auto measured=power(window.samples)) {
            history[next]=*measured;
            history[next].end=window.end;
            next=(next+1)%history.size();
        }
    }

    void match(ReceivedSignalRange present,std::uint32_t rate) noexcept {
        const auto tick=present.past_last;
        if(noise_reference<=0 || (last && tick<=last->observation_sample))return;
        const auto aperture=rate/4;
        unsigned count=0;
        double occupied=0;
        for(const auto& window:history) {
            // Use complete power windows inside the actual observed payload.
            // The display worker may be seconds ahead of the receiver.
            if(window.end>=length && window.end-length>=present.first &&
               window.end<=tick && tick-window.end<=aperture) {
                occupied+=window.occupied;
                ++count;
            }
        }
        if(count<2)return;
        const auto signal=occupied/count-noise_occupied;
        // With short noisy apertures, tiny power excess has no useful ratio.
        if(signal<=std::max({1e-12,.2*noise_occupied,2*noise_deviation}))return;
        last=LoopbackSnrEstimate{10*std::log10(signal/noise_reference),10*std::log10(noise_reference),
            reference.bandwidth_hz(),tick};
    }
};

detail::CaptureTelemetry::CaptureTelemetry(const Profile& profile,std::uint64_t stream_id,std::size_t preroll_samples):
    metadata_(metadata(profile,false,stream_id)) {
    if(preroll_samples)try {
        noise_=std::make_unique<NoiseMeasurement>(profile,std::min<std::size_t>(preroll_samples,profile.sample_rate/4));
    } catch(...) {} // A diagnostic allocation cannot abort an audio session.
}
detail::CaptureTelemetry::~CaptureTelemetry()=default;
void detail::CaptureTelemetry::record_pcm(std::span<const float> pcm) noexcept {
    if(pcm.empty())return;
    const auto first=captured_;
    captured_+=pcm.size();
    if(noise_ && first<noise_->preroll.size()) {
        const auto prefix=pcm.first(std::min<std::size_t>(pcm.size(),noise_->preroll.size()-first));
        std::ranges::copy(prefix,noise_->preroll.begin()+static_cast<std::ptrdiff_t>(first));
    }
    const std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return;
    const auto tail=pcm.last(std::min(window_.samples.size(),pcm.size()));
    const auto retained=window_.end==first?std::min(window_.count,window_.samples.size()-tail.size()):0;
    if(retained && retained<window_.count)
        std::copy_n(window_.samples.begin()+static_cast<std::ptrdiff_t>(window_.count-retained),retained,
                    window_.samples.begin());
    std::ranges::copy(tail,window_.samples.begin()+static_cast<std::ptrdiff_t>(retained));
    window_.count=retained+tail.size();window_.end=captured_;
    window_.captured_at=Telemetry::Clock::now();
    if(noise_ && captured_>=noise_->preroll.size())noise_->preroll_complete=true;
}
void detail::CaptureTelemetry::record_receiver(Telemetry& telemetry,std::size_t consumed,bool acquired,
                                               std::optional<ReceivedSignalRange> present,
                                               Telemetry::Clock::time_point now) noexcept {
    telemetry.samples_+=consumed;
    if(auto frame=telemetry.publish(acquired,now)) {
        const std::lock_guard lock(mutex_);
        receiver_=std::move(frame);
        present_=acquired?present:std::nullopt;
    }
}
std::shared_ptr<const Diagnostics> detail::CaptureTelemetry::publish(Telemetry::Clock::time_point now) noexcept {
    if(revision_ && now-last_publication_<std::chrono::milliseconds(100))return {};
    Window window;
    std::shared_ptr<const Diagnostics> receiver;
    std::optional<ReceivedSignalRange> present;
    bool preroll_complete=false;
    {
        const std::lock_guard lock(mutex_);
        const auto receiver_revision=receiver_?receiver_->revision:0;
        if(!window_.count || (window_.end==published_end_ && receiver_revision==published_receiver_revision_))return {};
        window=window_;receiver=receiver_;
        present=present_;
        preroll_complete=noise_ && noise_->preroll_complete;
    }
    try {
        auto frame=std::make_shared<Diagnostics>(receiver?*receiver:metadata_);
        frame->revision=++revision_;frame->samples=window.end;
        frame->captured_at=window.captured_at;
        frame->waveform_count=std::min(frame->waveform.size(),window.count);
        std::copy_n(window.samples.begin()+static_cast<std::ptrdiff_t>(window.count-frame->waveform_count),
                    frame->waveform_count,frame->waveform.begin());
        measure_waveform(*frame);
        if(preroll_complete) {
            if(!noise_->calibrated)noise_->calibrate();
            if(window.end!=published_end_)noise_->observe(window);
            if(present)noise_->match(*present,frame->sample_rate);
            frame->last_loopback_snr=noise_->last;
        }
        last_publication_=now;published_end_=window.end;
        published_receiver_revision_=receiver?receiver->revision:0;
        return frame;
    } catch(...) {return {};}
}
} // namespace datapump::fast

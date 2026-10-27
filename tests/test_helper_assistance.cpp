#include "datapump/helper_assistance.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/helper_filter.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/transfer.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/fhss_schedule.hpp"
#include "datapump/tuning.hpp"
#include "../src/live_receptions.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>

using namespace datapump;
namespace {
constexpr double tau=2*std::numbers::pi;
void check(bool condition,const char* message){if(!condition)throw Error(message);}
modem::Config config() {
    modem::Config c;c.sample_rate=2000;c.carrier_hz=500;c.bandwidth_hz=200;
    c.spreading_factor=128;c.scramble=true;c.pulse_shaping=false;c.stream_epoch=1789312671;
    for(std::size_t i=0;i<c.spreading_seed.size();++i)c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7);
    return c;
}
modem::PatternSearch search() {
    modem::PatternSearch s;s.hypotheses={{0,0}};s.drift_tolerant=false;s.differential_window_seconds=0;
    s.worker_threads=1;s.initial_stream_symbols=1;s.start_offset_seconds=.2;s.start_uncertainty_seconds=.01;
    return s;
}
helper::Reference reference(const modem::Config& c,double offset) {
    helper::Reference r;r.sample_rate=c.sample_rate;r.sample=200;r.frequency_hz=1250+offset;
    r.frequency_uncertainty_hz=0;r.frequency_valid=true;r.phase_valid=true;
    r.phase_uncertainty_radians=.001;r.dwell_start_seconds=.2;r.dwell_uncertainty_seconds=.001;r.timing_valid=true;
    return r;
}
helper::AssistanceRequest request() {
    helper::AssistanceRequest r;r.expected_helper_hz=1250;r.current_sample=200;r.maximum_hypotheses=65;
    return r;
}
void planner() {
    const auto c=config();auto original=search();original.hypotheses={{1,-200},{2,0},{3,200}};
    const auto before=original.hypotheses;auto r=reference(c,2);auto q=request();
    const auto plan=helper::plan_assistance(c,original,r,q);
    check(plan.search.has_value(),"valid reference was not usable");
    check(original.hypotheses==before && original.false_alarm_probability==1e-10,"planner changed the original search");
    check(plan.search->hypotheses.size()==3,"planner dropped independent original clock lanes");
    check(std::abs(plan.search->hypotheses[0].frequency_offset_hz-2.15)<1e-12 &&
          std::abs(plan.search->hypotheses[2].frequency_offset_hz-1.85)<1e-12,
          "helper-to-data frequency transfer forgot the independent sample clock");
    check(plan.search->false_alarm_probability==original.false_alarm_probability*.001/2,
          "optional branch lost revision alpha spending");
    check(plan.timing_alternatives.empty(),"uncertified symbol-to-dwell alignment narrowed timing");
    q.data_start_after_dwell_seconds=0;
    const auto timed=helper::plan_assistance(c,original,r,q);
    check(timed.timing_alternatives.size()==1 && timed.search->start_uncertainty_seconds<.002,
          "unique certified punctuation timing was not refined");
    original.start_uncertainty_seconds=1;
    const auto ambiguous=helper::plan_assistance(c,original,r,q);
    check(ambiguous.timing_alternatives.size()==5 && ambiguous.search->start_uncertainty_seconds==1,
          "modulo-dwell timing manufactured an absolute clock");
    original.start_offset_seconds=2.2;original.start_uncertainty_seconds=.01;r.dwell_uncertainty_seconds=.000001;
    const auto clock_timed=helper::plan_assistance(c,original,r,q);
    check(clock_timed.timing_alternatives.size()==1 &&
          clock_timed.search->start_uncertainty_seconds>.00039,
          "punctuation timing discarded the original audio-clock interval");
    q.current_sample=3000;check(!helper::plan_assistance(c,original,r,q).search,"stale helper reference was admitted");
    q.current_sample=100;check(!helper::plan_assistance(c,original,r,q).search,"future helper reference was backdated");
    q=request();q.maximum_hypotheses=2;
    check(!helper::plan_assistance(c,original,r,q).search,"optional lane cap silently discarded original clock alternatives");
    q=request();r.sample_rate=48000;
    check(!helper::plan_assistance(c,original,r,q).search,"raw hardware reference bypassed logical clock mapping");
    r=reference(c,2);q=request();q.reference_sequence=0;
    check(!helper::plan_assistance(c,original,r,q).search,"zero lifetime assistance ordinal was admitted");
    long double spent=0;
    for(std::uint64_t sequence=1;sequence<=32;++sequence) {
        q.reference_sequence=sequence;const auto attempt=helper::plan_assistance(c,original,r,q);
        check(attempt.search.has_value(),"valid ordinal exhausted optional alpha unexpectedly");
        spent+=attempt.search->false_alarm_probability/(original.false_alarm_probability*.001L);
    }
    check(std::abs(spent-32.L/33)<1e-14L,"revision alpha spending did not telescope");
    r.timing_valid=false;
    check(!helper::plan_assistance(c,original,r,q).phase_reference_available,
          "unknown punctuation sign was advertised as a qualified phase reference");
    r=reference(c,2);r.sample=4000;q=request();q.current_sample=r.sample;
    r.frequency_sample=1000;
    check(!helper::plan_assistance(c,original,r,q).search,"new phase publication concealed an expired frequency measurement");
    r.frequency_sample=r.sample+1;
    check(!helper::plan_assistance(c,original,r,q).search,"future frequency measurement was fused into an older phase publication");
    r.frequency_sample=3000;
    const auto fused=helper::plan_assistance(c,original,r,q);
    check(fused.search && fused.phase_reference_available && r.sample==4000 && r.frequency_sample==3000,
          "fresh independently dated frequency/phase reference was rejected or redated");
    r.frequency_sample=0;
    check(helper::plan_assistance(c,original,r,q).search.has_value(),"legacy same-date frequency reference lost compatibility");
}
std::vector<float> waveform(const modem::Config& c,const Bytes& bits,double offset,double wander) {
    modem::PatternTransmitter tx(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(tx.total_samples()));tx.read_analytic(analytic);
    const auto padding=static_cast<std::ptrdiff_t>(modem::pattern_pulse_padding_samples(c));
    const auto delay=static_cast<std::size_t>(c.sample_rate/5);
    std::vector<float> samples(delay+analytic.size()-2*padding+8*c.sample_rate);
    std::mt19937_64 rng(5181);std::normal_distribution<double> noise(0,.015);
    for(auto& x:samples)x=static_cast<float>(noise(rng));
    for(std::size_t i=padding;i<analytic.size()-padding;++i) {
        const auto at=delay+i-padding;const auto t=static_cast<double>(at)/c.sample_rate;
        const auto phase=.73+tau*offset*t+wander*std::sin(tau*2*t);
        samples[at]+=static_cast<float>((analytic[i]*std::polar(1.,phase)).real());
    }
    return samples;
}
struct Reception {Bytes bits;std::size_t completed=0;modem::PatternFftWork work;};
Reception receive(const modem::Config& c,const modem::PatternSearch& s,const std::vector<float>& samples,
                  double wander=0,std::size_t chunk=137) {
    modem::PatternReceiver receiver(c,4*1024*1024,s);Reception result;
    std::vector<modem::PatternPhaseKnot> knots;
    if(wander) {
        check(receiver.phase_assistance_supported(),"phase fixture selected an unsupported engine");
        for(std::size_t n=0;n<=samples.size()+100;n+=20)
            knots.push_back({n,wander*std::sin(tau*2*n/c.sample_rate)});
    }
    const auto drain=[&] {
        for(const auto& b:receiver.take_bursts()) {
            result.bits.insert(result.bits.end(),b.bits.begin(),b.bits.end());result.completed+=b.complete;
        }
    };
    for(std::size_t at=0;at<samples.size();) {
        const auto count=std::min(chunk,samples.size()-at);const auto part=std::span(samples).subspan(at,count);
        if(wander)check(receiver.push_phase_assisted(part,knots,20),"valid immutable phase knots were rejected");
        else receiver.push(part);
        at+=count;drain();
    }
    receiver.finish();drain();result.work=receiver.fft_work();return result;
}
void adjacent_filter_coordinates_and_gain() {
    constexpr std::uint32_t rate=8000;
    constexpr double carrier=1500,helper_hz=2450;
    const auto apply=[&](const std::vector<float>& input,std::size_t chunk) {
        helper::PayloadFilter filter(rate,helper_hz-200,425);
        std::vector<float> result,output(chunk);
        for(std::size_t first=0;first<input.size();first+=chunk) {
            const auto n=std::min(chunk,input.size()-first);
            const auto count=filter.push(std::span(input).subspan(first,n),output);
            result.insert(result.end(),output.begin(),output.begin()+count);
        }
        check(result.size()==input.size()-2*filter.delay() && filter.output_samples()==result.size(),
              "helper filter inserted unobserved history or lost its sample-center map");
        return result;
    };
    helper::Options options;options.power_w=100;options.callsign="N0CALL";options.timecode=false;
    helper::Plan placement;placement.frequency_hz=helper_hz;placement.auxiliary_frequency_hz=helper_hz-400;
    placement.helper_gain=.7;placement.data_gain=0;placement.effective_power_w=options.power_w;
    helper::Generator generator(options,placement,1800000000,true);
    std::vector<float> input(3*rate);
    for(std::size_t i=0;i<input.size();++i)
        input[i]=static_cast<float>(.02*std::cos(tau*carrier*i/rate)+
            generator.mix(0,static_cast<long double>(i)/rate,rate));
    const auto one=apply(input,1),many=apply(input,137);
    check(one==many,"helper filtering depends on caller chunk boundaries");
    helper::PayloadFilter geometry(rate,helper_hz-200,425);
    const auto amplitude=[&](double frequency) {
        std::complex<double> sum{};
        for(std::size_t i=0;i<one.size();++i)
            sum+=static_cast<double>(one[i])*std::polar(1.,-tau*frequency*(i+geometry.delay())/rate);
        return 2*std::abs(sum)/one.size();
    };
    check(std::abs(amplitude(carrier)-.02)<.00002 && amplitude(helper_hz)<.00007&&
          amplitude(placement.auxiliary_frequency_hz)<.00007,
          "adjacent helper filter changed payload gain or failed to remove both neighboring tones");
    helper::PayloadFilter warmup(rate,helper_hz-200,425);std::vector<float> short_input(2*warmup.delay(),1),output(short_input.size());
    check(warmup.push(short_input,output)==0 && warmup.output_samples()==0,
          "helper filter published zero padding before a complete physical window");
}
void filtered_noise_and_beacon_do_not_carry_bits() {
    auto c=config();c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=1000;c.spreading_factor=64;
    helper::Options options;options.power_w=100;options.callsign="N0CALL";
    const auto placement=helper::plan(options,c.carrier_hz,helper::occupied_bandwidth(c),48000);
    for(unsigned seed=0;seed<8;++seed)for(const bool beacon:{false,true}) {
        helper::PayloadFilter filter(c.sample_rate,placement.frequency_hz-200,425);
        auto selected=search();selected.start_offset_seconds=0;selected.start_uncertainty_seconds=1;
        selected.input_sample_origin=filter.delay();
        modem::PatternReceiver receiver(c,4*1024*1024,selected);
        helper::Generator generator(options,placement,c.stream_epoch,true);
        std::mt19937_64 random(5181+seed);std::normal_distribution<double> noise(0,.03);
        std::array<float,137> input{},output{};
        for(std::size_t first=0;first<9*c.sample_rate;first+=input.size()) {
            for(std::size_t i=0;i<input.size();++i) {
                const auto tone=beacon?generator.mix(0,static_cast<long double>(first+i)/c.sample_rate,c.sample_rate):0.f;
                input[i]=static_cast<float>(tone+noise(random));
            }
            const auto count=filter.push(input,output);receiver.push(std::span(output).first(count));
            for(const auto& burst:receiver.take_bursts())
                check(burst.bits.empty(),"filtered noise/helper-only signal manufactured Robust message bits");
        }
        receiver.finish();
        for(const auto& burst:receiver.take_bursts())
            check(burst.bits.empty(),"filtered noise/helper-only EOF manufactured Robust message bits");
    }
}
void actual_timing_benefit() {
    auto c=config();c.stream_epoch=1800000000;auto original=search();original.start_uncertainty_seconds=1;
    auto r=reference(c,0);r.dwell_start_seconds=-.2;auto q=request();q.qualify_epoch_timing=true;
    const auto plan=helper::plan_assistance(c,original,r,q);
    check(plan.search && plan.search->qualified_start_window && plan.timing_alternatives.size()>1,
          "measured dwell aliases did not create a qualified optional epoch hypothesis");
    const auto samples=waveform(c,{0,0,1},0,0);
    const auto before=receive(c,original,samples),after=receive(c,*plan.search,samples);
    check(before.bits==Bytes({0,0,1}) && after.bits==before.bits && after.completed==1,
          "qualified helper timing changed actual payload bits or physical completion");
    check(after.work.restricted_timing_active && after.work.scored_start_trials<before.work.scored_start_trials,
          "helper timing prior did not actually reduce executed FFT timing cells");
    original.hypotheses={{0,-200},{0,200}};original.search_stream_phases=true;
    for(const auto epoch:{1800000000ULL,1800000001ULL})for(const auto origin:{0ULL,12345ULL}) {
        c.stream_epoch=epoch;auto dated=r;auto dated_request=q;
        dated.sample+=origin;dated_request.current_sample+=origin;
        dated.dwell_start_seconds+=static_cast<double>(origin)/c.sample_rate;
        dated_request.receiver_input_origin_samples=origin;
        const auto clocked=helper::plan_assistance(c,original,dated,dated_request);
        check(clocked.search && clocked.search->qualified_start_window,"dated epoch helper qualifier was unavailable");
        const auto& window=*clocked.search->qualified_start_window;
        const auto local_dwell=static_cast<long double>(dated.dwell_start_seconds)-static_cast<long double>(origin)/c.sample_rate;
        // Recover the selected nominal alias from its envelope center. This
        // tests both epoch parities without assuming which tied alias wins.
        const auto a=(window.epoch_origin_samples/c.sample_rate-local_dwell)/window.phase_scale;
        check(std::abs(std::remainder(a-.2L*(epoch%2),.4L))<1e-12L,"epoch parity or receiver-origin mapping shifted the helper dwell");
        for(const auto ppm:{-200.,200.})for(const auto phase:{std::uint64_t{0},modem::canonical_phase_extent(c)-1}) {
            const auto actual=(local_dwell+(a+static_cast<long double>(phase)/c.sample_rate)/(1+ppm*1e-6L))*c.sample_rate;
            check(std::abs(actual-window.epoch_origin_samples-window.phase_scale*phase)<=window.half_width_samples,
                  "helper epoch timing excluded an original private phase or audio clock");
        }
    }
}
void actual_frequency_benefit() {
    for(const bool gated:{false,true})for(const bool compact:{false,true}) {
    auto c=config();
    if(gated) {
        c.pulse_shaping=true;
        std::array<std::uint8_t,32> key{};c.data_key=Crypto(key);
        auto hopping=std::make_shared<modem::FhssPlan>();hopping->channels=2;
        hopping->shifts[1]=100;hopping->dwell_seconds=.4;
        // Resolve the production smooth envelope. A coefficient-omission
        // fixture's eight-chip pulse support plus the default lag/clock guard
        // exceeds this dwell, even though the normal smooth envelope fits.
        c.fake_hopping=modem::resolve_fhss_power(*hopping,c);
        check(c.fake_hopping->power_ramps && c.fake_hopping->smooth_ramps && !c.fake_hopping->defer_symbols,
              "helper frequency fixture did not resolve a valid nondeferred FHSS envelope");
    }
    // Smooth hop gating can leave enough spectral leakage to copy a very
    // strong three-bit control at 2.17 Hz. Use a longer independent pattern
    // and a larger out-of-bank offset to discriminate actual assistance.
    const Bytes bits=gated?Bytes{0,0,1,1,0,1,0,0,1,0,1,1,1,0,0,1}:Bytes{0,0,1};
    const auto offset=gated?15.37:2.17;
    const auto samples=waveform(c,bits,offset,0);
    auto original=search();original.compact_clock_search=compact;
    const auto assisted=helper::plan_assistance(c,original,reference(c,offset),request());
    check(assisted.search.has_value(),"frequency fixture planner failed");
    const auto before=receive(c,original,samples);const auto after=receive(c,*assisted.search,samples);
    check(before.bits!=bits,"frequency fixture did not discriminate helper-derived frequency assistance");
    check(after.bits==bits && after.completed==1,"helper-derived exact-frequency lane failed physical reception");
    }
}
void practical_long_symbol_search() {
    auto c=tuning::resolve(36,8,tuning::PatternMode::auto_keystream,true,1500,10,
        modem::OuterDsssVersion::interleaved_v2).config;
    c.stream_epoch=1800000000;
    std::array<std::uint8_t,32> key{};c.data_key=Crypto(key);
    check(c.sample_rate==6120 && c.spreading_factor==256 && modem::symbol_sample_count(c)==87040,
          "realistic helper fixture no longer represents the first36Hz/DSSS10 profile");
    auto original=search();original.hypotheses={{0,-.0003},{0,0},{0,.0003}};
    original.start_uncertainty_seconds=1;original.search_stream_phases=true;
    auto measured=reference(c,2.17);measured.frequency_hz=6370+2.17;
    measured.frequency_uncertainty_hz=.1;measured.phase_valid=measured.timing_valid=false;
    auto q=request();q.expected_helper_hz=6370;
    const auto assisted=helper::plan_assistance(c,original,measured,q);
    check(assisted.search && assisted.search->hypotheses.size()==39,
          "realistic0.1Hz uncertainty failed to retain all independent clock lanes");
    check(!helper::prefer_compact_assistance(c,*assisted.search),
          "helper automatically selected a costly compact private-phase search");
    for(const auto invalid:{std::numeric_limits<double>::quiet_NaN(),-1000001.}) {
        auto malformed=*assisted.search;malformed.compact_clock_search=true;
        malformed.hypotheses.front().clock_error_ppm=invalid;
        check(!helper::prefer_compact_assistance(c,malformed),"compact selector accepted an invalid clock rate");
    }
    auto malformed=*assisted.search;malformed.hypotheses.front().frequency_offset_hz=
        std::numeric_limits<double>::infinity();
    check(!helper::prefer_compact_assistance(c,malformed),"compact selector accepted an invalid carrier offset");
    modem::PatternReceiver native(c,24*1024*1024,*assisted.search);
    check(!native.clock_windowed() && native.initial_fft_input_samples()>32ULL*c.sample_rate,
          "long-symbol helper tail no longer covers the actual native FFT fill");
    auto compact=*assisted.search;compact.compact_clock_search=true;
    modem::PatternReceiver receiver(c,64*1024*1024,compact);
    check(receiver.clock_windowed() && !receiver.phase_assistance_supported() && !receiver.initial_fft_input_samples(),
          "realistic helper prior did not construct the existing compact coherent backend");
    auto deferred=std::make_shared<modem::FhssPlan>();deferred->channels=2;
    deferred->shifts[1]=100;deferred->dwell_seconds=.4;deferred->defer_symbols=true;
    c.fake_hopping=deferred;
    check(!helper::plan_assistance(c,original,measured,q).search,
          "unresolved deferred FHSS aperture was silently admitted by helper planning");
}
void actual_phase_benefit() {
    const auto c=config();const Bytes bits{0,0,1};const auto samples=waveform(c,bits,0,4);
    const auto before=receive(c,search(),samples);const auto after=receive(c,search(),samples,4);
    check(before.bits!=bits,"phase fixture did not discriminate nonlinear phase assistance");
    check(after.bits==bits && after.completed==1,"nonlinear phase correction failed actual received payload/absence");
    check(receive(c,search(),samples,4,1).bits==bits,"partial bins changed dated phase correction");
}
void unsupported_and_zero_identity() {
    auto c=config();c.spreading_factor=8;c.scramble=false;auto s=search();
    modem::PatternReceiver unsupported(c,4*1024*1024,s);
    check(!unsupported.phase_assistance_supported(),"sample-Gram path accepted approximate phase rotation");
    const std::array<modem::PatternPhaseKnot,2> knots{{{0,0},{100000,0}}};
    const std::array<float,3> raw{.1f,.2f,.3f};
    check(!unsupported.push_phase_assisted(raw,knots,100000),"unsupported path consumed assisted PCM");
    check(unsupported.candidates().empty(),"rejected phase model changed detector state");
    c=config();const auto samples=waveform(c,{0,0,1},0,0);
    modem::PatternReceiver a(c,4*1024*1024,s),b(c,4*1024*1024,s);
    check(a.phase_assistance_supported(),"zero model fixture unsupported");
    a.push(samples);check(b.push_phase_assisted(samples,knots,100000),"zero phase model rejected");
    a.finish();b.finish();const auto aa=a.take_bursts(),bb=b.take_bursts();
    check(aa.size()==bb.size(),"zero phase model changed burst inventory");
    for(std::size_t i=0;i<aa.size();++i)
        check(aa[i].bits==bb[i].bits && aa[i].score==bb[i].score && aa[i].first_sample==bb[i].first_sample &&
              aa[i].end_sample==bb[i].end_sample && aa[i].complete==bb[i].complete,
              "zero phase model changed exact admission/progress/completion");
    modem::PatternReceiver invalid(c,4*1024*1024,s);
    const std::array<modem::PatternPhaseKnot,2> short_knots{{{0,0},{10,0}}};
    check(!invalid.push_phase_assisted(samples,short_knots,10),"uncovered phase model was extrapolated");
    invalid.push(samples);invalid.finish();const auto fallback=invalid.take_bursts();
    check(fallback.size()==aa.size() && fallback.front().bits==aa.front().bits && fallback.front().score==aa.front().score,
          "rejected phase model altered original fallback arithmetic");
    const auto nan=std::numeric_limits<double>::quiet_NaN();
    const std::array malformed{
        std::array<modem::PatternPhaseKnot,2>{{{10,0},{0,0}}},
        std::array<modem::PatternPhaseKnot,2>{{{0,0},{0,0}}},
        std::array<modem::PatternPhaseKnot,2>{{{0,nan},{100000,0}}},
        std::array<modem::PatternPhaseKnot,2>{{{0,1e13},{100000,0}}}};
    for(const auto& model:malformed) {
        modem::PatternReceiver rejected(c,4*1024*1024,s);
        check(!rejected.push_phase_assisted(samples,model,100000),"malformed phase knots were accepted");
        rejected.push(samples);rejected.finish();const auto ordinary=rejected.take_bursts();
        check(ordinary.size()==aa.size() && ordinary.front().score==aa.front().score && ordinary.front().bits==aa.front().bits,
              "malformed phase rejection changed subsequent ordinary output");
    }
    modem::PatternReceiver gap(c,4*1024*1024,s);
    check(!gap.push_phase_assisted(samples,knots,99999),"oversized interpolation gap was accepted");
    auto compact=s;compact.compact_clock_search=true;
    modem::PatternReceiver correlator(c,4*1024*1024,compact);
    check(correlator.clock_windowed() && !correlator.phase_assistance_supported() &&
          !correlator.push_phase_assisted(raw,knots,100000),"correlator fallback accepted FFT phase rotation");
    correlator.push(raw);
    auto differential=config();differential.sample_rate=64;differential.carrier_hz=16;
    differential.bandwidth_hz=8;differential.spreading_factor=4096;
    auto diff_search=s;diff_search.drift_tolerant=true;diff_search.differential_window_seconds=.01;
    modem::PatternReceiver diff(differential,8*1024*1024,diff_search);
    check(!diff.phase_assistance_supported() && !diff.push_phase_assisted(raw,knots,100000),
          "differential Gram path accepted approximate FFT phase rotation");
    diff.push(raw);
}
void noise_gate() {
    const auto c=config();const auto s=search();
    std::mt19937_64 rng(18271);std::normal_distribution<double> noise(0,.2);
    for(unsigned trial=0;trial<8;++trial) {
        std::vector<float> samples(10*c.sample_rate);for(auto& x:samples)x=static_cast<float>(noise(rng));
        modem::PatternReceiver receiver(c,4*1024*1024,s);
        std::vector<modem::PatternPhaseKnot> knots;
        for(std::size_t n=0;n<=samples.size()+100;n+=100)
            knots.push_back({n,2*std::sin(.007*n+trial)});
        check(receiver.push_phase_assisted(samples,knots,100),"noise fixture phase path rejected");
        receiver.finish();
        for(const auto& burst:receiver.take_bursts())check(burst.bits.empty(),"phase rotation admitted Gaussian noise as payload");
    }
}
void covariance_bound() {
    for(unsigned n=2;n<64;++n)for(double angle:{.1,.7,1.9}) {
        double cc=0,ss=0,cs=0;
        for(unsigned i=0;i<n;++i){const auto c=std::cos(.73*i),s=std::sin(.73*i);cc+=c*c;ss+=s*s;cs+=c*s;}
        const auto c=std::cos(angle),s=std::sin(angle);
        const auto rc=c*c*cc+s*s*ss-2*c*s*cs,rs=s*s*cc+c*c*ss+2*c*s*cs;
        const auto cross=c*s*(cc-ss)+(c*c-s*s)*cs;
        check(std::abs(rc+rs-cc-ss)<1e-12*n && std::abs(rc*rs-cross*cross-(cc*ss-cs*cs))<1e-12*n*n,
              "unit bin rotation changed real-noise covariance eigenvalues");
    }
}
void ordinary_arbitration_priority() {
    using H=live::detail::ReceptionHistory;
    struct PreviousCandidateLayout {
        std::uint64_t id=0,family=0,first=0,end=0;
        double frequency=0,frequency_tolerance=0;
        std::uint64_t absence_samples=0;double score=0;bool complete=false;
        std::uint64_t signal_id=0,symbol_samples=0,timing_tolerance=0;
        double amplitude_low_hz=0,amplitude_high_hz=0;
        std::optional<modem::SymbolBurstCoordinate> burst_first,burst_next;
    };
    static_assert(sizeof(H::Candidate)==sizeof(PreviousCandidateLayout));
    static_assert(offsetof(H::Candidate,signal_id)==offsetof(PreviousCandidateLayout,signal_id));
    for(const bool helper_first:{true,false}) {
        H history;std::uint64_t next=0;
        H::Candidate helper;helper.id=1;helper.family=9;helper.first=100;helper.end=3100;
        helper.frequency=500;helper.frequency_tolerance=1;helper.absence_samples=6000;
        helper.score=3000;helper.symbol_samples=1000;helper.timing_tolerance=5;helper.helper_assisted=true;
        auto ordinary=helper;ordinary.id=2;ordinary.end=1100;ordinary.score=1000;ordinary.helper_assisted=false;
        const auto first=history.observe(helper_first?helper:ordinary,[&]{return ++next;});
        const auto second=history.observe(helper_first?ordinary:helper,[&]{return ++next;});
        check(first.selected && second.selected==!helper_first && second.signal_id==first.signal_id && next==1,
              "helper provenance overrode stronger Robust support or changed its pending row");
        check(helper_first?second.revision==first.revision:second.revision>first.revision,
              "support arbitration changed revision without changing its owner");
        const auto helper_revision=helper_first?first.revision:second.revision;
        auto weaker=ordinary;weaker.end=2100;weaker.score=2000;weaker.complete=true;
        const auto rejected=history.observe(weaker,[&]{return ++next;});
        check(!rejected.selected && rejected.signal_id==first.signal_id && rejected.revision==helper_revision,
              "weaker ordinary END retracted a stronger pending payload");
        helper.score=6000;helper.end=6100;
        const auto later=history.observe(helper,[&]{return ++next;});
        check(later.selected && later.signal_id==first.signal_id && later.revision==helper_revision,
              "stronger helper-owner progress was delayed or changed its revision");
        ordinary.end=7100;ordinary.score=7000;
        const auto replacement=history.observe(ordinary,[&]{return ++next;});
        check(replacement.selected && replacement.signal_id==first.signal_id && replacement.revision>helper_revision,
              "stronger ordinary support could not replace the helper owner under the same row");
        ordinary.end=8100;ordinary.score=8000;
        const auto progress=history.observe(ordinary,[&]{return ++next;});
        check(progress.selected && progress.signal_id==first.signal_id && progress.revision==replacement.revision,
              "ordinary-owner next-poll progress was delayed or changed its revision");
        weaker=helper;weaker.end=9100;weaker.score=1000;weaker.complete=true;
        const auto losing_helper=history.observe(weaker,[&]{return ++next;});
        check(!losing_helper.selected && losing_helper.signal_id==first.signal_id &&
              losing_helper.revision==progress.revision && next==1,
              "weaker helper END retracted ordinary progress or changed the pending identity");
    }
}
void full_assisted_payload_source() {
    transfer::Options options;options.modem=config();options.modem.scramble=false;options.content_limit=16384;
    options.timestamp=1800000000;options.recovery_options.workers=1;
    options.recovery_options.extra_errors=0;options.recovery_options.budget=std::chrono::seconds(5);
    options.compression=true;
    const auto burst=[](std::span<const std::uint8_t> source,std::size_t first,bool complete=false) {
        modem::PatternBurst b;b.bits.assign(source.begin(),source.end());b.complete=complete;b.stream_first_sample=100;
        b.first_stream_symbol=first;b.first_sample=100+128*first;b.end_sample=b.first_sample+128*source.size();return b;
    };
    auto quota=std::make_shared<transfer::ReceiveStorageQuota>(transfer::ReceiveStorageQuota{
        transfer::source_storage_limit(options.content_limit),0});
    for(const bool attachment:{false,true}) {
        Message sent;sent.data.resize(321);for(std::size_t i=0;i<sent.data.size();++i)sent.data[i]=static_cast<std::uint8_t>(i*37+11);
        if(attachment){sent.kind=MessageKind::file;sent.filename="assisted.bin";}
        const auto wire=transfer::message_wire_bits(sent,options);
        transfer::StreamReceiver assisted(options,options.timestamp,quota);
        for(std::size_t first=0;first<wire.size();) {
            const auto count=std::min<std::size_t>(37,wire.size()-first);
            const auto pending=assisted.push(burst(std::span(wire).subspan(first,count),first));first+=count;
            check(pending.observed_bits==first && !pending.stream_complete && !pending.content_validated &&
                  !pending.recovery && pending.content.message.data.empty(),
                  "assisted full source changed immediate progress or decoded before physical END");
        }
        const auto received=assisted.push(burst({},wire.size(),true));
        check(received.stream_complete && received.content_validated && received.content.message.data==sent.data &&
              received.content.message.kind==sent.kind && received.content.message.filename==sent.filename &&
              !quota->used && !quota->reserved,
              "assisted ordinary source path lost full framing/decompression/attachment semantics");
    }
    Message sent;sent.data=Bytes(17,'e');auto damaged=transfer::message_wire_bits(sent,options);
    for(std::size_t byte=0;byte<49;++byte)damaged[192+byte*8]=modem::missing_pattern_bit;
    transfer::StreamReceiver assisted(options,options.timestamp,quota);
    const auto pending=assisted.push(burst(damaged,0));
    check(!pending.stream_complete && !pending.recovery && pending.observed_bits==damaged.size(),
          "assisted recovery started before physical completion or hid accepted progress");
    auto complete=assisted.push(burst({},damaged.size(),true));
    check(complete.stream_complete && !complete.content_validated && complete.recovery &&
          complete.recovery_progress.state==transfer::RecoveryState::ready,
          "full assisted source options failed to retain ordinary post-END recovery");
    const auto recovered=transfer::recover_received(std::move(complete));
    check(recovered.stream_complete && recovered.content_validated && recovered.content.message.data==sent.data &&
          recovered.recovery_progress.state==transfer::RecoveryState::recovered,
          "full assisted source recovery did not restore exact decoded content");
}
void suspended_helper_keeps_physical_tail() {
    for(const bool compact:{false,true}) {
    auto c=config();c.scramble=false;c.receiver_strategy=modem::ReceiverStrategy::complex_only;
    auto selected=search();selected.compact_clock_search=compact;
    auto samples=waveform(c,{0,0,1},0,0);const auto later=waveform(c,{1,0},0,0);
    // Both independent streams use this same known symbol-clock lattice.
    // A displaced second lattice is outside the compact control's prior.
    const auto symbol=modem::symbol_sample_count(c);
    samples.resize(static_cast<std::size_t>((samples.size()+symbol-1)/symbol*symbol),0.f);
    samples.insert(samples.end(),later.begin(),later.end());
    const auto ordinary=receive(c,selected,samples);
    check(ordinary.bits==Bytes({0,0,1,1,0}) && ordinary.completed==2,
          "helper suspension control did not receive two independent physical streams");
    modem::PatternReceiver no_frontier(c,4*1024*1024,selected);
    check(no_frontier.clock_windowed()==compact,"helper tail fixture did not select the requested actual backend");
    check(!no_frontier.suspend_helper_acquisition(),"helper suspension claimed an unprocessed start frontier");
    Reception unchanged;
    const auto drain_unchanged=[&] {
        for(const auto& b:no_frontier.take_bursts()) {
            unchanged.bits.insert(unchanged.bits.end(),b.bits.begin(),b.bits.end());unchanged.completed+=b.complete;
        }
    };
    for(std::size_t at=0;at<samples.size();) {
        const auto n=std::min<std::size_t>(137,samples.size()-at);
        no_frontier.push(std::span(samples).subspan(at,n));at+=n;drain_unchanged();
    }
    no_frontier.finish();drain_unchanged();
    check(unchanged.bits==ordinary.bits && unchanged.completed==ordinary.completed,
          "rejected no-frontier suspension changed subsequent ordinary reception");
    modem::PatternReceiver receiver(c,4*1024*1024,selected);Reception held;bool suspended=false;
    for(std::size_t at=0;at<samples.size();) {
        const auto count=std::min<std::size_t>(137,samples.size()-at);
        receiver.push(std::span(samples).subspan(at,count));at+=count;
        for(const auto& b:receiver.take_bursts()) {
            held.bits.insert(held.bits.end(),b.bits.begin(),b.bits.end());held.completed+=b.complete;
        }
        if(!suspended && receiver.synchronized()) {
            check(receiver.suspend_helper_acquisition(),"admitted helper lane could not suspend new acquisition");
            suspended=true;
        }
    }
    receiver.finish();
    for(const auto& b:receiver.take_bursts()) {
        held.bits.insert(held.bits.end(),b.bits.begin(),b.bits.end());held.completed+=b.complete;
    }
    check(suspended && held.bits==Bytes({0,0,1}) && held.completed==1,
          "helper suspension lost admitted001/observedEND or admitted a later fresh stream");
    }
}
}
int main(){try{planner();adjacent_filter_coordinates_and_gain();filtered_noise_and_beacon_do_not_carry_bits();actual_frequency_benefit();practical_long_symbol_search();actual_timing_benefit();actual_phase_benefit();unsupported_and_zero_identity();covariance_bound();noise_gate();ordinary_arbitration_priority();full_assisted_payload_source();suspended_helper_keeps_physical_tail();
    std::cout<<"helper assistance passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

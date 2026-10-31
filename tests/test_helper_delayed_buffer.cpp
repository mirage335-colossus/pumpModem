#include "datapump/helper_delayed_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

using B=datapump::helper::HelperDelayedBuffer;
using S=std::complex<float>;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
B::CaptureBlock meta(std::uint64_t first,std::size_t n,long double period=1'000'000){
    B::CaptureBlock b;b.first_sample=first;b.sample_count=n;b.sample_period_nanoseconds=period;
    b.capture_monotonic_nanoseconds=static_cast<std::uint64_t>(std::llround(static_cast<long double>(first)*period));
    b.capture_system_nanoseconds=1'700'000'000'000'000'000LL+static_cast<std::int64_t>(b.capture_monotonic_nanoseconds);
    b.capture_uncertainty_nanoseconds=42;b.capture_clock_epoch=1;b.schedule_valid=true;b.schedule_epoch=17;b.schedule_slot=first/9600;
    return b;
}
void delay_case(std::uint64_t delay){
    constexpr std::size_t n=9600;
    B b({n*6,8,delay,1000});const auto period=1'000'000'000.L/24000;
    std::vector<S> x(n),out(n);for(std::size_t j=0;j<n;++j)x[j]={static_cast<float>(j),static_cast<float>(-static_cast<int>(j))};
    auto first=meta(0,n,period);first.transmitter_time={true,123,100.L,period*1e-9L,.00001L};
    require(b.push(first,x)==B::PushResult::accepted,"initial block rejected");
    B::ReleasedBlock released;const auto end=static_cast<std::uint64_t>(std::ceil((n-1)*period));
    require(b.pop(end+delay-1,end+delay-1,out,released)==B::PopResult::not_ready,"block released early");
    require(b.pop(end+delay,end+delay,out,released,true)==B::PopResult::released,"ready block not released");
    require(released.capture.first_sample==0&&released.capture.capture_system_nanoseconds==first.capture_system_nanoseconds&&
        released.capture.capture_monotonic_nanoseconds==0&&released.release_monotonic_nanoseconds==end+delay&&
        released.eligible_monotonic_nanoseconds==end+delay&&released.capture.schedule_slot==0&&
        released.capture.transmitter_time.valid&&released.capture.transmitter_time.reference_sample==123,
        "capture/release metadata changed");
    require(released.configured_helper_lookahead_available&&out==x,"ready sample bytes/lookahead changed");
    long double system=0,sigma=0;
    require(B::capture_system_time_at(released.capture,10.25L,system,sigma)&&
        std::abs(system-(static_cast<long double>(first.capture_system_nanoseconds)+10.25L*period))<.1L&&sigma>=42,
        "fractional capture time mapping incorrect");
    auto uncertain=first;uncertain.sample_period_uncertainty_nanoseconds=2;
    require(B::capture_system_time_at(uncertain,10.25L,system,sigma)&&sigma>=42+20.5L,
        "capture mapping omitted accumulated period uncertainty");
    require(!B::capture_system_time_at(first,static_cast<long double>(n),system,sigma),"capture mapping extrapolated beyond block");
    require(b.pop(end+delay-1,end+delay-1,out,released)==B::PopResult::nonmonotonic_release,"release time moved backwards");
    require(b.pop(end+delay,end+delay,out,released)==B::PopResult::empty,"queue not empty");
}
void streamed_startup_and_drain(){
    constexpr std::size_t n=9600,blocks=6;constexpr std::uint64_t delay=2'000'000'000ULL;
    const long double period=1'000'000'000.L/24000*(1+37e-6L);
    B b({n*blocks,blocks,delay,1000});std::vector<S> input(n),output(n);
    for(std::size_t k=0;k<blocks;++k){
        std::fill(input.begin(),input.end(),S(static_cast<float>(k),-static_cast<float>(k)));
        auto block=meta(static_cast<std::uint64_t>(k*n),n,period);
        block.schedule_epoch=3;block.schedule_slot=k;
        require(b.push(block,input)==B::PushResult::accepted,"streamed startup enqueue failed");
    }
    const auto first_end=static_cast<std::uint64_t>(std::ceil((n-1)*period));
    const auto last=meta(static_cast<std::uint64_t>((blocks-1)*n),n,period);
    const auto frontier=last.capture_monotonic_nanoseconds+first_end;
    B::ReleasedBlock r;require(frontier>=first_end+delay,"test frontier does not cover lookahead");
    require(b.pop(first_end+delay-1,first_end+delay-1,output,r,true)==B::PopResult::not_ready,
        "streamed startup released before two-second lookahead");
    require(b.pop(frontier,frontier,output,r,true)==B::PopResult::released&&r.capture.schedule_slot==0&&
        r.configured_helper_lookahead_available&&r.capture.first_sample==0&&output.front()==S(0,0),
        "streamed first mature block incorrect");
    // Draining as monotonic time advances cannot invent helper samples after
    // the last captured frontier. Preserve every subsequent sample and slot.
    for(std::size_t k=1;k<blocks;++k){
        const auto now=frontier+static_cast<std::uint64_t>(k)*500'000'000ULL;
        require(b.pop(now,frontier,output,r,true)==B::PopResult::released&&r.capture.schedule_slot==k&&
            r.capture.schedule_epoch==3&&r.capture.first_sample==k*n&&
            std::all_of(output.begin(),output.end(),[k](S value){return value==S(static_cast<float>(k),-static_cast<float>(k));})&&
            !r.configured_helper_lookahead_available,
            "streamed drain lost capture order, slot, bytes or tail validity");
    }
    require(b.queued_blocks()==0&&b.queued_samples()==0,"streamed drain retained data");
}
void gap_and_reference(){
    B b({64,8,100,1'000});std::vector<S> x(8,{1,2}),out(8);B::ReleasedBlock r;
    auto a=meta(0,8);a.transmitter_time={true,55,3.L,.001L,.01L};a.transmitter_time.reference_fractional_sample=.5L;
    require(b.push(a,x)==B::PushResult::accepted,"initial gap-control block failed");
    auto lost=meta(8,8);lost.samples_present=false;lost.transmitter_time={true,55,3.008L,.001L,.01L};lost.transmitter_time.reference_fractional_sample=.5L;
    require(b.push(lost,{})==B::PushResult::accepted,"explicit missing block failed");
    auto after=meta(18,8);after.capture_system_nanoseconds-=2'000'000'000LL;after.capture_clock_epoch=2;
    after.transmitter_time={true,55,3.018L,.001L,.01L};after.transmitter_time.reference_fractional_sample=.75L;
    require(b.push(after,x)==B::PushResult::reference_mismatch,"changed reference not reported");
    auto last=meta(26,8);last.capture_system_nanoseconds-=2'000'000'000LL;last.capture_clock_epoch=2;
    last.transmitter_time={true,55,3.026L,.001L,.01L};last.transmitter_time.reference_fractional_sample=.5L;
    require(b.push(last,x)==B::PushResult::accepted,"original reference failed after gap");
    constexpr std::uint64_t now=40'000'000;
    require(b.pop(now,now,out,r)==B::PopResult::released&&r.capture.first_sample==0&&!r.capture_time_discontinuity,"first block altered");
    require(b.pop(now,now,{},r)==B::PopResult::released&&!r.capture.samples_present&&r.capture.first_sample==8,"missing block silently lost");
    require(b.pop(now,now,out,r)==B::PopResult::released&&r.capture.first_sample==18&&r.missing_samples_before==2&&
        r.capture_time_discontinuity&&r.transmitter_reference_mismatch&&!r.capture.transmitter_time.valid&&
        r.capture.capture_clock_epoch==2&&r.capture.capture_system_nanoseconds==after.capture_system_nanoseconds,
        "gap/system-clock jump/reference mismatch hidden");
    require(b.pop(now,now,out,r)==B::PopResult::released&&r.capture.transmitter_time.valid&&
        r.capture.transmitter_time.reference_sample==55&&r.capture.transmitter_time.reference_fractional_sample==.5L&&
        !r.capture_time_discontinuity,"reference or epoch silently reset");
}
// Exercise a streaming queue with a smoothly drifting sample clock, not just
// a constant offset. Each producer block is one known schedule dwell. A
// separate missing block and skipped input indices model two ways data can be
// absent; neither produces fabricated sample bytes. Schedule slots here are
// opaque metadata, not an independent test of FHSS schedule generation.
void drifting_schedule_and_missing_capture(){
    constexpr std::size_t dwell=400, count=14;
    constexpr std::uint64_t delay=2'000'000'000ULL;
    constexpr long double period=1'000'000.L, curvature=.00002L;
    constexpr std::int64_t sys0=1'700'000'000'000'000'000LL;
    B b({dwell*6,7,delay,1000});
    std::vector<B::CaptureBlock> expected;
    std::vector<S> input(dwell),out(dwell);
    const S sentinel(-30000,30000);
    B::ReleasedBlock r;
    std::size_t next=0,empty_blocks=0,missing_indices=0,missing_helper_coverage=0;
    bool helper_gap_seen=false;
    std::uint64_t captured_end=0;
    auto mono=[](std::uint64_t n){return static_cast<std::uint64_t>(std::llround(
        period*static_cast<long double>(n)+curvature*static_cast<long double>(n)*static_cast<long double>(n)));};
    auto release=[&](std::uint64_t now,std::uint64_t frontier){
        for(;;){
            std::fill(out.begin(),out.end(),sentinel);
            // The frontier alone does not prove continuous Helper evidence.
            // Conservatively retain the gap indication for the rest of this
            // test; requalification is a receiver decision, not buffer policy.
            const auto status=b.pop(now,frontier,out,r,!helper_gap_seen);
            if(status==B::PopResult::empty||status==B::PopResult::not_ready)break;
            require(status==B::PopResult::released&&next<expected.size(),"drift stream failed to release in order");
            const auto& e=expected[next];
            require(r.capture.first_sample==e.first_sample&&r.capture.sample_count==e.sample_count&&
                r.capture.capture_system_nanoseconds==e.capture_system_nanoseconds&&
                r.capture.capture_monotonic_nanoseconds==e.capture_monotonic_nanoseconds&&
                r.capture.capture_clock_epoch==e.capture_clock_epoch&&r.capture.schedule_valid&&
                r.capture.schedule_epoch==e.schedule_epoch&&r.capture.schedule_slot==e.schedule_slot&&
                r.capture.sample_period_nanoseconds==e.sample_period_nanoseconds&&
                r.capture.samples_present==e.samples_present&&r.release_monotonic_nanoseconds==now,
                "drift stream altered capture, epoch, schedule or release metadata");
            require(r.missing_samples_before==(next==8?3U:0U),"skipped sample indices hidden or moved");
            missing_indices+=r.missing_samples_before;
            if(helper_gap_seen){
                ++missing_helper_coverage;
                require(!r.helper_coverage_contiguous&&!r.configured_helper_lookahead_available,
                    "missing ADC samples silently qualified Helper coverage");
            }
            require(r.capture_time_discontinuity==(next==7||next==11),"system clock step not correctly delimited");
            long double date=0,sigma=0;
            if(!e.samples_present){
                ++empty_blocks;
                require(std::all_of(out.begin(),out.end(),[&](S v){return v==sentinel;})&&
                    !B::capture_system_time_at(r.capture,static_cast<long double>(e.first_sample),date,sigma),
                    "missing dwell fabricated data or a valid sample time");
            }else{
                for(std::size_t j=0;j<dwell;++j){
                    const auto v=static_cast<float>(e.first_sample+j);
                    require(out[j]==S(v,-v),"captured bytes or sample order changed");
                }
                constexpr long double offset=127.25L;
                const long double sample=static_cast<long double>(e.first_sample)+offset;
                require(B::capture_system_time_at(r.capture,sample,date,sigma),"drift sample timestamp unavailable");
                const long double expected_time=static_cast<long double>(e.capture_system_nanoseconds)+
                    offset*(period+2*curvature*static_cast<long double>(e.first_sample))+curvature*offset*offset;
                require(std::abs(date-expected_time)<=sigma&&sigma>=e.capture_uncertainty_nanoseconds,
                    "clock drift fell outside reported timestamp uncertainty");
            }
            ++next;
        }
    };
    for(std::size_t k=0;k<count;++k){
        const auto first=static_cast<std::uint64_t>(k*dwell+(k>=8?3:0));
        B::CaptureBlock m;m.first_sample=first;m.sample_count=dwell;m.samples_present=k!=4;
        m.sample_period_nanoseconds=period+2*curvature*static_cast<long double>(first);
        m.sample_period_uncertainty_nanoseconds=curvature*dwell;
        m.capture_monotonic_nanoseconds=mono(first);m.capture_uncertainty_nanoseconds=2;
        m.capture_system_nanoseconds=sys0+static_cast<std::int64_t>(m.capture_monotonic_nanoseconds)+
            (k>=7?-2'000'000'000LL:0)+(k>=11?3'000'000'000LL:0);
        m.capture_clock_epoch=k>=11?3:k>=7?2:1;
        m.schedule_valid=true;m.schedule_epoch=99;m.schedule_slot=k;
        captured_end=mono(first+dwell-1);
        // Release already-mature blocks first so the fixed sample ring wraps.
        release(captured_end,captured_end);
        for(std::size_t j=0;j<dwell;++j){const auto v=static_cast<float>(first+j);input[j]=S(v,-v);}
        require(b.push(m,m.samples_present?std::span<const S>(input):std::span<const S>())==B::PushResult::accepted,
            "drift stream enqueue failed");
        if(!m.samples_present||k==8)helper_gap_seen=true;
        expected.push_back(m);
        release(captured_end,captured_end);
    }
    require(next<count,"startup unexpectedly delivered all blocks before drain");
    // No future Helper observation is invented when the final blocks drain.
    release(captured_end+delay+1'000'000'000ULL,captured_end);
    require(next==count&&empty_blocks==1&&missing_indices==3&&missing_helper_coverage>0&&
        b.queued_blocks()==0&&b.queued_samples()==0,
        "drift stream did not preserve missing ranges and drain exactly once");
}
void guards_and_tail(){
    B b({8,2,100,10});auto m=meta(0,8);std::vector<S> data(8,{3,4}),small(7),out(8);B::ReleasedBlock r;
    require(b.push(m,data)==B::PushResult::accepted,"capacity initial failed");
    require(b.push(meta(8,8),data)==B::PushResult::full,"sample cap ignored");
    require(b.push(m,data)==B::PushResult::out_of_order,"overlap accepted");
    require(b.pop(7'000'101,7'000'000,small,r)==B::PopResult::output_too_small&&b.queued_samples()==8,"small output consumed data");
    require(b.pop(7'000'101,7'000'000,out,r)==B::PopResult::released&&!r.configured_helper_lookahead_available&&
        r.helper_frontier_advance_nanoseconds==0&&out==data,"drain incorrectly claimed future Helper evidence");
    auto future=meta(8,8);require(b.push(future,data)==B::PushResult::accepted,"ring reuse failed");
    require(b.pop(15'000'101,15'000'102,out,r)==B::PopResult::released&&r.invalid_helper_frontier&&
        !r.configured_helper_lookahead_available,"future helper frontier granted validity");
    auto uncertain_end=meta(16,8);uncertain_end.sample_period_uncertainty_nanoseconds=100;
    require(b.push(uncertain_end,data)==B::PushResult::accepted,"uncertain clock block failed");
    require(b.pop(23'000'100,23'000'100,out,r)==B::PopResult::not_ready,"period uncertainty released block early");
    require(b.pop(23'000'801,23'000'000,out,r)==B::PopResult::released,"period uncertainty deadline not released");
    auto monotonic_bound=meta(24,8);monotonic_bound.capture_end_monotonic_valid=true;
    monotonic_bound.capture_end_monotonic_nanoseconds=31'000'000;monotonic_bound.capture_monotonic_uncertainty_nanoseconds=9;
    require(b.push(monotonic_bound,data)==B::PushResult::accepted,"bounded monotonic stamp failed");
    require(b.pop(31'000'108,31'000'000,out,r)==B::PopResult::not_ready,"monotonic timestamp bound ignored");
    require(b.pop(31'000'109,31'000'000,out,r)==B::PopResult::released,"monotonic timestamp deadline failed");
    auto huge=meta(32,8);huge.sample_period_nanoseconds=std::numeric_limits<long double>::infinity();
    require(b.push(huge,data)==B::PushResult::invalid,"infinite sample period accepted");
    auto bad_reference=meta(40,8);bad_reference.transmitter_time={true,55,3.04L,.001L,.01L};
    for(const long double bad:{-0.5L,1.L,std::numeric_limits<long double>::quiet_NaN()}){
        bad_reference.transmitter_time.reference_fractional_sample=bad;
        require(b.push(bad_reference,data)==B::PushResult::invalid,"invalid fractional reference accepted");
    }
    B clock_jumps({16,2,0,1000});auto c0=meta(0,8);c0.capture_uncertainty_nanoseconds=1'000'000'000;
    auto c1=meta(8,8);c1.capture_uncertainty_nanoseconds=1'000'000'000;c1.capture_monotonic_nanoseconds+=2'000'000;
    require(clock_jumps.push(c0,data)==B::PushResult::accepted&&clock_jumps.push(c1,data)==B::PushResult::accepted,
        "clock-jump setup failed");
    require(clock_jumps.pop(20'000'000,20'000'000,out,r)==B::PopResult::released&&
            clock_jumps.pop(20'000'000,20'000'000,out,r)==B::PopResult::released&&r.capture_time_discontinuity,
        "system-time uncertainty concealed monotonic timestamp discontinuity");
    bool threw=false;try{B invalid({0,8,100,10});}catch(const std::invalid_argument&){threw=true;}
    require(threw,"zero memory bound accepted");
}
}
int main(){try{
    for(const std::uint64_t d:{400'000'000ULL,1'000'000'000ULL,2'000'000'000ULL,3'000'000'000ULL})delay_case(d);
    streamed_startup_and_drain();gap_and_reference();drifting_schedule_and_missing_capture();guards_and_tail();
    std::cout<<"helper_delayed_buffer checks=delays_capture_release_fractional_gap_epoch_reference_drain_capacity_drift_schedule_missing passed=1\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

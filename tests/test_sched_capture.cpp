#include "../src/sched_capture.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

using namespace datapump;
using namespace datapump::modem;
using namespace datapump::live::detail;

namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
Crypto key(unsigned salt=0) {
    Bytes data(32);for(unsigned i=0;i<data.size();++i)data[i]=static_cast<std::uint8_t>(i+salt);
    return Crypto(data);
}
void test_capture_fence() {
    check(checkedCaptureFence(10.98995L,10,.001L,10),"nominal callback unexpectedly crossed admission fence");
    check(!checkedCaptureFence(10.98995L,10,.00101L,10),"one-percent mapped-rate crossing discarded PCM");
    check(checkedCaptureFence(10.75L,1,.125L,10),"interior mapped callback rejected");
    check(!checkedCaptureFence(10.75L,2,.125L,10),"exact UTC fence discarded PCM");
    check(!checkedCaptureFence(11,0,.125L,10),"next-second callback retained old admission proof");
    for(const auto rate:{0.L,-.001L,std::numeric_limits<long double>::quiet_NaN(),
            std::numeric_limits<long double>::infinity()})
        check(!checkedCaptureFence(10.5L,1,rate,10),"invalid mapped rate accepted");
    check(!checkedCaptureFence(std::numeric_limits<long double>::quiet_NaN(),1,.001L,10),
          "invalid callback UTC accepted");
    check(!checkedCaptureFence(10.5L,1,.001L,10.25L),"fractional admission fence accepted");
}
void test_callback_timestamps() {
    check(checkedCaptureTimestamp(500,1500,500,1000,100,101,.001L),
          "nominal affine callback rejected");
    // The start is inside the envelope, but a modestly faster/slower mapped
    // clock crosses it before this callback ends. Start-only checks miss both.
    check(checkedCaptureTimestamp(0,0,0,1000,100,100.9999L,.001002L) &&
          !checkedCaptureTimestamp(0,0,1000,1000,100,100.9999L,.001002L),
          "callback-end upper-cone crossing discarded capture");
    check(checkedCaptureTimestamp(0,0,0,1000,100,99.0001L,.000998L) &&
          !checkedCaptureTimestamp(0,0,1000,1000,100,99.0001L,.000998L),
          "callback-end lower-cone crossing discarded capture");
    // Converting two absolute large counters before subtraction can lose this
    // one-sample displacement on double-width long double implementations.
    constexpr auto large=std::uint64_t{1}<<60;
    check(checkedCaptureTimestamp(large,large+1,0,1,100,101.5L,1),
          "large sample origin lost its exact relative age");
    constexpr auto extreme_age=std::uint64_t{1}<<63;
    check(!checkedCaptureTimestamp(0,extreme_age,1,1,0,
              1.001L*static_cast<long double>(extreme_age),2),
          "precision-ambiguous relative timestamp age accepted");
    constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
    check(checkedCaptureTimestamp(maximum-4,maximum-2,2,1000,100,100.002L,.001L) &&
          !checkedCaptureTimestamp(maximum-4,maximum-2,3,1000,100,100.002L,.001L),
          "callback sample endpoint overflow was not rejected");
    for(const auto edge:{99.L,101.L})
        check(checkedCaptureTimestamp(0,0,0,1000,100,edge,.001L),
              "inclusive zero-duration timestamp boundary rejected");
    check(!checkedCaptureTimestamp(0,0,0,1000,100,std::nextafter(99.L,0.L),.001L) &&
          !checkedCaptureTimestamp(0,0,0,1000,100,std::nextafter(101.L,102.L),.001L),
          "timestamp boundary silently gained an epsilon allowance");
    // Adding this duration to the absolute epoch rounds it away on both
    // extended-precision and double-width long double implementations.
    const auto epoch=std::ldexp(1.L,std::numeric_limits<long double>::digits-2);
    check(checkedCaptureTimestamp(0,0,0,1000000000,epoch,epoch+1,1.01e-9L) &&
          !checkedCaptureTimestamp(0,0,1,1000000000,epoch,epoch+1,1.01e-9L),
          "tiny callback-end violation vanished in absolute UTC arithmetic");
    check(!checkedCaptureTimestamp(500,499,1,1000,100,100,.001L) &&
          !checkedCaptureTimestamp(0,0,1,0,100,100,.001L),
          "invalid callback reference or sample rate accepted");
    for(const auto invalid:{-1.L,std::numeric_limits<long double>::quiet_NaN(),
            std::numeric_limits<long double>::infinity(),-std::numeric_limits<long double>::infinity()}) {
        check(!checkedCaptureTimestamp(0,0,1,1000,invalid,100,.001L) &&
              !checkedCaptureTimestamp(0,0,1,1000,100,invalid,.001L),
              "negative or nonfinite callback UTC accepted");
    }
    for(const auto invalid:{0.L,-.001L,std::numeric_limits<long double>::quiet_NaN(),
            std::numeric_limits<long double>::infinity(),-std::numeric_limits<long double>::infinity()})
        check(!checkedCaptureTimestamp(0,0,0,1000,100,100,invalid),
              "nonpositive or nonfinite callback slope accepted");
    check(!checkedCaptureTimestamp(0,0,2,1,0,0,std::numeric_limits<long double>::max()),
          "nonfinite callback duration accepted");
    check(!checkedCaptureTimestamp(0,0,1,1,std::numeric_limits<long double>::max(),
              std::numeric_limits<long double>::max(),std::numeric_limits<long double>::max()),
          "nonfinite absolute callback endpoint accepted");
    check(checkedCaptureTimestamp(0,1000000,1000,1000,100,1101.5L,.001L) &&
          !checkedCaptureTimestamp(0,1000000,1000,1000,100,1102.5L,.001L),
          "aged callbacks reset the original future-certificate UTC budget");
}
void test_future_corners() {
    SchedCaptureFuture f;
    f.reference_sample=500;f.horizon_samples=100000;f.utc_at_reference={10000000,0};
    f.sample_rate=1000;f.utc_rate_lower=.9999L;f.utc_rate_upper=1.0001L;
    f.minimum_rate=.999L;f.maximum_rate=1.001L;f.propagation_samples=1234;
    f.epoch_rounding_radius_samples=7000;f.start_uncertainty_samples=7000;
    f.maximum_phase_samples=999;f.timestamp_radius_samples=25;
    const auto bound=sched_capture_future_envelope(f);check(bound.has_value(),"future bound unavailable");
    // Includes interior points as well as extrema and old/future epochs.
    for(const auto age:{0.L,1.L,25000.L,100000.L})
        for(const auto tau:{.9999L,1.L,1.0001L})for(const auto r:{.999L,1.L,1.001L})
            for(const auto e:{-7000.L,0.L,7000.L})for(const auto d:{-7000.L,0.L,7000.L})
                for(const auto phase:{0.L,500.L,999.L})for(const auto stamp:{-25.L,25.L}) {
                    const auto q=f.utc_at_reference.add_signed(-1234+phase+(tau-r)*age+(r-1)*e-r*d+stamp);
                    check(q.distance(bound->lower)>=0 && bound->upper.distance(q)>=0,
                          "future admission escaped envelope");
                    // Independent original mapping at the horizon, not the
                    // rearranged corner expression used by the constructor.
                    const auto utc_at_admission=10000000.L+tau*age+stamp;
                    const auto epoch_sample=utc_at_admission-1234-e;
                    const auto origin=epoch_sample-utc_at_admission+1234+d;
                    const auto direct=f.utc_at_reference.add_signed(epoch_sample-10000000.L+phase+
                        r*(static_cast<long double>(f.horizon_samples)-age-origin));
                    const auto lo=bound->lower.add_signed(bound->minimum_rate*f.horizon_samples);
                    const auto hi=bound->upper.add_signed(bound->maximum_rate*f.horizon_samples);
                    // Only this oracle forms/subtracts large absolute floats.
                    check(direct.distance(lo)>=-1e-8L && hi.distance(direct)>=-1e-8L,
                          "original future mapping escaped propagated envelope");
                }
    f.minimum_rate=0;check(!sched_capture_future_envelope(f),"invalid future rate accepted");
    f.minimum_rate=.999L;f.reference_sample=std::numeric_limits<std::uint64_t>::max();
    check(!sched_capture_future_envelope(f),"overflowing certificate horizon accepted");
    f.reference_sample=500;f.utc_at_reference={0,0};
    check(!sched_capture_future_envelope(f),"negative UTC bound accepted");
}
void test_union_and_identity() {
    const SchedHopSourceMap a({100,1},key(),1000),b({100,1},key(1),1000);
    const auto find_null=[&] {
        for(std::uint64_t first=1000;first<39000;first+=100)
            if(a.locate(first-10).source.distance(a.locate(first+110).source)==0 &&
               b.locate(first-10).source.distance(b.locate(first+110).source)==0)return first;
        throw std::runtime_error("no paired null fixture");
    };
    const auto first=find_null();
    std::array<SchedCaptureBound,2> bounds{{{&a,{first,{first-5,0},{first+5,0},.999L,1.001L}},
                                        {&b,{first,{first-5,0},{first+5,0},.999L,1.001L}}}};
    SchedCaptureCertificate c{{3,7},first,first+100,bounds,true,true,true,true};
    check(c.excludes(first,100,{3,7}),"known union null rejected");
    check(!c.excludes(first,100,{3,8}),"stale revision accepted");
    check(!c.excludes(first,101,{3,7}),"expired certificate accepted");
    c.future_admissions_complete=false;check(!c.excludes(first,100,{3,7}),"unknown future banks discarded");
    c.future_admissions_complete=true;c.paused_source_skip_supported=false;
    check(!c.excludes(first,100,{3,7}),"unsupported partial-FFT policy accepted");
    c.paused_source_skip_supported=true;c.timestamp_model_valid=false;
    check(!c.excludes(first,100,{3,7}),"unqualified continuing timing accepted");
    c.timestamp_model_valid=true;
    const auto active=a.source_to_physical({0,0});
    bounds[0].envelope={first,active,active,1,1};
    const auto location=a.locate(active);
    check(SchedHopSourceMap::gain(location,active)==0 &&
          SchedHopSourceMap::gain(location,active.add_samples(4))==0,
          "zero-gain guard fixture changed");
    check(!c.excludes(first,5,{3,7}),"zero-gain but advancing source discarded");
    bounds[0].envelope={first,{first+5,0},{first-5,0},1,1};
    check(!c.excludes(first,5,{3,7}),"reversed envelope accepted");
    bounds[0].envelope={first,{first,0},{first,0},std::numeric_limits<long double>::quiet_NaN(),1};
    check(!c.excludes(first,5,{3,7}),"NaN envelope rate accepted");
    bounds[0].envelope={first,{std::numeric_limits<std::uint64_t>::max(),0},
        {std::numeric_limits<std::uint64_t>::max(),0},1,1};
    check(!c.excludes(first,5,{3,7}),"overflowing capture coordinate accepted");
}
void test_broad_null_width() {
    // A broad +/-7 s original prior cannot fit in a 4 s frame's null.
    const SchedHopSourceMap small({10,1},key(),1000),large({100,1},key(),1000);
    bool small_skip=false,large_skip=false;
    for(std::uint64_t at=8000;at<32000;at+=100) {
        ScheduledCaptureEnvelope e{at,{at-7000,0},{at+7999,0},1,1};
        SchedCaptureBound sb{&small,e},lb{&large,e};
        SchedCaptureCertificate c{{1,1},at,at+100,std::span(&sb,1),true,true,true,true};
        small_skip|=c.excludes(at,100,{1,1});c.bounds=std::span(&lb,1);
        large_skip|=c.excludes(at,100,{1,1});
    }
    check(!small_skip && large_skip,"broad-prior calendar crossover changed");
}
}
int main() {
    try {test_capture_fence();test_callback_timestamps();test_future_corners();test_union_and_identity();test_broad_null_width();
        std::cout<<"scheduled capture certificate tests passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"scheduled capture certificate failure: "<<e.what()<<'\n';return 1;}
}

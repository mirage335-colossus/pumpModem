#include "scheduled_residual_prefix.hpp"
#include <array>
#include <cfenv>
#include <complex>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace rp=datapump::modem::detail::scheduled_residual_prefix;
namespace cert=datapump::modem::detail::scheduled_acquisition_certificate;
static std::size_t checked=0;
#define CHECK(...) do {++checked;if(!(__VA_ARGS__))throw std::runtime_error(#__VA_ARGS__);}while(false)
template<class F> static bool rejected(F&& f){try{f();}catch(const rp::Error&){return true;}return false;}
static constexpr rp::Identity identity{1,1,500,-7};
static void paired() {
    constexpr std::size_t rows=1024,starts=7;
    std::vector<float> pcm(rows+starts-1);std::vector<double> residual(rows);
    for(std::size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<float>(.2*std::sin(i*.17)+.03*std::cos(i*.071));
    rp::AbsolutePrefix input(identity,pcm,{},rp::AbsolutePrefix::reservation(pcm.size()));
    // Force bounded-table coarsening, retaining outer/window/mask-zero jumps.
    rp::ResidualRuns program(rows,8,rp::ResidualRuns::reservation(8));
    for(std::size_t i=0;i<rows;++i){residual[i]=cert::up(i%127==0?.07:(i%17==0?.002:1e-8));CHECK(program.append(residual[i]));}
    CHECK(program.seal());CHECK(program.coarsened());
    for(std::size_t start=0;start<starts;++start){const auto bound=program.dot_upper(input,identity,start,2,true);CHECK(bound.valid);
        for(unsigned bit=0;bit<2;++bit){std::complex<long double> delta{};
            for(std::size_t i=0;i<rows;++i){const auto dq=std::polar(.93L*residual[i],static_cast<long double>(i)*(bit?.071L:.113L));
                const auto oscillator=std::polar(1.91L,static_cast<long double>(i+start)*.019L);
                delta+=static_cast<long double>(pcm[i+start])*oscillator*dq;}
            CHECK(std::abs(delta)<=bound.upper);}
    }
    CHECK(!program.dot_upper(input,rp::Identity{1,2,500,-7},0,2,true).valid);
    CHECK(!program.dot_upper(input,identity,starts,2,true).valid);
    CHECK(!program.dot_upper(input,identity,0,2,false).valid);
}
static void useful_locality() {
    std::array<float,8> pcm{1000,1,1,1,1,1,1,1};
    rp::AbsolutePrefix input(identity,pcm,{},rp::AbsolutePrefix::reservation(pcm.size()));
    rp::ResidualRuns program(8,8,rp::ResidualRuns::reservation(8));
    CHECK(program.append(0));for(unsigned i=1;i<8;++i)CHECK(program.append(1e-8));CHECK(program.seal());
    const auto bound=program.dot_upper(input,identity,0,2,true);CHECK(bound.valid);
    CHECK(bound.upper>=1.4e-7);CHECK(bound.upper<2*1000*7e-8);
}
static void missing_and_mask_zero() {
    std::array<float,4> pcm{.1f,.2f,.3f,.4f};
    std::array<rp::SampleState,4> state{rp::SampleState::observed,rp::SampleState::unknown,rp::SampleState::observed,rp::SampleState::observed};
    rp::AbsolutePrefix input(identity,pcm,state,rp::AbsolutePrefix::reservation(4));
    rp::ResidualRuns full(4,4,rp::ResidualRuns::reservation(4));
    // A proxy mask-zero row may carry a mandatory pulse/window residual.
    for(double e:std::array<double,4>{0,.5,0,0})CHECK(full.append(e));CHECK(full.seal());
    CHECK(!full.dot_upper(input,identity,0,2,true).valid);
    rp::ResidualRuns zero(4,4,rp::ResidualRuns::reservation(4));
    for(double e:std::array<double,4>{.1,0,.1,.1})CHECK(zero.append(e));CHECK(zero.seal());
    CHECK(zero.dot_upper(input,identity,0,2,true).valid); // unknown input multiplies exactly0
    state[1]=rp::SampleState::excluded;rp::AbsolutePrefix excluded(identity,pcm,state,rp::AbsolutePrefix::reservation(4));
    CHECK(full.dot_upper(excluded,identity,0,2,true).valid); // caller's independent proof only
}
static void outward_subtraction() {
    std::array<float,5> pcm{1e30f,1e-20f,1e-20f,1e-20f,1e-20f};
    rp::AbsolutePrefix input(identity,pcm,{},rp::AbsolutePrefix::reservation(5));
    const auto later=input.absolute_upper(identity,1,5);CHECK(later.valid);CHECK(later.upper>=4*static_cast<long double>(pcm[1]));
    CHECK(input.absolute_upper(identity,5,5).valid);
    CHECK(!input.absolute_upper(identity,3,2).valid);CHECK(!input.absolute_upper(identity,0,6).valid);
}
static void fragments_cancel_and_limits() {
    std::array<float,4> pcm{.1f,.2f,.3f,.4f};std::array<std::size_t,1> starts{0};
    rp::AbsolutePrefix input(identity,pcm,{},rp::AbsolutePrefix::reservation(4));
    rp::ResidualRuns program(4,4,rp::ResidualRuns::reservation(4));for(unsigned i=0;i<4;++i)CHECK(program.append(.01));CHECK(program.seal());
    rp::DotTotals total(1,rp::DotTotals::reservation(1));CHECK(total.add_fragment(input,program,identity,starts,2,true));
    CHECK(total.finish(false).empty()); // never publish a partial original-order sum
    rp::DotTotals cancelled(1,rp::DotTotals::reservation(1));std::stop_source stop;stop.request_stop();
    CHECK(rejected([&]{cancelled.add_fragment(input,program,identity,starts,2,true,stop.get_token());}));CHECK(cancelled.finish(true).empty());
    CHECK(rejected([&]{rp::AbsolutePrefix too_small(identity,pcm,{},1);}));
    CHECK(rejected([&]{rp::AbsolutePrefix invalid_owner({},pcm,{},rp::AbsolutePrefix::reservation(4));}));
    CHECK(rejected([&]{rp::ResidualRuns too_small(4,4,1);}));
    rp::ResidualRuns partial(4,4,rp::ResidualRuns::reservation(4));CHECK(partial.append(.1));CHECK(!partial.seal());CHECK(!partial.dot_upper(input,identity,0,2,true).valid);
    rp::ResidualRuns invalid(1,1,rp::ResidualRuns::reservation(1));CHECK(!invalid.append(-1));CHECK(!invalid.seal());
    const auto old_round=std::fegetround();CHECK(old_round==FE_TONEAREST);
    CHECK(std::fesetround(FE_UPWARD)==0);const bool refuses=rejected([&]{rp::AbsolutePrefix altered(identity,pcm,{},rp::AbsolutePrefix::reservation(4));});
    CHECK(std::fesetround(old_round)==0);CHECK(refuses);
}
int main(){paired();useful_locality();missing_and_mask_zero();outward_subtraction();fragments_cancel_and_limits();std::cout<<"checked="<<checked<<'\n';}

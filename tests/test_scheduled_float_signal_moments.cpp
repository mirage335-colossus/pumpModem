#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/scheduled_float_signal_moments.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <vector>

using namespace datapump::modem::detail::scheduled_float_signal;
using C=std::complex<double>;
using W=std::complex<long double>;

W oracle(std::span<const C> samples,std::size_t left,std::size_t count,C v,C s) {
    W sum{};
    for(std::size_t k=0;k<count;++k) {
        const W t=W{v.real(),v.imag()}+static_cast<long double>(k)*W{s.real(),s.imag()};
        sum+=t*W{samples[left+k].real(),samples[left+k].imag()};
    }
    return sum;
}

int main() {
    assert(arithmetic_supported());
    std::mt19937_64 random(0x51a7e11ULL);
    std::uniform_real_distribution<double> uniform(-1,1);
    std::size_t cases=0,tile_pieces=0,certified=0,rejected=0;
    constexpr std::array<unsigned,7> lengths{1U,2U,6U,17U,32U,64U,128U};
    std::array<std::size_t,7> accepted_by_length{},rejected_by_length{},tight_by_length{};
    long double largest_ratio=0;
    for(const auto scale:{1e-30,1e-8,1.,1e8,1e30}) {
        std::array<C,block_capacity> samples;
        for(auto& x:samples)x=scale*C{uniform(random),uniform(random)};
        Moments moments;assert(moments.prepare(samples)==Failure::none);
        for(std::size_t left=0;left<block_capacity;++left) {
            for(std::size_t family=0;family<lengths.size();++family) {
                const auto nominal_count=lengths[family];
                const auto count=std::min<std::size_t>(nominal_count,block_capacity-left);
                const std::array<C,2> value={C{uniform(random),uniform(random)},C{uniform(random),uniform(random)}};
                const std::array<C,2> slope={C{uniform(random),uniform(random)}/128.,C{uniform(random),uniform(random)}/128.};
                const auto result=moments.contract(left,count,value,slope);assert(result);
                tile_pieces+=result.tiles;
                for(unsigned bit=0;bit<2;++bit) {
                    const auto expected=oracle(samples,left,count,value[bit],slope[bit]);
                    const W actual{result.dots[bit].value.real(),result.dots[bit].value.imag()};
                    const auto error=std::abs(actual-expected);
                    assert(error<=result.dots[bit].error);
                    if(result.dots[bit].error)largest_ratio=std::max(largest_ratio,error/result.dots[bit].error);
                    double energy=0;
                    for(std::size_t i=0;i<count;++i)energy+=std::norm(samples[left+i]);
                    const auto status=certify(result.dots[bit],count/2.,count/2.,0,energy,1e-4);
                    (status==Failure::none?certified:rejected)++;
                    (status==Failure::none?accepted_by_length[family]:rejected_by_length[family])++;
                    if(certify(result.dots[bit],count/2.,count/2.,0,energy,1e-5)==Failure::none)
                        ++tight_by_length[family];
                }
                ++cases;
            }
        }
    }
    // All prefix endpoints/tile crossings/candidate signs covered above.
    // Severe within-block dynamic range is explicit FP64 fallback, never FTZ.
    {
        Moments moments;std::array<C,2> tiny{{{1,0},{1e-300,0}}};
        assert(moments.prepare(tiny)==Failure::input_range);
        std::array<C,2> invalid{{{1,0},{std::numeric_limits<double>::infinity(),0}}};
        assert(moments.prepare(invalid)==Failure::nonfinite_input);
        std::array<C,1> huge{{{std::numeric_limits<double>::max(),0}}};
        assert(moments.prepare(huge)==Failure::input_range);
        assert(moments.prepare(std::span<const C>{})==Failure::unsupported_size);
    }
    {
        Moments moments;std::array<C,64> alternating{};
        for(std::size_t i=0;i<alternating.size();++i)alternating[i]=(i&1)?C{-1,1}:C{1,-1};
        assert(moments.prepare(alternating)==Failure::none);
        const std::array<C,2> v{{{1,0},{0,1}}},s{};
        const auto result=moments.contract(1,62,v,s);assert(result);
        for(const auto& dot:result.dots)assert(std::abs(dot.value)<=dot.error);
        assert(certify(result.dots[0],1,1,1,62,1e-4)==Failure::ill_conditioned_gram);
        assert(certify(result.dots[0],1,1,0,62,0)==Failure::inconclusive_error);
    }
    {
        const auto previous=std::fegetround();assert(std::fesetround(FE_UPWARD)==0);
        Moments moments;const std::array<C,1> sample{{{1,0}}};
        assert(moments.prepare(sample)==Failure::arithmetic_environment);
        assert(std::fesetround(previous)==0);
    }
#if defined(__SSE2__)
    {
        const auto previous=_mm_getcsr();_mm_setcsr(previous|(1U<<15)|(1U<<6));
        Moments moments;const std::array<C,1> sample{{{1,0}}};
        assert(moments.prepare(sample)==Failure::arithmetic_environment);
        _mm_setcsr(previous);
    }
#endif
    std::cout<<"certificate_cases="<<cases<<" contraction_tiles="<<tile_pieces
        <<" largest_observed_error_over_bound="<<static_cast<double>(largest_ratio)
        <<" local_certified="<<certified<<" local_inconclusive="<<rejected
        <<" scratch_bytes="<<Moments::working_bytes()<<'\n';
    for(std::size_t family=0;family<lengths.size();++family)
        std::cout<<"nominal_count="<<lengths[family]<<" loose_accepted="<<accepted_by_length[family]
            <<" loose_inconclusive="<<rejected_by_length[family]
            <<" tight_accepted="<<tight_by_length[family]<<'\n';
}

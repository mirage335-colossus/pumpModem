#define DATAPUMP_ORDERED_PULSE_CONTRACT_OFF 1
#include "scheduled_fp32_pulse_tile.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
using Native=datapump::modem::detail::ScheduledFp32PulseTile<>;
static void check(bool value){if(!value)throw datapump::Error("packed FP32 L1 bound failure");}
int main() {
    if(!Native::supported()) {
        std::cerr<<"native FP32 execution unavailable; packed-bound coverage omitted\n";
        return 77;
    }
    check(Native::supported());auto plan=std::make_unique<Native::Plan>();
    auto packed=std::make_unique<Native::PackedPlan>();Native::Statistics statistics;
    std::mt19937_64 random(0x1a04664);std::size_t rows=0;
    for(unsigned iteration=0;iteration<4096;++iteration) {
        *plan={};plan->valid=true;plan->samples=1;plan->symbol_samples=96000;plan->chip_samples=15;
        const auto taps=iteration%19;plan->coefficients=taps;plan->rows[0].taps=taps;
        for(unsigned p=0;p<taps;++p) {
            // Positive/negative atoms, lost-half-ULP additions, wide relative
            // range down to the smallest accepted normalized float input.
            double value;
            if(iteration%3==0)value=p==0?1.:0x1p-54;
            else if(iteration%3==1)value=std::ldexp(1.+(random()%4096)/4096.,-static_cast<int>(random()%120));
            else value=p==0?1.:0x1p-125;
            if(p%2)value=-value;
            plan->rows[0].weight[p]=value;plan->rows[0].nonzero|=1U<<p;plan->active_taps.set(p);
        }
        plan->rows[0].nonzero_count=static_cast<std::uint8_t>(taps);
        check(packed->pack(*plan,statistics));long double exact=0;
        for(unsigned p=0;p<taps;++p)exact+=std::abs(static_cast<long double>(plan->rows[0].weight[p])/packed->scale);
        check(static_cast<long double>(packed->rows[0].absolute_weight_sum)>=exact);
        check(packed->maximum_weight_sum>=packed->rows[0].absolute_weight_sum);++rows;
    }
    // Nonzero original scaling to zero must reject rather than claiming a
    // per-row outward sum over already-underflowed normalized doubles.
    *plan={};plan->valid=true;plan->samples=2;plan->symbol_samples=96000;plan->chip_samples=15;plan->coefficients=2;
    for(unsigned n=0;n<2;++n){plan->rows[n].taps=1;plan->rows[n].first_offset=n;plan->rows[n].nonzero=1;plan->rows[n].nonzero_count=1;plan->active_taps.set(n);}
    plan->rows[0].weight[0]=1;plan->rows[1].weight[0]=std::numeric_limits<double>::denorm_min();
    const auto before=statistics.range_fallbacks;
    check(!packed->pack(*plan,statistics)&&!packed->valid&&statistics.range_fallbacks==before+1);
    std::cout<<"packed L1 ordinary-sum/inflation rows="<<rows<<" normalization-underflow fallback passed\n";
}

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/scheduled_pulse_moments.hpp"
#include "../src/scheduled_float_gram.hpp"
#include <cassert>
#include <iostream>

using namespace datapump::modem::detail;
int main() {
    ScheduledSpanMoments m;m.count=6;m.energy=4;m.norm={6,15,55};
    const std::array<std::complex<double>,2> value{{{1,0},{0,1}}},slope{},dots{{{.25,-.5},{.125,-.75}}};
    const auto native=scheduled_float_gram::evaluate(m,value,slope);assert(native);
    std::array<CorrelationFit,2> original,overridden;
    assert(scheduled_affine_pair(m,value,slope,original,&dots));
    assert(scheduled_affine_pair(m,value,slope,overridden,&dots,&native.gram));
    for(unsigned bit=0;bit<2;++bit) {
        assert(overridden[bit].xc==dots[bit].real() && overridden[bit].xs==dots[bit].imag());
        assert(overridden[bit].cc==native.gram[bit].cc && overridden[bit].ss==native.gram[bit].ss && overridden[bit].cs==native.gram[bit].cs);
        assert(overridden[bit].count==m.count && overridden[bit].energy==m.energy);
        assert(std::abs(overridden[bit].cc-original[bit].cc)<=native.entry_error[bit]);
    }
    // A finite override bypasses both nonfinite original dot and original Gram
    // products. Original rejection remains when the respective override is absent.
    m.square[0]={std::numeric_limits<double>::infinity(),0};
    m.signal[0]={std::numeric_limits<double>::infinity(),0};
    assert(!scheduled_affine_pair(m,value,slope,original));
    assert(scheduled_affine_pair(m,value,slope,overridden,&dots,&native.gram));
    auto rejected=native.gram;rejected[0].cc=-1;
    const auto before=overridden;
    assert(!scheduled_affine_pair(m,value,slope,overridden,&dots,&rejected));
    for(unsigned bit=0;bit<2;++bit)assert(overridden[bit].cc==before[bit].cc && overridden[bit].xc==before[bit].xc);
    std::cout<<"original_Gram_products_bypassed original_dot_bypassed atomic_override_failure_passed\n";
}

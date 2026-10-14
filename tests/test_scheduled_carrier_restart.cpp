#include "../src/scheduled_carrier_restart.hpp"
#include <iostream>
#include <numbers>
#include <cfenv>
#include <bit>

using namespace datapump::modem::detail;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){try {
    constexpr double tau=2*std::numbers::pi;
    for(auto frequency:{-1500.,127.375,1501.441})for(auto origin:{0ULL,927117ULL,1ULL<<52})
        for(unsigned offset:{0U,1U,100U,511U})for(unsigned count:{1U,17U,512U}) {
            const auto seed=[&](std::uint64_t n){return std::polar(1.,static_cast<double>(std::remainder(
                static_cast<long double>(n)*tau*frequency/6120,static_cast<long double>(tau))));};
            const auto step=std::polar(1.,tau*frequency/6120);
            auto old=seed(origin);for(unsigned i=0;i<offset;++i)old*=step;
            auto now=seed(origin+offset);const auto bound=scheduled_carrier_restart::difference(old,now,step,count);
            check(bound.valid,"native recurrence restart certificate unavailable");
            double error=0,magnitude=0;
            for(unsigned i=0;i<count;++i) {
                error=std::max(error,scheduled_acquisition_certificate::norm(old-now));
                magnitude=std::max({magnitude,scheduled_acquisition_certificate::norm(old),scheduled_acquisition_certificate::norm(now)});
                old*=step;now*=step;
            }
            check(error<=bound.radius+8*std::numeric_limits<double>::denorm_min(),"actual restarted carrier exceeded bound");
            check(magnitude<=bound.magnitude,"actual native carrier magnitude exceeded bound");
            if(!offset)check(bound.radius==0,"identical native seeds were not exact-reused");
        }
    check(!scheduled_carrier_restart::difference({1,0},{1,0},{1,0},513).valid,"unbounded restart extent accepted");
    const auto rounding=std::fegetround();std::fesetround(FE_UPWARD);
    check(!scheduled_carrier_restart::difference({1,0},{1,0},{1,0},1).valid,"unsupported rounding accepted");
    std::fesetround(rounding);
    std::cout<<"scheduled carrier restart certificates passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

#include "../src/search_fft.hpp"
#include "../src/pattern_fft_batch.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>
using namespace datapump;using namespace datapump::modem;
int main(){try{
    std::mt19937_64 random(117);std::uniform_real_distribution<float> value(-1,1);
    detail::search_fft::FloatPlan fp(524288);detail::search_fft::DoublePlan dp(524288);
    double max_float_l2=0,max_double_l2=0;std::size_t checked=0;
    for(std::size_t n:{1U,2U,8U,64U,256U,1024U,4096U,16384U,131072U,262144U,524288U})for(unsigned kind=0;kind<3;++kind){
        std::vector<std::complex<float>> native(n),scalar(n);std::vector<std::complex<double>> ref(n),legacy(n),dscalar(n);
        for(std::size_t i=0;i<n;++i){native[i]=kind==0?std::complex<float>{value(random),value(random)}:
            kind==1?std::complex<float>{i==0?1.F:0.F,0.F}:std::complex<float>{(i&1)?-1.F:1.F,(i%3)?0.F:.25F};
            ref[i]={native[i].real(),native[i].imag()};}scalar=native;legacy=ref;dscalar=ref;
        for(bool inverse:{false,true}){
            detail::search_fft::fft(std::span(native),inverse,fp);
            detail::search_fft::fft(std::span(scalar),inverse,fp,{},detail::search_fft::Kernel::scalar);
            detail::search_fft::fft(std::span(ref),inverse,dp);
            detail::search_fft::fft(std::span(dscalar),inverse,dp,{},detail::search_fft::Kernel::scalar);
            detail::pattern_fft(legacy,inverse,{});
            long double float_error=0,double_error=0,power=0;
            for(std::size_t i=0;i<n;++i){
                if(native[i]!=scalar[i] || ref[i]!=dscalar[i])throw Error("table scalar/SIMD arithmetic differed");
                if(ref[i]!=legacy[i])throw Error("FP64 table changed legacy FFT recurrence rounding");
                float_error+=std::norm(std::complex<double>{native[i].real(),native[i].imag()}-ref[i]);
                double_error+=std::norm(legacy[i]-ref[i]);power+=std::norm(ref[i]);++checked;
            }
            const auto ferr=std::sqrt(static_cast<double>(float_error/std::max(1e-30L,power)));
            const auto derr=std::sqrt(static_cast<double>(double_error/std::max(1e-30L,power)));
            max_float_l2=std::max(max_float_l2,ferr);max_double_l2=std::max(max_double_l2,derr);
            if(ferr>3e-6 || derr>1e-10)throw Error("table FFT numeric bound failed at n="+std::to_string(n));
        }
    }
    // Independent inverse inputs and full double mantissas qualify the exact
    // recurrence contract beyond a float-promoted forward/inverse round trip.
    std::uniform_real_distribution<double> wide_value(-1,1);
    for(const std::size_t n:{256U,4096U,65536U})for(const bool inverse:{false,true})
        for(const double scale:{1e-100,1.,1e100}) {
            std::vector<std::complex<double>> native(n),legacy;
            for(auto& v:native)v={wide_value(random)*scale,wide_value(random)*scale};
            legacy=native;detail::search_fft::fft(std::span(native),inverse,dp);
            detail::pattern_fft(legacy,inverse,{});
            if(native!=legacy)throw Error("native double FFT changed independent legacy transform");
        }
    const std::array<std::complex<double>,2> ordinary{{{.1,.2},{-.3,.4}}};
    if(!detail::search_fft::float_input_safe(ordinary,524288))throw Error("ordinary float input rejected");
    for(double magnitude:{1e30,1e-30,std::numeric_limits<double>::infinity()}) {
        const std::array<std::complex<double>,1> extreme{{{magnitude,0}}};
        if(detail::search_fft::float_input_safe(extreme,524288))throw Error("unsafe float input admitted");
    }
    std::stop_source cancelled;cancelled.request_stop();std::vector<std::complex<float>> x(256);
    bool caught=false;try{detail::search_fft::fft(std::span(x),false,fp,cancelled.get_token());}catch(const Error&){caught=true;}
    if(!caught)throw Error("table FFT missed cancellation");
    std::cout<<"table scalar/SIMD exact checked="<<checked<<" float_relative_l2="<<max_float_l2
        <<" legacy_double_relative_l2="<<max_double_l2<<" dispatch="<<detail::search_fft::kernel_name()<<" cancellation=pass\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

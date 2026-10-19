#include "../src/search_fft.hpp"
#include "../src/pattern_fft_batch.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cstdint>
#include <limits>
#if (defined(__x86_64__) || defined(__i386__)) && defined(__SSE2__) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define TEST_SEARCH_FFT_HAS_MXCSR 1
#endif
#include <cmath>
#include <array>
#include <cstring>
#include <numbers>
#include <iostream>
#include <random>
#include <type_traits>
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

    using detail::search_fft::DoubleTableView;
    using detail::search_fft::prepare_double_tables;
    using detail::search_fft::fft;
    static_assert(std::is_trivially_copyable_v<DoubleTableView>);
    auto bytes_equal=[](const auto& a,const auto& b){
        return a.size()==b.size() && (a.empty() || std::memcmp(a.data(),b.data(),a.size()*sizeof(a[0]))==0);
    };
    auto expect_error=[](auto&& action,const char* message){
        bool rejected=false;try{action();}catch(const Error&){rejected=true;}
        if(!rejected)throw Error(message);
    };
    constexpr double tau=2*std::numbers::pi;
    for(const std::size_t maximum:{1U,2U,16384U}){
        std::vector<std::complex<double>> forward(maximum-1),inverse(maximum-1);
        const auto view=prepare_double_tables(forward,inverse);
        if(view.maximum!=maximum || view.forward.data()!=forward.data() || view.inverse.data()!=inverse.data())
            throw Error("double table view retained or replaced caller storage");
        for(std::size_t length=2;length<=maximum;length*=2){
            const auto half=length/2,offset=half-1;
            const auto fstep=std::polar(1.,-tau/static_cast<double>(length));
            const auto istep=std::polar(1.,tau/static_cast<double>(length));
            std::complex<double> fvalue{1,0},ivalue{1,0};
            for(std::size_t j=0;j<half;++j){
                if(std::memcmp(&forward[offset+j],&fvalue,sizeof(fvalue))!=0 ||
                   std::memcmp(&inverse[offset+j],&ivalue,sizeof(ivalue))!=0)
                    throw Error("double direction table recurrence changed");
                fvalue*=fstep;ivalue*=istep;
            }
            if(length==maximum)break;
        }
        for(const std::size_t n:{1U,2U,8U,4096U,16384U})if(n<=maximum)
            for(const bool backwards:{false,true})for(const double scale:{1e-100,1.,1e100}){
                std::vector<std::complex<double>> actual(n),expected;
                for(std::size_t i=0;i<n;++i){
                    const auto re=wide_value(random)*scale,im=wide_value(random)*scale;
                    actual[i]={re,im};
                }
                if(n==1 && scale==1.)actual[0]={-0.0,+0.0};
                if(n>1 && scale==1.){
                    if(n%2==0)for(std::size_t i=0;i<n;++i)actual[i]={(i&1)?-1.0:1.0,(i&1)?+0.0:-0.0};
                    actual[0]={1.0,0.0};
                }
                expected=actual;detail::pattern_fft(expected,backwards,{});
                fft(actual,backwards,view);
                if(actual!=expected || (n==1 && scale==1. && !bytes_equal(actual,expected)))
                    throw Error("double-table FFT differs from independent pattern FFT");
            }
    }
    std::vector<std::complex<double>> forward16384(16383),inverse16384(16383);
    const auto view16384=prepare_double_tables(forward16384,inverse16384);
    for(const std::size_t n:{4096U,16384U})for(unsigned kind=0;kind<4;++kind)
        for(const bool backwards:{false,true}){
            std::vector<std::complex<double>> actual(n),expected;
            for(std::size_t i=0;i<n;++i){
                if(kind==0)actual[i]={wide_value(random),wide_value(random)};
                else if(kind==1)actual[i]={i==0?1.0:0.0,0.0};
                else if(kind==2)actual[i]={(i&1)?-1.0:1.0,(i&1)?+0.0:-0.0};
                else actual[i]={-0.0,(i&1)?+0.0:-0.0};
            }
            expected=actual;detail::pattern_fft(expected,backwards,{});
            fft(actual,backwards,view16384);
            if(actual!=expected || (kind==3 && !bytes_equal(actual,expected)))
                throw Error("double-table pattern FFT mismatch");
        }

    // Keep every existing seeded fixture unchanged while checking the small
    // borrowed transforms and boundary against the independent original-order oracle.
    auto boundary_random=random;auto boundary_value=wide_value;
    for(const std::size_t n:{256U,512U,8192U})
      for(const std::size_t maximum:std::array<std::size_t,2>{n,16384U}) {
        std::vector<std::complex<double>> forward(maximum-1),inverse(maximum-1);
        const auto view=prepare_double_tables(forward,inverse);
        for(unsigned kind=0;kind<4;++kind)for(const bool backwards:{false,true})
            for(const double scale:{1e-100,1.,1e100}) {
                std::vector<std::complex<double>> actual(n),expected;
                for(std::size_t i=0;i<actual.size();++i) {
                    if(kind==0)actual[i]={boundary_value(boundary_random)*scale,boundary_value(boundary_random)*scale};
                    else if(kind==1)actual[i]={i==0?scale:0.0,0.0};
                    else if(kind==2)actual[i]={(i&1)?-scale:scale,(i&1)?+0.0:-0.0};
                    else actual[i]={-0.0,(i&1)?+0.0:-0.0};
                }
                expected=actual;detail::pattern_fft(expected,backwards,{});
                fft(actual,backwards,view);
                if(actual!=expected || (kind>=2 && !bytes_equal(actual,expected)))
                    throw Error("small-boundary double-table FFT mismatch");
            }
    }

    // Negative controls use live, correctly typed backing spans. Malformed
    // span construction itself would violate the caller's C++ preconditions.
    std::vector<std::complex<double>> untouched_a(3,{7.,-2.}),untouched_b(2,{3.,4.});
    const auto before_a=untouched_a,before_b=untouched_b;
    expect_error([&]{prepare_double_tables(untouched_a,untouched_b);},"unequal table spans accepted");
    if(!bytes_equal(untouched_a,before_a) || !bytes_equal(untouched_b,before_b))throw Error("unequal spans were written");
    std::vector<std::complex<double>> malformed_a(2,{7.,-2.}),malformed_b(2,{3.,4.});
    const auto malformed_a_before=malformed_a,malformed_b_before=malformed_b;
    expect_error([&]{prepare_double_tables(malformed_a,malformed_b);},"non-power-of-two table geometry accepted");
    if(!bytes_equal(malformed_a,malformed_a_before) || !bytes_equal(malformed_b,malformed_b_before))throw Error("bad geometry was written");
    std::array<std::complex<double>,8> overlapping{{{1,2},{3,4},{5,6},{7,8},{9,10},{11,12},{13,14},{15,16}}};
    const auto overlapping_before=overlapping;
    expect_error([&]{prepare_double_tables(std::span(overlapping).first(7),std::span(overlapping).subspan(1,7));},
        "overlapping direction tables accepted");
    if(!bytes_equal(overlapping,overlapping_before))throw Error("overlapping output spans were written");
    std::array<std::complex<double>,7> cancelled_forward{},cancelled_inverse{};
    const auto cancelled_forward_before=cancelled_forward,cancelled_inverse_before=cancelled_inverse;
    expect_error([&]{prepare_double_tables(cancelled_forward,cancelled_inverse,cancelled.get_token());},
        "pre-cancelled table construction accepted");
    if(!bytes_equal(cancelled_forward,cancelled_forward_before) || !bytes_equal(cancelled_inverse,cancelled_inverse_before))
        throw Error("cancelled construction wrote table output");
    const auto fresh_view=prepare_double_tables(cancelled_forward,cancelled_inverse);
    if(fresh_view.maximum!=8)throw Error("fresh table construction after cancellation failed");

    auto expect_fft_rejection_preserves=[&](std::span<std::complex<double>> values,const DoubleTableView& view,
                                              std::stop_token stop,const char* message){
        std::vector<std::complex<double>> before(values.begin(),values.end());
        expect_error([&]{fft(values,false,view,stop);},message);
        if(!bytes_equal(values,before))throw Error("rejected double FFT input was written");
    };
    expect_fft_rejection_preserves(std::span<std::complex<double>>(untouched_a),fresh_view,{},"non-power-of-two FFT input accepted");
    std::array<std::complex<double>,16> short_input{};
    expect_fft_rejection_preserves(std::span(short_input),fresh_view,{},"FFT input larger than tables accepted");
    const DoubleTableView malformed_view{16,fresh_view.forward,fresh_view.inverse};
    std::array<std::complex<double>,8> valid_input{};
    expect_fft_rejection_preserves(std::span(valid_input),malformed_view,{},"malformed table view accepted");
    expect_fft_rejection_preserves(std::span(valid_input),DoubleTableView{}, {},"empty table view accepted");
    const DoubleTableView nonpower_view{7,fresh_view.forward,fresh_view.inverse};
    expect_fft_rejection_preserves(std::span(valid_input),nonpower_view,{},"non-power-of-two view accepted");
    const DoubleTableView overlapping_view{8,fresh_view.forward,fresh_view.forward};
    expect_fft_rejection_preserves(std::span(valid_input),overlapping_view,{},"overlapping direction view accepted");
    expect_fft_rejection_preserves(std::span(valid_input),fresh_view,cancelled.get_token(),"pre-cancelled table FFT accepted");
    std::array<std::complex<double>,8> alias_input{};
    std::array<std::complex<double>,7> alias_inverse{};
    const auto alias_view=prepare_double_tables(std::span(alias_input).first(7),alias_inverse);
    const auto alias_before=alias_input;
    expect_error([&]{fft(std::span(alias_input),false,alias_view);},"FFT input/table overlap accepted");
    if(!bytes_equal(alias_input,alias_before))throw Error("overlapping FFT input was written");

    std::array<std::complex<double>,7> independent_forward{};
    std::array<std::complex<double>,8> inverse_alias_input{};
    const auto inverse_alias_view=prepare_double_tables(independent_forward,std::span(inverse_alias_input).first(7));
    const auto inverse_alias_before=inverse_alias_input;
    expect_error([&]{fft(std::span(inverse_alias_input),false,inverse_alias_view);},
        "FFT input/inverse-table overlap accepted for forward execution");
    if(!bytes_equal(inverse_alias_input,inverse_alias_before))throw Error("inverse-aliased FFT input was written");
    std::array<std::complex<double>,14> adjacent_tables{};
    const auto adjacent_view=prepare_double_tables(std::span(adjacent_tables).first(7),std::span(adjacent_tables).subspan(7));
    std::array<std::complex<double>,8> adjacent_input{};adjacent_input[0]={1,0};
    auto adjacent_expected=adjacent_input;detail::pattern_fft(adjacent_expected,false,{});
    fft(std::span(adjacent_input),false,adjacent_view);
    if(!bytes_equal(adjacent_input,adjacent_expected))throw Error("adjacent disjoint direction tables changed FFT");

    // Boundaries for the small borrowed SIMD gate. Expected values come from
    // the unchanged legacy transform; custom-table cases use its scalar
    // table-driven operation order below.
    auto component_equal=[](double a,double b){
        if(std::isnan(a)||std::isnan(b))return std::isnan(a)&&std::isnan(b);
        return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);
    };
    auto logical_equal=[&](const auto& a,const auto& b){
        if(a.size()!=b.size())return false;
        for(std::size_t i=0;i<a.size();++i)
            if(!component_equal(a[i].real(),b[i].real()) ||
               !component_equal(a[i].imag(),b[i].imag()))return false;
        return true;
    };
    auto compare_legacy=[&](const auto& input,bool inverse,const DoubleTableView& view){
        auto expected=input,actual=input;
        detail::pattern_fft(expected,inverse,{});
        fft(actual,inverse,view);
        if(!logical_equal(actual,expected))throw Error("borrowed FFT differs from legacy transform");
    };
    auto borrowed_reference=[](std::span<std::complex<double>> a,bool inverse,
                               const DoubleTableView& view){
        const auto n=a.size();
        for(std::size_t i=1,j=0;i<n;++i){
            auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
            if(i<j)std::swap(a[i],a[j]);
        }
        for(std::size_t length=2;length<=n;length*=2){
            const auto half=length/2;
            const auto* table=(inverse?view.inverse:view.forward).data()+half-1;
            for(std::size_t begin=0;begin<n;begin+=length)
                for(std::size_t j=0;j<half;++j){
                    const auto u=a[begin+j],v=a[begin+j+half]*table[j];
                    a[begin+j]={u.real()+v.real(),u.imag()+v.imag()};
                    a[begin+j+half]={u.real()-v.real(),u.imag()-v.imag()};
                }
            if(length==n)break;
        }
        if(inverse)for(auto& value:a)value/=static_cast<double>(n);
    };
    auto compare_custom=[&](const auto& input,bool inverse,const DoubleTableView& view){
        auto expected=input,actual=input;
        borrowed_reference(expected,inverse,view);
        fft(actual,inverse,view);
        if(!logical_equal(actual,expected))throw Error("borrowed FFT custom-table result differs");
    };

    constexpr std::uint64_t sign_bit=0x8000000000000000ULL;
    constexpr std::uint64_t mantissa_bits=0x000fffffffffffffULL;
    auto full_mantissa=[&](std::uint64_t bits){
        return std::bit_cast<double>((bits&sign_bit)|(1022ULL<<52)|(bits&mantissa_bits));
    };
    for(const std::size_t n:{4U,8U,256U,512U,8192U,16384U}){
        std::vector<std::complex<double>> full(n),zero_signs(n),subnormal(n);
        for(std::size_t i=0;i<n;++i){
            full[i]={full_mantissa(random()),full_mantissa(random())};
            zero_signs[i]={(i&1)?-0.0:0.0,(i&2)?-0.0:0.0};
            const double edge=i%3==0?std::numeric_limits<double>::min():
                i%3==1?std::numeric_limits<double>::denorm_min():
                    std::nextafter(std::numeric_limits<double>::min(),0.0);
            subnormal[i]={(i&1)?-edge:edge,(i&2)?-edge:edge};
        }
        for(const bool inverse:{false,true}){
            compare_legacy(full,inverse,view16384);
            compare_legacy(zero_signs,inverse,view16384);
            compare_legacy(subnormal,inverse,view16384);
        }
    }

    // Check the inclusive input bound and its adjacent value above it.
    const double input_limit=std::numeric_limits<double>::max()/(8.0*4*4*4);
    for(const double peak:{input_limit,std::nextafter(input_limit,
                                      std::numeric_limits<double>::infinity())}){
        std::array<std::complex<double>,4> input{};
        input[0]={peak,-peak};
        for(const bool inverse:{false,true})compare_legacy(input,inverse,view16384);
    }

    // Arbitrary views exercise table-component eligibility without comparing
    // against unrelated generated twiddles.
    for(const double component:{2.0,std::nextafter(2.0,
                                      std::numeric_limits<double>::infinity())}){
        std::vector<std::complex<double>> forward{{1,0},{component,0},{1,component}};
        std::vector<std::complex<double>> inverse=forward;
        const DoubleTableView custom{4,forward,inverse};
        const std::array<std::complex<double>,4> input{{{1,.25},{-2,3},{4,-1},{.5,2}}};
        for(const bool backwards:{false,true})compare_custom(input,backwards,custom);
    }
    for(const double exceptional:{std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::quiet_NaN()}){
        for(const bool backwards:{false,true}){
            std::vector<std::complex<double>> forward{{1,0},{1,0},{1,0}};
            std::vector<std::complex<double>> inverse=forward;
            auto& selected=backwards?inverse:forward;
            selected[1].real(exceptional);
            const DoubleTableView custom{4,forward,inverse};
            const std::array<std::complex<double>,4> input{{{1,.25},{-2,3},{4,-1},{.5,2}}};
            compare_custom(input,backwards,custom);
        }
        auto input=std::array<std::complex<double>,4>{{{1,.25},{-2,3},{4,-1},{.5,2}}};
        input[0].real(exceptional);
        for(const bool backwards:{false,true})compare_legacy(input,backwards,view16384);
    }

    // Fixed directional tables remain the oracle input when changing the
    // environment; regenerating twiddles under that mode is a different FFT.
    // Restore the complete process floating-point environment even if a
    // comparison throws. Change FE rounding and MXCSR controls independently.
    struct RestoreFpEnvironment {
        std::fenv_t environment{};
#if defined(TEST_SEARCH_FFT_HAS_MXCSR)
        unsigned mxcsr=0;
#endif
        RestoreFpEnvironment(){
            if(std::fegetenv(&environment)!=0)throw Error("cannot save floating-point environment");
#if defined(TEST_SEARCH_FFT_HAS_MXCSR)
            mxcsr=_mm_getcsr();
#endif
        }
        ~RestoreFpEnvironment(){
            (void)std::fesetenv(&environment);
#if defined(TEST_SEARCH_FFT_HAS_MXCSR)
            _mm_setcsr(mxcsr);
#endif
        }
    } restore_fp_environment;
    if(std::fesetround(FE_TONEAREST)!=0)throw Error("cannot set nearest rounding mode");
#if defined(TEST_SEARCH_FFT_HAS_MXCSR)
    constexpr unsigned mxcsr_controls=(3U<<13)|(1U<<15)|(1U<<6);
    _mm_setcsr(_mm_getcsr()&~mxcsr_controls);
#endif
    std::array<std::complex<double>,8> mode_input{};
    for(auto& value:mode_input)value={full_mantissa(random()),full_mantissa(random())};
    for(const int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
        if(std::fesetround(mode)!=0)throw Error("cannot set requested rounding mode");
        compare_custom(mode_input,false,view16384);
        compare_custom(mode_input,true,view16384);
    }
#if defined(TEST_SEARCH_FFT_HAS_MXCSR)
    if(std::fesetround(FE_TONEAREST)!=0)throw Error("cannot restore nearest rounding mode");
    const auto nearest_mxcsr=_mm_getcsr()&~mxcsr_controls;
    std::array<std::complex<double>,8> mxcsr_input{};
    for(std::size_t i=0;i<mxcsr_input.size();++i){
        const double edge=i%3==0?std::numeric_limits<double>::min():
            i%3==1?std::numeric_limits<double>::denorm_min():
                std::nextafter(std::numeric_limits<double>::min(),0.0);
        mxcsr_input[i]={(i&1)?-edge:edge,(i&2)?-edge:edge};
    }
    for(const unsigned rounding:{1U,2U,3U}){
        _mm_setcsr(nearest_mxcsr|(rounding<<13));
        compare_custom(mxcsr_input,false,view16384);
    }
    for(const unsigned control:{1U<<15,1U<<6,(1U<<15)|(1U<<6)}){
        _mm_setcsr(nearest_mxcsr|control);
        compare_custom(mxcsr_input,true,view16384);
    }
#endif

    std::cout<<"table scalar/SIMD exact checked="<<checked<<" float_relative_l2="<<max_float_l2
        <<" legacy_double_relative_l2="<<max_double_l2<<" dispatch="<<detail::search_fft::kernel_name()<<" cancellation=pass\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

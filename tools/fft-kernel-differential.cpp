#define _GNU_SOURCE
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <time.h>
#include <vector>
#include <xmmintrin.h>
using Complex = std::complex<double>;
extern "C" void baseline_prepare(Complex*, Complex*, std::size_t);
extern "C" void candidate_prepare(Complex*, Complex*, std::size_t);
extern "C" void baseline_run(Complex*, std::size_t, bool, const Complex*, const Complex*, std::size_t, std::stop_token);
extern "C" void candidate_run(Complex*, std::size_t, bool, const Complex*, const Complex*, std::size_t, std::stop_token);
namespace {
constexpr unsigned kMxcsrStatus = 0x3f;
constexpr unsigned kMxcsrDaz = 1u << 6;
constexpr unsigned kMxcsrExceptionMasks = 0x3f << 7;
constexpr unsigned kMxcsrRounding = 3u << 13;
constexpr unsigned kMxcsrFtz = 1u << 15;
constexpr std::array<std::string_view, 4> kRecipes{"ordinary", "signed_zero", "tiny_normal", "subnormal"};
constexpr std::array<std::string_view, 4> kVariants{"baseline", "candidate", "candidate", "baseline"};
struct EnvironmentGuard {
    fenv_t env{}; unsigned mxcsr{};
    EnvironmentGuard() { if (fegetenv(&env) != 0) throw std::runtime_error("fegetenv failed"); mxcsr = _mm_getcsr(); }
    ~EnvironmentGuard() { fesetenv(&env); _mm_setcsr(mxcsr); }
};
struct Controls { int rounding{}; int traps{}; unsigned mxcsr{}; };
struct Observation { std::vector<Complex> output; int fe_flags{}; unsigned mxcsr_flags{}; };
std::uint64_t clock_ns(clockid_t id) {
    timespec ts{};
    if (clock_gettime(id, &ts) != 0) throw std::runtime_error("clock_gettime failed");
    return std::uint64_t(ts.tv_sec)*1'000'000'000ULL + std::uint64_t(ts.tv_nsec);
}
std::uint64_t steady_ns() {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
void clear_status() {
    if (feclearexcept(FE_ALL_EXCEPT) != 0) throw std::runtime_error("feclearexcept failed");
    _mm_setcsr(_mm_getcsr() & ~kMxcsrStatus);
}
Observation observe(const std::vector<Complex>& input, bool candidate, std::size_t n, const std::vector<Complex>& forward, const std::vector<Complex>& backward) {
    Observation o; o.output=input; clear_status();
    if(candidate) candidate_run(o.output.data(),n,true,forward.data(),backward.data(),n,std::stop_token{});
    else baseline_run(o.output.data(),n,true,forward.data(),backward.data(),n,std::stop_token{});
    o.fe_flags=fetestexcept(FE_ALL_EXCEPT);o.mxcsr_flags=_mm_getcsr() & kMxcsrStatus;return o;
}
bool same_bits(const std::vector<Complex>& a,const std::vector<Complex>& b) {
    if(a.size()!=b.size()) return false;
    for(std::size_t i=0;i<a.size();++i) if(std::bit_cast<std::uint64_t>(a[i].real())!=std::bit_cast<std::uint64_t>(b[i].real()) || std::bit_cast<std::uint64_t>(a[i].imag())!=std::bit_cast<std::uint64_t>(b[i].imag())) return false;
    return true;
}
std::uint64_t checksum(const std::vector<Complex>& a) {
    std::uint64_t h=14695981039346656037ULL;
    for(const auto& z:a) for(std::uint64_t word:{std::bit_cast<std::uint64_t>(z.real()),std::bit_cast<std::uint64_t>(z.imag())}) for(unsigned shift=0;shift<64;shift+=8){h^=(word>>shift)&0xffu;h*=1099511628211ULL;}
    return h;
}
std::vector<Complex> make_input(std::string_view recipe,std::size_t n) {
    std::vector<Complex> v(n);
    for(std::size_t i=0;i<n;++i){
        const double sign=(i&1)?-1.0:1.0;
        if(recipe=="ordinary") {const int r=int(i%13)-6,q=int((i*7)%11)-5;v[i]={0.75+double(r)/32.0,sign*(0.5+double(q)/32.0)};}
        else if(recipe=="signed_zero") {const auto zero=std::uint64_t((i&1)?(1ULL<<63):0),other=std::uint64_t((i&2)?(1ULL<<63):0);v[i]={std::bit_cast<double>(zero),std::bit_cast<double>(other)};}
        else if(recipe=="tiny_normal") {const double a=double(1+(i%7))*1.0e-100,b=double(1+((i*3)%5))*1.0e-100;v[i]={sign*a,((i&2)?-1.0:1.0)*b};}
        else {const std::uint64_t a=1+(i%31),b=1+((i*3)%29);const auto sa=a|((i&1)?(1ULL<<63):0),sb=b|((i&2)?(1ULL<<63):0);v[i]={std::bit_cast<double>(sa),std::bit_cast<double>(sb)};}
    }
    return v;
}
void require(bool c,const char* message){if(!c)throw std::runtime_error(message);}
Controls check_environment(){
    Controls c{};c.rounding=fegetround();c.traps=fegetexcept();const unsigned csr=_mm_getcsr();c.mxcsr=csr & ~kMxcsrStatus;
    require(c.rounding==FE_TONEAREST,"rounding mode must be nearest");require(c.traps==0,"floating-point traps must be disabled");
    require((csr&kMxcsrRounding)==0,"MXCSR rounding must be nearest");require((csr&(kMxcsrDaz|kMxcsrFtz))==0,"MXCSR DAZ and FTZ must be disabled");
    require((csr&kMxcsrExceptionMasks)==kMxcsrExceptionMasks,"MXCSR exceptions must be masked");return c;
}
void emit_environment(const Controls& c,std::size_t iterations){std::cout<<"{\"type\":\"environment\",\"rounding\":"<<c.rounding<<",\"traps\":"<<c.traps<<",\"mxcsr_control\":"<<c.mxcsr<<",\"iterations\":"<<iterations<<"}\n";}
void emit_block(std::size_t n,std::string_view recipe,std::string_view variant,unsigned order,std::size_t iterations,std::uint64_t wall,std::uint64_t cpu,int fe_flags,unsigned mxcsr_flags,const Controls& c,std::uint64_t hash){
    std::cout<<"{\"type\":\"block\",\"n\":"<<n<<",\"recipe\":\""<<recipe<<"\",\"variant\":\""<<variant<<"\",\"order\":"<<order<<",\"iterations\":"<<iterations<<",\"wall_ns\":"<<wall<<",\"thread_cpu_ns\":"<<cpu<<",\"fe_flags\":"<<fe_flags<<",\"mxcsr_flags\":"<<mxcsr_flags<<",\"rounding\":"<<c.rounding<<",\"traps\":"<<c.traps<<",\"mxcsr_control\":"<<c.mxcsr<<",\"output_exact\":true,\"output_hash_fnv64\":\""<<std::hex<<std::setw(16)<<std::setfill('0')<<hash<<std::dec<<std::setfill(' ')<<"\"}\n";
}
void run_block(std::size_t n,std::string_view recipe,std::string_view variant,unsigned order,std::size_t iterations,const std::vector<Complex>& input,const std::vector<Complex>& expected,int expected_fe_flags,unsigned expected_mxcsr_flags,const std::vector<Complex>& forward,const std::vector<Complex>& backward,const Controls& controls){
    const bool candidate=variant=="candidate";std::vector<Complex> work(n);clear_status();const auto wall0=steady_ns();const auto cpu0=clock_ns(CLOCK_THREAD_CPUTIME_ID);
    for(std::size_t i=0;i<iterations;++i){std::copy(input.begin(),input.end(),work.begin());if(candidate)candidate_run(work.data(),n,true,forward.data(),backward.data(),n,std::stop_token{});else baseline_run(work.data(),n,true,forward.data(),backward.data(),n,std::stop_token{});}
    const auto cpu1=clock_ns(CLOCK_THREAD_CPUTIME_ID);const auto wall1=steady_ns();const int fe_flags=fetestexcept(FE_ALL_EXCEPT);const unsigned mxcsr_flags=_mm_getcsr() & kMxcsrStatus;
    require(same_bits(work,expected),"timed output differs from baseline");require(fe_flags==expected_fe_flags && mxcsr_flags==expected_mxcsr_flags,"timed exception flags differ from warm baseline");
    emit_block(n,recipe,variant,order,iterations,wall1-wall0,cpu1-cpu0,fe_flags,mxcsr_flags,controls,checksum(work));
}
std::size_t parse_iterations(int argc,char**argv){
    if(argc==1)return 4096;require(argc==3&&std::string_view(argv[1])=="--iterations","usage: harness [--iterations N]");const std::string_view text(argv[2]);require(!text.empty(),"iteration count is empty");std::size_t value=0;
    for(char ch:text){require(ch>='0'&&ch<='9',"iteration count must be decimal");value=value*10+std::size_t(ch-'0');require(value<=16384,"iteration count exceeds 16384");}require(value>=1,"iteration count must be at least 1");return value;
}
}
int main(int argc,char**argv){
    try{
        EnvironmentGuard restore_environment;const std::size_t requested=parse_iterations(argc,argv);const Controls controls=check_environment();emit_environment(controls,requested);std::uint64_t block_count=0;
        for(std::size_t n:{std::size_t(256),std::size_t(512)}){
            std::vector<Complex> forward(n-1),backward(n-1);baseline_prepare(forward.data(),backward.data(),n);std::vector<Complex> candidate_forward(n-1),candidate_backward(n-1);candidate_prepare(candidate_forward.data(),candidate_backward.data(),n);
            require(same_bits(forward,candidate_forward)&&same_bits(backward,candidate_backward),"prepared tables differ bitwise");
            for(std::string_view recipe:kRecipes){
                const std::vector<Complex> input=make_input(recipe,n);const Observation warm_base=observe(input,false,n,forward,backward),warm_candidate=observe(input,true,n,forward,backward);
                require(same_bits(warm_base.output,warm_candidate.output),"warm outputs differ bitwise");require(warm_base.fe_flags==warm_candidate.fe_flags&&warm_base.mxcsr_flags==warm_candidate.mxcsr_flags,"warm exception flags differ");
                const std::size_t iterations=recipe=="subnormal"?std::min<std::size_t>(requested,256):requested;
                for(unsigned order=0;order<kVariants.size();++order){run_block(n,recipe,kVariants[order],order,iterations,input,warm_base.output,warm_base.fe_flags,warm_base.mxcsr_flags,forward,backward,controls);++block_count;}
            }
            baseline_prepare(candidate_forward.data(),candidate_backward.data(),n);require(same_bits(forward,candidate_forward)&&same_bits(backward,candidate_backward),"baseline tables changed during use");
            candidate_prepare(candidate_forward.data(),candidate_backward.data(),n);require(same_bits(forward,candidate_forward)&&same_bits(backward,candidate_backward),"candidate tables changed during use");
        }
        require(block_count==32,"unexpected block count");std::cout<<"{\"type\":\"complete\",\"blocks\":"<<block_count<<",\"environment_rows\":1,\"complete\":true}\n";return 0;
    }catch(const std::exception&e){std::cerr<<"harness error: "<<e.what()<<'\n';return 2;}
}

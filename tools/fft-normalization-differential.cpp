#define _GNU_SOURCE
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <time.h>
#include <vector>
#include <xmmintrin.h>
using Complex=std::complex<double>;
extern "C" void baseline_prepare(Complex*,Complex*,std::size_t);
extern "C" void candidate_prepare(Complex*,Complex*,std::size_t);
extern "C" void baseline_run(Complex*,std::size_t,bool,const Complex*,const Complex*,std::size_t,std::stop_token);
extern "C" void candidate_run(Complex*,std::size_t,bool,const Complex*,const Complex*,std::size_t,std::stop_token);
extern "C" void baseline_normalize(Complex*,std::size_t,std::stop_token);
extern "C" void candidate_normalize(Complex*,std::size_t,bool,std::stop_token);
namespace {
constexpr unsigned status_mask=0x3f;
void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
void clear_flags(){need(feclearexcept(FE_ALL_EXCEPT)==0,"feclearexcept failed");_mm_setcsr(_mm_getcsr()&~status_mask);}
std::pair<int,unsigned> flags(){return {fetestexcept(FE_ALL_EXCEPT),_mm_getcsr()&status_mask};}
std::uint64_t cpu_ns(){timespec t{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t))throw std::runtime_error("thread CPU clock failed");return std::uint64_t(t.tv_sec)*1000000000ULL+std::uint64_t(t.tv_nsec);}
std::uint64_t wall_ns(){return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
bool bits_equal(const std::vector<Complex>& a,const std::vector<Complex>& b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(std::bit_cast<std::uint64_t>(a[i].real())!=std::bit_cast<std::uint64_t>(b[i].real())||std::bit_cast<std::uint64_t>(a[i].imag())!=std::bit_cast<std::uint64_t>(b[i].imag()))return false;return true;}
std::uint64_t hash_bits(const std::vector<Complex>& a){std::uint64_t h=14695981039346656037ULL;for(const auto& z:a)for(auto w:{std::bit_cast<std::uint64_t>(z.real()),std::bit_cast<std::uint64_t>(z.imag())})for(unsigned s=0;s<64;s+=8){h^=(w>>s)&255U;h*=1099511628211ULL;}return h;}
std::size_t iterations(int argc,char** argv){if(argc==1)return 4096;need(argc==3&&std::string_view(argv[1])=="--iterations","usage: harness [--iterations N]");std::size_t x=0;for(char c:std::string_view(argv[2])){need(c>='0'&&c<='9',"iterations must be decimal");x=x*10+std::size_t(c-'0');need(x<=16384,"iterations exceeds 16384");}need(x>0,"iterations must be positive");return x;}
std::vector<Complex> input_for(std::string_view recipe,std::size_t n){std::vector<Complex> a(n);for(std::size_t i=0;i<n;++i){if(recipe=="ordinary"){a[i]={0.75+double(int(i%13)-6)/32.0,0.5+double(int((i*7)%11)-5)/32.0};}else{const double s=(i&1)?-1.0:1.0;a[i]={s*double(1+i%7)*1.0e-100,s*double(1+(i*3)%5)*1.0e-100};}}return a;}
std::size_t normalization_controls(){
 need(__builtin_cpu_supports("avx2"),"AVX2 unavailable for normalization diagnostic");
 constexpr std::array<std::uint64_t,17> words={0ULL,0x8000000000000000ULL,1ULL,2ULL,3ULL,127ULL,128ULL,129ULL,383ULL,384ULL,385ULL,0x0010000000000000ULL,0x0010000000000001ULL,0x8010000000000000ULL,0x8000000000000001ULL,0x7fefffffffffffffULL,0xffefffffffffffffULL};
 std::size_t controls=0;
 for(std::size_t n:{std::size_t(4),std::size_t(256),std::size_t(8192)})for(bool vectorized:{false,true}){
  std::vector<double> cases;for(auto w:words)cases.push_back(std::bit_cast<double>(w));
  // A half-subnormal tie rounds up to minimum normal after division by n.
  const double boundary=std::numeric_limits<double>::min()*static_cast<double>(n);
  for(double x:{std::nextafter(boundary,0.0),boundary,std::nextafter(boundary,std::numeric_limits<double>::infinity()),std::numeric_limits<double>::denorm_min()*static_cast<double>(n/2)}){cases.push_back(x);cases.push_back(-x);}
  for(std::size_t offset=0;offset<cases.size();offset+=2*n){
   std::vector<Complex> original(n),reference,trial;
   for(std::size_t i=0;i<n;++i)original[i]={cases[(offset+2*i)%cases.size()],cases[(offset+2*i+1)%cases.size()]};
   reference=original;trial=original;clear_flags();baseline_normalize(reference.data(),n,std::stop_token{});const auto baseline_flags=flags();
   clear_flags();candidate_normalize(trial.data(),n,vectorized,std::stop_token{});const auto candidate_flags=flags();
   need(bits_equal(reference,trial),"isolated inverse normalization bits differ");need(baseline_flags==candidate_flags,"isolated inverse normalization flags differ");
  }
  ++controls;
 }
 return controls;
}
void run_block(std::size_t n,std::string_view recipe,std::string_view variant,unsigned order,std::size_t count,const std::vector<Complex>& input,const std::vector<Complex>& expected,const std::pair<int,unsigned>& expected_flags,const std::vector<Complex>& f,const std::vector<Complex>& b){
 std::vector<Complex> work(n);const bool cand=variant=="candidate";clear_flags();const auto w0=wall_ns(),c0=cpu_ns();for(std::size_t i=0;i<count;++i){std::copy(input.begin(),input.end(),work.begin());if(cand)candidate_run(work.data(),n,true,f.data(),b.data(),n,std::stop_token{});else baseline_run(work.data(),n,true,f.data(),b.data(),n,std::stop_token{});}const auto c1=cpu_ns(),w1=wall_ns();const auto got_flags=flags();need(bits_equal(work,expected),"full FFT output differs");need(got_flags==expected_flags,"full FFT flags differ");
 std::cout<<"{\"type\":\"block\",\"n\":"<<n<<",\"recipe\":\""<<recipe<<"\",\"variant\":\""<<variant<<"\",\"order\":"<<order<<",\"iterations\":"<<count<<",\"wall_ns\":"<<w1-w0<<",\"thread_cpu_ns\":"<<c1-c0<<",\"fe_flags\":"<<got_flags.first<<",\"mxcsr_flags\":"<<got_flags.second<<",\"output_exact\":true,\"hash_fnv64\":\""<<std::hex<<std::setw(16)<<std::setfill('0')<<hash_bits(work)<<std::dec<<std::setfill(' ')<<"\"}\n";
}
}
int main(int argc,char** argv){try{
 const auto count=iterations(argc,argv);need(fegetround()==FE_TONEAREST&&fegetexcept()==0,"requires nearest nontrapping FP environment");const unsigned csr=_mm_getcsr();need((csr&((1U<<15)|(1U<<6)|(3U<<13)|(0x3fU<<7)))==(0x3fU<<7),"requires masked nearest MXCSR with FTZ/DAZ off");const auto controls=normalization_controls();
 std::cout<<"{\"type\":\"environment\",\"iterations\":"<<count<<",\"avx2\":true,\"normalization_controls\":"<<controls<<"}\n";std::size_t blocks=0;
 for(std::size_t n:{std::size_t(256),std::size_t(512)}){std::vector<Complex> f(n-1),b(n-1),cf(n-1),cb(n-1);baseline_prepare(f.data(),b.data(),n);candidate_prepare(cf.data(),cb.data(),n);need(bits_equal(f,cf)&&bits_equal(b,cb),"forward/inverse tables differ");for(auto recipe:{std::string_view("ordinary"),std::string_view("tiny_normal")}){auto in=input_for(recipe,n);auto ref=in,trial=in;clear_flags();baseline_run(ref.data(),n,true,f.data(),b.data(),n,std::stop_token{});const auto expected_flags=flags();clear_flags();candidate_run(trial.data(),n,true,cf.data(),cb.data(),n,std::stop_token{});need(bits_equal(ref,trial),"warm full FFT output differs");need(expected_flags==flags(),"warm full FFT flags differ");need(__builtin_cpu_supports("avx2"),"AVX2 kernel is unavailable");for(unsigned order=0;order<4;++order){const std::string_view variant=(order==0||order==3)?"baseline":"candidate";run_block(n,recipe,variant,order,count,in,ref,expected_flags,f,b);++blocks;}}need(bits_equal(f,cf)&&bits_equal(b,cb),"tables changed during FFT use");}
 need(blocks==16,"incomplete ABBA block inventory");std::cout<<"{\"type\":\"complete\",\"blocks\":16,\"normalization_controls\":"<<controls<<",\"complete\":true}\n";return 0;
 }catch(const std::exception& e){std::cerr<<"normalization diagnostic: "<<e.what()<<'\n';return 2;}}

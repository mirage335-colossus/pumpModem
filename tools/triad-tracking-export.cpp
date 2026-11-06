#include "datapump/continuous_clock.hpp"
#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_shaping.hpp"
#include "datapump/tuning.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

// Scheduled DATA-only qualification. No noise or receiver is hidden here:
// practical affine templates and channel truth have separate file identities.
namespace {
using namespace datapump;
using namespace datapump::triad::v3;
namespace fs = std::filesystem;
constexpr long double pi = std::numbers::pi_v<long double>;
#ifdef DATAPUMP_TRIAD_TRACKING_DRIVER_SHA256
constexpr const char* compiled_driver_sha = DATAPUMP_TRIAD_TRACKING_DRIVER_SHA256;
#else
constexpr const char* compiled_driver_sha = "not-embedded";
#endif
struct Options {
    Format format{};
    std::string output, clock = "periodic", spread = "none", bit = "both";
    double data_if = -1, rf = 0, seconds = 2*86400., phase = 0;
    double width = 0, excursion_ms = 0, knot = 3600, slow = 0, fast = 0;
    std::optional<double> selective_rate;
    std::optional<double> clock_drift_ppm, gps_holdover_seconds;
    std::vector<double> ppm{-100,0,100};
    std::uint64_t seed = 1, key_seed = 1, cap = 2ULL*1024*1024*1024;
    std::size_t chunk = 4096;
    bool raw = false, basis_server = false;
    std::optional<std::array<double,2>> window;
};
double number(const std::string& s) {
    std::size_t end=0; const double x=std::stod(s,&end);
    if(end!=s.size() || !std::isfinite(x)) throw std::invalid_argument("expected finite number");
    return x;
}
std::uint64_t integer(const std::string& s) {
    if(s.empty() || s[0]=='-') throw std::invalid_argument("expected unsigned integer");
    std::size_t end=0; const auto x=std::stoull(s,&end);
    if(end!=s.size()) throw std::invalid_argument("expected unsigned integer");
    return x;
}
std::uint32_t u32(const std::string& s) {
    const auto n=integer(s); if(n>UINT32_MAX) throw std::invalid_argument("integer exceeds uint32");
    return static_cast<std::uint32_t>(n);
}
Options parse(int argc,char** argv) {
    Options o; o.format.inner_rate_hz=.001; o.format.added_symbols=1; o.format.sample_rate=1;
    for(int i=1;i<argc;++i) {
        const std::string a=argv[i]; auto next=[&](){if(++i==argc)throw std::invalid_argument("missing value for "+a);return std::string(argv[i]);};
        if(a=="--rate")o.format.inner_rate_hz=number(next());
        else if(a=="--added")o.format.added_symbols=u32(next());
        else if(a=="--sample-rate")o.format.sample_rate=u32(next());
        else if(a=="--sample-rate-denominator")o.format.sample_rate_denominator=u32(next());
        else if(a=="--seconds")o.seconds=number(next());
        else if(a=="--days")o.seconds=number(next())*86400.;
        else if(a=="--data-if") {const auto x=next();o.data_if=x=="half-rate"?-1:number(x);}
        else if(a=="--shift" || a=="--rf")o.rf=number(next());
        else if(a=="--bit")o.bit=next();
        else if(a=="--seed")o.seed=integer(next());
        else if(a=="--key-seed")o.key_seed=integer(next());
        else if(a=="--epoch")o.format.epoch=integer(next());
        else if(a=="--output" || a=="--outputdir")o.output=next();
        else if(a=="--max-output-bytes")o.cap=integer(next());
        else if(a=="--chunk")o.chunk=integer(next());
        else if(a=="--clock-profile")o.clock=next();
        else if(a=="--pulse-shape") {
            const auto shape=next();
            if(shape=="legacy")o.format.pulse_shape=PulseShape::LegacyRrc;
            else if(shape=="c2")o.format.pulse_shape=PulseShape::TaperedRrc;
            else throw std::invalid_argument("pulse-shape must be legacy or c2");
        }
        else if(a=="--clock-drift-ppm")o.clock_drift_ppm=number(next());
        else if(a=="--gps-holdover-seconds")o.gps_holdover_seconds=number(next());
        else if(a=="--spread")o.spread=next();
        else if(a=="--random-doppler-width-hz")o.width=number(next());
        else if(a=="--delay-excursion-ms")o.excursion_ms=number(next());
        else if(a=="--delay-knot-seconds")o.knot=number(next());
        else if(a=="--selective-slow-depth")o.slow=number(next());
        else if(a=="--selective-fast-depth")o.fast=number(next());
        else if(a=="--selective-each-depth")o.slow=o.fast=number(next());
        else if(a=="--selective-modulation-rate-hz")o.selective_rate=number(next());
        else if(a=="--phase-diffusion")o.phase=number(next());
        else if(a=="--raw-sources")o.raw=true;
        else if(a=="--basis-server")o.basis_server=true;
        else if(a=="--source-window")o.window=std::array<double,2>{number(next()),number(next())};
        else if(a=="--template-ppm") {o.ppm.clear();std::stringstream ss(next());std::string x;while(std::getline(ss,x,','))o.ppm.push_back(number(x));}
        else if(a=="--help") {
            std::cout<<"DATA-only triad tracking export: --output DIR --rate HZ --added N --seconds S|--days D\n"
                "--sample-rate INTEGER [--sample-rate-denominator N] --data-if half-rate|HZ --shift HZ\n"
                "--clock-profile off|periodic|wandering|gps-disciplined --template-ppm -100,0,100 --spread none|sub9best|sub9moderate|sub9worst\n"
                "--pulse-shape legacy|c2 (default legacy; shape only, no other planner geometry changes)\n"
                "--clock-drift-ppm P (GPS profile: default20, signed system-oscillator bound) --gps-holdover-seconds S (default86400, weekly)\n"
                "--random-doppler-width-hz B --delay-excursion-ms A --delay-knot-seconds T\n"
                "--selective-each-depth 0..0.65 (or slow/fast-depth) --selective-modulation-rate-hz B\n"
                "--phase-diffusion RAD/SQRT(S) --seed N --key-seed N --epoch N --bit both|0|1\n"
                "--raw-sources --source-window ABSOLUTE_START_SECONDS LENGTH_SECONDS --chunk N --max-output-bytes N\n";
            std::cout<<"--basis-server --clock-profile off: persistent public exact native basis protocol (no channel)\n";
            std::exit(0);
        } else throw std::invalid_argument("unknown option: "+a);
    }
    if((o.output.empty() && !o.basis_server) || o.seconds<=0 || o.seconds>10*366*86400. || !std::isfinite(o.seconds) ||
       o.chunk==0 || o.chunk>1048576 || o.cap==0 || o.ppm.empty() || o.ppm.size()>257 ||
       (o.clock!="off" && o.clock!="periodic" && o.clock!="wandering" && o.clock!="gps-disciplined") ||
       (o.bit!="both" && o.bit!="0" && o.bit!="1"))throw std::invalid_argument("invalid export limits or clock/bit selection");
    if(o.data_if==-1)o.data_if=o.format.inner_rate_hz/2;
    if((o.clock_drift_ppm || o.gps_holdover_seconds) && o.clock!="gps-disciplined")
        throw std::invalid_argument("clock-drift-ppm and gps-holdover-seconds require gps-disciplined profile");
    if((o.clock_drift_ppm && std::abs(*o.clock_drift_ppm)>200) ||
       (o.gps_holdover_seconds && (*o.gps_holdover_seconds<0 || *o.gps_holdover_seconds>3*86400.)))
        throw std::invalid_argument("GPS clock drift or holdover exceeds diagnostic bounds");
    if(o.data_if<0 || o.rf<0 || o.width<0 || o.excursion_ms<0 || o.knot<=0 || o.phase<0 ||
       o.slow<0 || o.slow>.65 || o.fast<0 || o.fast>.65 || (o.selective_rate && *o.selective_rate<0))
        throw std::invalid_argument("invalid physical channel setting");
    for(double p:o.ppm)if(std::abs(p)>1000)throw std::invalid_argument("public template ppm exceeds 1000");
    if(o.window && ((*o.window)[0]<0 || (*o.window)[1]<=0))throw std::invalid_argument("invalid source window");
    if(o.window && (o.spread!="none" || o.width || o.excursion_ms || o.slow || o.fast || o.phase))
        throw std::invalid_argument("source-window is source-only; channel effects are unsupported there");
    if(o.basis_server && (o.clock!="off" || o.spread!="none" || o.width || o.excursion_ms || o.slow || o.fast || o.phase ||
        o.selective_rate.value_or(0)!=0 || o.window || o.raw || o.bit!="both" || !o.output.empty()))
        throw std::invalid_argument("basis-server requires clock-profile off, no channel effects, both bits and no output/window/raw export");
    o.format.bit_seconds=o.seconds;
    return o;
}
std::uint64_t mix(std::uint64_t x) {x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
double uniform(std::uint64_t seed,std::uint64_t index) {return (mix(seed^mix(index))>>11)*0x1.0p-53;}
Crypto key(std::uint64_t seed) {
    std::seed_seq seq{static_cast<std::uint32_t>(seed),static_cast<std::uint32_t>(seed>>32),0U,0U};
    std::mt19937 rng(seq);std::array<std::uint8_t,32> bytes{};for(auto& b:bytes)b=static_cast<std::uint8_t>(rng());return Crypto(bytes);
}
std::string sha(const fs::path& path) {
    std::ifstream f(path,std::ios::binary);if(!f)return "unavailable";
    auto ctx=std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    if(!ctx || EVP_DigestInit_ex(ctx.get(),EVP_sha256(),nullptr)!=1)throw std::runtime_error("SHA initialization failed");
    std::array<char,65536> data{};while(f.read(data.data(),data.size()) || f.gcount())
        if(EVP_DigestUpdate(ctx.get(),data.data(),static_cast<std::size_t>(f.gcount()))!=1)throw std::runtime_error("SHA update failed");
    unsigned length=0;std::array<unsigned char,EVP_MAX_MD_SIZE> digest{};
    if(EVP_DigestFinal_ex(ctx.get(),digest.data(),&length)!=1)throw std::runtime_error("SHA finalization failed");
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(unsigned i=0;i<length;++i)out<<std::setw(2)<<unsigned(digest[i]);return out.str();
}

// Same 1-second controller clock at every export Fs. The historical profiles
// deliberately apply independent UTC steps to ideal monotonic/device clocks.
// gps-disciplined instead drifts monotonic and UTC together, then corrects UTC
// toward physical time. ADC/DAC frame error stays explicitly zero: q therefore
// measures that one system-oscillator drift rather than counting it twice.
class ClockMap {
    audio::ContinuousClock clock_{1,false};
    const Options& o_; std::uint64_t tick_=0,hour_=1,quarter_=1;
    // Integrate only the small displacement, not a growing multi-day time.
    long double displacement_=0,utc_offset_=0,last_physical_=0,last_logical_=0; double c_=0,target_=0;
    std::uint64_t next_hour()const {return hour_*3600+static_cast<std::int64_t>(600*uniform(o_.seed^0x484f5552,hour_))-300;}
    long double monotonic_offset(long double t) const {
        if(o_.clock!="gps-disciplined")return 0;
        const long double omega=2*pi/(7*86400.L),phase=2*pi*uniform(o_.seed^0x47505344,0);
        const long double drift=o_.clock_drift_ppm.value_or(20)*1e-6L;
        return drift*(.85L*t+.15L*(std::sin(omega*t+phase)-std::sin(phase))/omega);
    }
    bool gps_holdover() const {
        const auto within=static_cast<long double>(tick_%(7*86400));
        return within>=3*86400.L && within<3*86400.L+o_.gps_holdover_seconds.value_or(86400);
    }
    std::uint64_t last_gps_fix_=0;
    void observe() {
        const auto monotonic_error=monotonic_offset(tick_);
        if(o_.clock=="gps-disciplined") {
            while(tick_>=next_hour()) {
                if(gps_holdover())++gps_skipped_fixes;
                else {
                    const auto next=-monotonic_error+.001L*(2*uniform(o_.seed^0x47505346,hour_)-1);
                    const double step=static_cast<double>(next-utc_offset_);
                    const auto gap=tick_-last_gps_fix_;
                    gps_max_fix_gap_seconds=std::max(gps_max_fix_gap_seconds,gap);
                    gps_max_step_seconds=std::max(gps_max_step_seconds,std::abs(step));
                    if(gap>4200)gps_max_recovery_step_seconds=std::max(gps_max_recovery_step_seconds,std::abs(step));
                    else gps_max_normal_step_seconds=std::max(gps_max_normal_step_seconds,std::abs(step));
                    utc_offset_=next;last_gps_fix_=tick_;++gps_fixes;
                }
                ++hour_;
            }
        } else if(o_.clock!="off") {
            while(tick_>=next_hour()) {
                const double sign=o_.clock=="periodic"?1:(uniform(o_.seed^0x5349474e,hour_)<.5?-1:1);
                utc_offset_+=sign*(.05+.1*uniform(o_.seed^0x53544550,hour_));
                if(hour_%24==0)utc_offset_+=(uniform(o_.seed^0x4c415247,hour_)<.5?-2:2);
                ++hour_;
            }
            while(tick_>=quarter_*900) {
                if(o_.clock=="wandering")utc_offset_+=(2*uniform(o_.seed^0x51554152,quarter_)-1)*.01;
                ++quarter_;
            }
        }
        const auto utc_error=monotonic_error+utc_offset_;
        max_utc_error=std::max(max_utc_error,std::abs(static_cast<double>(utc_error)));
        audio::FrameTimestamp stamp;stamp.frame=tick_;stamp.steady_seconds=tick_+monotonic_error;
        stamp.utc_seconds=static_cast<long double>(o_.format.epoch)+tick_+utc_error;
        stamp.quality=audio::TimingQuality::bounded;clock_.observe(stamp);
        const auto r=clock_.update(tick_,static_cast<long double>(tick_)+displacement_,c_);
        target_=o_.clock=="off"?0:o_.clock=="gps-disciplined"?r.target_correction:
            std::clamp(r.target_correction,-200e-6,200e-6);
        max_error=std::max(max_error,std::abs(r.phase_error_seconds));
        max_rate=std::max(max_rate,std::abs(c_));
        min_applied_rate=std::min(min_applied_rate,c_);max_applied_rate=std::max(max_applied_rate,c_);
        max_displacement=std::max(max_displacement,std::abs(static_cast<double>(displacement_)));
    }
    std::pair<long double,double> advance(double dt)const {
        const double delta=target_-c_,slope=std::copysign(1e-5,delta),ramp=std::min(dt,std::abs(delta)/1e-5);
        return {dt+c_*ramp+.5*slope*ramp*ramp+target_*(dt-ramp),c_+slope*ramp};
    }
public:
    double max_error=0,max_rate=0,max_utc_error=0,max_displacement=0,min_applied_rate=0,max_applied_rate=0;
    double gps_max_step_seconds=0,gps_max_normal_step_seconds=0,gps_max_recovery_step_seconds=0;
    std::uint64_t gps_fixes=0,gps_skipped_fixes=0,gps_max_fix_gap_seconds=0;
    explicit ClockMap(const Options& o):o_(o){observe();}
    long double at(long double t) {
        if(t<tick_)throw std::logic_error("clock source queried backward");
        last_physical_=t;
        if(o_.clock=="off")return last_logical_=t;
        while(t>=static_cast<long double>(tick_)+1) {const auto step=advance(1);displacement_+=step.first-1;c_=step.second;++tick_;observe();}
        const auto fraction=static_cast<double>(t-tick_);
        return last_logical_=t+displacement_+(advance(fraction).first-fraction);
    }
    std::uint64_t ticks()const{return tick_;}
    long double displacement()const{return last_logical_-last_physical_;}
    long double final_physical_seconds()const{return last_physical_;}
    long double final_utc_error_seconds()const{return monotonic_offset(last_physical_)+utc_offset_;}
};
Complex at(PatternSource& source,long double t,long double u,double fsample,double center,
           long double source_sample_shift) {
    const auto phase=2*pi*std::remainder(static_cast<long double>(center)*(u-t),1.L);
    return source.sample_at(u*fsample+source_sample_shift)*
        Complex(static_cast<double>(std::cos(phase)),static_cast<double>(std::sin(phase)));
}
struct Sink {
    std::string name,role;int bit=0,index=-1;std::ofstream out;long double energy=0;
    Sink(const fs::path& root,std::string n,std::string r,int b,int i=-1):name(std::move(n)),role(std::move(r)),bit(b),index(i),out(root/name,std::ios::binary) {
        if(!out)throw std::runtime_error("cannot open "+name);
    }
    void write(std::span<const Complex> values) {
        std::vector<std::complex<float>> packed;packed.reserve(values.size());
        for(auto x:values){packed.emplace_back(x);energy+=std::norm(x);}
        out.write(reinterpret_cast<const char*>(packed.data()),static_cast<std::streamsize>(packed.size()*sizeof(packed[0])));
        if(!out)throw std::runtime_error("write failed for "+name);
    }
};
std::uint64_t checked_count(long double x) {
    if(!std::isfinite(x) || x<0 || x>UINT64_MAX-32.L)throw std::invalid_argument("sample count overflow");
    return static_cast<std::uint64_t>(std::ceil(x));
}
// Public exact source queries for timing hypotheses. No simulator, unknown
// TX clock realization, oracle coordinates, or interpolation of cached samples.
int serve_basis(const Options& o,const Geometry& g,const Crypto& crypto) {
#ifdef _WIN32
    // Only output carries binary samples; stdin retains its line protocol.
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        throw std::runtime_error("cannot switch basis-server stdout to binary mode");
#endif
    const double sample_rate=effective_sample_rate(o.format);
    const long double canonical_origin=8.L/g.fine_chip_rate_hz;
    const long double shift=g.calendar_origin_sample-canonical_origin*sample_rate;
    std::array<PatternSource,2> source{PatternSource(o.format,crypto,0),PatternSource(o.format,crypto,1)};
    std::cout<<std::setprecision(17)<<"{\"schema\":\"triad-tracking-basis-server-v1\",\"dtype\":\"<c8\",\"layout\":\"bit,state,sample\",\"bits\":2"
        <<",\"sample_rate_hz\":"<<sample_rate<<",\"sample_rate_numerator\":"<<o.format.sample_rate<<",\"sample_rate_denominator\":"<<o.format.sample_rate_denominator
        <<",\"inner_rate_hz\":"<<o.format.inner_rate_hz<<",\"added_symbols\":"<<o.format.added_symbols<<",\"data_if_hz\":"<<o.data_if
        <<",\"pattern_revision\":\""<<pattern_revision_name(o.format.pattern_revision)<<"\",\"public_epoch_seconds\":"<<o.format.epoch<<",\"key_seed\":"<<o.key_seed<<",\"test_key_only\":true"
        <<",\"pulse_shape\":\""<<pulse_shape_name(o.format.pulse_shape)<<"\""
        <<",\"source_calendar\":\"canonical-8chips-v1\",\"canonical_payload_origin_seconds\":"<<static_cast<double>(canonical_origin)
        <<",\"native_source_sample_shift\":"<<static_cast<double>(shift)<<",\"native_calendar_origin_sample\":"<<g.calendar_origin_sample
        <<",\"source_samples\":"<<g.source_samples<<",\"bit_seconds\":"<<g.bit_seconds<<",\"requested_bit_seconds\":"<<o.seconds
        <<",\"maximum_count\":16384,\"maximum_states\":256,\"maximum_response_bytes\":8388608,\"maximum_rate_ppm\":4096,\"compiled_driver_sha256\":\""<<compiled_driver_sha<<"\",\"source_sha256\":\""<<sha(__FILE__)<<"\",\"binary_sha256\":\""<<sha("/proc/self/exe")<<"\"}\n"<<std::flush;
    for(;;) {
        std::string line;char ch=0;
        while(std::cin.get(ch) && ch!='\n') {if(line.size()>=65536)throw std::invalid_argument("basis request line exceeds 65536 bytes");line+=ch;}
        if(!std::cin && line.empty())return 0;
        if(!std::cin)throw std::invalid_argument("unterminated basis request");
        if(line=="quit")return 0;
        std::istringstream input(line);std::string command,first_text,count_text,states_text,anchor_text;
        if(!(input>>command>>first_text>>count_text>>states_text>>anchor_text) || command!="basis")
            throw std::invalid_argument("expected basis FIRST COUNT STATES ANCHOR followed by delay/rate pairs");
        const auto first=integer(first_text),count=integer(count_text),states=integer(states_text);
        const long double anchor=number(anchor_text);
        if(!count || count>16384 || !states || states>256 || count*states>8388608/16 || first>(1ULL<<63)-count)
            throw std::invalid_argument("basis count/state/coordinate or response byte limit exceeded");
        std::vector<std::array<long double,2>> parameters;parameters.reserve(states);
        const long double begin=static_cast<long double>(first)/sample_rate;
        const long double end=static_cast<long double>(first+count-1)/sample_rate;
        for(std::uint64_t k=0;k<states;++k) {
            std::string delay,ppm;if(!(input>>delay>>ppm))throw std::invalid_argument("missing basis delay/rate pair");
            const long double d=number(delay),r=number(ppm)*1e-6L;
            // Independent local/remote software discipline can require the
            // maintained public timing bank's +/-2000ppm extent. This query
            // bound is separate from a transmitter's physical slew limits.
            if(std::abs(r)>.004096L)throw std::invalid_argument("basis rate exceeds 4096 ppm");
            const auto edge=g.occupied_bandwidth_hz*.5L*(1+r)+std::abs(o.data_if*r);
            if(edge>=sample_rate*.5L)
                throw std::invalid_argument("basis candidate exceeds sampled DATA passband");
            for(const auto t:{begin,end}) {
                const auto displacement=d+r*(t-anchor),u=t+displacement,position=u*sample_rate+shift;
                if(!std::isfinite(position) || std::abs(position)>=std::ldexp(1.L,63) ||
                   !std::isfinite(displacement*o.data_if) || std::abs(displacement*o.data_if)>1e12L)
                    throw std::invalid_argument("basis source coordinate or phase precision bound exceeded");
            }
            parameters.push_back({d,r});
        }
        std::string extra;if(input>>extra)throw std::invalid_argument("extra basis request fields");
        std::vector<std::complex<float>> response;response.reserve(2*count*states);
        for(auto& bit:source)for(const auto& p:parameters)for(std::uint64_t j=0;j<count;++j) {
            const long double t=static_cast<long double>(first+j)/sample_rate;
            const auto u=t+p[0]+p[1]*(t-anchor);
            response.emplace_back(at(bit,t,u,sample_rate,o.data_if,shift));
        }
        const auto bytes=response.size()*sizeof(response[0]);
        std::cout<<"ok "<<bytes<<'\n';std::cout.write(reinterpret_cast<const char*>(response.data()),static_cast<std::streamsize>(bytes));std::cout.flush();
        if(!std::cout)throw std::runtime_error("basis response write failed");
    }
}
int run(const Options& o) {
    if(std::endian::native!=std::endian::little)throw std::runtime_error("export currently requires little-endian host");
    const auto started=std::chrono::steady_clock::now();const auto g=geometry(o.format);const double fsample=effective_sample_rate(o.format);
    // Native source padding includes ceil(8Fs/fine)+2 samples. Remove only
    // that representation-dependent origin from this diagnostic calendar,
    // before clock warp/channel propagation, including relative carrier phase.
    // Retain the conservative native extent and all channel tails below.
    const long double canonical_origin_seconds=8.L/g.fine_chip_rate_hz;
    const long double source_sample_shift=g.calendar_origin_sample-canonical_origin_seconds*fsample;
    const auto crypto=key(o.key_seed);
    if(o.basis_server)return serve_basis(o,g,crypto);
    std::array<std::unique_ptr<PatternSource>,2> sources,templates;
    std::array<std::unique_ptr<ClockMap>,2> clocks;
    for(int bit=0;bit<2;++bit){sources[bit]=std::make_unique<PatternSource>(o.format,crypto,bit);templates[bit]=std::make_unique<PatternSource>(o.format,crypto,bit);clocks[bit]=std::make_unique<ClockMap>(o);}
    // GPS profile keeps measured system-rate calibration separate from the
    // 200-ppm phase cap. Retain the full controller domain in capture sizing.
    const double warp=o.clock=="off"?0:o.clock=="gps-disciplined"?.001:200e-6;
    StreamChannelConfig cfg;auto& e=cfg.effects;e.sample_rate=fsample;e.occupied_bandwidth_hz=g.occupied_bandwidth_hz;
    e.rf_hz=o.rf;e.nominal_conversion_if_hz=o.data_if;e.seed=o.seed;e.noise_enabled=false;
    e.tx.phase_noise_rad_per_sqrt_second=e.rx.phase_noise_rad_per_sqrt_second=o.phase/std::sqrt(2.);
    auto spread=o.spread;if(spread=="sub9best")spread="sub9-best";if(spread=="sub9moderate")spread="sub9-moderate";if(spread=="sub9worst")spread="sub9-worst";
    if(spread!="none" && spread!="sub9-best" && spread!="sub9-moderate" && spread!="sub9-worst")throw std::invalid_argument("unsupported spread preset");
    const auto preset=tuning::parse_spread_preset(spread);e.spread_delay_ms=preset.delay_ms;e.spread_doppler_hz=preset.doppler_hz;
    e.perturbation_slow_depth=o.slow;e.perturbation_fast_depth=o.fast;cfg.complex_noise_variance=0;cfg.maximum_chunk_samples=o.chunk;
    cfg.path_evolution.random_doppler_width_hz=o.width;cfg.path_evolution.delay_excursion_seconds=o.excursion_ms*.001;cfg.path_evolution.delay_knot_seconds=o.knot;
    cfg.selective_model.coefficient_law=SelectiveCoefficientLaw::GaussianErf;cfg.selective_model.modulation_rate_hz=o.selective_rate;
    cfg.selective_model.correlation_spacing_hz=g.carrier_spacing_hz;
    const double edge=g.occupied_bandwidth_hz*.5*(1+warp)+o.data_if*warp;
    if(edge>=fsample*.5)throw std::invalid_argument("sample rate cannot contain DATA plus full-audio clock-warp CFO allowance");
    std::array<std::unique_ptr<StreamingChannel>,2> channels;
    std::array<long double,2> emitted_energy{};
    std::uint64_t count=0,first=0,source_count=checked_count(g.source_samples/(1.L-warp));
    if(o.window) {first=checked_count((*o.window)[0]*fsample);count=checked_count((*o.window)[1]*fsample);if(first>UINT64_MAX-count)throw std::invalid_argument("window overflow");}
    else for(int bit=0;bit<2;++bit) {
        EmittedSource emitted;emitted.format=o.format;emitted.geometry=g;emitted.geometry.source_samples=source_count;
        emitted.minimum_frequency_hz=-edge;emitted.maximum_frequency_hz=edge;emitted.working_bytes=sources[bit]->working_bytes()+sizeof(ClockMap);
        emitted.sample_at=[&,bit](std::uint64_t n){const long double t=static_cast<long double>(n)/fsample;
            const auto value=at(*sources[bit],t,clocks[bit]->at(t),fsample,o.data_if,source_sample_shift);emitted_energy[bit]+=std::norm(value);return value;};
        channels[bit]=std::make_unique<StreamingChannel>(std::move(emitted),cfg);
        if(bit && count!=channels[bit]->total_samples())throw std::logic_error("paired channel calendars differ");
        count=channels[bit]->total_samples();
    }
    const unsigned signal_files=o.bit=="both"?2:1;const bool raw=o.raw||o.window.has_value();
    const auto file_count=2*o.ppm.size()+signal_files+(raw?4:0);
    const long double bytes=static_cast<long double>(count)*(8*file_count+(o.window?8:0));
    if(bytes>o.cap)throw std::invalid_argument("output exceeds --max-output-bytes before writing");
    const fs::path root=o.output;if(fs::exists(root) && !fs::is_empty(root))throw std::invalid_argument("output directory must be empty");fs::create_directories(root);
    std::vector<Sink> sinks;for(int bit=0;bit<2;++bit) {
        if(o.bit=="both" || o.bit==std::to_string(bit))sinks.emplace_back(root,"signal_bit"+std::to_string(bit)+".c64",o.window?"source-warped-oracle":"channel-signal-oracle",bit);
        for(std::size_t i=0;i<o.ppm.size();++i)sinks.emplace_back(root,"basis_bit"+std::to_string(bit)+"_ppm"+std::to_string(i)+".c64","public-affine-basis",bit,static_cast<int>(i));
        if(raw)for(const auto* name:{"unwarped","warped"})sinks.emplace_back(root,"source_"+std::string(name)+"_bit"+std::to_string(bit)+".c64",name,bit);
    }
    ClockMap raw_clock(o);
    std::ofstream coordinates;if(o.window)coordinates.open(root/"oracle_source_time.f64",std::ios::binary);
    std::array<std::vector<Complex>,2> y;for(auto& a:y)a.resize(o.chunk);std::vector<Complex> buffer(o.chunk);std::vector<long double> mapped(o.chunk);
    for(std::uint64_t cursor=0;cursor<count;) {
        const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(o.chunk,count-cursor));
        if(!o.window)for(int bit=0;bit<2;++bit)if(channels[bit]->read(std::span(y[bit]).first(size))!=size)throw std::logic_error("short channel read");
        if(raw)for(std::size_t j=0;j<size;++j)mapped[j]=raw_clock.at(static_cast<long double>(first+cursor+j)/fsample);
        if(o.window)for(std::size_t j=0;j<size;++j){const double value=static_cast<double>(mapped[j]);coordinates.write(reinterpret_cast<const char*>(&value),sizeof(value));}
        for(auto& s:sinks) {
            const bool signal=s.role=="channel-signal-oracle";
            if(signal){s.write(std::span(y[s.bit]).first(size));continue;}
            for(std::size_t j=0;j<size;++j) {
                const long double t=static_cast<long double>(first+cursor+j)/fsample;
                const auto u=s.index>=0?t*(1+static_cast<long double>(o.ppm[s.index])*1e-6L):s.role=="unwarped"?t:mapped[j];
                buffer[j]=at(*templates[s.bit],t,u,fsample,o.data_if,source_sample_shift);
            }
            s.write(std::span(buffer).first(size));
        }
        cursor+=size;
    }
    if(o.window && !coordinates)throw std::runtime_error("oracle coordinate write failed");
    for(auto& s:sinks){s.out.close();if(!s.out)throw std::runtime_error("finalizing failed for "+s.name);}
    std::ofstream out(root/"manifest.json");out<<std::setprecision(17)
        <<"{\"schema\":\"triad-tracking-export-v1\",\"mode\":\""<<(o.window?"source-window-no-channel":"continuous-shared-channel")<<"\",\"dtype\":\"<c8\",\"sample_rate_hz\":"<<fsample
        <<",\"sample_rate_numerator\":"<<o.format.sample_rate<<",\"sample_rate_denominator\":"<<o.format.sample_rate_denominator
        <<",\"first_sample\":"<<first<<",\"samples\":"<<count<<",\"requested_bit_seconds\":"<<o.seconds<<",\"bit_seconds\":"<<g.bit_seconds
        <<",\"source_samples\":"<<g.source_samples<<",\"padded_emitted_source_samples\":"<<source_count<<",\"calendar_origin_sample\":"<<g.calendar_origin_sample
        <<",\"source_calendar\":\"canonical-8chips-v1\",\"canonical_payload_origin_seconds\":"<<static_cast<double>(canonical_origin_seconds)
        <<",\"native_calendar_origin_sample\":"<<g.calendar_origin_sample<<",\"native_source_sample_shift\":"<<static_cast<double>(source_sample_shift)
        <<",\"extent_policy\":\"conservative native extent plus clock allowance and full channel tails; Fs-dependent extra silence retained\""
        <<",\"nominal_payload_energy_seconds\":"<<g.nominal_payload_energy_seconds
        <<",\"inner_rate_hz\":"<<o.format.inner_rate_hz<<",\"added_symbols\":"<<o.format.added_symbols<<",\"pattern_revision\":\""<<pattern_revision_name(o.format.pattern_revision)<<"\",\"encryption\":true,\"interleave\":true"
        <<",\"pulse_shape\":\""<<pulse_shape_name(o.format.pulse_shape)<<"\""
        <<",\"carrier_spacing_hz\":"<<g.carrier_spacing_hz<<",\"occupied_bandwidth_hz\":"<<g.occupied_bandwidth_hz<<",\"data_if_hz\":"<<o.data_if<<",\"rf_hz\":"<<o.rf
        <<",\"public_epoch_seconds\":"<<o.format.epoch<<",\"key_seed\":"<<o.key_seed<<",\"test_key_only\":true,\"channel_seed\":"<<o.seed<<",\"transmit_gain\":1,\"channel_noise_enabled\":false"
        <<",\"noise_convention\":\"nominal unit-symbol reference power; external complex sample variance Fs / linear-CN0; shaping/guards reduce integrated energy; use emitted-source energy, no realized-energy normalization\""
        <<",\"clock_profile\":\""<<o.clock<<"\",\"clock_controller_tick_seconds\":1,\"phase_catchup_cap_ppm\":"<<(o.clock=="off"?0:200)
        <<",\"device_rate_error_ppm\":0,\"clock_slew_ppm_per_second\":10,\"clock_events\":\""
        <<(o.clock=="gps-disciplined"?"GPS corrects one drifting system UTC toward physical time; no independent phase steps added":"seeded hourly 50..150ms with +/-300s jitter; independent +/-2s every24 events; wandering also 10ms quarter-hour steps")<<"\""
        <<",\"clock_domain_model\":\""<<(o.clock=="gps-disciplined"?"shared monotonic/UTC oscillator drift; GPS UTC steps toward physical time; ideal independent device frame clock":"ideal monotonic/device clocks; independent UTC phase-step stress")<<"\""
        <<",\"system_oscillator_drift_bound_ppm\":"<<(o.clock=="gps-disciplined"?std::abs(o.clock_drift_ppm.value_or(20)):0)
        <<",\"system_oscillator_drift_signed_ppm\":"<<(o.clock=="gps-disciplined"?o.clock_drift_ppm.value_or(20):0)
        <<",\"gps_holdover_seconds\":"<<(o.clock=="gps-disciplined"?o.gps_holdover_seconds.value_or(86400):0)
        <<",\"gps_model\":\""<<(o.clock=="gps-disciplined"?"hourly fixes +/-300s jitter; independent +/-1ms residual; weekly holdover begins physical day3; drift=P*(.85+.15*cos(2pi*t/7days+seedphase))":"inactive")<<"\""
        <<",\"capture_source_halfwidth_allowance_hz\":"<<edge<<",\"stochastic_spectral_tail_convention\":\"shared channel 6B random-ray total; GaussianErf 10B per edge; not compact support\""
        <<",\"spread\":\""<<spread<<"\",\"spread_doppler_hz\":"<<e.spread_doppler_hz<<",\"spread_delay_ms\":"<<e.spread_delay_ms
        <<",\"random_doppler_width_hz\":"<<o.width<<",\"delay_excursion_ms\":"<<o.excursion_ms<<",\"delay_knot_seconds\":"<<o.knot
        <<",\"selective_law\":\"gaussian-erf-v1\",\"selective_slow_depth\":"<<o.slow<<",\"selective_fast_depth\":"<<o.fast<<",\"selective_rate_hz\":"<<o.selective_rate.value_or(e.spread_doppler_hz)
        <<",\"phase_diffusion_rad_sqrt_s\":"<<o.phase<<",\"unsupported\":[\"Helper at reduced Fs\",\"device drift and higher oscillator spectra\",\"FHSS and frontend effects\",\"unknown initial epoch/arrival acquisition\"]"
        <<",\"compiled_driver_sha256\":\""<<compiled_driver_sha<<"\",\"source_sha256\":\""<<sha(__FILE__)<<"\",\"binary_sha256\":\""<<sha("/proc/self/exe")<<"\",\"template_ppm\":[";
    for(std::size_t i=0;i<o.ppm.size();++i)out<<(i?",":"")<<o.ppm[i];
    out<<"],\"files\":[";
    for(std::size_t i=0;i<sinks.size();++i){const auto& s=sinks[i];out<<(i?",":"")<<"{\"path\":\""<<s.name<<"\",\"role\":\""<<s.role<<"\",\"bit\":"<<s.bit<<",\"template_index\":"<<s.index<<",\"energy_seconds\":"<<static_cast<double>(s.energy/fsample)<<"}";}
    out<<"],\"channel_reports\":[";
    if(!o.window)for(int bit=0;bit<2;++bit){const auto& r=channels[bit]->report();out<<(bit?",":"")<<"{\"bit\":"<<bit<<",\"tail_samples\":"<<r.tail_samples<<",\"natural_complete_samples\":"<<r.natural_complete_samples<<",\"working_bytes\":"<<r.working_bytes<<",\"signal_energy\":"<<r.signal_energy<<",\"emitted_source_energy_seconds\":"<<static_cast<double>(emitted_energy[bit]/fsample)<<",\"out_of_passband_samples\":"<<r.out_of_passband_samples;
        if(r.selective_perturbation)out<<",\"pre_selective_energy_samples\":"<<r.selective_perturbation->pre_selective_energy_samples<<",\"post_selective_energy_samples\":"<<r.selective_perturbation->post_selective_energy_samples;
        out<<"}";}
    out<<"],\"output_bytes\":"<<static_cast<std::uint64_t>(bytes)<<",\"source_workspace_bytes_each\":"<<sources[0]->working_bytes()
        <<",\"oracle_clock_max_phase_error_seconds\":"<<(o.window?raw_clock.max_error:clocks[0]->max_error)
        <<",\"oracle_clock_max_applied_rate_ppm\":"<<1e6*(o.window?raw_clock.max_rate:clocks[0]->max_rate)
        <<",\"oracle_clock_final_displacement_seconds\":"<<static_cast<double>(o.window?raw_clock.displacement():clocks[0]->displacement())
        <<",\"oracle_clock_final_physical_seconds\":"<<static_cast<double>(o.window?raw_clock.final_physical_seconds():clocks[0]->final_physical_seconds())
        <<",\"clock_controller_ticks\":"<<(o.window?raw_clock.ticks():clocks[0]->ticks());
    const auto& final_clock=o.window?raw_clock:*clocks[0];
    out<<",\"oracle_clock_max_displacement_seconds\":"<<final_clock.max_displacement
        <<",\"oracle_clock_min_applied_rate_ppm\":"<<final_clock.min_applied_rate*1e6
        <<",\"oracle_clock_max_positive_applied_rate_ppm\":"<<final_clock.max_applied_rate*1e6
        <<",\"oracle_system_utc_max_error_seconds\":"<<final_clock.max_utc_error
        <<",\"oracle_system_utc_final_error_seconds\":"<<static_cast<double>(final_clock.final_utc_error_seconds())
        <<",\"gps_fixes\":"<<final_clock.gps_fixes<<",\"gps_skipped_fixes\":"<<final_clock.gps_skipped_fixes
        <<",\"gps_max_fix_gap_seconds\":"<<final_clock.gps_max_fix_gap_seconds
        <<",\"gps_max_step_seconds\":"<<final_clock.gps_max_step_seconds
        <<",\"gps_max_normal_step_seconds\":"<<final_clock.gps_max_normal_step_seconds
        <<",\"gps_max_recovery_step_seconds\":"<<final_clock.gps_max_recovery_step_seconds
        <<",\"wall_seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<"}\n";
    if(!out)throw std::runtime_error("manifest write failed");
    out.close();std::cout<<"{\"samples\":"<<count<<",\"output_bytes\":"<<static_cast<std::uint64_t>(bytes)<<"}\n";return 0;
}
}
int main(int argc,char** argv) {try{return run(parse(argc,argv));}catch(const std::exception& e){std::cerr<<"triad-tracking-export: "<<e.what()<<'\n';return 1;}}

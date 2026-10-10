#include "pattern_fft_batch.hpp"
#include "pattern_drift.hpp"
#include "search_parallel.hpp"
#include "search_arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <openssl/crypto.h>

namespace datapump::modem::detail {
namespace {
using Complex = FftComplex;
constexpr double tau = 2 * std::numbers::pi;
void cancelled(std::stop_token stop) { if(stop.stop_requested()) throw Error("pattern search cancelled"); }
Complex template_value(PatternCode& code,const FftSearchGeometry& geometry,
                       const FftSearchJob& job,std::size_t bin,unsigned bit,
                       std::span<const std::array<Complex,2>> nominal_reference={},bool rotate=true) {
    const auto& pattern=geometry.pattern;
    const auto sample_position=static_cast<long double>(bin)*geometry.bin_samples+
        static_cast<long double>(geometry.bin_samples-1)/2;
    const auto source_position=sample_position*job.clock_ratio;
    if(geometry.extended_clock_window && source_position>=pattern.symbol_samples)return {};
    const auto chip_position=source_position/pattern.chip_samples;
    const auto local=static_cast<std::uint64_t>(chip_position);
    if(job.symbol>(std::numeric_limits<std::uint64_t>::max()-local)/pattern.chips_per_symbol)
        throw Error("pattern stream coordinate overflow");
    const auto chip=job.symbol*pattern.chips_per_symbol+local;
    const auto fraction=static_cast<double>(chip_position-local);
    const bool cached=!nominal_reference.empty() && job.clock_ratio==1 &&
        !pattern.scramble && !pattern.dsss && pattern.spreading_mode==static_cast<std::uint32_t>(SpreadingMode::pattern);
    const auto value=cached?nominal_reference[bin][bit]:pattern.shaped?
        code.shaped_value(job.symbol*pattern.chips_per_symbol,bit,static_cast<double>(source_position)):
        code.value(chip,bit,fraction);
    return rotate?value*std::polar(1.,tau*job.frequency_hz*static_cast<double>(sample_position)/pattern.sample_rate):value;
}
Complex carrier_square(const FftSearchGeometry& geometry,std::uint64_t bin) {
    const auto phase=std::remainder(2*static_cast<long double>(tau)*geometry.carrier_hz*
        (static_cast<long double>(bin)*geometry.bin_samples+(geometry.bin_samples-1)/2.L)/
        geometry.pattern.sample_rate,static_cast<long double>(tau));
    return std::polar(1.,static_cast<double>(phase));
}
} // namespace
bool FftPrivateTemplate::eligible(const FftSearchGeometry& g,const FftSearchJob& job) {
    if(g.private_template_reuse!=PrivateTemplateReuse::bounded_interpolation ||
       effective_search_arithmetic(g)!=SearchArithmetic::fp32 || g.sample_fit ||
       g.drift_sections!=1 || g.differential_window_samples || !g.pattern.shaped ||
       (!g.pattern.scramble&&!g.pattern.dsss) ||
       g.pattern.spreading_mode!=static_cast<std::uint32_t>(SpreadingMode::pattern) ||
       !g.bins_per_symbol || g.bin_samples<2 || !g.pattern.sample_rate || !g.pattern.symbol_samples || !g.pattern.chip_samples ||
       g.bin_samples>g.pattern.chip_samples/2 || !g.pattern.chips_per_symbol ||
       // Half-integral bin centers stay at least half a sample from integral
       // finite-pulse cutoffs. Never interpolate across a cutoff at the origin.
       (g.bin_samples&1U) || (g.pattern.chip_samples&1U) || (g.pattern.symbol_samples&1U) ||
       !std::isfinite(job.clock_ratio) || job.clock_ratio<=0)return false;
    const auto last=static_cast<long double>(g.bins_per_symbol-1)*g.bin_samples+(g.bin_samples-1)/2.L;
    return std::abs(last*(static_cast<long double>(job.clock_ratio)-1))<=maximum_displacement_samples;
}
std::size_t FftPrivateTemplate::required_bytes(const FftSearchGeometry& g) {
    constexpr auto pair_bytes=sizeof(decltype(values_)::value_type);
    const auto maximum=std::numeric_limits<std::size_t>::max();
    if(g.bins_per_symbol>(maximum-sizeof(FftPrivateTemplate))/pair_bytes-2)
        throw Error("private template cache exceeds address space");
    return sizeof(FftPrivateTemplate)+(static_cast<std::size_t>(g.bins_per_symbol)+2)*pair_bytes;
}
FftPrivateTemplate::FftPrivateTemplate(const FftSearchGeometry& g,const FftSearchJob& job,
        PatternCode& code,std::stop_token stop):pattern_(g.pattern),bins_(g.bins_per_symbol),
        bin_samples_(g.bin_samples),symbol_(job.symbol),phase_(job.phase) {
    try {
        if(!eligible(g,job))throw Error("unsupported private template interpolation geometry");
        (void)required_bytes(g);
        if(symbol_>std::numeric_limits<std::uint64_t>::max()/pattern_.chips_per_symbol)
            throw Error("pattern stream coordinate overflow");
        cancelled(stop);code.set_stream_phase_samples(phase_);
        code.prepare_symbol(symbol_*pattern_.chips_per_symbol,stop);
        values_.resize(static_cast<std::size_t>(bins_)+2);
        for(std::size_t i=0;i<values_.size();++i) {
            if((i&4095U)==0)cancelled(stop);
            // Evaluate genuine finite-pulse halos, including negative source
            // coordinates; do not manufacture zeros at the symbol boundary.
            const auto position=(static_cast<long double>(i)-1)*bin_samples_+(bin_samples_-1)/2.L;
            const auto pair=code.shaped_values(symbol_*pattern_.chips_per_symbol,static_cast<double>(position));
            for(unsigned bit=0;bit<2;++bit)values_[i][bit]={static_cast<float>(pair[bit].real()),static_cast<float>(pair[bit].imag())};
        }
        cancelled(stop);
    }catch(...) {
        OPENSSL_cleanse(values_.data(),values_.size()*sizeof(values_[0]));
        OPENSSL_cleanse(&pattern_,sizeof(pattern_));throw;
    }
}
FftPrivateTemplate::~FftPrivateTemplate() {
    OPENSSL_cleanse(values_.data(),values_.size()*sizeof(values_[0]));
    OPENSSL_cleanse(&pattern_,sizeof(pattern_));
}
bool FftPrivateTemplate::matches(const FftSearchGeometry& g,const FftSearchJob& job) const {
    return eligible(g,job) && g.pattern==pattern_ && g.bins_per_symbol==bins_ &&
        g.bin_samples==bin_samples_ && job.symbol==symbol_ && job.phase==phase_;
}
std::size_t FftPrivateTemplate::working_bytes() const {
    return sizeof(*this)+values_.capacity()*sizeof(values_[0]);
}
std::array<FftComplex,2> FftPrivateTemplate::value(std::size_t bin,double ratio) const {
    if(bin>=bins_)throw Error("private template cache index out of range");
    const auto position=static_cast<long double>(bin)*bin_samples_+(bin_samples_-1)/2.L;
    const auto coordinate=static_cast<long double>(bin)+1+position*(static_cast<long double>(ratio)-1)/bin_samples_;
    const auto lower=std::floor(coordinate);
    if(lower<0 || lower+1>=values_.size())throw Error("private template cache halo exceeded");
    const auto index=static_cast<std::size_t>(lower);
    const auto fraction=static_cast<float>(coordinate-lower);
    std::array<FftComplex,2> result{};
    for(unsigned bit=0;bit<2;++bit) {
        const auto value=values_[index][bit]+fraction*(values_[index+1][bit]-values_[index][bit]);
        result[bit]={value.real(),value.imag()};
    }
    return result;
}
void prepare_fft_nominal_reference(const FftSearchGeometry& geometry,PatternCode& code,
                                   std::span<std::array<FftComplex,2>> reference,std::stop_token stop) {
    const auto& pattern=geometry.pattern;
    if(reference.size()!=geometry.bins_per_symbol || !geometry.bins_per_symbol || !geometry.bin_samples ||
       !pattern.chip_samples || !pattern.chips_per_symbol ||
       !pattern.sample_rate || !pattern.symbol_samples || pattern.scramble || pattern.dsss ||
       pattern.spreading_mode!=static_cast<std::uint32_t>(SpreadingMode::pattern))
        throw Error("invalid public nominal pattern reference");
    const FftSearchJob nominal;
    for(std::size_t bin=0;bin<reference.size();++bin) {
        if((bin&4095U)==0)cancelled(stop);
        for(unsigned bit=0;bit<2;++bit)
            reference[bin][bit]=template_value(code,geometry,nominal,bin,bit,{},false);
    }
}
void pattern_fft(std::span<FftComplex> a, bool inverse, std::stop_token stop) {
    // Every transform uses these same power-of-two stages. Preserve the exact
    // polar calculation while sharing its immutable result between transforms
    // and workers; no receiver workspace or mutable FFT plan is retained.
    static const auto steps=[] {
        std::array<std::array<Complex,2>,std::numeric_limits<std::size_t>::digits-1> result{};
        std::size_t length=2;
        for(auto& stage:result) {
            stage={std::polar(1.,-tau/static_cast<double>(length)),
                   std::polar(1.,tau/static_cast<double>(length))};
            if(length<=std::numeric_limits<std::size_t>::max()/2)length*=2;
        }
        return result;
    }();
    const auto n=a.size();
    for(std::size_t i=1,j=0;i<n;++i) {
        auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j)std::swap(a[i],a[j]);
    }
    for(std::size_t length=2,stage=0;length<=n;length*=2,++stage) {
        cancelled(stop);const auto step=steps[stage][inverse?1:0];
        for(std::size_t begin=0;begin<n;begin+=length) {
            Complex w{1,0};
            for(std::size_t j=0;j<length/2;++j) {
                const auto u=a[begin+j],v=a[begin+j+length/2]*w;
                a[begin+j]=u+v;a[begin+j+length/2]=u-v;w*=step;
            }
        }
        if(length==n)break;
    }
    if(inverse)for(auto& value:a)value/=static_cast<double>(n);
}
// FFT storage remains double-sized so callers reuse the same bounded arenas;
// FP32 execution rounds every operand, butterfly and twiddle to float.
FftComplex pattern_fft_product(FftComplex a,FftComplex b,SearchArithmetic mode) {
    if(mode!=SearchArithmetic::fp32)return a*b;
    const std::complex<float> x{static_cast<float>(a.real()),static_cast<float>(a.imag())};
    const std::complex<float> y{static_cast<float>(b.real()),static_cast<float>(b.imag())};
    const auto z=x*y;return {z.real(),z.imag()};
}
void pattern_fft(std::span<FftComplex> a,bool inverse,std::stop_token stop,SearchArithmetic mode) {
    if(mode!=SearchArithmetic::fp32){pattern_fft(a,inverse,stop);return;}
    const auto n=a.size();
    for(std::size_t i=0;i<n;++i){
        if((i&4095U)==0)cancelled(stop);
        a[i]={static_cast<float>(a[i].real()),static_cast<float>(a[i].imag())};
    }
    for(std::size_t i=1,j=0;i<n;++i){
        auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j)std::swap(a[i],a[j]);
    }
    for(std::size_t length=2;length<=n;length*=2){
        cancelled(stop);
        const auto step=std::polar(1.F,static_cast<float>((inverse?tau:-tau)/static_cast<double>(length)));
        for(std::size_t begin=0;begin<n;begin+=length){
            std::complex<float> w{1,0};
            for(std::size_t j=0;j<length/2;++j){
                const auto x=a[begin+j],y=a[begin+j+length/2];
                const std::complex<float> u{static_cast<float>(x.real()),static_cast<float>(x.imag())};
                const auto v=std::complex<float>{static_cast<float>(y.real()),static_cast<float>(y.imag())}*w;
                const auto sum=u+v,difference=u-v;
                a[begin+j]={sum.real(),sum.imag()};a[begin+j+length/2]={difference.real(),difference.imag()};
                w*=step;
            }
        }
        if(length==n)break;
    }
    if(inverse)for(auto& value:a){
        const auto z=std::complex<float>{static_cast<float>(value.real()),static_cast<float>(value.imag())}/static_cast<float>(n);
        value={z.real(),z.imag()};
    }
}
double pattern_explained(Complex dot,double template_energy,double condition,bool exact_real,Complex template_square) {
    if(template_energy<=1e-30)return 0;
    auto fitted=std::norm(dot)/(template_energy*condition);
    if(exact_real) {
        // Individual real samples fit two real carrier bases. Their exact
        // Gram matrix is encoded by sum(|template|^2) and sum(template^2).
        // The trace-only bound loses half the energy even for an exact fit.
        const auto determinant=template_energy*template_energy-std::norm(template_square);
        if(determinant>1e-12*template_energy*template_energy)
            fitted=2*(template_energy*std::norm(dot)-(template_square*dot*dot).real())/determinant;
    }
    return fitted;
}
double pattern_evidence(Complex dot,double energy,double template_energy,double count,double condition,bool real_rank,
                bool exact_real,Complex template_square) {
    if(count<4 || energy<=1e-30 || template_energy<=1e-30)return 0;
    const auto fraction=std::clamp(pattern_explained(dot,template_energy,condition,exact_real,template_square)/energy,0.,1.-1e-15);
    // Nonsingular quadrature bins use a covariance-eigenratio bound. Individual
    // real samples use the exact rank-two fit above; an ill-conditioned Gram
    // matrix retains the conservative lambda_max<=trace bound. These scores
    // assume independent Gaussian input samples, with unknown common variance.
    return -(real_rank?(count-2)/2:count-1)*std::log1p(-fraction);
}
double pattern_projection_image_ratio(std::uint64_t bin_samples,double carrier_hz,std::uint32_t sample_rate) {
    const auto omega=tau*carrier_hz/sample_rate;
    const auto sine=std::sin(omega);
    return std::abs(sine)>1e-12?std::sin(static_cast<double>(bin_samples)*omega)/(static_cast<double>(bin_samples)*sine):
        std::cos(static_cast<double>(bin_samples-1)*omega);
}
Complex pattern_differential_whiten(Complex dot,double norm,Complex square,double image_ratio) {
    const auto image=square*image_ratio;
    // dot=sum(y*conj(p)) uses the negative sine convention. Reverse it to
    // match the real cosine/sine Gram represented by sum(p*p*carrier^2).
    return differential_whiten(dot.real(),-dot.imag(),.5*(norm+image.real()),
                              .5*(norm-image.real()),.5*image.imag());
}

std::size_t FftQuantizedDirect::required_bytes(std::size_t n,std::size_t l) {
    constexpr auto tile=search_arithmetic::quantized_tile_size;
    const auto add=[](std::size_t a,std::size_t b){
        if(b>std::numeric_limits<std::size_t>::max()-a)throw Error("integer search cache size overflow");
        return a+b;
    };
    const auto multiply=[](std::size_t a,std::size_t b){
        if(b&&a>std::numeric_limits<std::size_t>::max()/b)throw Error("integer search cache size overflow");
        return a*b;
    };
    return add(multiply(add(n,multiply(2,l)),2*sizeof(search_arithmetic::Complex8)),
        multiply(add(n/tile+(n%tile!=0),multiply(2,l/tile+(l%tile!=0))),sizeof(search_arithmetic::CompensatedBlock)));
}
FftQuantizedDirect::FftQuantizedDirect(std::size_t n,std::size_t l):input(n),input_residual(n),
    rows{std::vector<search_arithmetic::Complex8>(l),std::vector<search_arithmetic::Complex8>(l)},
    row_residual{std::vector<search_arithmetic::Complex8>(l),std::vector<search_arithmetic::Complex8>(l)},
    input_blocks(n/search_arithmetic::quantized_tile_size+(n%search_arithmetic::quantized_tile_size!=0)),
    row_blocks{std::vector<search_arithmetic::CompensatedBlock>(l/search_arithmetic::quantized_tile_size+(l%search_arithmetic::quantized_tile_size!=0)),
               std::vector<search_arithmetic::CompensatedBlock>(l/search_arithmetic::quantized_tile_size+(l%search_arithmetic::quantized_tile_size!=0))} {}
std::size_t FftQuantizedDirect::working_bytes() const {
    return (input.capacity()+input_residual.capacity()+rows[0].capacity()+rows[1].capacity()+row_residual[0].capacity()+row_residual[1].capacity())*sizeof(search_arithmetic::Complex8)+
        (input_blocks.capacity()+row_blocks[0].capacity()+row_blocks[1].capacity())*sizeof(search_arithmetic::CompensatedBlock);
}
FftSearchBatch::~FftSearchBatch() {
    OPENSSL_cleanse(geometry.pattern.spreading_seed.data(),geometry.pattern.spreading_seed.size());
    OPENSSL_cleanse(geometry.pattern.dsss_seed.data(),geometry.pattern.dsss_seed.size());
}
FftSearchWorkspace::FftSearchWorkspace(const Config& config,std::size_t transform,bool needs_code,std::size_t drift_starts,
                                     std::size_t differential_starts)
    :product(transform),drift(drift_starts),differential(differential_starts) {
    if(needs_code)code=std::make_unique<PatternCode>(config,config.stream_epoch);
}
std::size_t FftSearchWorkspace::working_bytes() const {
    return (code?code->working_bytes():0)+product.capacity()*sizeof(FftComplex)+drift.capacity()*sizeof(FftDriftAccumulator)+
        differential.capacity()*sizeof(DifferentialAccumulator);
}
void execute_drift_search_job(const FftSearchBatch& batch,const FftSearchJob& job,
                             std::span<FftSearchScore> scores,std::span<FftComplex> product,
                             std::span<FftDriftAccumulator> scratch,PatternCode& code,std::stop_token stop,
                             std::span<DifferentialAccumulator> differential) {
    const auto& g=batch.geometry;
    const auto transform=batch.spectrum.size();
    const auto length=static_cast<std::size_t>(g.bins_per_symbol);
    if(g.drift_sections!=4 || !transform || (transform&(transform-1)) ||
       g.bins_per_symbol>std::numeric_limits<std::size_t>::max() || !length || length>transform ||
       !batch.starts || batch.starts>transform-length+1 || batch.energy_prefix.size()<length+batch.starts ||
       !g.bin_samples || !g.pattern.symbol_samples || !g.pattern.chip_samples ||
       !g.pattern.chips_per_symbol || !g.pattern.sample_rate ||
       (!batch.nominal_reference.empty() && batch.nominal_reference.size()!=length) ||
       !std::isfinite(job.frequency_hz) || !std::isfinite(g.carrier_hz) ||
       !std::isfinite(job.clock_ratio) || job.clock_ratio<=0 ||
       scores.size()<batch.starts || scratch.size()<batch.starts || product.size()!=transform ||
       (g.differential_window_samples && (differential.size()<batch.starts ||
        g.pattern.symbol_samples/g.differential_window_samples<256 ||
        g.differential_window_samples/g.pattern.chip_samples<16 ||
        g.differential_window_samples%g.pattern.chip_samples)))
        throw Error("invalid drift search geometry");
    code.set_stream_phase_samples(job.phase);
    if(job.symbol>std::numeric_limits<std::uint64_t>::max()/g.pattern.chips_per_symbol)
        throw Error("pattern stream coordinate overflow");
    code.prepare_symbol(job.symbol*g.pattern.chips_per_symbol,stop);
    std::array<std::size_t,5> edges{};edges.back()=length;
    for(unsigned section=1;section<g.drift_sections;++section) {
        const auto boundary=static_cast<long double>(drift_boundary(section,g.pattern.symbol_samples,g.drift_sections));
        const auto bin=std::ceil((boundary/job.clock_ratio-(g.bin_samples-1)/2.L)/g.bin_samples);
        edges[section]=static_cast<std::size_t>(std::clamp(bin,static_cast<long double>(edges[section-1]),static_cast<long double>(length)));
    }
    const bool direct=batch.starts<=4 && batch.observations.size()>=length+batch.starts-1;
    for(unsigned bit=0;bit<2;++bit) {
        std::fill_n(scratch.begin(),batch.starts,FftDriftAccumulator{});
        double full_norm=0;Complex full_square{};
        for(unsigned section=0;section<g.drift_sections;++section) {
            cancelled(stop);
            double norm=0;Complex square{};
            if(direct) {
                std::array<Complex,4> dots{};
                // Every start uses the same template. Generate it once while
                // retaining each dot product's original sample order.
                for(std::size_t i=edges[section];i<edges[section+1];++i) {
                    if((i&4095U)==0)cancelled(stop);
                    const auto p=template_value(code,g,job,i,bit,batch.nominal_reference);
                    norm+=std::norm(p);
                    if(g.sample_fit)square+=p*p*carrier_square(g,i);
                    for(std::size_t j=0;j<batch.starts;++j)
                        dots[j]+=batch.observations[j+i]*std::conj(p);
                }
                for(std::size_t j=0;j<batch.starts;++j) {
                    const auto dot=dots[j];
                    const auto gram=g.sample_fit?square*carrier_square(g,batch.first_bin+j):Complex{};
                    scratch[j].dot+=dot;
                    const auto explained=pattern_explained(dot,norm,g.noise_condition,g.sample_fit,gram);
                    scratch[j].explained+=explained;
                    scratch[j].strongest=std::max(scratch[j].strongest,explained);
                }
            } else {
                std::fill(product.begin(),product.end(),Complex{});
                for(std::size_t i=edges[section];i<edges[section+1];++i) {
                    if((i&4095U)==0)cancelled(stop);
                    const auto p=template_value(code,g,job,i,bit,batch.nominal_reference);
                    product[length-1-i]=std::conj(p);norm+=std::norm(p);
                    if(g.sample_fit)square+=p*p*carrier_square(g,i);
                }
                pattern_fft(product,false,stop);
                for(std::size_t i=0;i<product.size();++i)product[i]*=batch.spectrum[i];
                pattern_fft(product,true,stop);
                for(std::size_t j=0;j<batch.starts;++j) {
                    const auto dot=product[length-1+j];
                    const auto gram=g.sample_fit?square*carrier_square(g,batch.first_bin+j):Complex{};
                    scratch[j].dot+=dot;
                    const auto explained=pattern_explained(dot,norm,g.noise_condition,g.sample_fit,gram);
                    scratch[j].explained+=explained;
                    scratch[j].strongest=std::max(scratch[j].strongest,explained);
                }
            }
            full_norm+=norm;full_square+=square;
        }
        for(std::size_t j=0;j<batch.starts;++j) {
            const auto energy=batch.energy_prefix[j+length]-batch.energy_prefix[j];
            const auto gram=g.sample_fit?full_square*carrier_square(g,batch.first_bin+j):Complex{};
            const auto coherent=pattern_evidence(scratch[j].dot,energy,full_norm,g.evidence_count,
                g.noise_condition,g.real_rank,g.sample_fit,gram);
            // A filter tail or isolated strong section must not carry an
            // otherwise absent whole bit. Removing its fitted energy only
            // lowers the statistic, preserving the conservative K=4 tail.
            const auto drift=drift_evidence(scratch[j].explained-scratch[j].strongest,
                energy,g.evidence_count,g.drift_sections,g.real_rank);
            const auto score=combine_drift_evidence(coherent,drift,g.drift_sections);
            if(bit==0)scores[j].zero=score;else scores[j].one=score;
        }
        if(!g.differential_window_samples)continue;
        std::fill_n(differential.begin(),batch.starts,DifferentialAccumulator{});
        const auto window=g.differential_window_samples;
        const auto windows=g.pattern.symbol_samples/window;
        const auto image_ratio=pattern_projection_image_ratio(g.bin_samples,g.carrier_hz,g.pattern.sample_rate);
        const auto inverse_origin=std::conj(carrier_square(g,0));
        // A few short-window fits are cheaper than hundreds of full-sized
        // transforms. Keep the crossover deterministic and reuse the already
        // budgeted section dots after publishing their complete-bit scores.
        const auto direct_limit=static_cast<long double>(windows)*std::log2(static_cast<double>(transform));
        const bool direct_differential=batch.observations.size()>=length+batch.starts-1 &&
            batch.starts<=std::max(4.L,direct_limit);
        const auto boundary=[&](std::uint64_t position) {
            const auto bin=std::ceil((static_cast<long double>(position)/job.clock_ratio-(g.bin_samples-1)/2.L)/g.bin_samples);
            return std::max(0.L,bin);
        };
        std::size_t begin=0;
        for(std::uint64_t k=0;k<windows;++k) {
            cancelled(stop);
            const auto edge=boundary((k+1)*window);
            if(edge>length)break; // A truncated local fit cannot enter a pair.
            const auto end=static_cast<std::size_t>(edge);
            double norm=0;Complex square{};
            const auto add=[&](std::size_t j,Complex dot) {
                const auto gram=square*carrier_square(g,batch.first_bin+j)*inverse_origin;
                differential[j].add(pattern_differential_whiten(dot,norm,gram,image_ratio),k,
                                    g.pattern.symbol_samples,window);
            };
            if(direct_differential) {
                for(std::size_t j=0;j<batch.starts;++j)scratch[j].dot={};
                for(auto i=begin;i<end;++i) {
                    if((i&4095U)==0)cancelled(stop);
                    const auto p=template_value(code,g,job,i,bit,batch.nominal_reference);
                    norm+=std::norm(p);square+=p*p*carrier_square(g,i);
                    for(std::size_t j=0;j<batch.starts;++j)scratch[j].dot+=batch.observations[j+i]*std::conj(p);
                }
                for(std::size_t j=0;j<batch.starts;++j)add(j,scratch[j].dot);
            } else {
                std::fill(product.begin(),product.end(),Complex{});
                for(auto i=begin;i<end;++i) {
                    if((i&4095U)==0)cancelled(stop);
                    const auto p=template_value(code,g,job,i,bit,batch.nominal_reference);
                    product[length-1-i]=std::conj(p);norm+=std::norm(p);square+=p*p*carrier_square(g,i);
                }
                pattern_fft(product,false,stop);
                for(std::size_t i=0;i<product.size();++i)product[i]*=batch.spectrum[i];
                pattern_fft(product,true,stop);
                for(std::size_t j=0;j<batch.starts;++j)add(j,product[length-1+j]);
            }
            begin=end;
        }
        for(std::size_t j=0;j<batch.starts;++j) {
            auto& score=bit==0?scores[j].zero:scores[j].one;
            score=combine_differential_evidence(score,differential[j].score(),true);
        }
    }
}
void execute_fft_search_cpu(const FftSearchBatch& batch,std::span<const FftSearchJob> jobs,
                            std::span<FftSearchScore> scores,std::span<FftSearchWorkspace> workspaces,
                            std::stop_token stop) {
    if(jobs.empty())return;
    const auto& geometry=batch.geometry;
    const auto transform=batch.spectrum.size();
    const auto length=static_cast<std::size_t>(geometry.bins_per_symbol);
    if(workspaces.empty() || !transform || (transform&(transform-1)) ||
       geometry.bins_per_symbol>std::numeric_limits<std::size_t>::max() ||
       !length || length>transform || !batch.starts ||
       batch.starts>transform-length+1 || batch.score_stride<batch.starts ||
       jobs.size()>scores.size()/batch.score_stride || batch.energy_prefix.size()<length+batch.starts ||
       (!batch.nominal_reference.empty() && batch.nominal_reference.size()!=length) ||
       (geometry.differential_window_samples && geometry.drift_sections!=4) ||
       (geometry.sample_fit && geometry.drift_sections<=1 && batch.carrier_square.size()<batch.starts))
        throw Error("invalid pattern FFT batch geometry");
    const bool generates=std::any_of(jobs.begin(),jobs.end(),[](const auto& job) {
        return job.prepared_template==std::numeric_limits<std::uint64_t>::max();
    });
    if(generates && (!geometry.bin_samples || !geometry.pattern.chip_samples ||
       !geometry.pattern.chips_per_symbol || !geometry.pattern.sample_rate))
        throw Error("invalid generated pattern FFT geometry");
    for(const auto& workspace:workspaces)if(workspace.product.size()!=transform)
        throw Error("invalid pattern FFT worker workspace");
    for(const auto& job:jobs) {
        if(job.range_count==std::numeric_limits<std::size_t>::max())continue;
        if(geometry.drift_sections>1 || job.range_begin>batch.start_ranges.size() ||
           job.range_count>batch.start_ranges.size()-job.range_begin)
            throw Error("invalid restricted pattern FFT ranges");
        std::size_t previous=0;
        for(const auto range:batch.start_ranges.subspan(job.range_begin,job.range_count)) {
            if(!range.count || range.first<previous || range.first>=batch.starts ||
               range.count>batch.starts-range.first)
                throw Error("invalid restricted pattern FFT start");
            previous=range.first+range.count;
        }
    }
    // This backend preserves the original within-job floating-point order.
    // A GPU backend can map job/start/bin dimensions to many more execution
    // lanes, but must pass the same score and ordered-publication checks.
    parallel_search_ranges(jobs.size(),workspaces.size(),1,
        [&](std::size_t worker,std::size_t begin,std::size_t end) {
        auto& workspace=workspaces[worker];
        for(auto job_index=begin;job_index<end;++job_index) {
            cancelled(stop);
            const auto& job=jobs[job_index];
            const auto full=job.range_count==std::numeric_limits<std::size_t>::max();
            const auto ranges=full?std::span<const FftStartRange>{}:
                batch.start_ranges.subspan(job.range_begin,job.range_count);
            std::size_t selected=full?batch.starts:0;
            for(const auto range:ranges)selected+=range.count;
            if(!selected)continue;
            if(geometry.drift_sections>1) {
                if(!workspace.code)throw Error("drift search requires a pattern cache");
                execute_drift_search_job(batch,job,scores.subspan(job_index*batch.score_stride,batch.starts),
                    workspace.product,workspace.drift,*workspace.code,stop,workspace.differential);
                continue;
            }
            const FftPreparedTemplate* prepared=nullptr;
            if(job.prepared_template!=std::numeric_limits<std::uint64_t>::max()) {
                if(job.prepared_template>=batch.prepared.size())throw Error("invalid prepared pattern FFT template");
                prepared=&batch.prepared[static_cast<std::size_t>(job.prepared_template)];
                for(const auto row:prepared->rows)if(row.size()!=transform)
                    throw Error("invalid prepared pattern FFT row");
            } else {
                if(!workspace.code)throw Error("pattern FFT generation needs a private pattern cache");
                workspace.code->set_stream_phase_samples(job.phase);
                if(job.symbol>std::numeric_limits<std::uint64_t>::max()/geometry.pattern.chips_per_symbol)
                    throw Error("pattern stream coordinate overflow");
                workspace.code->prepare_symbol(job.symbol*geometry.pattern.chips_per_symbol,stop);
            }
            const auto direct=pattern_fft_direct_eligible(geometry,!prepared,selected,
                batch.observations.size()>=length+batch.starts-1);
            const auto visit=[&](const auto& action) {
                if(full)for(std::size_t j=0;j<batch.starts;++j)action(j);
                else for(const auto range:ranges)
                    for(std::size_t j=range.first;j<range.first+range.count;++j)action(j);
            };
            const auto mode=effective_search_arithmetic(geometry);
            for(unsigned bit=0;bit<2;++bit) {
                auto& row=workspace.product;
                double norm=0;Complex square{};
                if(!prepared) {
                    std::fill(row.begin(),row.end(),Complex{});
                    for(std::size_t i=0;i<length;++i) {
                        if((i&4095U)==0)cancelled(stop);
                        auto value=template_value(*workspace.code,geometry,job,i,bit,batch.nominal_reference);
                        if(mode==SearchArithmetic::fp32)value={static_cast<float>(value.real()),static_cast<float>(value.imag())};
                        row[direct?i:length-1-i]=std::conj(value);norm+=std::norm(value);
                        if(geometry.sample_fit)square+=value*value*carrier_square(geometry,i);
                    }
                    if(!direct) {
                        pattern_fft(row,false,stop,mode);
                        for(std::size_t i=0;i<transform;++i)row[i]=pattern_fft_product(batch.spectrum[i],row[i],mode);
                    }
                } else {
                    norm=prepared->energy[bit];square=prepared->square[bit];
                    for(std::size_t i=0;i<transform;++i)row[i]=pattern_fft_product(batch.spectrum[i],prepared->rows[bit][i],mode);
                }
                if(!direct)pattern_fft(row,true,stop,mode);
                visit([&](std::size_t j) {
                    Complex dot{};
                    if(direct)for(std::size_t i=0;i<length;++i) {
                        if((i&4095U)==0)cancelled(stop);
                        if(mode==SearchArithmetic::fp32){
                            const auto x=batch.observations[j+i],t=row[i];
                            const auto z=std::complex<float>{static_cast<float>(dot.real()),static_cast<float>(dot.imag())}+
                                std::complex<float>{static_cast<float>(x.real()),static_cast<float>(x.imag())}*
                                std::complex<float>{static_cast<float>(t.real()),static_cast<float>(t.imag())};
                            dot={z.real(),z.imag()};
                        }else dot+=batch.observations[j+i]*row[i];
                    }
                    else dot=row[length-1+j];
                    const auto score=pattern_evidence(dot,
                        batch.energy_prefix[j+length]-batch.energy_prefix[j],norm,
                        geometry.evidence_count,geometry.noise_condition,geometry.real_rank,
                        geometry.sample_fit,geometry.sample_fit?square*batch.carrier_square[j]:Complex{});
                    auto& output=scores[job_index*batch.score_stride+j];
                    if(bit==0)output.zero=score;else output.one=score;
                });
            }
        }
    });
}
} // namespace datapump::modem::detail

namespace datapump::modem::detail::partitioned_paired {
namespace {
using Complex=FftComplex;
constexpr auto maximum=std::numeric_limits<std::size_t>::max();
constexpr double tau=2*std::numbers::pi;
void cancelled(std::stop_token stop){if(stop.stop_requested())throw Error("partitioned pattern search cancelled");}
std::size_t add(std::size_t a,std::size_t b){if(b>maximum-a)throw Error("partitioned size overflow");return a+b;}
std::size_t mul(std::size_t a,std::size_t b){if(a&&b>maximum/a)throw Error("partitioned size overflow");return a*b;}
std::span<const FftStartRange> selected(const FftSearchBatch& batch,const FftSearchJob& job){
    return batch.start_ranges.subspan(job.range_begin,job.range_count);
}
bool supported(const FftSearchBatch& batch){
    const auto& g=batch.geometry;
    return !g.sample_fit&&g.drift_sections<=1&&!g.differential_window_samples&&
        g.bins_per_symbol&&g.bins_per_symbol<=maximum&&g.bin_samples&&g.pattern.chip_samples&&
        g.pattern.chips_per_symbol&&g.pattern.sample_rate&&g.pattern.symbol_samples&&batch.starts&&
        (batch.nominal_reference.empty()||batch.nominal_reference.size()==g.bins_per_symbol);
}
void validate_job(const FftSearchBatch& batch,const FftSearchJob& job){
    if(job.range_begin>batch.start_ranges.size()||job.range_count>batch.start_ranges.size()-job.range_begin||
       !std::isfinite(job.clock_ratio)||job.clock_ratio<=0||!std::isfinite(job.frequency_hz))
        throw Error("invalid partitioned job/ranges");
    if(batch.private_template && !batch.private_template->matches(batch.geometry,job))
        throw Error("private template cache identity or eligibility mismatch");
    std::size_t previous=0;const auto L=static_cast<std::size_t>(batch.geometry.bins_per_symbol);
    for(const auto range:selected(batch,job)){
        if(!range.count||range.first<previous||range.first>=batch.starts||range.count>batch.starts-range.first)
            throw Error("invalid partitioned selected start");
        previous=range.first+range.count;
        if(previous>batch.observations.size()||L-1>batch.observations.size()-previous||
           previous>batch.energy_prefix.size()||L>batch.energy_prefix.size()-previous)
            throw Error("partitioned selected start not fully observed");
    }
}
template<class F> std::size_t tiles(const FftSearchBatch& batch,const FftSearchJob& job,std::size_t B,F&& action){
    std::size_t next=0,rank=0;
    for(const auto range:selected(batch,job)){
        const auto first=std::max(next,range.first/B),last=(range.first+range.count-1)/B;
        for(auto q=first;q<=last;++q){action(q,rank++);}
        next=last+1;
    }
    return rank;
}
std::size_t selected_count(const FftSearchBatch& batch,const FftSearchJob& job){
    std::size_t n=0;for(const auto range:selected(batch,job))n=add(n,range.count);return n;
}
template<class A,class B> bool overlaps(std::span<A> a,std::span<B> b){
    if(a.empty()||b.empty())return false;
    const auto first=reinterpret_cast<std::uintptr_t>(a.data()),second=reinterpret_cast<std::uintptr_t>(b.data());
    const auto a_size=mul(a.size(),sizeof(A)),b_size=mul(b.size(),sizeof(B));
    if(a_size>std::numeric_limits<std::uintptr_t>::max()-first||b_size>std::numeric_limits<std::uintptr_t>::max()-second)
        throw Error("partitioned address overflow");
    return first<second+b_size&&second<first+a_size;
}
template<class T> bool aliases_inputs(std::span<T> buffer,const FftSearchBatch& batch){
    return overlaps(buffer,batch.observations)||overlaps(buffer,batch.energy_prefix)||
        overlaps(buffer,batch.nominal_reference)||overlaps(buffer,batch.start_ranges);
}
std::span<Complex> row_at(Context& context,std::size_t row){
    const auto offset=mul(row,context.geometry.transform);
    if(offset<context.first.size())return context.first.subspan(offset,context.geometry.transform);
    return context.second.subspan(offset-context.first.size(),context.geometry.transform);
}
std::span<std::complex<float>> native_row_at(Context& context,std::size_t row){
    const auto offset=mul(row,context.geometry.transform);
    return std::span(context.native).subspan(offset,context.geometry.transform);
}
void double_transform(Context& context,std::span<Complex> row,bool inverse,std::stop_token stop,SearchArithmetic mode) {
    if(context.double_plan)search_fft::fft(row,inverse,*context.double_plan,stop);
    else pattern_fft(row,inverse,stop,mode);
}
void native_transform(Context& context,std::span<std::complex<float>> row,bool inverse,std::stop_token stop) {
    if(context.native_plan)search_fft::fft(row,inverse,*context.native_plan,stop);
    else throw Error("native FFT plan missing");
}
std::array<Complex,2> pair_value(PatternCode& code,const FftSearchBatch& batch,const FftSearchJob& job,std::size_t bin,
                               bool allow_private=true){
    const auto& g=batch.geometry;const auto& p=g.pattern;
    const auto position=static_cast<long double>(bin)*g.bin_samples+static_cast<long double>(g.bin_samples-1)/2;
    const auto source=position*job.clock_ratio;
    if(g.extended_clock_window&&source>=p.symbol_samples)return {};
    const auto chip_position=source/p.chip_samples;const auto local=static_cast<std::uint64_t>(chip_position);
    if(job.symbol>(std::numeric_limits<std::uint64_t>::max()-local)/p.chips_per_symbol)
        throw Error("pattern stream coordinate overflow");
    const auto first=job.symbol*p.chips_per_symbol,chip=first+local;
    const auto fraction=static_cast<double>(chip_position-local);
    const bool cached=!batch.nominal_reference.empty()&&job.clock_ratio==1&&!p.scramble&&!p.dsss&&
        p.spreading_mode==static_cast<std::uint32_t>(SpreadingMode::pattern);
    auto values=(allow_private&&batch.private_template)?batch.private_template->value(bin,job.clock_ratio):
        cached?batch.nominal_reference[bin]:p.shaped?code.shaped_values(first,static_cast<double>(source)):
        std::array<Complex,2>{code.value(chip,0,fraction),code.value(chip,1,fraction)};
    const auto rotation=std::polar(1.,tau*job.frequency_hz*static_cast<double>(position)/p.sample_rate);
    for(auto& value:values)value=value*rotation;
    return values;
}
} // namespace
long double operations(const Requirements& r,std::size_t jobs) {
    const auto p=static_cast<long double>(r.transform),q=static_cast<long double>(r.max_job_output_tiles);
    return 5*p*std::log2(p)*(r.input_tiles+2.L*jobs*(r.chunks+q))+
        12.L*jobs*r.chunks*q*p;
}
std::optional<Requirements> choose_geometry(std::size_t length,std::size_t starts,
        std::span<const FftStartRange> ranges,std::size_t jobs,std::size_t transform,std::size_t available) {
    if(!length||!starts||!jobs||!transform||(transform&(transform-1)))return {};
    std::size_t previous=0;
    for(const auto range:ranges) {
        if(!range.count||range.first<previous||range.first>=starts||range.count>starts-range.first)
            throw Error("invalid partitioned selected start");
        previous=range.first+range.count;
    }
    const auto n=static_cast<long double>(transform);
    auto best_work=.9L*(5*n*std::log2(n)+jobs*(20*n*std::log2(n)+12*n));
    std::optional<Requirements> best;
    for(std::size_t tile=16;tile<=transform/2 && tile<=16384;tile*=2) {
        Requirements candidate;candidate.tile_size=tile;candidate.transform=2*tile;
        candidate.length=length;candidate.chunks=1+(length-1)/tile;
        std::size_t first=maximum,last=0,next=0;
        for(const auto range:ranges) {
            const auto begin=std::max(next,range.first/tile),end=(range.first+range.count-1)/tile;
            if(begin<=end) {
                first=std::min(first,begin);last=std::max(last,end);
                candidate.max_job_output_tiles=add(candidate.max_job_output_tiles,end-begin+1);
            }
            next=end+1;
        }
        if(first==maximum)continue;
        candidate.first_input_tile=first;candidate.input_tiles=add(last-first,candidate.chunks);
        candidate.complex_count=mul(add(add(candidate.input_tiles,mul(2,candidate.max_job_output_tiles)),2),candidate.transform);
        if(candidate.complex_count>available)continue;
        const auto work=operations(candidate,jobs);
        if(work<best_work){best=candidate;best_work=work;}
    }
    return best;
}
std::optional<Requirements> choose(const FftSearchBatch& batch,const FftSearchJob& job,
        std::size_t jobs,std::size_t transform,std::size_t available) {
    if(!supported(batch)||job.prepared_template!=std::numeric_limits<std::uint64_t>::max()||
        job.range_count==maximum)return {};
    validate_job(batch,job);
    return choose_geometry(static_cast<std::size_t>(batch.geometry.bins_per_symbol),batch.starts,
        selected(batch,job),jobs,transform,available);
}
std::optional<Requirements> preflight(const FftSearchBatch& batch,std::span<const FftSearchJob> jobs,
                                      std::size_t B,std::size_t available){
    if(!supported(batch)||!B||(B&(B-1))||B>maximum/2)return {};
    Requirements r;r.tile_size=B;r.transform=2*B;r.length=static_cast<std::size_t>(batch.geometry.bins_per_symbol);
    r.chunks=1+(r.length-1)/B;std::size_t first=maximum,last=0;
    for(const auto& job:jobs){
        if(job.prepared_template!=std::numeric_limits<std::uint64_t>::max()||job.range_count==maximum)return {};
        validate_job(batch,job);
        const auto count=tiles(batch,job,B,[&](std::size_t q,std::size_t){first=std::min(first,q);last=std::max(last,q);});
        r.max_job_output_tiles=std::max(r.max_job_output_tiles,count);
    }
    if(first==maximum)return r;
    r.first_input_tile=first;r.input_tiles=add(last-first,r.chunks);
    r.complex_count=mul(add(add(r.input_tiles,mul(2,r.max_job_output_tiles)),2),r.transform);
    if(r.complex_count>available)return {};
    return r;
}
std::size_t native_bytes(const Requirements& r,SearchArithmetic mode) {
    if(!r.transform)return 0;
    return mode==SearchArithmetic::fp32?
        mul(add(r.complex_count,r.transform-1),sizeof(std::complex<float>)):
        mul(r.transform-1,sizeof(Complex));
}
namespace {
void prepare_double(Context& context,Work* work,std::stop_token stop) {
    const auto& r=context.geometry;const auto& batch=*context.batch;
    for(std::size_t m=0;m<r.input_tiles;++m){
        cancelled(stop);auto row=row_at(context,m);std::fill(row.begin(),row.end(),Complex{});
        const auto begin=mul(add(r.first_input_tile,m),r.tile_size);
        for(std::size_t n=0;n<r.transform-1;++n)if(begin<batch.observations.size()&&n<batch.observations.size()-begin)
            row[n]=batch.observations[begin+n];
        double_transform(context,row,false,stop,context.execution);
        if(work){++work->input_transforms;if(context.double_plan)++work->tabulated_fp64_transforms;}
    }
}
}
std::optional<Context> prepare(const FftSearchBatch& batch,const Requirements& r,
                               std::span<Complex> first,std::span<Complex> second,Work* work,std::stop_token stop,std::size_t additional_bytes){
    if(!supported(batch)||!r.tile_size||(r.tile_size&(r.tile_size-1))||r.tile_size>maximum/2||
       r.transform!=2*r.tile_size||r.length!=batch.geometry.bins_per_symbol||
       r.chunks!=1+(r.length-1)/r.tile_size)return {};
    cancelled(stop);Context context;context.batch=&batch;context.geometry=r;
    if(!r.input_tiles){if(r.complex_count||r.max_job_output_tiles)return {};return context;}
    const auto input_count=mul(r.input_tiles,r.transform),sum_count=mul(mul(2,r.max_job_output_tiles),r.transform);
    if(r.input_tiles<r.chunks||!r.max_job_output_tiles||
       r.max_job_output_tiles>r.input_tiles-r.chunks+1||
       r.complex_count!=add(add(input_count,sum_count),mul(2,r.transform))||
       r.complex_count>add(first.size(),second.size())||first.size()%r.transform||second.size()%r.transform||
       overlaps(first,second)||aliases_inputs(first,batch)||aliases_inputs(second,batch))return {};
    context.first=first;context.second=second;
    context.execution=effective_search_arithmetic(batch.geometry);
    const auto needed=native_bytes(r,context.execution);
    // Tiny/huge finite inputs remain on the original FP64 path. This check is
    // independent of timing geometry and does not exclude a candidate.
    const bool safe=context.execution!=SearchArithmetic::fp32 ||
        search_fft::float_input_safe(batch.observations,std::max(r.length,r.transform));
    if(context.execution==SearchArithmetic::fp32 && !safe)context.execution=SearchArithmetic::fp64;
    if(additional_bytes>=needed && context.execution==SearchArithmetic::fp32) {
        context.native_plan.emplace(r.transform,stop);context.native.resize(r.complex_count);
        if(context.extra_bytes()>additional_bytes)throw Error("native partition storage exceeds reservation");
        for(std::size_t m=0;m<r.input_tiles;++m) {
            cancelled(stop);auto row=native_row_at(context,m);std::fill(row.begin(),row.end(),std::complex<float>{});
            const auto begin=mul(add(r.first_input_tile,m),r.tile_size);
            for(std::size_t n=0;n<r.transform-1;++n)if(begin<batch.observations.size()&&n<batch.observations.size()-begin) {
                const auto value=batch.observations[begin+n];row[n]={static_cast<float>(value.real()),static_cast<float>(value.imag())};
            }
            native_transform(context,row,false,stop);
            if(work){++work->input_transforms;++work->native_fp32_transforms;}
        }
        return context;
    }
    if(context.execution==SearchArithmetic::fp32)context.execution=SearchArithmetic::fp64;
    if(additional_bytes>=native_bytes(r,SearchArithmetic::fp64))context.double_plan.emplace(r.transform,stop);
    if(context.extra_bytes()>additional_bytes)throw Error("partition FFT plan exceeds reservation");
    if(work && effective_search_arithmetic(batch.geometry)==SearchArithmetic::fp32)++work->precision_fallback_jobs;
    prepare_double(context,work,stop);return context;
}

bool score_direct(const FftSearchBatch& batch,const FftSearchJob& job,PatternCode& code,
                  std::span<Complex> output,Work* work,std::stop_token stop){
    if(!supported(batch)||job.prepared_template!=std::numeric_limits<std::uint64_t>::max()||
       job.range_count==maximum||output.size()<batch.starts)return false;
    validate_job(batch,job);cancelled(stop);
    const auto K=selected_count(batch,job);
    if(K>32||aliases_inputs(output,batch))return false;
    if(!K)return true;
    if(work&&batch.geometry.search_arithmetic!=static_cast<std::uint32_t>(SearchArithmetic::fp64))
        ++work->precision_fallback_jobs;
    code.set_stream_phase_samples(job.phase);
    if(job.symbol>std::numeric_limits<std::uint64_t>::max()/batch.geometry.pattern.chips_per_symbol)
        throw Error("pattern stream coordinate overflow");
    code.prepare_symbol(job.symbol*batch.geometry.pattern.chips_per_symbol,stop);
    std::array<std::array<Complex,2>,32> dots{};
    std::array<double,2> norm{};
    const auto L=static_cast<std::size_t>(batch.geometry.bins_per_symbol);
    for(std::size_t i=0;i<L;++i){
        if((i&4095U)==0)cancelled(stop);
        const auto values=pair_value(code,batch,job,i,false);
        const std::array<Complex,2> conjugate{std::conj(values[0]),std::conj(values[1])};
        for(unsigned bit=0;bit<2;++bit)norm[bit]+=std::norm(values[bit]);
        std::size_t rank=0;
        for(const auto range:selected(batch,job))for(std::size_t j=range.first;j<range.first+range.count;++j){
            for(unsigned bit=0;bit<2;++bit)dots[rank][bit]+=batch.observations[j+i]*conjugate[bit];
            ++rank;
        }
        if(work){work->template_values+=2;work->complex_products+=2*K;}
    }
    cancelled(stop);std::size_t rank=0;
    for(const auto range:selected(batch,job))for(std::size_t j=range.first;j<range.first+range.count;++j){
        cancelled(stop);std::array<double,2> evidence{};
        for(unsigned bit=0;bit<2;++bit)evidence[bit]=pattern_evidence(dots[rank][bit],
            batch.energy_prefix[j+L]-batch.energy_prefix[j],norm[bit],batch.geometry.evidence_count,
            batch.geometry.noise_condition,batch.geometry.real_rank,false);
        output[j]={evidence[0],evidence[1]};++rank;
    }
    if(work)work->selected_starts+=K;
    return true;
}
namespace {
// -log(1-f) <= f/(1-f) avoids a transcendental approximation in a rejection
// certificate. Every operation rounds outward, with a further FP64 scoring
// allowance. An inconclusive/nonfinite bound always requests exact refinement.
double evidence_upper(double magnitude,double energy,double norm,const FftSearchGeometry& g) {
    if(!std::isfinite(magnitude)||!std::isfinite(energy)||!std::isfinite(norm)||
       !std::isfinite(g.noise_condition)||g.noise_condition<=0)return std::numeric_limits<double>::infinity();
    if(g.evidence_count<4||energy<=1e-30||norm<=1e-30)return 0;
    const auto up=[](double x){return std::nextafter(x,std::numeric_limits<double>::infinity());};
    const auto down=[](double x){return std::nextafter(x,0.);};
    magnitude=up(magnitude*(1+16*std::numeric_limits<double>::epsilon()));
    const auto denominator=down(norm*g.noise_condition);
    if(!(denominator>0))return std::numeric_limits<double>::infinity();
    const auto fitted=up(up(magnitude*magnitude)/denominator);
    const auto f=up(fitted/energy);
    if(!(f<1))return std::numeric_limits<double>::infinity();
    const auto exponent=g.real_rank?(g.evidence_count-2)/2:g.evidence_count-1;
    const auto bound=up(up(exponent*f)/down(1-f));
    return up(bound+64*std::numeric_limits<double>::epsilon()*(1+bound));
}
}
namespace {
// Keep optional integer tile storage out of the original FP64/fallback frame.
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
bool score_direct_arithmetic(const FftSearchBatch& batch,const FftSearchJob& job,PatternCode& code,
        std::span<Complex> output,std::span<Complex> first,std::span<Complex> second,
        Work* work,std::stop_token stop,SearchArithmetic mode,std::size_t K,std::size_t L){
    const std::array<std::span<Complex>,2> rows{first.first(L),second.first(L)};
    code.set_stream_phase_samples(job.phase);
    if(job.symbol>std::numeric_limits<std::uint64_t>::max()/batch.geometry.pattern.chips_per_symbol)
        throw Error("pattern stream coordinate overflow");
    code.prepare_symbol(job.symbol*batch.geometry.pattern.chips_per_symbol,stop);
    std::array<double,2> norm{};
    for(std::size_t i=0;i<L;++i){
        if((i&4095U)==0)cancelled(stop);
        auto values=pair_value(code,batch,job,i);
        for(unsigned bit=0;bit<2;++bit){
            if(mode==SearchArithmetic::fp32)values[bit]={static_cast<float>(values[bit].real()),static_cast<float>(values[bit].imag())};
            rows[bit][i]=values[bit];norm[bit]+=std::norm(values[bit]);
        }
        if(work)work->template_values+=2;
    }
    std::array<std::size_t,integer_direct_start_limit> starts{};std::size_t rank=0;
    for(const auto range:selected(batch,job))for(std::size_t j=range.first;j<range.first+range.count;++j)starts[rank++]=j;
    std::array<std::array<Complex,2>,integer_direct_start_limit> evidence{};
    if(mode==SearchArithmetic::fp32){
        for(std::size_t k=0;k<K;++k)for(unsigned bit=0;bit<2;++bit){
            cancelled(stop);
            std::complex<float> sum{};
            for(std::size_t offset=0;offset<L;offset+=std::min(std::size_t{1024},L-offset)) {
                cancelled(stop);const auto count=std::min(std::size_t{1024},L-offset);
                const auto part=search_arithmetic::dot_fp32(batch.observations.subspan(starts[k]+offset,count),rows[bit].subspan(offset,count));
                sum+=std::complex<float>{static_cast<float>(part.real()),static_cast<float>(part.imag())};
            }
            const Complex dot{sum.real(),sum.imag()};
            evidence[k][bit]={pattern_evidence(dot,batch.energy_prefix[starts[k]+L]-batch.energy_prefix[starts[k]],
                norm[bit],batch.geometry.evidence_count,batch.geometry.noise_condition,batch.geometry.real_rank,false),0};
            if(work){++work->fp32_dots;work->complex_products+=L;}
        }
    }else{
        namespace arithmetic=search_arithmetic;
        std::array<std::array<arithmetic::BoundedDot,2>,integer_direct_start_limit> dots{};
        std::array<double,integer_direct_start_limit> input_energy{};std::array<double,2> template_energy{};
        std::array<std::array<arithmetic::Complex8,arithmetic::quantized_tile_size>,2> packed_templates{};
        std::array<arithmetic::Complex8,arithmetic::quantized_tile_size> packed_input{};
        const auto upper_add=[](double a,double b){return std::nextafter(a+b,std::numeric_limits<double>::infinity());};
        if(auto* cache=batch.quantized_direct) {
            std::array<std::array<arithmetic::CompensatedReduction,2>,integer_direct_start_limit> reductions{};
            constexpr auto tile=arithmetic::quantized_tile_size;
            const auto blocks=[](std::size_t n){return n/tile+(n%tile!=0);};
            if(cache->input.size()<batch.observations.size() || cache->input_residual.size()<batch.observations.size() || cache->row_residual[0].size()<L || cache->row_residual[1].size()<L || cache->input_blocks.size()<blocks(batch.observations.size()) ||
               cache->rows[0].size()<L || cache->rows[1].size()<L || cache->row_blocks[0].size()<blocks(L) || cache->row_blocks[1].size()<blocks(L))
                throw Error("integer search cache exceeds supplied storage");
            if(cache->source!=batch.observations.data() || cache->source_size!=batch.observations.size()) {
                cache->source=nullptr;cache->source_size=0;
                for(std::size_t offset=0;offset<batch.observations.size();offset+=std::min(tile,batch.observations.size()-offset)) {
                    cancelled(stop);const auto n=std::min(tile,batch.observations.size()-offset);
                    cache->input_blocks[offset/tile]=arithmetic::pack_compensated(batch.observations.subspan(offset,n),std::span(cache->input).subspan(offset,n),std::span(cache->input_residual).subspan(offset,n));
                }
                cache->source=batch.observations.data();cache->source_size=batch.observations.size();
            }
            for(unsigned bit=0;bit<2;++bit)for(std::size_t offset=0;offset<L;offset+=std::min(tile,L-offset)) {
                cancelled(stop);const auto n=std::min(tile,L-offset);
                cache->row_blocks[bit][offset/tile]=arithmetic::pack_compensated(rows[bit].subspan(offset,n),std::span(cache->rows[bit]).subspan(offset,n),std::span(cache->row_residual[bit]).subspan(offset,n));
                template_energy[bit]=upper_add(template_energy[bit],cache->row_blocks[bit][offset/tile].first.original_energy_upper);
            }
            for(std::size_t k=0;k<K;++k)for(std::size_t offset=0;offset<L;) {
                cancelled(stop);const auto position=starts[k]+offset;
                const auto n=std::min({L-offset,tile-offset%tile,tile-position%tile});
                // A subset's nonnegative energies cannot exceed its full tile.
                // These deliberately conservative bounds avoid per-start prefix
                // matrices while preserving exact integer operands and scales.
                auto qx=cache->input_blocks[position/tile];qx.first.size=qx.residual.size=n;
                input_energy[k]=upper_add(input_energy[k],qx.first.original_energy_upper);
                for(unsigned bit=0;bit<2;++bit) {
                    auto qt=cache->row_blocks[bit][offset/tile];qt.first.size=qt.residual.size=n;
                    const auto dots=arithmetic::dot_compensated_i8(
                        std::span(cache->input).subspan(position,n),std::span(cache->input_residual).subspan(position,n),
                        std::span(cache->rows[bit]).subspan(offset,n),std::span(cache->row_residual[bit]).subspan(offset,n));
                    arithmetic::accumulate_compensated(reductions[k][bit],dots[0],dots[1],dots[2],qx,qt);
                    if(work){work->int8_dots+=3;work->complex_products+=3*n;}
                }
                offset+=n;
            }
            for(std::size_t k=0;k<K;++k)for(unsigned bit=0;bit<2;++bit)
                dots[k][bit]=arithmetic::finish_compensated(reductions[k][bit]);
        } else {
        for(std::size_t offset=0;offset<L;offset+=std::min(arithmetic::quantized_tile_size,L-offset)){
            cancelled(stop);const auto count=std::min(arithmetic::quantized_tile_size,L-offset);
            std::array<arithmetic::QuantizedBlock,2> qt;
            for(unsigned bit=0;bit<2;++bit){
                qt[bit]=arithmetic::pack(rows[bit].subspan(offset,count),std::span(packed_templates[bit]).first(count));
                template_energy[bit]=upper_add(template_energy[bit],qt[bit].original_energy_upper);
            }
            for(std::size_t k=0;k<K;++k){
                const auto qx=arithmetic::pack(batch.observations.subspan(starts[k]+offset,count),std::span(packed_input).first(count));
                input_energy[k]=upper_add(input_energy[k],qx.original_energy_upper);
                for(unsigned bit=0;bit<2;++bit){
                    const auto dot=arithmetic::dot_i8(std::span(packed_input).first(count),std::span(packed_templates[bit]).first(count));
                    arithmetic::accumulate(dots[k][bit],arithmetic::combine(dot,qx,qt[bit]));
                    if(work){++work->int8_dots;work->complex_products+=count;}
                }
            }
        }
        }
        for(std::size_t k=0;k<K;++k){
            cancelled(stop);
            const auto energy=batch.energy_prefix[starts[k]+L]-batch.energy_prefix[starts[k]];
            std::array<double,2> bound{};
            for(unsigned bit=0;bit<2;++bit){auto bounded=dots[k][bit];
                bounded.error=upper_add(bounded.error,arithmetic::reference_roundoff_error(input_energy[k],template_energy[bit],L));
                bound[bit]=evidence_upper(arithmetic::magnitude_upper(bounded),energy,norm[bit],batch.geometry);
            }
            // Admission consumes the PAIR, including alternative-bit scores.
            // Once either bit could be retained, refine both so confidence,
            // chain evidence and later publication retain original arithmetic.
            if(batch.retain_floor>0&&bound[0]<batch.retain_floor&&bound[1]<batch.retain_floor){
                if(work)work->certified_rejects+=2;
                continue;
            }
            for(unsigned bit=0;bit<2;++bit){
                Complex dot{};
                for(std::size_t i=0;i<L;++i){
                    if((i&4095U)==0)cancelled(stop);
                    dot+=batch.observations[starts[k]+i]*std::conj(rows[bit][i]);
                }
                evidence[k][bit]={pattern_evidence(dot,energy,norm[bit],batch.geometry.evidence_count,
                    batch.geometry.noise_condition,batch.geometry.real_rank,false),0};
                if(work){++work->exact_refines;work->complex_products+=L;}
            }
        }
    }
    cancelled(stop);
    for(std::size_t k=0;k<K;++k)output[starts[k]]={evidence[k][0].real(),evidence[k][1].real()};
    if(work)work->selected_starts+=K;
    return true;
}
} // namespace
bool score_direct(const FftSearchBatch& batch,const FftSearchJob& job,PatternCode& code,
                  std::span<Complex> output,std::span<Complex> first,std::span<Complex> second,
                  Work* work,std::stop_token stop){
    const auto mode=effective_search_arithmetic(batch.geometry);
    if(mode==SearchArithmetic::matrix4)throw Error("matrix4 search arithmetic is not implemented");
    if(!search_arithmetic_is_int8(mode)&&mode!=SearchArithmetic::fp32&&mode!=SearchArithmetic::fp64)
        throw Error("invalid pattern search arithmetic");
    if(mode==SearchArithmetic::fp64)return score_direct(batch,job,code,output,work,stop);
    if(!supported(batch)||job.prepared_template!=std::numeric_limits<std::uint64_t>::max()||
       job.range_count==maximum||output.size()<batch.starts)return false;
    validate_job(batch,job);cancelled(stop);
    const auto K=selected_count(batch,job),L=static_cast<std::size_t>(batch.geometry.bins_per_symbol);
    const auto direct_limit=search_arithmetic_is_int8(mode)&&batch.quantized_direct?
        integer_direct_start_limit:std::size_t{32};
    if(K>direct_limit||aliases_inputs(output,batch)||overlaps(first,second)||
       overlaps(output,first)||overlaps(output,second)||aliases_inputs(first,batch)||aliases_inputs(second,batch))return false;
    if(!K)return true;
    if(!std::isfinite(batch.retain_floor)||batch.retain_floor<0)throw Error("invalid pattern retention floor");
    if(first.size()<L||second.size()<L){
        return score_direct(batch,job,code,output,work,stop);
    }
    return score_direct_arithmetic(batch,job,code,output,first,second,work,stop,mode,K,L);
}
namespace {
bool score_job_native(Context& context,const FftSearchJob& job,PatternCode& code,std::span<Complex> output,
        Work* work,std::stop_token stop,std::size_t Q) {
    const auto& batch=*context.batch;const auto& r=context.geometry;
    cancelled(stop);code.set_stream_phase_samples(job.phase);
    if(job.symbol>std::numeric_limits<std::uint64_t>::max()/batch.geometry.pattern.chips_per_symbol)
        throw Error("pattern stream coordinate overflow");
    code.prepare_symbol(job.symbol*batch.geometry.pattern.chips_per_symbol,stop);
    for(std::size_t slot=0;slot<2*Q;++slot) {
        auto sum=native_row_at(context,r.input_tiles+slot);
        std::fill(sum.begin(),sum.end(),std::complex<float>{});
    }
    std::array<double,2> norm{};
    const auto limit=search_fft::float_component_limit(std::max(r.length,r.transform));
    const std::array<std::span<std::complex<float>>,2> template_rows{
        native_row_at(context,r.input_tiles+2*r.max_job_output_tiles),
        native_row_at(context,r.input_tiles+2*r.max_job_output_tiles+1)};
    for(std::size_t offset=0,p=0;offset<r.length;offset+=std::min(r.tile_size,r.length-offset),++p) {
        cancelled(stop);
        for(const auto row:template_rows)std::fill(row.begin(),row.end(),std::complex<float>{});
        const auto count=std::min(r.tile_size,r.length-offset);
        for(std::size_t i=0;i<count;++i) {
            if((i&4095U)==0)cancelled(stop);
            const auto values=pair_value(code,batch,job,offset+i);
            for(const auto value:values)if(!std::isfinite(value.real()) || !std::isfinite(value.imag()) ||
                std::abs(value.real())>limit || std::abs(value.imag())>limit)return false;
            for(unsigned bit=0;bit<2;++bit) {
                const std::complex<float> value{static_cast<float>(values[bit].real()),static_cast<float>(values[bit].imag())};
                // Preserve the legacy FP32 template norm: square/sum the
                // float-rounded waveform in DOUBLE, not std::norm(float).
                norm[bit]+=std::norm(Complex{value.real(),value.imag()});
                template_rows[bit][r.tile_size-1-i]=std::conj(value);
            }
            if(work)work->template_values+=2;
        }
        for(unsigned bit=0;bit<2;++bit) {
            native_transform(context,template_rows[bit],false,stop);
            if(work){++work->template_transforms;++work->native_fp32_transforms;}
        }
        tiles(batch,job,r.tile_size,[&](std::size_t q,std::size_t rank) {
            cancelled(stop);const auto input=native_row_at(context,q+p-r.first_input_tile);
            for(unsigned bit=0;bit<2;++bit) {
                auto sum=native_row_at(context,r.input_tiles+2*rank+bit);
                const auto row=template_rows[bit];
                for(std::size_t k=0;k<r.transform;++k)sum[k]+=input[k]*row[k];
                if(work)work->complex_products+=r.transform;
            }
        });
    }
    for(std::size_t slot=0;slot<2*Q;++slot) {
        native_transform(context,native_row_at(context,r.input_tiles+slot),true,stop);
        if(work){++work->inverse_transforms;++work->native_fp32_transforms;}
    }
    if(!search_fft::finite(std::span(context.native)))return false;
    cancelled(stop);
    tiles(batch,job,r.tile_size,[&](std::size_t q,std::size_t rank) {
        cancelled(stop);const auto low=q*r.tile_size,high=add(low,r.tile_size);
        const std::array<std::span<std::complex<float>>,2> dots{
            native_row_at(context,r.input_tiles+2*rank),native_row_at(context,r.input_tiles+2*rank+1)};
        for(const auto range:selected(batch,job)) {
            const auto first=std::max(low,range.first),end=std::min(high,range.first+range.count);
            for(auto j=first;j<end;++j) {
                if((j&4095U)==0)cancelled(stop);std::array<double,2> evidence{};
                for(unsigned bit=0;bit<2;++bit) {
                    const auto dot=dots[bit][r.tile_size-1+j%r.tile_size];
                    evidence[bit]=pattern_evidence(Complex{dot.real(),dot.imag()},
                        batch.energy_prefix[j+r.length]-batch.energy_prefix[j],norm[bit],batch.geometry.evidence_count,
                        batch.geometry.noise_condition,batch.geometry.real_rank,false);
                }
                output[j]={evidence[0],evidence[1]};
            }
        }
    });
    if(work)work->selected_starts+=selected_count(batch,job);
    return true;
}
} // namespace
bool score_job(Context& context,const FftSearchJob& job,PatternCode& code,std::span<Complex> output,
               Work* work,std::stop_token stop){
    if(!context.batch)return false;const auto& batch=*context.batch;const auto& r=context.geometry;
    if(job.prepared_template!=std::numeric_limits<std::uint64_t>::max()||job.range_count==maximum||output.size()<batch.starts)return false;
    validate_job(batch,job);cancelled(stop);if(!job.range_count)return true;
    if(!r.input_tiles||!r.max_job_output_tiles)return false;
    std::size_t qfirst=maximum,qlast=0;
    const auto Q=tiles(batch,job,r.tile_size,[&](std::size_t q,std::size_t){qfirst=std::min(qfirst,q);qlast=std::max(qlast,q);});
    if(!Q)return true;
    if(Q>r.max_job_output_tiles||qfirst<r.first_input_tile||qlast-r.first_input_tile>r.input_tiles-r.chunks||
       overlaps(output,context.first)||overlaps(output,context.second)||aliases_inputs(output,batch))return false;
    if(!context.native.empty()) {
        if(score_job_native(context,job,code,output,work,stop,Q))return true;
        // Nothing was published. Rebuild original input and this complete job
        // in FP64; failed float work remains included in diagnostic work totals.
        context.native.clear();context.native_plan.reset();context.execution=SearchArithmetic::fp64;
        if(work)++work->precision_fallback_jobs;
        prepare_double(context,work,stop);
    }
    const auto mode=context.execution;
    if(work&&search_arithmetic_is_int8(mode))++work->precision_fallback_jobs;
    cancelled(stop);code.set_stream_phase_samples(job.phase);
    if(job.symbol>std::numeric_limits<std::uint64_t>::max()/batch.geometry.pattern.chips_per_symbol)
        throw Error("pattern stream coordinate overflow");
    code.prepare_symbol(job.symbol*batch.geometry.pattern.chips_per_symbol,stop);
    for(std::size_t slot=0;slot<2*Q;++slot){auto sum=row_at(context,r.input_tiles+slot);
        std::fill(sum.begin(),sum.end(),Complex{});}
    std::array<double,2> norm{};
    const std::array<std::span<Complex>,2> template_rows{row_at(context,r.input_tiles+2*r.max_job_output_tiles),
        row_at(context,r.input_tiles+2*r.max_job_output_tiles+1)};
    for(std::size_t offset=0,p=0;offset<r.length;offset+=std::min(r.tile_size,r.length-offset),++p){
        cancelled(stop);
        for(const auto row:template_rows)std::fill(row.begin(),row.end(),Complex{});
        const auto count=std::min(r.tile_size,r.length-offset);
        for(std::size_t i=0;i<count;++i){
            if((i&4095U)==0)cancelled(stop);auto values=pair_value(code,batch,job,offset+i,mode==SearchArithmetic::fp32);
            if(mode==SearchArithmetic::fp32)for(auto& value:values)
                value={static_cast<float>(value.real()),static_cast<float>(value.imag())};
            for(unsigned bit=0;bit<2;++bit){norm[bit]+=std::norm(values[bit]);
                template_rows[bit][r.tile_size-1-i]=std::conj(values[bit]);}
            if(work)work->template_values+=2;
        }
        for(unsigned bit=0;bit<2;++bit){double_transform(context,template_rows[bit],false,stop,mode);
            if(work){++work->template_transforms;if(context.double_plan)++work->tabulated_fp64_transforms;}}
        tiles(batch,job,r.tile_size,[&](std::size_t q,std::size_t rank){
            cancelled(stop);const auto input=row_at(context,q+p-r.first_input_tile);
            for(unsigned bit=0;bit<2;++bit){auto sum=row_at(context,r.input_tiles+2*rank+bit);
                const auto row=template_rows[bit];
                for(std::size_t k=0;k<r.transform;++k){
                    const auto value=pattern_fft_product(input[k],row[k],mode);
                    if(mode==SearchArithmetic::fp32){
                        const auto x=sum[k];
                        const auto z=std::complex<float>{static_cast<float>(x.real()),static_cast<float>(x.imag())}+
                            std::complex<float>{static_cast<float>(value.real()),static_cast<float>(value.imag())};
                        sum[k]={z.real(),z.imag()};
                    }else sum[k]+=value;
                }
                if(work)work->complex_products+=r.transform;}
        });
    }
    for(std::size_t slot=0;slot<2*Q;++slot){double_transform(context,row_at(context,r.input_tiles+slot),true,stop,mode);
        if(work){++work->inverse_transforms;if(context.double_plan)++work->tabulated_fp64_transforms;}}
    // Only now write scores. Host admission/peak publication follows successful
    // return, in the caller's unchanged job/start order. No component timing
    // or trial accounting is owned by this kernel.
    cancelled(stop);
    tiles(batch,job,r.tile_size,[&](std::size_t q,std::size_t rank){
        cancelled(stop);const auto low=q*r.tile_size,high=add(low,r.tile_size);
        const std::array<std::span<Complex>,2> dots{row_at(context,r.input_tiles+2*rank),row_at(context,r.input_tiles+2*rank+1)};
        for(const auto range:selected(batch,job)){
            const auto first=std::max(low,range.first),end=std::min(high,range.first+range.count);
            for(auto j=first;j<end;++j){if((j&4095U)==0)cancelled(stop);std::array<double,2> evidence{};
                for(unsigned bit=0;bit<2;++bit)evidence[bit]=pattern_evidence(
                    dots[bit][r.tile_size-1+j%r.tile_size],
                    batch.energy_prefix[j+r.length]-batch.energy_prefix[j],norm[bit],batch.geometry.evidence_count,
                    batch.geometry.noise_condition,batch.geometry.real_rank,false);
                output[j]={evidence[0],evidence[1]};
            }
        }
    });
    if(work)work->selected_starts+=selected_count(batch,job);
    return true;
}
}

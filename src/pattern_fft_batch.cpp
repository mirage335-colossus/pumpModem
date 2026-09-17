#include "pattern_fft_batch.hpp"
#include "search_parallel.hpp"
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
                       const FftSearchJob& job,std::size_t bin,unsigned bit) {
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
    const auto value=pattern.shaped?
        code.shaped_value(job.symbol*pattern.chips_per_symbol,bit,static_cast<double>(source_position)):
        code.value(chip,bit,fraction);
    return value*std::polar(1.,tau*job.frequency_hz*static_cast<double>(sample_position)/pattern.sample_rate);
}
Complex carrier_square(const FftSearchGeometry& geometry,std::uint64_t bin) {
    const auto phase=std::remainder(2*static_cast<long double>(tau)*geometry.carrier_hz*
        (static_cast<long double>(bin)*geometry.bin_samples+(geometry.bin_samples-1)/2.L)/
        geometry.pattern.sample_rate,static_cast<long double>(tau));
    return std::polar(1.,static_cast<double>(phase));
}
} // namespace
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
double pattern_evidence(Complex dot,double energy,double template_energy,double count,double condition,bool real_rank,
                bool exact_real,Complex template_square) {
    if(count<4 || energy<=1e-30 || template_energy<=1e-30)return 0;
    auto fitted=std::norm(dot)/(template_energy*condition);
    if(exact_real) {
        // Individual real samples fit two real carrier bases. Their exact
        // Gram matrix is encoded by sum(|template|^2) and sum(template^2).
        // The trace-only bound loses half the energy even for an exact fit.
        const auto determinant=template_energy*template_energy-std::norm(template_square);
        if(determinant>1e-12*template_energy*template_energy)
            fitted=2*(template_energy*std::norm(dot)-(template_square*dot*dot).real())/determinant;
    }
    const auto fraction=std::clamp(fitted/energy,0.,1.-1e-15);
    // Nonsingular quadrature bins use a covariance-eigenratio bound. Individual
    // real samples use the exact rank-two fit above; an ill-conditioned Gram
    // matrix retains the conservative lambda_max<=trace bound. These scores
    // assume independent Gaussian input samples, with unknown common variance.
    return -(real_rank?(count-2)/2:count-1)*std::log1p(-fraction);
}

FftSearchBatch::~FftSearchBatch() {
    OPENSSL_cleanse(geometry.pattern.spreading_seed.data(),geometry.pattern.spreading_seed.size());
    OPENSSL_cleanse(geometry.pattern.dsss_seed.data(),geometry.pattern.dsss_seed.size());
}
FftSearchWorkspace::FftSearchWorkspace(const Config& config,std::size_t transform,bool needs_code)
    :product(transform) {
    if(needs_code)code=std::make_unique<PatternCode>(config,config.stream_epoch);
}
std::size_t FftSearchWorkspace::working_bytes() const {
    return (code?code->working_bytes():0)+product.capacity()*sizeof(FftComplex);
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
       (geometry.sample_fit && batch.carrier_square.size()<batch.starts))
        throw Error("invalid pattern FFT batch geometry");
    const bool generates=std::any_of(jobs.begin(),jobs.end(),[](const auto& job) {
        return job.prepared_template==std::numeric_limits<std::uint64_t>::max();
    });
    if(generates && (!geometry.bin_samples || !geometry.pattern.chip_samples ||
       !geometry.pattern.chips_per_symbol || !geometry.pattern.sample_rate))
        throw Error("invalid generated pattern FFT geometry");
    for(const auto& workspace:workspaces)if(workspace.product.size()!=transform)
        throw Error("invalid pattern FFT worker workspace");
    // This backend preserves the original within-job floating-point order.
    // A GPU backend can map job/start/bin dimensions to many more execution
    // lanes, but must pass the same score and ordered-publication checks.
    parallel_search_ranges(jobs.size(),workspaces.size(),1,
        [&](std::size_t worker,std::size_t begin,std::size_t end) {
        auto& workspace=workspaces[worker];
        for(auto job_index=begin;job_index<end;++job_index) {
            cancelled(stop);
            const auto& job=jobs[job_index];
            const FftPreparedTemplate* prepared=nullptr;
            if(job.prepared_template!=std::numeric_limits<std::uint64_t>::max()) {
                if(job.prepared_template>=batch.prepared.size())throw Error("invalid prepared pattern FFT template");
                prepared=&batch.prepared[static_cast<std::size_t>(job.prepared_template)];
                for(const auto row:prepared->rows)if(row.size()!=transform)
                    throw Error("invalid prepared pattern FFT row");
            } else {
                if(!workspace.code)throw Error("pattern FFT generation needs a private pattern cache");
                workspace.code->set_stream_phase_samples(job.phase);
            }
            for(unsigned bit=0;bit<2;++bit) {
                auto& row=workspace.product;
                double norm=0;Complex square{};
                if(!prepared) {
                    std::fill(row.begin(),row.end(),Complex{});
                    for(std::size_t i=0;i<length;++i) {
                        const auto value=template_value(*workspace.code,geometry,job,i,bit);
                        row[length-1-i]=std::conj(value);norm+=std::norm(value);
                        if(geometry.sample_fit)square+=value*value*carrier_square(geometry,i);
                    }
                    pattern_fft(row,false,stop);
                    for(std::size_t i=0;i<transform;++i)row[i]=batch.spectrum[i]*row[i];
                } else {
                    norm=prepared->energy[bit];square=prepared->square[bit];
                    for(std::size_t i=0;i<transform;++i)row[i]=batch.spectrum[i]*prepared->rows[bit][i];
                }
                pattern_fft(row,true,stop);
                for(std::size_t j=0;j<batch.starts;++j) {
                    const auto score=pattern_evidence(row[length-1+j],
                        batch.energy_prefix[j+length]-batch.energy_prefix[j],norm,
                        geometry.evidence_count,geometry.noise_condition,geometry.real_rank,
                        geometry.sample_fit,geometry.sample_fit?square*batch.carrier_square[j]:Complex{});
                    auto& output=scores[job_index*batch.score_stride+j];
                    if(bit==0)output.zero=score;else output.one=score;
                }
            }
        }
    });
}
} // namespace datapump::modem::detail

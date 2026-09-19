#include "pattern_fft_batch.hpp"
#include "pattern_drift.hpp"
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
    // This backend preserves the original within-job floating-point order.
    // A GPU backend can map job/start/bin dimensions to many more execution
    // lanes, but must pass the same score and ordered-publication checks.
    parallel_search_ranges(jobs.size(),workspaces.size(),1,
        [&](std::size_t worker,std::size_t begin,std::size_t end) {
        auto& workspace=workspaces[worker];
        for(auto job_index=begin;job_index<end;++job_index) {
            cancelled(stop);
            const auto& job=jobs[job_index];
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
            }
            for(unsigned bit=0;bit<2;++bit) {
                auto& row=workspace.product;
                double norm=0;Complex square{};
                if(!prepared) {
                    std::fill(row.begin(),row.end(),Complex{});
                    for(std::size_t i=0;i<length;++i) {
                        if((i&4095U)==0)cancelled(stop);
                        const auto value=template_value(*workspace.code,geometry,job,i,bit,batch.nominal_reference);
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

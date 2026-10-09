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
            for(unsigned bit=0;bit<2;++bit) {
                auto& row=workspace.product;
                double norm=0;Complex square{};
                if(!prepared) {
                    std::fill(row.begin(),row.end(),Complex{});
                    for(std::size_t i=0;i<length;++i) {
                        if((i&4095U)==0)cancelled(stop);
                        const auto value=template_value(*workspace.code,geometry,job,i,bit,batch.nominal_reference);
                        row[direct?i:length-1-i]=std::conj(value);norm+=std::norm(value);
                        if(geometry.sample_fit)square+=value*value*carrier_square(geometry,i);
                    }
                    if(!direct) {
                        pattern_fft(row,false,stop);
                        for(std::size_t i=0;i<transform;++i)row[i]=batch.spectrum[i]*row[i];
                    }
                } else {
                    norm=prepared->energy[bit];square=prepared->square[bit];
                    for(std::size_t i=0;i<transform;++i)row[i]=batch.spectrum[i]*prepared->rows[bit][i];
                }
                if(!direct)pattern_fft(row,true,stop);
                visit([&](std::size_t j) {
                    Complex dot{};
                    if(direct)for(std::size_t i=0;i<length;++i) {
                        if((i&4095U)==0)cancelled(stop);
                        dot+=batch.observations[j+i]*row[i];
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
std::array<Complex,2> pair_value(PatternCode& code,const FftSearchBatch& batch,const FftSearchJob& job,std::size_t bin){
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
    auto values=cached?batch.nominal_reference[bin]:p.shaped?code.shaped_values(first,static_cast<double>(source)):
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
std::optional<Context> prepare(const FftSearchBatch& batch,const Requirements& r,
                               std::span<Complex> first,std::span<Complex> second,Work* work,std::stop_token stop){
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
    for(std::size_t m=0;m<r.input_tiles;++m){
        cancelled(stop);auto row=row_at(context,m);std::fill(row.begin(),row.end(),Complex{});
        const auto begin=mul(add(r.first_input_tile,m),r.tile_size);
        for(std::size_t n=0;n<r.transform-1;++n)if(begin<batch.observations.size()&&n<batch.observations.size()-begin)
            row[n]=batch.observations[begin+n];
        pattern_fft(row,false,stop);if(work)++work->input_transforms;
    }
    return context;
}
bool score_direct(const FftSearchBatch& batch,const FftSearchJob& job,PatternCode& code,
                  std::span<Complex> output,Work* work,std::stop_token stop){
    if(!supported(batch)||job.prepared_template!=std::numeric_limits<std::uint64_t>::max()||
       job.range_count==maximum||output.size()<batch.starts)return false;
    validate_job(batch,job);cancelled(stop);
    const auto K=selected_count(batch,job);
    if(K>32||aliases_inputs(output,batch))return false;
    if(!K)return true;
    code.set_stream_phase_samples(job.phase);
    if(job.symbol>std::numeric_limits<std::uint64_t>::max()/batch.geometry.pattern.chips_per_symbol)
        throw Error("pattern stream coordinate overflow");
    code.prepare_symbol(job.symbol*batch.geometry.pattern.chips_per_symbol,stop);
    std::array<std::array<Complex,2>,32> dots{};
    std::array<double,2> norm{};
    const auto L=static_cast<std::size_t>(batch.geometry.bins_per_symbol);
    for(std::size_t i=0;i<L;++i){
        if((i&4095U)==0)cancelled(stop);
        const auto values=pair_value(code,batch,job,i);
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
            if((i&4095U)==0)cancelled(stop);const auto values=pair_value(code,batch,job,offset+i);
            for(unsigned bit=0;bit<2;++bit){norm[bit]+=std::norm(values[bit]);
                template_rows[bit][r.tile_size-1-i]=std::conj(values[bit]);}
            if(work)work->template_values+=2;
        }
        for(unsigned bit=0;bit<2;++bit){pattern_fft(template_rows[bit],false,stop);
            if(work)++work->template_transforms;}
        tiles(batch,job,r.tile_size,[&](std::size_t q,std::size_t rank){
            cancelled(stop);const auto input=row_at(context,q+p-r.first_input_tile);
            for(unsigned bit=0;bit<2;++bit){auto sum=row_at(context,r.input_tiles+2*rank+bit);
                const auto row=template_rows[bit];
                for(std::size_t k=0;k<r.transform;++k)sum[k]+=input[k]*row[k];
                if(work)work->complex_products+=r.transform;}
        });
    }
    for(std::size_t slot=0;slot<2*Q;++slot){pattern_fft(row_at(context,r.input_tiles+slot),true,stop);
        if(work)++work->inverse_transforms;}
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

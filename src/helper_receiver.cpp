#include <type_traits>
#include "datapump/helper_receiver.hpp"
#include "helper_punctuation_numeric.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/helper_filter.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <utility>

namespace datapump::helper {
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr std::size_t vertices=11;
constexpr std::size_t narrow_bins=72;
using Pair=std::array<double,2>;
void attach_punctuation(Reference& reference,const PunctuationObservation& observation) {
    reference.punctuation=observation.known && observation.last_sample<=reference.sample?
        observation:PunctuationObservation{};
}
// Raw fit state never carries decoded metadata. Keep every measured field, but
// do not reserve two dormant strings/Metadata optionals in the persistent state.
struct ToneReference {
    std::uint64_t sample=0;
    std::uint32_t sample_rate=0;
    double frequency_hz=0,phase_radians=0;
    double frequency_uncertainty_hz=0,phase_uncertainty_radians=0;
    double dwell_start_seconds=0,dwell_uncertainty_seconds=0;
    bool frequency_valid=false,phase_valid=false,timing_valid=false,phase_observation_known=false;
    std::uint64_t frequency_sample=0,phase_observation_sample=0,frequency_first_sample=0;
    FrequencyFit frequency_fit=FrequencyFit::unknown;
    double frequency_phase_diffusion_gain=0,frequency_process_uncertainty_hz=0;
    double phase_process_uncertainty_radians=0;
    // Actual local-fit coherence for optional metadata only. Keep these before
    // adding the conservative synchronization process allowance; recovering
    // them by subtracting large published totals would lose numerical detail.
    double metadata_frequency_uncertainty_hz=0,metadata_phase_uncertainty_radians=0;
    Reference publication(const PunctuationObservation& punctuation) const {
        Reference r;
        r.sample=sample;r.sample_rate=sample_rate;
        r.frequency_hz=frequency_hz;r.phase_radians=phase_radians;
        r.frequency_uncertainty_hz=frequency_uncertainty_hz;
        r.phase_uncertainty_radians=phase_uncertainty_radians;
        r.dwell_start_seconds=dwell_start_seconds;
        r.dwell_uncertainty_seconds=dwell_uncertainty_seconds;
        r.frequency_valid=frequency_valid;r.phase_valid=phase_valid;r.timing_valid=timing_valid;
        r.frequency_sample=frequency_sample;r.phase_observation_known=phase_observation_known;
        r.phase_observation_sample=phase_observation_sample;
        r.frequency_first_sample=frequency_first_sample;r.frequency_fit=frequency_fit;
        r.frequency_phase_diffusion_gain=frequency_phase_diffusion_gain;
        r.frequency_process_uncertainty_hz=frequency_process_uncertainty_hz;
        attach_punctuation(r,punctuation);return r;
    }
};
struct Block { double xx=0,xy=0,yy=0; };
struct Tri { std::array<Block,vertices> diagonal{};std::array<Block,vertices-1> upper{}; };
struct Cross { std::array<Block,vertices> diagonal{};std::array<Block,vertices-1> upper{},lower{}; };
struct Matrix { double a=0,b=0,c=0,d=0; };
Matrix matrix(Block b) {return {b.xx,b.xy,b.xy,b.yy};}
Matrix operator-(Matrix x,Matrix y) {return {x.a-y.a,x.b-y.b,x.c-y.c,x.d-y.d};}
Matrix operator*(Matrix x,Matrix y) {
    return {x.a*y.a+x.b*y.c,x.a*y.b+x.b*y.d,x.c*y.a+x.d*y.c,x.c*y.b+x.d*y.d};
}
Pair operator*(Matrix m,Pair x) {return {m.a*x[0]+m.b*x[1],m.c*x[0]+m.d*x[1]};}
Pair operator-(Pair x,Pair y) {return {x[0]-y[0],x[1]-y[1]};}
bool inverse(Matrix m,Matrix& out) {
    const auto det=m.a*m.d-m.b*m.c;
    if(!std::isfinite(det)||det<=1e-18)return false;
    out={m.d/det,-m.b/det,-m.c/det,m.a/det};return true;
}
Block add(Block a,Block b,double scale=1) {return {a.xx+scale*b.xx,a.xy+scale*b.xy,a.yy+scale*b.yy};}
Block outer(Pair p,double weight) {return {weight*p[0]*p[0],weight*p[0]*p[1],weight*p[1]*p[1]};}
double dot(Pair a,Pair b) {return a[0]*b[0]+a[1]*b[1];}
double phase_wrap(double x) {return std::remainder(x,tau);}
double smooth(double x) {x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x));}
double smooth_derivative(double x) {return x<=0||x>=1?0:30*x*x*(1-x)*(1-x);}
// Auxiliary reversal and metadata templates match the C3 transmit shape;
// common/pilot4ms ramps continue to use the C2 smooth() above.
double auxiliary_smooth(double x) {
    x=std::clamp(x,0.,1.);return x*x*x*x*(35+x*(-84+x*(70-20*x)));
}
double auxiliary_smooth_derivative(double x) {
    return x<=0||x>=1?0:140*x*x*x*(1-x)*(1-x)*(1-x);
}
bool solve6(std::array<std::array<double,6>,6> a,std::array<double,6> b,
            std::array<double,6>& x) {
    for(unsigned i=0;i<6;++i) {
        unsigned pivot=i;
        for(unsigned j=i+1;j<6;++j)if(std::abs(a[j][i])>std::abs(a[pivot][i]))pivot=j;
        if(std::abs(a[pivot][i])<1e-14)return false;
        std::swap(a[i],a[pivot]);std::swap(b[i],b[pivot]);
        for(unsigned j=i+1;j<6;++j) {
            const auto f=a[j][i]/a[i][i];
            for(unsigned k=i+1;k<6;++k)a[j][k]-=f*a[i][k];
            b[j]-=f*b[i];
        }
    }
    for(int i=5;i>=0;--i) {
        auto v=b[static_cast<unsigned>(i)];
        for(unsigned j=static_cast<unsigned>(i)+1;j<6;++j)v-=a[static_cast<unsigned>(i)][j]*x[j];
        x[static_cast<unsigned>(i)]=v/a[static_cast<unsigned>(i)][static_cast<unsigned>(i)];
    }
    return true;
}
}
namespace {
struct ProfileDiagnostics {
    std::uint64_t look=0;
    std::size_t cells=0,invalid_response=0,invalid_model=0,unresolved_energy=0,
        line_rejected=0,coherence_rejected=0,ray_rejected=0,ray_retained=0,partition_mismatches=0;
    double far_frequency=0,far_operator_norm=0,far_noise_variance=0,far_q=0,
        far_cn0=0,far_line_ratio=1e100,far_ray_ratio=1e100,
        far_line_energy=0,far_line_noise_bound=0,far_line_model=0,
        far_n=0,precision_log=0,admission_log=0,far_old_line_ratio=1e100,far_old_ray_ratio=1e100,far_coherence_ratio=1e100;
};
class FrequencyProfile {
    static constexpr double tau=2*std::numbers::pi;
    static constexpr std::size_t capacity=2048,phase_slots=64,mask_slots=32,ray_slots=64,max_cells=32768,coherence_rows=24;
    struct Stored {std::complex<float> value{};float noise=0;};
    struct M {double a=0,b=0,c=0,d=0;};
    struct Moment {double xx=0,xy=0,yy=0;};
    static Moment sum(Moment a,Moment b) {return {a.xx+b.xx,a.xy+b.xy,a.yy+b.yy};}
    static Moment difference(Moment a,Moment b) {return {a.xx-b.xx,a.xy-b.xy,a.yy-b.yy};}
    static double project(Moment a,std::complex<double> direction) {
        return direction.real()*direction.real()*a.xx+2*direction.real()*direction.imag()*a.xy+
            direction.imag()*direction.imag()*a.yy;
    }
    static M mul(M x,M y) {return {x.a*y.a+x.b*y.c,x.a*y.b+x.b*y.d,x.c*y.a+x.d*y.c,x.c*y.b+x.d*y.d};}
    static M transpose(M x) {return {x.a,x.c,x.b,x.d};}
    static double norm(M x) {
        const auto aa=x.a*x.a+x.c*x.c,bb=x.a*x.b+x.c*x.d,dd=x.b*x.b+x.d*x.d;
        return std::sqrt(std::max(0.,.5*(aa+dd+std::hypot(aa-dd,2*bb))));
    }
    static double eigen(M x) {return .5*(x.a+x.d+std::hypot(x.a-x.d,x.b+x.c));}
    static std::optional<M> inverse(M x) {
        const auto det=x.a*x.d-x.b*x.c;
        if(!std::isfinite(det)||std::abs(det)<1e-12)return {};
        return M{x.d/det,-x.b/det,-x.c/det,x.a/det};
    }
    struct Ingest {
        std::array<Stored,capacity> ring{};
        std::uint64_t next=0,bin_first=0,last_look=0,scheduled_end=0,ordinal=0;
        std::size_t count=0,position=0,bin_count=0;
        std::complex<double> oscillator{1,0},rhs{};
        double energy=0;
        bool configured=false,valid=true;
    } input_;
    std::array<Stored,capacity> ring_{};
    std::array<std::uint8_t,capacity> slots_{},groups_{};
    std::array<std::array<std::uint16_t,phase_slots+1>,coherence_rows> group_counts_{};
    std::size_t group_count_=0;
    std::array<std::array<Moment,phase_slots>,ray_slots> ray_moments_{};
    std::array<Moment,ray_slots> ray_mask_{};
    std::array<Moment,2*ray_slots+1> ray_prefix_{};
    // This scratch is funded by the optional object, not by the base strong
    // receiver's project stack allowance.
    std::array<std::size_t,phase_slots> counts_{},recent_counts_{};
    std::array<std::uint64_t,phase_slots> recent_support_first_{},recent_support_end_{};
    // Fixed raw-bin coordinates, independently of every tested frequency.
    std::array<std::complex<double>,capacity> raw_y_{};
    static constexpr std::size_t shape_positions=4,shape_slots=mask_slots/shape_positions;
    static constexpr std::size_t maximum_work_units_per_advance=8;
    struct ShapeGroup {
        std::array<std::array<double,6>,6> gram{};
        std::array<double,6> rhs{};
        double gram_error=0,rhs_error=0,gram_scale=0,rhs_scale=0;
        std::size_t n=0;
    };
    std::array<ShapeGroup,coherence_rows+1> shape_groups_{};
    std::array<double,phase_slots> shape_raw_energy_{};
    std::array<bool,phase_slots> shape_candidates_{};
    std::size_t shape_cell_=0,shape_offset_=0,shape_group_count_=0;
    std::size_t shape_omitted_a_=SIZE_MAX,shape_omitted_b_=SIZE_MAX,shape_subset_phase_=0;
    double shape_largest_=0,shape_smallest_=0,shape_q_=0,shape_threshold_=0;
    double shape_aperture_energy_=0,shape_delta_gram_=0,shape_delta_rhs_=0;
    bool shape_pending_=false,shape_table_ready_=false,shape_mask_ready_=false;
    M raw_whitening_{};
    double raw_noise_gain_=0;
    std::array<double,phase_slots> xx_{},xy_{},yy_{},recent_energy_{};
    std::array<std::complex<double>,ray_slots+1> ray_directions_{};
    std::uint32_t rate_=0;std::uint64_t next_=0,look_=0;
    std::size_t bin_n_=0,target_=0,count_=0,position_=0;
    double expected_=0,width_=0,sigma_=0,clock_=0,cc_=0,ss_=0,cs_=0,lambda_=0;
    M covariance_{};
    std::complex<double> step_{};
    std::optional<Reference> update_;
    ProfileDiagnostics diagnostics_{};
    struct Work {
        std::uint64_t first=0,end=0,revision=0,generation=0,prepared_ordinal=0;
        std::size_t cells=0,cursor=0,work_units=0,presence_cursor=0;
        double duration=0,cell_width=0,score_log=0,admission_log=0,phase_bound=0,
            coherence_log=0,group_phase_bound=0,max_stored=0,variance=0;
        std::uint64_t recent_first=0,presence_first=0,presence_end=0;
        double recent_cn0=0;
        double union_low=std::numeric_limits<double>::infinity(),union_high=-union_low;
        bool present=false,recent=false,pending=false,started=false,complete=false,presence_prepass_complete=false;
    } work_;
    std::optional<Reference> qualified_;
    FrequencyWorkDecline decline_=FrequencyWorkDecline::warming_aperture;
    // ray_moments_ is reused as nominal-dwell/phase-slot prefix sums of
    // (Re sum, Im sum, full energy) until every coherence mask is evaluated.
    // It is then overwritten for the existing common-ray statistic.
    Moment group_range(int row,std::size_t left,std::size_t right) const {
        if(row<0||static_cast<std::size_t>(row)>=group_count_)return {};
        const auto hi=right==phase_slots?ray_mask_[row]:right?ray_moments_[row][right-1]:Moment{};
        const auto lo=left?ray_moments_[row][left-1]:Moment{};
        return difference(hi,lo);
    }
    std::size_t group_bins(int row,std::size_t left,std::size_t right) const {
        if(row<0||static_cast<std::size_t>(row)>=group_count_)return 0;
        return group_counts_[row][right]-group_counts_[row][left];
    }
    std::complex<double> geometric(double frequency) const {
        const auto w=tau*frequency/rate_;
        if(std::abs(w)<1e-12)return {static_cast<double>(bin_n_),0};
        return (std::sin(.5*w*bin_n_)/std::sin(.5*w))*std::polar(1.,.5*w*(bin_n_-1));
    }
    M forward_response(double f) const {
        const auto minus=geometric(f-expected_),plus=geometric(f+expected_);
        const M cross{.5*(minus.real()+plus.real()),-.5*(plus.imag()+minus.imag()),
                      .5*(plus.imag()-minus.imag()),-.5*(minus.real()-plus.real())};
        const auto det=cc_*ss_-cs_*cs_;
        const M inv{ss_/det,-cs_/det,-cs_/det,cc_/det};
        auto result=mul(inv,cross);result.c=-result.c;result.d=-result.d;
        return result;
    }
    std::optional<M> response(double f) const {return inverse(forward_response(f));}
    std::complex<double> rotation(double f,std::uint64_t at) const {
        const auto cycles=std::remainder(static_cast<long double>(f)*at/rate_,1.L);
        return std::polar(1.,-tau*static_cast<double>(cycles));
    }
    static double chi(double degrees,double logarithm) {
        return degrees+2*std::sqrt(degrees*logarithm)+2*logarithm;
    }
    static double ray_chi(double n,double logarithm) {
        // For fixed true nonnegative amplitude and common phase, distance to
        // that ray is dominated by Z_perp^2 + min(Z_parallel,0)^2. Its MGF
        // obeys log M(t)<=1.5t+2t^2/(1-2t). Covariance<=V I follows by
        // convex Gaussian comparison; the common phase is nuisance-minimized.
        return 1.5*n+std::sqrt(8*n*logarithm)+2*logarithm;
    }
    void finish_ingest_bin() {
        const auto det=cc_*ss_-cs_*cs_;
        const auto a=(ss_*input_.rhs.real()-cs_*input_.rhs.imag())/det;
        const auto b=(cc_*input_.rhs.imag()-cs_*input_.rhs.real())/det;
        const auto explained=a*input_.rhs.real()+b*input_.rhs.imag();
        const auto z=std::complex<double>{a,-b}*rotation(expected_,input_.bin_first);
        input_.ring[input_.position]={static_cast<std::complex<float>>(z),
            static_cast<float>(std::nextafter((std::max(0.,input_.energy-explained)+4096*std::numeric_limits<double>::epsilon()*
                (input_.energy+std::abs(explained)))*(1+1e-6),
                                             std::numeric_limits<double>::infinity()))};
        input_.position=(input_.position+1)%capacity;input_.count=std::min(input_.count+1,capacity);
        input_.bin_first+=bin_n_;input_.bin_count=0;input_.oscillator={1,0};input_.rhs={};input_.energy=0;
        if(input_.count>=target_ && (!input_.last_look||input_.next-input_.last_look>=rate_*2/5)) {
            input_.last_look=input_.next;input_.scheduled_end=input_.next;
            if(input_.ordinal!=UINT64_MAX)++input_.ordinal;
        }
    }
    bool begin_analysis() {
        work_.first=next_-count_*bin_n_,work_.end=next_-1;
        work_.duration=static_cast<double>(count_*bin_n_)/rate_;
        // Retain every frequency cell, including both endpoints. Refinement
        // geometry is fixed before samples; rejected looks still spend.
        work_.cells=std::max<std::size_t>(1,static_cast<std::size_t>(std::ceil(128*width_*work_.duration)));
        if(work_.cells>max_cells)throw Error("optional Helper Carrier fixed cell budget exceeded");
        work_.cell_width=2*width_/work_.cells;
        const auto ordinal=static_cast<double>(look_);
        const auto spending=std::log(ordinal)+std::log(ordinal+1);
        const auto common_log=std::log(16000.)+spending;
        // Confidence-set inversion controls the fixed true cell and one
        // deterministic clean mask. Testing FALSE hypotheses does not spend
        // coverage probability. All cells/masks still remain in the set if
        // compatible; the full look-elsewhere factor is retained separately
        // for the noise-existence/recent maxima below.
        work_.score_log=std::log(2.)+common_log;
        work_.admission_log=work_.score_log+std::log(static_cast<double>(work_.cells))+std::log(static_cast<double>(phase_slots));
        diagnostics_={};diagnostics_.look=look_;diagnostics_.cells=work_.cells;
        diagnostics_.precision_log=work_.score_log;diagnostics_.admission_log=work_.admission_log;
        const auto model_z=std::max(8.,std::sqrt(2*(common_log+std::log(4.))));
        work_.phase_bound=model_z*sigma_*std::sqrt(work_.duration)+tau*work_.cell_width*work_.duration/4;
        // A continuous mask group has <=200ms center support plus one full
        // raw bin. Its arbitrary complex mean is anchored at its midpoint,
        // so half-cell frequency error has <=half-support phase distance.
        // The Wiener envelope conservatively uses the FULL support duration.
        // Spend independently over every possible nonempty group at this look.
        const auto group_support=.2+static_cast<double>(bin_n_)/rate_;
        work_.coherence_log=common_log+std::log(4.);
        const auto group_z=std::max(8.,std::sqrt(2*(common_log+std::log(4.*(coherence_rows+1)))));
        work_.group_phase_bound=group_z*sigma_*std::sqrt(group_support)+tau*work_.cell_width*group_support/4;
        auto& counts=counts_;auto& recent_counts=recent_counts_;counts.fill(0);recent_counts.fill(0);
        const auto recent_span=std::min(work_.duration,1.6);
        work_.recent_first=next_-static_cast<std::uint64_t>(recent_span*rate_);
        double raw_noise=0;work_.max_stored=0;
        for(auto& row:group_counts_)row.fill(0);
        group_count_=0;std::uint64_t first_dwell=0;
        for(std::size_t j=0;j<count_;++j) {
            const auto& bin=ring_[(position_+j)%target_];raw_noise+=bin.noise;
            work_.max_stored=std::max(work_.max_stored,std::abs(static_cast<std::complex<double>>(bin.value)));
            const auto center=static_cast<long double>(work_.first+j*bin_n_)+.5L*(bin_n_-1);
            auto phase=std::fmod(center/rate_,.4L);if(phase<0)phase+=.4L;
            const auto slot=std::min<std::size_t>(phase_slots-1,static_cast<std::size_t>(phase/.4L*phase_slots));
            slots_[j]=static_cast<std::uint8_t>(slot);++counts[slot];
            const auto absolute_dwell=static_cast<std::uint64_t>(std::floor(center/rate_/.4L));
            if(j==0)first_dwell=absolute_dwell;
            const auto group=absolute_dwell-first_dwell;
            if(group>=coherence_rows)return false; // Unsupported geometry yields optional precision.
            groups_[j]=static_cast<std::uint8_t>(group);group_count_=std::max(group_count_,static_cast<std::size_t>(group+1));
            ++group_counts_[group][slot+1];
            if(work_.first+j*bin_n_>=work_.recent_first)++recent_counts[slot];
        }
        recent_support_first_.fill(UINT64_MAX);recent_support_end_.fill(0);
        for(std::size_t j=0;j<count_;++j) {
            const auto at=work_.first+j*bin_n_;if(at<work_.recent_first)continue;
            for(std::size_t offset=0;offset<phase_slots;++offset)
                if((slots_[j]+phase_slots-offset)%phase_slots<mask_slots) {
                    recent_support_first_[offset]=std::min(recent_support_first_[offset],at);
                    recent_support_end_[offset]=std::max(recent_support_end_[offset],at+bin_n_-1);
                }
        }
        for(std::size_t g=0;g<group_count_;++g)
            for(std::size_t k=0;k<phase_slots;++k)group_counts_[g][k+1]+=group_counts_[g][k];
        const auto degrees=static_cast<double>(count_*(bin_n_-2));
        const auto denominator=degrees-2*std::sqrt(degrees*common_log);
        if(!(denominator>0)||!(raw_noise>0))return false;
        work_.variance=raw_noise/denominator;
        shape_pending_=shape_table_ready_=shape_mask_ready_=false;
        shape_raw_energy_.fill(0);shape_aperture_energy_=0;
        for(std::size_t j=0;j<count_;++j) {
            const auto at=work_.first+j*bin_n_;
            const auto raw=static_cast<std::complex<double>>(ring_[j].value)*std::conj(rotation(expected_,at));
            raw_y_[j]={raw_whitening_.a*raw.real()+raw_whitening_.b*raw.imag(),
                       raw_whitening_.c*raw.real()+raw_whitening_.d*raw.imag()};
            const auto energy=std::norm(raw_y_[j]);shape_aperture_energy_+=energy;
            for(std::size_t offset=0;offset<phase_slots;++offset)
                if((slots_[j]+phase_slots-offset)%phase_slots<mask_slots)shape_raw_energy_[offset]+=energy;
        }
        return true;
    }
    Moment paired_range(int group,unsigned half,std::size_t left,std::size_t right) const {
        if(group<0||static_cast<std::size_t>(group)>=group_count_)return {};
        const auto row=2*static_cast<std::size_t>(group)+half;
        const auto hi=right==phase_slots?ray_mask_[row]:right?ray_moments_[row][right-1]:Moment{};
        const auto lo=left?ray_moments_[row][left-1]:Moment{};
        return difference(hi,lo);
    }
    static double bound_norm(M value) {
        return std::nextafter((std::abs(value.a)+std::abs(value.b)+std::abs(value.c)+std::abs(value.d))*
                              (1+8*std::numeric_limits<double>::epsilon()),
                              std::numeric_limits<double>::infinity());
    }
    static double bound_vector(std::array<double,2> value) {
        return std::nextafter((std::abs(value[0])+std::abs(value[1]))*
            (1+4*std::numeric_limits<double>::epsilon()),std::numeric_limits<double>::infinity());
    }
    static std::array<double,2> apply(M matrix,std::array<double,2> value) {
        return {matrix.a*value[0]+matrix.b*value[1],matrix.c*value[0]+matrix.d*value[1]};
    }
    void admit_cell(std::size_t cell) {
        work_.union_low=std::min(work_.union_low,expected_-width_+cell*work_.cell_width);
        work_.union_high=std::max(work_.union_high,expected_-width_+(cell+1)*work_.cell_width);
    }
    // Fixed raw coordinates and exact forward columns, independent of every
    // alternative frequency. This table replaces the failed two-pass paired
    // search. Its sufficient statistics reuse the funded ray prefix storage.
    bool prepare_shape_table() {
        const auto f=expected_-width_+(shape_cell_+.5)*work_.cell_width;
        const auto forward=mul(raw_whitening_,forward_response(f));
        shape_largest_=norm(forward);
        const auto determinant=forward.a*forward.d-forward.b*forward.c;
        shape_smallest_=shape_largest_>0?std::abs(determinant)/shape_largest_*(1-1e-10):0;
        shape_q_=std::sqrt(raw_noise_gain_*bin_n_)*work_.group_phase_bound;
        if(!std::isfinite(shape_largest_)||!std::isfinite(shape_smallest_)||shape_smallest_<=0||
           shape_largest_/shape_smallest_>4||!std::isfinite(shape_q_)||shape_q_>=shape_smallest_)return false;
        for(std::size_t g=0;g<2*group_count_;++g)ray_moments_[g].fill(Moment{});
        auto phase=std::complex<double>{1,0};
        const auto step=std::polar(1.,tau*f*bin_n_/rate_);
        for(std::size_t j=0;j<count_;++j) {
            if((j&63U)==0)phase=std::conj(rotation(f,j*bin_n_));
            const auto c=phase.real(),v=phase.imag();
            const std::complex<double> x0{forward.a*c+forward.b*v,forward.c*c+forward.d*v};
            const std::complex<double> x1{-forward.a*v+forward.b*c,-forward.c*v+forward.d*c};
            const auto y=raw_y_[j];const auto row=2*static_cast<std::size_t>(groups_[j]);
            auto& a=ray_moments_[row][slots_[j]];auto& b=ray_moments_[row+1][slots_[j]];
            a.xx+=std::real(std::conj(x0)*y);a.xy+=std::real(std::conj(x1)*y);a.yy+=std::norm(x0);
            b.xx+=std::real(std::conj(x0)*x1);b.xy+=std::norm(x1);b.yy+=std::norm(y);
            phase*=step;
        }
        for(std::size_t g=0;g<2*group_count_;++g) {
            Moment prefix{};for(auto& value:ray_moments_[g]) {prefix=sum(prefix,value);value=prefix;}
            ray_mask_[g]=prefix;
        }
        // Absolute whole-aperture error, not the possibly cancelled selected
        // Gram or Schur score. The bounded 2048-bin/64-step-reseed arithmetic
        // uses fewer than 131072 roundings per statistic. The existing finite
        // forward-column allowance is 1e-10; eight such allowances dominate
        // column, raw-coordinate and prefix/difference composition. This is
        // the declared IEEE-double/finite-operator numerical scope, not a
        // claim about arbitrary libm implementations or unsupported geometry.
        constexpr auto epsilon=std::numeric_limits<double>::epsilon();
        constexpr auto arithmetic=131072*epsilon/(1-131072*epsilon);
        constexpr auto envelope=8e-10+arithmetic;
        shape_delta_gram_=2*envelope*count_*shape_largest_*shape_largest_;
        shape_delta_rhs_=2*envelope*shape_largest_*std::sqrt(count_*shape_aperture_energy_);
        return std::isfinite(shape_delta_gram_)&&std::isfinite(shape_delta_rhs_);
    }
    Moment shape_range(int group,unsigned half,std::size_t left,std::size_t right) const {
        Moment result{};
        if(left<phase_slots)result=paired_range(group,half,left,std::min(right,phase_slots));
        if(right>phase_slots)result=sum(result,paired_range(group+1,half,
            left>phase_slots?left-phase_slots:0,right-phase_slots));
        return result;
    }
    std::size_t shape_bins(int group,std::size_t left,std::size_t right) const {
        std::size_t result=0;
        if(left<phase_slots)result=group_bins(group,left,std::min(right,phase_slots));
        if(right>phase_slots)result+=group_bins(group+1,left>phase_slots?left-phase_slots:0,right-phase_slots);
        return result;
    }
    bool prepare_shape_mask() {
        constexpr auto fp=128*std::numeric_limits<double>::epsilon();
        shape_group_count_=0;
        std::size_t selected=0;
        for(int g=-1;g<static_cast<int>(group_count_);++g) {
            std::array<M,shape_positions> gram{};
            std::array<std::array<double,2>,shape_positions> rhs{};
            M total{};std::array<double,2> total_rhs{};
            double gram_scale=0,rhs_scale=0;std::size_t n=0;
            for(std::size_t k=0;k<shape_positions;++k) {
                const auto left=shape_offset_+k*shape_slots,right=left+shape_slots;
                const auto a=shape_range(g,0,left,right),b=shape_range(g,1,left,right);
                gram[k]={a.yy,b.xx,b.xx,b.xy};rhs[k]={a.xx,a.xy};
                total.a+=gram[k].a;total.b+=gram[k].b;total.c+=gram[k].c;total.d+=gram[k].d;
                total_rhs[0]+=rhs[k][0];total_rhs[1]+=rhs[k][1];
                gram_scale+=bound_norm(gram[k]);rhs_scale+=bound_vector(rhs[k]);
                n+=shape_bins(g,left,right);
            }
            if(!n)continue;
            if(shape_group_count_==shape_groups_.size())return false;
            auto& group=shape_groups_[shape_group_count_++];group=ShapeGroup{};group.n=n;selected+=n;
            const auto inverse_gram=inverse(total);
            if(!inverse_gram)return false;
            const auto inverse=*inverse_gram;
            const auto inverse_norm=bound_norm(inverse),total_norm=bound_norm(total);
            const auto delta_total=shape_positions*shape_delta_gram_+fp*gram_scale;
            const auto delta_total_rhs=shape_positions*shape_delta_rhs_+fp*rhs_scale;
            const auto product=mul(inverse,total);
            const auto residual=bound_norm({1-product.a,-product.b,-product.c,1-product.d})+
                fp*(1+inverse_norm*total_norm);
            const auto error=residual+inverse_norm*delta_total;
            if(!std::isfinite(error)||error>.25)return false;
            const auto inverse_error=error/(1-error)*inverse_norm;
            const auto total_rhs_norm=bound_vector(total_rhs);
            for(std::size_t k=0;k<shape_positions-1;++k) {
                const auto a_norm=bound_norm(gram[k]);
                const auto projected=apply(mul(gram[k],inverse),total_rhs);
                group.rhs[2*k]=rhs[k][0]-projected[0];group.rhs[2*k+1]=rhs[k][1]-projected[1];
                const auto rhs_uncancelled=bound_vector(rhs[k])+a_norm*inverse_norm*total_rhs_norm;
                const auto rhs_error=shape_delta_rhs_+
                    shape_delta_gram_*(inverse_norm+inverse_error)*(total_rhs_norm+delta_total_rhs)+
                    a_norm*inverse_error*(total_rhs_norm+delta_total_rhs)+
                    a_norm*inverse_norm*delta_total_rhs+fp*rhs_uncancelled;
                group.rhs_error=std::max(group.rhs_error,rhs_error);
                group.rhs_scale=std::max(group.rhs_scale,rhs_uncancelled);
                for(std::size_t l=0;l<shape_positions-1;++l) {
                    const auto c_norm=bound_norm(gram[l]);
                    const auto projected_gram=mul(mul(gram[k],inverse),gram[l]);
                    const auto diagonal=k==l?gram[k]:M{};
                    const M block{diagonal.a-projected_gram.a,diagonal.b-projected_gram.b,
                                  diagonal.c-projected_gram.c,diagonal.d-projected_gram.d};
                    group.gram[2*k][2*l]=block.a;group.gram[2*k][2*l+1]=block.b;
                    group.gram[2*k+1][2*l]=block.c;group.gram[2*k+1][2*l+1]=block.d;
                    const auto uncancelled=(k==l?a_norm:0)+a_norm*inverse_norm*c_norm;
                    const auto block_error=(k==l?shape_delta_gram_:0)+
                        shape_delta_gram_*(inverse_norm+inverse_error)*(c_norm+shape_delta_gram_)+
                        a_norm*inverse_error*(c_norm+shape_delta_gram_)+
                        a_norm*inverse_norm*shape_delta_gram_+fp*uncancelled;
                    group.gram_error=std::max(group.gram_error,block_error);
                    group.gram_scale=std::max(group.gram_scale,uncancelled);
                }
            }
            if(!std::isfinite(group.gram_error)||!std::isfinite(group.rhs_error)||
               !std::isfinite(group.gram_scale)||!std::isfinite(group.rhs_scale))return false;
        }
        std::size_t expected=0;
        for(std::size_t k=0;k<mask_slots;++k)expected+=counts_[(shape_offset_+k)%phase_slots];
        if(selected!=expected||!selected||!shape_group_count_)return false;
        const auto noise_variance=work_.variance*raw_noise_gain_;
        const auto noise_norm=std::sqrt(noise_variance*chi(2*static_cast<double>(selected),work_.coherence_log));
        const auto rounding=norm(raw_whitening_)*work_.max_stored*std::sqrt(static_cast<double>(selected))*
            (8*std::numeric_limits<float>::epsilon()+4096*std::numeric_limits<double>::epsilon());
        const auto model=shape_q_/(shape_smallest_-shape_q_)*
            (std::sqrt(shape_raw_energy_[shape_offset_])+noise_norm+rounding)+rounding;
        // A separate rank-six event; false frequencies/masks/deletions do not
        // add a look-elsewhere factor to inversion at the fixed true cell,
        // deterministic clean mask and actual deterministic dirty deletion.
        const auto threshold=std::sqrt(noise_variance*chi(6,work_.coherence_log+std::log(2.)))+model;
        shape_threshold_=std::nextafter(threshold*threshold*(1+1024*std::numeric_limits<double>::epsilon()),
                                       std::numeric_limits<double>::infinity());
        shape_omitted_a_=shape_omitted_b_=SIZE_MAX;shape_subset_phase_=0;
        return std::isfinite(shape_threshold_);
    }
    // true means compatible OR uncertifiable. Any uncertifiable deletion
    // keeps the mask: that deletion could be the actual clean subset S0.
    bool shape_subset_compatible() const {
        constexpr auto fp=128*std::numeric_limits<double>::epsilon();
        std::array<std::array<double,6>,6> gram{};
        std::array<double,6> rhs{},coefficients{};
        double gram_error=0,rhs_error=0,gram_scale=0,rhs_scale=0;
        std::size_t n=0;
        for(std::size_t g=0;g<shape_group_count_;++g) {
            if(g==shape_omitted_a_||g==shape_omitted_b_)continue;
            const auto& group=shape_groups_[g];n+=group.n;
            for(std::size_t k=0;k<6;++k) {
                rhs[k]+=group.rhs[k];
                for(std::size_t l=0;l<6;++l)gram[k][l]+=group.gram[k][l];
            }
            gram_error+=group.gram_error;rhs_error+=group.rhs_error;
            gram_scale+=group.gram_scale;rhs_scale+=group.rhs_scale;
        }
        if(!n)return true;
        gram_error+=fp*gram_scale;rhs_error+=fp*rhs_scale;
        if(!std::isfinite(gram_error)||!std::isfinite(rhs_error)||!solve6(gram,rhs,coefficients))return true;
        double linear=0,quadratic=0,absolute=0,coefficient_norm=0;
        for(std::size_t k=0;k<6;++k) {
            if(!std::isfinite(coefficients[k]))return true;
            const auto term=2*rhs[k]*coefficients[k];linear+=term;absolute+=std::abs(term);
            for(std::size_t l=0;l<6;++l) {
                const auto term2=coefficients[k]*gram[k][l]*coefficients[l];
                quadratic+=term2;absolute+=std::abs(term2);
            }
            if((k&1U)==0)coefficient_norm+=bound_vector({coefficients[k],coefficients[k+1]});
        }
        // For ANY finite a, 2r'a-a'Ha <= r'H^+r. The solve chooses a witness;
        // its condition/error cannot increase this exact lower bound. Explicit
        // uncancelled Schur/whole-aperture errors enclose its FP evaluation.
        const auto allowance=2*rhs_error*coefficient_norm+
            gram_error*coefficient_norm*coefficient_norm+fp*absolute;
        const auto lower=linear-quadratic-allowance;
        return !std::isfinite(lower)||lower<=shape_threshold_;
    }
    bool next_shape_subset() {
        if(shape_subset_phase_==0) {
            shape_subset_phase_=1;shape_omitted_a_=0;return true;
        }
        if(shape_subset_phase_==1) {
            if(++shape_omitted_a_<shape_group_count_)return true;
            if(shape_group_count_<2)return false;
            shape_subset_phase_=2;shape_omitted_a_=0;shape_omitted_b_=1;return true;
        }
        if(++shape_omitted_b_<shape_group_count_)return true;
        if(++shape_omitted_a_+1<shape_group_count_) {shape_omitted_b_=shape_omitted_a_+1;return true;}
        return false;
    }
    // One bounded stage per call: at most one 2048-bin prefix construction,
    // one 25-group/four-position Schur construction, OR one six-column solve.
    // An advance has at most eight stages and may complete zero cells. No
    // unfinished cell, mask or deletion can publish an interval.
    bool advance_shape() {
        if(!shape_table_ready_) {
            if(!prepare_shape_table()) {admit_cell(shape_cell_);shape_pending_=false;return true;}
            shape_table_ready_=true;return false;
        }
        if(!shape_mask_ready_) {
            while(shape_offset_<phase_slots&&!shape_candidates_[shape_offset_])++shape_offset_;
            if(shape_offset_==phase_slots) {shape_pending_=false;return true;}
            if(!prepare_shape_mask()) {admit_cell(shape_cell_);shape_pending_=false;return true;}
            shape_mask_ready_=true;return false;
        }
        if(shape_subset_compatible()) {admit_cell(shape_cell_);shape_pending_=false;return true;}
        if(!next_shape_subset()) {shape_mask_ready_=false;++shape_offset_;}
        return false;
    }
    // This is the SAME recent observed-energy gate used by the full cell
    // inversion below. It grants no phase/frequency or physical absence
    // authority and spends no additional event: both evaluations use the same
    // frozen samples, fixed frequency/mask domain and lifetime admission_log.
    bool recent_admitted(double fresh,std::size_t nrecent,double V,double& cn0) const {
        cn0=nrecent?std::max(0.,fresh-2*nrecent*V)/
            (2*nrecent*V*static_cast<double>(bin_n_)/rate_):0;
        return nrecent>=8&&fresh>V*chi(2*static_cast<double>(nrecent),work_.admission_log)&&
            cn0>=10*std::pow(10.,.6);
    }
    bool recent_presence_cell(std::size_t cell) {
        const auto low=expected_-width_+cell*work_.cell_width,high=low+work_.cell_width,f=.5*(low+high);
        const auto transform=response(f);if(!transform)return false;
        const auto inverse_response=*transform;
        const auto corrected_cov=mul(mul(inverse_response,covariance_),transpose(inverse_response));
        const auto V=work_.variance*eigen(corrected_cov)*(1+1e-10);
        const auto Q=std::sqrt(static_cast<double>(bin_n_)/lambda_)*norm(inverse_response);
        const auto fraction=Q*work_.phase_bound;
        // The original full cell skips its recent gate under exactly these
        // conditions. No unsupported operator is used to qualify presence.
        if(!std::isfinite(V)||!(V>0)||!std::isfinite(fraction)||fraction>=1)return false;
        const std::complex<double> alpha{.5*(inverse_response.a+inverse_response.d),
                                         .5*(inverse_response.c-inverse_response.b)};
        const std::complex<double> beta{.5*(inverse_response.a-inverse_response.d),
                                        .5*(inverse_response.c+inverse_response.b)};
        const auto first_recent=static_cast<std::size_t>(std::min<std::uint64_t>(count_,
            (work_.recent_first-work_.first+bin_n_-1)/bin_n_));
        const auto anchor=first_recent/64*64;
        auto r1=rotation(f-expected_,work_.first+anchor*bin_n_);
        auto r2=rotation(f+expected_,work_.first+anchor*bin_n_);
        const auto d1=std::polar(1.,-tau*(f-expected_)*bin_n_/rate_);
        const auto d2=std::polar(1.,-tau*(f+expected_)*bin_n_/rate_);
        recent_energy_.fill(0);
        // Begin at the preceding original 64-bin reset. The same complex
        // recurrence, power and increasing-bin accumulation order reproduce
        // the full scan's recent sums without scoring older bins/moments.
        for(std::size_t j=anchor;j<count_;++j) {
            if((j&63U)==0) {r1=rotation(f-expected_,work_.first+j*bin_n_);r2=rotation(f+expected_,work_.first+j*bin_n_);}
            if(j>=first_recent) {
                const auto z=static_cast<std::complex<double>>(ring_[(position_+j)%target_].value);
                const auto u=alpha*z*r1+beta*std::conj(z)*r2;
                recent_energy_[slots_[j]]+=std::norm(u);
            }
            r1*=d1;r2*=d2;
        }
        for(std::size_t offset=0;offset<phase_slots;++offset) {
            double fresh=0;std::size_t nrecent=0;
            for(std::size_t k=0;k<mask_slots;++k) {
                const auto slot=(offset+k)%phase_slots;
                fresh+=recent_energy_[slot];nrecent+=recent_counts_[slot];
            }
            double cn0=0;if(recent_admitted(fresh,nrecent,V,cn0))return true;
        }
        return false;
    }
    bool analyze_cell(std::size_t cell) {
        shape_candidates_.fill(false);
        const auto& counts=counts_;const auto& recent_counts=recent_counts_;
            const auto low=expected_-width_+cell*work_.cell_width,high=low+work_.cell_width,f=.5*(low+high);
            // Fixed quarter-domain diagnostic, never chosen by signal or truth.
            const bool representative=cell==work_.cells/4;
            if(representative)diagnostics_.far_frequency=f;
            const auto transform=response(f);
            if(!transform) {++diagnostics_.invalid_response;work_.union_low=std::min(work_.union_low,low);work_.union_high=std::max(work_.union_high,high);return true;}
            const auto inverse_response=*transform;
            const auto corrected_cov=mul(mul(inverse_response,covariance_),transpose(inverse_response));
            const auto V=work_.variance*eigen(corrected_cov)*(1+1e-10);
            const auto Q=std::sqrt(static_cast<double>(bin_n_)/lambda_)*norm(inverse_response);
            const auto fraction=Q*work_.phase_bound;
            const auto group_fraction=Q*work_.group_phase_bound;
            if(representative) {diagnostics_.far_operator_norm=norm(inverse_response);diagnostics_.far_noise_variance=V;diagnostics_.far_q=fraction;}
            if(!std::isfinite(V)||!(V>0)||!std::isfinite(fraction)||fraction>=1) {
                ++diagnostics_.invalid_model;work_.union_low=std::min(work_.union_low,low);work_.union_high=std::max(work_.union_high,high);return true;
            }
            const std::complex<double> alpha{.5*(inverse_response.a+inverse_response.d),
                                             .5*(inverse_response.c-inverse_response.b)};
            const std::complex<double> beta{.5*(inverse_response.a-inverse_response.d),
                                            .5*(inverse_response.c+inverse_response.b)};
            auto r1=rotation(f-expected_,work_.first),r2=rotation(f+expected_,work_.first);
            const auto d1=std::polar(1.,-tau*(f-expected_)*bin_n_/rate_);
            const auto d2=std::polar(1.,-tau*(f+expected_)*bin_n_/rate_);
            auto& xx=xx_;auto& xy=xy_;auto& yy=yy_;auto& recent_energy=recent_energy_;
            xx.fill(0);xy.fill(0);yy.fill(0);recent_energy.fill(0);
            std::array<double,6> largest{};
            for(std::size_t g=0;g<group_count_;++g)ray_moments_[g].fill(Moment{});
            for(std::size_t j=0;j<count_;++j) {
                if((j&63U)==0) {r1=rotation(f-expected_,work_.first+j*bin_n_);r2=rotation(f+expected_,work_.first+j*bin_n_);}
                const auto z=static_cast<std::complex<double>>(ring_[(position_+j)%target_].value);
                const auto u=alpha*z*r1+beta*std::conj(z)*r2;
                const auto slot=slots_[j];const auto power=std::norm(u);
                xx[slot]+=u.real()*u.real();xy[slot]+=u.real()*u.imag();yy[slot]+=u.imag()*u.imag();
                auto& group=ray_moments_[groups_[j]][slot];
                group.xx+=u.real();group.xy+=u.imag();group.yy+=power;
                if(work_.first+j*bin_n_>=work_.recent_first)recent_energy[slot]+=power;
                if(power>largest.back()) {largest.back()=power;std::sort(largest.begin(),largest.end(),std::greater<double>());}
                r1*=d1;r2*=d2;
            }
            for(std::size_t g=0;g<group_count_;++g) {
                Moment prefix{};
                for(auto& value:ray_moments_[g]) {prefix=sum(prefix,value);value=prefix;}
                ray_mask_[g]=prefix;
            }
            const auto trim=largest[0]+largest[1]+largest[2]+largest[3]+largest[4]+largest[5];
            double aperture_energy=0;for(std::size_t k=0;k<phase_slots;++k)aperture_energy+=xx[k]+yy[k];
            std::array<bool,phase_slots> compatible{},old_compatible{};
            std::array<double,phase_slots> thresholds{},old_thresholds{};
            bool any_compatible=false,any_line=false,cell_energy=false,old_any_compatible=false;
            bool accepted=false;
#if defined(DATAPUMP_PRIVATE_PRESENCE_CHECK)
            bool full_recent_admitted=false;
#endif
            for(std::size_t offset=0;offset<phase_slots;++offset) {
                double a=0,b=0,c=0,fresh=0;std::size_t n=0,nrecent=0;
                for(std::size_t k=0;k<mask_slots;++k) {
                    const auto slot=(offset+k)%phase_slots;
                    a+=xx[slot];b+=xy[slot];c+=yy[slot];fresh+=recent_energy[slot];
                    n+=counts[slot];nrecent+=recent_counts[slot];
                }
                if(n<8)continue;
                const auto energy=a+c;
                const auto residual=std::max(0.,.5*(energy-std::hypot(a-c,2*b))-trim-
                    256*std::numeric_limits<double>::epsilon()*energy);
                const auto noise_norm=std::sqrt(V*chi(2*static_cast<double>(n),work_.score_log));
                const auto model=fraction/(1-fraction)*(std::sqrt(energy)+noise_norm);
                const auto rounding=norm(inverse_response)*work_.max_stored*std::sqrt(static_cast<double>(n))*
                    (8*std::numeric_limits<float>::epsilon()+4096*std::numeric_limits<double>::epsilon());
                const auto threshold=std::sqrt(V*chi(static_cast<double>(n),work_.score_log))+model+rounding/(1-fraction);
                if(representative && std::sqrt(residual)/threshold<diagnostics_.far_line_ratio) {
                    diagnostics_.far_line_ratio=std::sqrt(residual)/threshold;
                    diagnostics_.far_line_energy=energy;
                    diagnostics_.far_line_noise_bound=V*chi(static_cast<double>(n),work_.score_log);
                    diagnostics_.far_line_model=model;diagnostics_.far_n=static_cast<double>(n);
                }
                if(representative) {
                    const auto old_norm=std::sqrt(V*chi(2*static_cast<double>(n),work_.admission_log));
                    const auto old_model=fraction/(1-fraction)*(std::sqrt(energy)+old_norm);
                    const auto old_line=std::sqrt(V*chi(static_cast<double>(n),work_.admission_log))+old_model+rounding/(1-fraction);
                    diagnostics_.far_old_line_ratio=std::min(diagnostics_.far_old_line_ratio,std::sqrt(residual)/old_line);
                    old_compatible[offset]=std::sqrt(residual)<=old_line;
                    old_any_compatible=old_any_compatible||old_compatible[offset];
                    old_thresholds[offset]=std::sqrt(V*ray_chi(static_cast<double>(n),work_.admission_log))+old_model+rounding/(1-fraction);
                }
                const auto line_compatible=std::sqrt(residual)<=threshold;
                any_line=any_line||line_compatible;
                bool coherent=true;
                if(line_compatible && std::isfinite(group_fraction)&&group_fraction<1) {
                    double score=0,first_group_energy=0,second_group_energy=0;
                    std::size_t grouped_n=0,nonempty=0;
                    // Wrapped masks join the suffix of nominal row g to the
                    // prefix of g+1. This is exactly floor((center-offset)/.4).
                    for(int g=-1;g<static_cast<int>(group_count_);++g) {
                        auto value=group_range(g,offset,std::min(phase_slots,offset+mask_slots));
                        auto bins=group_bins(g,offset,std::min(phase_slots,offset+mask_slots));
                        if(offset+mask_slots>phase_slots) {
                            value=sum(value,group_range(g+1,0,offset+mask_slots-phase_slots));
                            bins+=group_bins(g+1,0,offset+mask_slots-phase_slots);
                        }
                        if(!bins)continue;
                        ++nonempty;grouped_n+=bins;
                        score+=value.yy-(value.xx*value.xx+value.xy*value.xy)/bins;
                        const auto power=std::max(0.,value.yy);
                        if(power>first_group_energy) {second_group_energy=first_group_energy;first_group_energy=power;}
                        else second_group_energy=std::max(second_group_energy,power);
                    }
                    if(grouped_n!=n) {++diagnostics_.partition_mismatches;} // Keep the unknown mask.
                    else {
                        const auto rank=2*static_cast<double>(n-nonempty);
                        const auto lower=std::max(0.,score-first_group_energy-second_group_energy-
                            1e-9*(aperture_energy+first_group_energy+second_group_energy));
                        const auto group_noise=std::sqrt(V*chi(2*static_cast<double>(n),work_.coherence_log));
                        const auto group_model=group_fraction/(1-group_fraction)*(std::sqrt(energy)+group_noise);
                        const auto group_threshold=std::sqrt(V*chi(rank,work_.coherence_log))+group_model+rounding/(1-group_fraction);
                        if(representative)diagnostics_.far_coherence_ratio=std::min(diagnostics_.far_coherence_ratio,
                            std::sqrt(lower)/group_threshold);
                        coherent=std::sqrt(lower)<=group_threshold;
                    }
                }
                // Both independently spent tests must fit the SAME mask.
                compatible[offset]=line_compatible&&coherent;
                any_compatible=any_compatible||compatible[offset];
                thresholds[offset]=std::sqrt(V*ray_chi(static_cast<double>(n),work_.score_log))+model+rounding/(1-fraction);
                const auto observed_cn0=std::max(0.,energy-2*n*V)/(2*n*V*static_cast<double>(bin_n_)/rate_);
                if(representative)diagnostics_.far_cn0=std::max(diagnostics_.far_cn0,observed_cn0);
                if(energy>V*chi(2*static_cast<double>(n),work_.admission_log)&&observed_cn0>=10*std::pow(10.,.6)) {
                    work_.present=true;cell_energy=true;
                }
                double recent_cn0=0;
                if(recent_admitted(fresh,nrecent,V,recent_cn0)) {
#if defined(DATAPUMP_PRIVATE_PRESENCE_CHECK)
                    full_recent_admitted=true;
#endif
                    // This is a distinct full-search noise-existence/level
                    // criterion, not independent-data evidence or a posterior.
                    // Record actual selected-bin envelope, never commit time.
                    if(!work_.recent||recent_cn0>work_.recent_cn0) {
                        work_.presence_first=recent_support_first_[offset];
                        work_.presence_end=recent_support_end_[offset];work_.recent_cn0=recent_cn0;
                    }
                    work_.recent=true;
                }
            }
#if defined(DATAPUMP_PRIVATE_PRESENCE_CHECK)
            // Qualification-only shadow: probe scratch is dead after this
            // mask loop. No statistic, selection or threshold is changed.
            if(recent_presence_cell(cell)!=full_recent_admitted)
                throw Error("recent-presence prepass predicate mismatch");
#endif
            // A noise-only/weak cell is left unresolved without expensive ray
            // work; no cell is excluded by failing an existence test. This bounds
            // the optional work for absent references. Presence is separate.
            if(!any_line)++diagnostics_.line_rejected;
            else if(!any_compatible)++diagnostics_.coherence_rejected;
            if(any_compatible&&!cell_energy) {accepted=true;++diagnostics_.unresolved_energy;}
            if((any_compatible||(representative&&old_any_compatible))&&cell_energy) {
                for(auto& row:ray_moments_)row.fill(Moment{});
                r1=rotation(f-expected_,work_.first);r2=rotation(f+expected_,work_.first);
                for(std::size_t j=0;j<count_;++j) {
                    if((j&63U)==0) {r1=rotation(f-expected_,work_.first+j*bin_n_);r2=rotation(f+expected_,work_.first+j*bin_n_);}
                    const auto z=static_cast<std::complex<double>>(ring_[(position_+j)%target_].value);
                    const auto u=alpha*z*r1+beta*std::conj(z)*r2;
                    auto angle=std::atan2(u.imag(),u.real());if(angle<0)angle+=tau;
                    const auto sector=std::min<std::size_t>(ray_slots-1,static_cast<std::size_t>(angle/tau*ray_slots));
                    auto& m=ray_moments_[sector][slots_[j]];
                    m.xx+=u.real()*u.real();m.xy+=u.real()*u.imag();m.yy+=u.imag()*u.imag();
                    r1*=d1;r2*=d2;
                }
                ray_mask_.fill(Moment{});
                for(std::size_t ray=0;ray<ray_slots;++ray)
                    for(std::size_t k=0;k<mask_slots;++k)ray_mask_[ray]=sum(ray_mask_[ray],ray_moments_[ray][k]);
                for(std::size_t offset=0;offset<phase_slots;++offset) {
                    if(compatible[offset]||(representative&&old_compatible[offset])) {
                        ray_prefix_[0]={};
                        for(std::size_t k=0;k<2*ray_slots;++k)
                            ray_prefix_[k+1]=sum(ray_prefix_[k],ray_mask_[k%ray_slots]);
                        const auto energy=ray_prefix_[ray_slots].xx+ray_prefix_[ray_slots].yy;
                        double lower=std::numeric_limits<double>::infinity();
                        for(std::size_t ray=0;ray<ray_slots;++ray) {
                            // For an angle OUTSIDE this interval, its closest
                            // positive endpoint is left in the preceding pi/2
                            // quadrant, right in the following pi/2 quadrant,
                            // and zero in the opposite half-plane. The uniform
                            // 64-sector boundaries align these quadrants.
                            // Inside this sector the bin chooses its own angle,
                            // exactly the former per-bin interval relaxation.
                            const auto left=difference(ray_prefix_[ray+ray_slots],
                                                       ray_prefix_[ray+3*ray_slots/4]);
                            const auto right=difference(ray_prefix_[ray+1+ray_slots/4],ray_prefix_[ray+1]);
                            const auto score=energy-ray_mask_[ray].xx-ray_mask_[ray].yy-
                                project(left,ray_directions_[ray])-project(right,ray_directions_[ray+1]);
                            lower=std::min(lower,score);
                        }
                        // Same six-bin trim and interval relaxation as v2.
                        // Absolute summation allowance covers double moment
                        // bucket/prefix arithmetic under the declared vectors.
                        lower=std::max(0.,lower-trim-1e-9*(aperture_energy+trim));
                        if(representative) {
                            if(compatible[offset])diagnostics_.far_ray_ratio=std::min(diagnostics_.far_ray_ratio,std::sqrt(lower)/thresholds[offset]);
                            if(old_compatible[offset])diagnostics_.far_old_ray_ratio=std::min(diagnostics_.far_old_ray_ratio,std::sqrt(lower)/old_thresholds[offset]);
                        }
                        if(compatible[offset]&&std::sqrt(lower)<=thresholds[offset]) {accepted=true;shape_candidates_[offset]=true;}
                    }
                    for(std::size_t ray=0;ray<ray_slots;++ray)
                        ray_mask_[ray]=sum(difference(ray_mask_[ray],ray_moments_[ray][offset]),
                                           ray_moments_[ray][(offset+mask_slots)%phase_slots]);
                }
                if(any_compatible) {if(accepted)++diagnostics_.ray_retained;else++diagnostics_.ray_rejected;}
            }
            if(accepted) {
                if(std::any_of(shape_candidates_.begin(),shape_candidates_.end(),[](bool value){return value;})) {
                    shape_pending_=true;shape_table_ready_=shape_mask_ready_=false;
                    shape_cell_=cell;shape_offset_=0;return false;
                }
                admit_cell(cell);
            }
            return true;
    }
    void finish_analysis() {
        if(work_.present&&work_.recent&&std::isfinite(work_.union_low)&&std::isfinite(work_.union_high)) {
            Reference r;r.sample=work_.end;r.sample_rate=rate_;r.frequency_sample=work_.end;r.frequency_first_sample=work_.first;
            r.frequency_fit=FrequencyFit::masked_tone_profile;r.frequency_hz=.5*(work_.union_low+work_.union_high);
            r.frequency_uncertainty_hz=std::nextafter(std::max(r.frequency_hz-work_.union_low,
                work_.union_high-r.frequency_hz),std::numeric_limits<double>::infinity());r.frequency_valid=true;
            r.phase_uncertainty_radians=std::numbers::pi;r.dwell_uncertainty_seconds=.4;
            update_=r;
        }
        work_.complete=true;
        decline_=update_?FrequencyWorkDecline::none:FrequencyWorkDecline::no_reference;
    }
public:
    static FrequencyWorkGeometry geometry(double expected,double width,std::uint32_t rate,
                                          double sigma,double clock_ppm) {
        FrequencyWorkGeometry result;result.sample_rate=rate;result.physical_prior_half_width_hz=width;
        if((rate!=24000&&rate!=48000&&rate!=48960)||!std::isfinite(expected)||!std::isfinite(width)||
           !std::isfinite(sigma)||!std::isfinite(clock_ppm)||sigma<0||clock_ppm<0||clock_ppm>1000||
           expected-width<10||expected+width>=rate/2.||width<0||width>100)return result;
        result.bin_samples=std::max<std::size_t>(8,rate/250);
        const auto dt=static_cast<double>(result.bin_samples)/rate;
        auto duration=std::min(result.maximum_supported_fit_seconds,
            width>0?max_cells/(128*width):result.maximum_supported_fit_seconds);
        if(width>20)duration=std::min(duration,2.4);
        if(sigma>0)duration=std::min(duration,.08*.08/(64*sigma*sigma));
        if(!std::isfinite(duration)||duration<=0)return result;
        result.aperture_bins=std::min<std::size_t>(capacity,static_cast<std::size_t>(std::floor(duration/dt)));
        if(result.aperture_bins<50)return result;
        result.aperture_seconds=result.aperture_bins*dt;
        if(1.001*(.2+dt)+.001*result.aperture_seconds+.00665>=.228)return result;
        result.look_cadence_seconds=static_cast<double>(((rate*2/5+result.bin_samples-1)/
            result.bin_samples)*result.bin_samples)/rate;
        result.startup_support_seconds=std::nextafter(result.aperture_seconds+
            result.look_cadence_seconds+1./rate,std::numeric_limits<double>::infinity());
        result.supported=true;result.decline=FrequencyWorkDecline::none;return result;
    }
    FrequencyProfile(double expected,double width,std::uint32_t rate,double sigma=0,double clock_ppm=1000)
        :rate_(rate),expected_(expected),width_(width),sigma_(sigma),clock_(clock_ppm) {
        const auto layout=geometry(expected,width,rate,sigma,clock_ppm);
        if(!layout.supported)throw Error("unsupported optional Helper Carrier frequency geometry");
        bin_n_=layout.bin_samples;target_=layout.aperture_bins;
        for(std::size_t i=0;i<=ray_slots;++i)ray_directions_[i]=std::polar(1.,tau*i/ray_slots);
        step_=std::polar(1.,tau*expected/rate);
        auto oscillator=std::complex<double>{1,0};
        for(std::size_t i=0;i<bin_n_;++i) {
            cc_+=oscillator.real()*oscillator.real();ss_+=oscillator.imag()*oscillator.imag();
            cs_+=oscillator.real()*oscillator.imag();oscillator*=step_;
        }
        lambda_=.5*(cc_+ss_-std::hypot(cc_-ss_,2*cs_));
        const auto det=cc_*ss_-cs_*cs_;covariance_={ss_/det,cs_/det,cs_/det,cc_/det};
        const auto l00=std::sqrt(covariance_.a),l10=covariance_.c/l00;
        const auto l11=std::sqrt(covariance_.d-l10*l10);
        raw_whitening_={1/l00,0,-l10/(l00*l11),1/l11};
        raw_noise_gain_=eigen(mul(mul(raw_whitening_,covariance_),transpose(raw_whitening_)))*(1+1e-10);
    }
    void reset_ingest(std::uint64_t first) noexcept {
        // Stale slots are unreachable until overwritten; avoid a 24KiB
        // aggregate temporary or needless clearing on the capture thread.
        input_.next=first;input_.bin_first=first;input_.last_look=0;input_.scheduled_end=0;
        input_.count=input_.position=input_.bin_count=0;input_.oscillator={1,0};
        input_.rhs={};input_.energy=0;input_.configured=true;input_.valid=true;qualified_.reset();
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void push(std::span<const float> input,std::uint32_t rate,std::uint64_t first) noexcept {
        if(!input_.configured||first!=input_.next)reset_ingest(first);
        if(rate!=rate_) {input_.valid=false;qualified_.reset();return;}
        for(const auto value:input) {
            if(!std::isfinite(value)||input_.next==UINT64_MAX) {
                input_.valid=false;qualified_.reset();return;
            }
            input_.rhs+=static_cast<double>(value)*input_.oscillator;
            input_.energy+=static_cast<double>(value)*value;
            input_.oscillator*=step_;++input_.next;
            if(++input_.bin_count==bin_n_)finish_ingest_bin();
        }
    }
    bool prepare(std::uint64_t revision,std::uint64_t generation) {
        if(work_.pending) {decline_=FrequencyWorkDecline::analysis_incomplete;return false;}
        if(generation==UINT64_MAX) {decline_=FrequencyWorkDecline::generation_changed;return false;}
        if(input_.ordinal==UINT64_MAX) {decline_=FrequencyWorkDecline::unsupported_geometry;return false;}
        if(!input_.valid) {decline_=FrequencyWorkDecline::unsupported_geometry;return false;}
        if(input_.count<target_||!input_.scheduled_end) {decline_=FrequencyWorkDecline::warming_aperture;return false;}
        if(input_.ordinal==look_) {decline_=FrequencyWorkDecline::no_new_opportunity;return false;}
        const auto lag=(input_.bin_first-input_.scheduled_end)/bin_n_;
        if(lag+target_>input_.count) {decline_=FrequencyWorkDecline::stale_support;return false;}
        const auto beginning=(input_.position+2*capacity-lag-target_)%capacity;
        for(std::size_t j=0;j<target_;++j)ring_[j]=input_.ring[(beginning+j)%capacity];
        position_=0;count_=target_;next_=input_.scheduled_end;look_=input_.ordinal;
        work_=Work{};work_.revision=revision;work_.generation=generation;
        work_.prepared_ordinal=look_;work_.pending=true;update_.reset();
        work_.first=next_-count_*bin_n_;work_.end=next_-1;
        work_.cells=std::max<std::size_t>(1,static_cast<std::size_t>(std::ceil(128*width_*count_*bin_n_/rate_)));
        decline_=FrequencyWorkDecline::analysis_incomplete;return true;
    }
    FrequencyWorkAdvance advance(std::size_t maximum) {
        FrequencyWorkAdvance result;
        if(!work_.pending||work_.complete||!maximum) {result.complete=work_.complete;return result;}
        if(!work_.started) {
            work_.started=true;
            ++result.processed_work_units;++work_.work_units;
            if(!begin_analysis()) {finish_analysis();result.complete=true;return result;}
        }
        while(result.processed_cells<maximum&&result.processed_work_units<maximum_work_units_per_advance) {
            if(!work_.presence_prepass_complete) {
                const auto positive=recent_presence_cell(work_.presence_cursor++);
                ++result.processed_work_units;++work_.work_units;
                if(positive)work_.presence_prepass_complete=true;
                else if(work_.presence_cursor==work_.cells) {
                    // Every fixed-domain recent gate failed. The original
                    // complete inversion could not publish a reference either.
                    // This is a terminal optional decline with ZERO precision
                    // cells scored, never a partial CI or physical absence.
                    update_.reset();work_.complete=true;decline_=FrequencyWorkDecline::presence_unqualified;
                    result.complete=true;return result;
                }
                continue;
            }
            if(work_.cursor==work_.cells)break;
            const auto complete=shape_pending_?advance_shape():analyze_cell(work_.cursor);
            ++result.processed_work_units;++work_.work_units;
            if(complete) {++work_.cursor;++result.processed_cells;}
        }
        if(work_.presence_prepass_complete&&work_.cursor==work_.cells)finish_analysis();
        result.complete=work_.complete;return result;
    }
    bool commit(const FrequencyPresenceCertificate& presence,std::uint64_t generation) {
        if(!work_.pending||!work_.complete) {decline_=FrequencyWorkDecline::analysis_incomplete;return false;}
        work_.pending=false;
        if(generation==UINT64_MAX||work_.generation!=generation||presence.generation!=generation||presence.capture_revision!=work_.revision) {
            decline_=FrequencyWorkDecline::generation_changed;update_.reset();qualified_.reset();return false;
        }
        if(!input_.valid||input_.next<=work_.end||input_.next-1-work_.end>rate_) {
            decline_=FrequencyWorkDecline::stale_support;update_.reset();qualified_.reset();return false;
        }
        if((presence.state!=FrequencyPresenceState::unavailable&&presence.observed_sample>=input_.next)||
           (presence.state==FrequencyPresenceState::absent&&presence.observed_sample>=work_.end)) {
            decline_=FrequencyWorkDecline::presence_unqualified;update_.reset();qualified_.reset();return false;
        }
        if(!work_.recent) {
            decline_=FrequencyWorkDecline::presence_unqualified;update_.reset();return false;
        }
        if(!update_) {decline_=FrequencyWorkDecline::no_reference;return false;}
        if(!(update_->frequency_uncertainty_hz<width_)) {
            decline_=FrequencyWorkDecline::aliases_remain;update_.reset();return false;
        }
        qualified_=std::move(update_);update_.reset();decline_=FrequencyWorkDecline::none;return true;
    }
    void cancel() noexcept {work_=Work{};update_.reset();}
    std::uint64_t ordinal() const noexcept {return input_.ordinal;}
    void set_ordinal(std::uint64_t ordinal) noexcept {input_.ordinal=ordinal;}
    const std::optional<Reference>& qualified() const noexcept {return qualified_;}
    FrequencyWorkDescriptor descriptor(std::uint64_t generation) const {
        FrequencyWorkDescriptor d;d.heap_bytes=sizeof(*this);
        d.worker_scratch_bytes=Receiver::optional_frequency_worker_scratch_bound;
        d.required_bytes=d.actual_bytes=d.heap_bytes+d.worker_scratch_bytes;d.sample_rate=rate_;
        d.generation=generation;d.capture_revision=work_.revision;
        d.look_ordinal=work_.pending?work_.prepared_ordinal:look_;d.opportunity_ordinal=input_.ordinal;
        d.capture_next_sample=input_.next;
        d.presence_interval_first_sample=work_.recent_first;d.presence_interval_end_sample=work_.end;
        d.presence_first_sample=work_.presence_first;d.presence_end_sample=work_.presence_end;
        d.recent_presence_qualified=work_.recent;
        d.presence_snr_db=work_.recent?10*std::log10(work_.recent_cn0/10):0;
        d.aperture_seconds=static_cast<double>(target_*bin_n_)/rate_;
        d.physical_prior_half_width_hz=width_;d.first_sample=work_.first;d.end_sample=work_.end;
        d.look_cadence_seconds=static_cast<double>(((rate_*2/5+bin_n_-1)/bin_n_)*bin_n_)/rate_;
        d.processed_cells=work_.cursor;d.total_cells=work_.cells;d.processed_work_units=work_.work_units;
        d.maximum_work_units_per_advance=maximum_work_units_per_advance;d.maximum_shape_subsets_per_mask=326;
        d.decline=decline_;
        d.state=qualified_?FrequencyWorkState::publication_qualified:FrequencyWorkState::ingested_support;
        if(work_.pending)d.state=!work_.complete?FrequencyWorkState::frozen_work:
            work_.cursor==work_.cells?FrequencyWorkState::all_cells_complete:FrequencyWorkState::analysis_declined;
        if(generation==UINT64_MAX||(work_.pending&&work_.generation!=generation))d.decline=FrequencyWorkDecline::generation_changed;
        // Pending work owns its descriptor, including a terminal early
        // decline. An older stale cache must not hide that completed result.
        if(!work_.pending&&qualified_&&input_.next>qualified_->frequency_sample&&input_.next-1-qualified_->frequency_sample>rate_) {
            d.state=FrequencyWorkState::ingested_support;d.decline=FrequencyWorkDecline::stale_support;
        }
        return d;
    }
};
}
struct Receiver::Impl {
    struct Bin {std::complex<double> z{};std::uint64_t first=0;};
    struct State {
        // Fixed builtin/complex<double> values, fixed arrays and disengaged
        // optionals only: no allocation or user-provided throwing constructor.
        // libstdc++ complex value initialization lacks a noexcept annotation.
        State() noexcept = default;
        std::uint32_t rate=0;std::uint64_t next=0;
        std::size_t bin_size=0,bin_count=0,ring_count=0,ring_position=0;
        std::uint64_t bin_first=0;
        std::complex<double> bin_osc{1,0},bin_step{},bin_sum{};
        double bin_cc=0,bin_ss=0,bin_cs=0,coarse_hz=0;
        // The physical search grid remains centered on immutable expected.
        // A basis chosen from past PCM is used only for later complete bins.
        double basis_hz=0,pending_basis_hz=0;
        std::uint64_t last_basis_sample=0;
        std::array<Bin,narrow_bins> ring{};
        bool coarse_valid=false;
        std::uint32_t nuisance_range=0;
        std::complex<double> previous_bin{};
        std::complex<double> increment_sum{};std::size_t increments=0;
        double guess_hz=0;
        std::size_t fit_count=0,fit_size=0;
        std::uint64_t phase_model_looks=0;
        std::uint64_t fit_first=0;
        std::complex<double> fit_osc{1,0},fit_step{};
        std::array<std::array<double,6>,6> fit_gram{};
        std::array<double,6> fit_rhs{};
        double fit_energy=0,fit_hz=0;
        // Fixed public-band sufficient statistics, not a raw physical PCM buffer.
        std::array<std::complex<double>,27> nuisance_rhs{};
        std::uint32_t nuisance_first() const {return nuisance_range>>5;}
        std::uint32_t nuisance_count() const {return nuisance_range&31;}
        std::optional<ToneReference> candidate,reference;
        std::optional<Reference> update;
        PunctuationObservation actual_punctuation;
        bool punctuation_arithmetic_qualified=true;
        double amplitude=0;
        double punctuation=-1,punctuation_uncertainty=0,anchor=0,timing_uncertainty=0;
        double boundary_notch=-1,boundary_level=1;
        bool timing_valid=false,metadata_timing_hint=false;
        std::uint64_t punctuation_count=0;
        double metadata_dwell=0,metadata_last_dwell=-1;
        // Source seconds per capture second. Only the private auxiliary
        // decoder sets this from the two measured tone frequencies; the
        // ordinary reference/discovery path retains its original clock.
        double waveform_scale=1;
        bool metadata_active=false;
        std::complex<double> metadata_osc{1,0},metadata_step{};
        Tri bb{},dd{};Cross bd{};
        std::array<Pair,vertices> rhs{},rhs_derivative{};
        double metadata_energy=0,metadata_gain=0;
        std::size_t metadata_samples=0;
    } state;
    double expected,width;
    std::vector<Crypto> keys;
    std::optional<double> capture_epoch; // UTC seconds at capture sample zero, if known.
    double capture_anchor_sample_seconds=0;
    OccupiedBand conditioning;
    FrequencyPrecisionOptions precision;
    PunctuationFormat punctuation_format;
    std::uint64_t punctuation_generation=0;
    std::unique_ptr<FrequencyProfile> optional_frequency;
    std::uint64_t optional_generation=1,optional_ordinal=0;
    FrequencyWorkDecline optional_decline=FrequencyWorkDecline::grant_unavailable;
    explicit Impl(double e,double w,std::vector<Crypto> k,std::optional<OccupiedBand> band,
                  FrequencyPrecisionOptions p,PunctuationFormat format)
        :expected(e),width(w),keys(std::move(k)),conditioning(band.value_or(OccupiedBand{})),precision(p),
         punctuation_format(format) {}

    void bump_optional_generation() noexcept {
        if(optional_generation!=UINT64_MAX)++optional_generation;
    }
    void reset_state() noexcept {
        static_assert(std::is_nothrow_default_constructible_v<State>);
        static_assert(std::is_nothrow_destructible_v<State>);
        std::destroy_at(&state);std::construct_at(&state);
        // Exhaustion permanently declines optional marker provenance instead
        // of reusing an identity from an older capture generation.
        if(punctuation_generation!=UINT64_MAX)++punctuation_generation;
    }
    void configure(std::uint32_t rate,std::uint64_t first) {
        reset_state();bump_optional_generation();
        if(optional_frequency)optional_frequency->reset_ingest(first);
        auto& s=state;
        if(rate<1000||expected-width<10||expected+width>=rate/2.-10)
            throw Error("Helper Carrier precision band is outside capture passband");
        s.rate=rate;s.next=first;s.bin_first=first;
        s.bin_size=std::max<std::size_t>(2,rate/500);
        s.fit_size=std::max<std::size_t>(16,rate/25);
        s.bin_step=std::polar(1.,-tau*expected/rate);
        s.guess_hz=expected;s.coarse_hz=expected;s.basis_hz=expected;
    }
    double seconds(std::uint64_t sample) const {return static_cast<double>(sample)/state.rate;}
    double local(double time) const {
        auto t=std::fmod((time-state.anchor)*state.waveform_scale,.4);return t<0?t+.4:t;
    }
    bool tone_time(double time) const {
        if(!state.timing_valid)return true;
        const auto t=local(time);
        // Both formats leave a straight flank before compact metadata. The
        // separate auxiliary remains inverted after its single240ms edge.
        const auto guard=std::max(1./state.rate,state.timing_uncertainty)*state.waveform_scale;
        const auto straight_begin=punctuation_format==PunctuationFormat::auxiliary_single?.244:.284;
        return (t>=.008&&t<.212)||(t>=straight_begin+guard&&t<.316-guard);
    }
    void fuse_frequency(Reference& reference) const {
        if(!optional_frequency||!optional_frequency->qualified())return;
        const auto& selected=*optional_frequency->qualified();
        const auto date=selected.frequency_sample;
        if(date>reference.sample||reference.sample-date>state.rate||
           selected.sample_rate!=reference.sample_rate||
           selected.frequency_uncertainty_hz>=reference.frequency_uncertainty_hz||
           std::abs(selected.frequency_hz-reference.frequency_hz)>
               selected.frequency_uncertainty_hz+reference.frequency_uncertainty_hz)return;
        reference.frequency_hz=selected.frequency_hz;
        reference.frequency_uncertainty_hz=selected.frequency_uncertainty_hz;
        reference.frequency_sample=date;reference.frequency_first_sample=selected.frequency_first_sample;
        reference.frequency_fit=selected.frequency_fit;
        reference.frequency_phase_diffusion_gain=selected.frequency_phase_diffusion_gain;
        reference.frequency_process_uncertainty_hz=selected.frequency_process_uncertainty_hz;
    }

#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    static std::array<std::complex<long double>,3> moments(long double frequency,
                                                          std::size_t count,std::uint32_t rate) {
        const auto omega=2*std::numbers::pi_v<long double>*frequency/rate;
        const auto q=std::polar(1.L,omega),qn=std::polar(1.L,omega*count);
        const auto d=1.L-q;const auto N=static_cast<long double>(count);
        const auto s0=(1.L-qn)/d;
        const auto s1=(q-N*qn+(N-1)*qn*q)/(d*d);
        const auto s2=(q+q*q-N*N*qn+(2*N*N-2*N-1)*qn*q-(N-1)*(N-1)*qn*q*q)/(d*d*d);
        return {s0,s1/N-.5L*s0,s2/(N*N)-s1/N+.25L*s0};
    }
    void start_nuisance() {
        auto& s=state;s.nuisance_rhs={};s.nuisance_range=0;
        if(!std::isfinite(conditioning.lower_hz)||!std::isfinite(conditioning.upper_hz)||
           conditioning.lower_hz<=0||conditioning.upper_hz<=conditioning.lower_hz||conditioning.upper_hz>=s.rate/2.||
           (conditioning.lower_hz<=expected+width&&conditioning.upper_hz>=expected-width))return;
        const auto step=static_cast<double>(s.rate)/s.fit_size;
        const auto first=std::floor(conditioning.lower_hz/step),last=std::ceil(conditioning.upper_hz/step);
        if(first<1||last>=s.fit_size/2.||last<first||last-first+1>s.nuisance_rhs.size()||
           (first*step<=expected+width&&last*step>=expected-width)||
           s.fit_size<=6+2*(last-first+1))return;
        if(first>static_cast<double>(UINT32_MAX>>5))return;
        s.nuisance_range=(static_cast<std::uint32_t>(first)<<5)|static_cast<std::uint32_t>(last-first+1);
    }
    void nuisance_sample(float sample) {
        auto& s=state;if(!s.nuisance_count())return;
        const auto angle=tau*static_cast<double>(s.fit_count)/s.fit_size;
        const auto harmonic=std::polar(1.,angle);
        auto oscillator=std::polar(1.,angle*s.nuisance_first());
        for(std::size_t k=0;k<s.nuisance_count();++k) {
            s.nuisance_rhs[k]+=static_cast<double>(sample)*oscillator;oscillator*=harmonic;
        }
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool nuisance_cross(std::size_t k,std::array<double,6>& cosine,std::array<double,6>& sine) const {
        const auto& s=state;
        const auto frequency=static_cast<long double>(s.nuisance_first()+k)*s.rate/s.fit_size;
        // Public-band overlap is rejected above; this also protects the
        // finite geometric-moment denominators if a fit approaches it.
        if(std::abs(s.fit_hz-static_cast<double>(frequency))<static_cast<double>(s.rate)/s.fit_size)return false;
        const auto minus=moments(s.fit_hz-frequency,s.fit_size,s.rate);
        const auto plus=moments(s.fit_hz+frequency,s.fit_size,s.rate);
        for(unsigned power=0;power<3;++power) {
            cosine[2*power]=static_cast<double>(.5L*(minus[power].real()+plus[power].real()));
            cosine[2*power+1]=static_cast<double>(.5L*(minus[power].imag()+plus[power].imag()));
            sine[2*power]=static_cast<double>(.5L*(plus[power].imag()-minus[power].imag()));
            sine[2*power+1]=static_cast<double>(.5L*(minus[power].real()-plus[power].real()));
        }
        return true;
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool condition_fit(double raw_residual,std::array<double,6>& coefficients,
                       double& residual,double& degrees,double& trace) {
        auto& s=state;if(!s.nuisance_count())return false;
        trace=0;
        const auto scale=2./s.fit_size;double nuisance_energy=0;
        for(std::size_t k=0;k<s.nuisance_count();++k) {
            std::array<double,6> cosine{},sine{};
            if(!nuisance_cross(k,cosine,sine))return false;
            for(unsigned column=0;column<2;++column) {
                const auto& cross=column?sine:cosine;
                const auto measurement=column?s.nuisance_rhs[k].imag():s.nuisance_rhs[k].real();
                std::array<double,6> inverse_cross{};
                if(!solve6(s.fit_gram,cross,inverse_cross))return false;
                for(unsigned i=0;i<6;++i) {
                    trace+=scale*cross[i]*inverse_cross[i];s.fit_rhs[i]-=scale*cross[i]*measurement;
                }
                nuisance_energy+=scale*measurement*measurement;
            }
        }
        if(!std::isfinite(trace)||trace<0||trace>.25)return false;
        // Raw candidate/reference/guess and publication are already committed.
        // These completed-frame arrays are scratch until the next fit starts.
        for(std::size_t k=0;k<s.nuisance_count();++k) {
            std::array<double,6> cosine{},sine{};
            if(!nuisance_cross(k,cosine,sine))return false;
            for(unsigned column=0;column<2;++column) {
                const auto& cross=column?sine:cosine;
                for(unsigned i=0;i<6;++i)for(unsigned j=0;j<6;++j)s.fit_gram[i][j]-=scale*cross[i]*cross[j];
            }
        }
        if(!solve6(s.fit_gram,s.fit_rhs,coefficients))return false;
        auto explained=nuisance_energy;
        for(unsigned i=0;i<6;++i)explained+=coefficients[i]*s.fit_rhs[i];
        residual=s.fit_energy-explained;
        degrees=static_cast<double>(s.fit_size)-6-2*s.nuisance_count();
        const auto roundoff=256*std::numeric_limits<double>::epsilon()*s.fit_size*
            (6+2*s.nuisance_count())*s.fit_energy;
        // Never clamp away negative/unexplained energy. Very pure numerical
        // cases and an inconsistent nested projection retain the raw estimate.
        if(!std::isfinite(residual)||residual<=roundoff||
           residual>raw_residual+roundoff||degrees<=0)return false;
        return true;
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    bool frequency_process_model(const std::array<double,6>& x,
            const std::array<std::array<double,6>,6>& gram,
            const std::array<double,6>& influence,double noise,double& gain,double& allowance,double& phase_allowance) const {
        const auto& s=state;
        const auto duration=static_cast<double>(s.fit_size)/s.rate;
        const auto sigma=precision.phase_noise_radians_per_sqrt_second;
        const auto ordinal=static_cast<double>(std::max<std::uint64_t>(1,s.phase_model_looks));
        // Reflection-principle allowance for the complete finite aperture.
        // These counters survive internal basis changes; rejected frames spend
        // an opportunity too. No publication-gap denominator is used.
        const auto z=std::max(8.,std::sqrt(2*(std::log(1600.)+
            std::log(ordinal)+std::log(ordinal+1))));
        const auto h=z*sigma*std::sqrt(duration);
        const std::complex<double> c0{x[0],-x[1]},c1{x[2],-x[3]},c2{x[4],-x[5]};
        const auto endpoint=static_cast<double>(s.fit_size-1)/s.fit_size-.5;
        const auto c=c0+endpoint*c1+endpoint*endpoint*c2,d=c1+2*endpoint*c2;
        const auto amplitude=std::abs(c0)+.5*std::abs(c1)+.25*std::abs(c2);
        if(!std::isfinite(h)||!(amplitude>0)||!(std::abs(c)>0))return false;
        double upper=amplitude;
        std::array<std::array<double,6>,4> endpoint_influence{};
        if(sigma>0) {
            // Separate variance and full-noise-norm events. The residual is a
            // noncentral Gaussian projection conditional on the phase path;
            // signal lack of fit therefore cannot reduce this variance upper.
            const auto logarithm=std::log(1600.)+std::log(ordinal)+std::log(ordinal+1);
            const auto degrees=static_cast<double>(s.fit_size)-6;
            const auto variance_denominator=degrees-2*std::sqrt(degrees*logarithm);
            if(!(variance_denominator>0))return false;
            const auto sigma_upper=noise*std::sqrt(degrees/variance_denominator);
            const auto count=static_cast<double>(s.fit_size);
            const auto noise_norm=sigma_upper*std::sqrt(count+2*std::sqrt(count*logarithm)+2*logarithm);
            const auto sine=std::min(std::sin(tau*(expected-width)/s.rate),
                                     std::sin(tau*(expected+width)/s.rate));
            if(!(sine>0))return false;
            const auto carrier_energy=(count-1/sine)/2;
            if(!(carrier_energy>0))return false;
            const auto amplitude_denominator=std::sqrt(carrier_energy)-std::sqrt(count)*h;
            if(!(amplitude_denominator>0))return false;
            // This is a physical amplitude ball from raw energy, not an
            // amplitude fitted to the same random phase path as the kernel.
            upper=(std::sqrt(s.fit_energy)+noise_norm)/amplitude_denominator;
            if(!std::isfinite(upper)||!(upper>0))return false;
            const std::array<std::array<double,6>,4> gradients{{
                {1,0,endpoint,0,endpoint*endpoint,0},
                {0,-1,0,-endpoint,0,-endpoint*endpoint},
                {0,0,1,0,2*endpoint,0},
                {0,0,0,-1,0,-2*endpoint}}};
            for(unsigned j=0;j<4;++j)
                if(!solve6(gram,gradients[j],endpoint_influence[j]))return false;
        }
        auto oscillator=std::polar(1.,tau*s.fit_hz*static_cast<double>(s.fit_size-1)/s.rate);
        const auto step=std::polar(1.,-tau*s.fit_hz/s.rate);
        double tail=0,integral=0,absolute_gain=0,Lc=0,Ld=0;
        for(std::size_t reverse=s.fit_size;reverse>0;--reverse) {
            const auto i=reverse-1;
            const auto u=static_cast<double>(i)/s.fit_size-.5;
            const auto real=oscillator.real(),imaginary=oscillator.imag();
            const std::array<double,6> basis{real,imaginary,u*real,u*imaginary,u*u*real,u*u*imaginary};
            double k=0;std::array<double,4> endpoint_kernel{};
            for(unsigned j=0;j<6;++j) {
                k+=influence[j]*basis[j];
                if(sigma>0)for(unsigned q=0;q<4;++q)
                    endpoint_kernel[q]+=endpoint_influence[q][j]*basis[j];
            }
            const auto fitted_phase_derivative=-std::imag((c0+u*c1+u*u*c2)*oscillator);
            tail+=k*fitted_phase_derivative;
            if(i>0)integral+=tail*tail/s.rate;
            absolute_gain+=std::abs(k)*upper;
            if(sigma>0) {
                Lc+=std::hypot(endpoint_kernel[0],endpoint_kernel[1])*upper;
                Ld+=std::hypot(endpoint_kernel[2],endpoint_kernel[3])*upper;
            }
            oscillator*=step;
        }
        // Diagnostic fixed fitted-kernel Brownian gain. Because this kernel
        // depends on the observed phase path, it is NOT treated as a fixed
        // Gaussian variable when constructing the process allowance below.
        gain=std::sqrt(integral)*(1+1e-10);
        allowance=phase_allowance=0;
        if(sigma>0) {
            const auto dc=Lc*h,dd=Ld*h,norm=std::abs(c);
            if(!std::isfinite(dc)||!std::isfinite(dd)||dc>=norm)return false;
            // On the uniformly bounded path event, the ENTIRE raw input
            // change is at most A_upper*h pointwise. This remains pathwise
            // valid for a data-dependent frequency gradient. The ratio term
            // encloses the full nonlinear d/c change over that input ball.
            const auto ratio_remainder=(dd*dc/(norm*norm)+std::abs(d)*dc*dc/(norm*norm*norm))/
                ((1-dc/norm)*tau*duration);
            allowance=h*absolute_gain+ratio_remainder;
            // The fit's endpoint phase and the actual endpoint phase both
            // move relative to the same no-RW counterfactual. Future phase
            // forecasting begins only after this measured endpoint allowance.
            phase_allowance=h+std::asin(dc/norm);
        }
        return std::isfinite(gain)&&gain>=0&&std::isfinite(allowance)&&allowance>=0&&
            std::isfinite(phase_allowance)&&phase_allowance>=0;
    }

    template<typename Result>
    bool estimate_frame(const std::array<double,6>& x,
            const std::array<std::array<double,6>,6>& gram,double residual,double degrees,
            Result& r,double& output_amplitude,bool raw_frequency_kernel=true) const {
        const auto& s=state;
        const auto amplitude=std::hypot(x[0],x[1]);
        if(!(amplitude>1e-9)||degrees<=0)return false;
        const auto duration=static_cast<double>(s.fit_size)/s.rate;
        const std::complex<double> c0{x[0],-x[1]},c1{x[2],-x[3]},c2{x[4],-x[5]};
        const auto correction=std::imag(c1*std::conj(c0))/std::norm(c0);
        // The quadratic envelope must agree with a slowly rotating phasor,
        // rather than admitting an arbitrary smooth burst as a reference.
        const auto curvature_error=std::abs(c2+.5*c0*correction*correction)/amplitude;
        if(std::abs(correction)>.12||curvature_error>.01)return false;
        r.sample=s.fit_first+s.fit_size-1;r.sample_rate=s.rate;
        r.phase_observation_sample=r.sample;r.phase_observation_known=true;
        const auto u=static_cast<double>(s.fit_size-1)/s.fit_size-.5;
        const auto c=c0+u*c1+u*u*c2,derivative=c1+2*u*c2;
        const auto frontier_amplitude=std::abs(c);
        if(frontier_amplitude<amplitude*.98||frontier_amplitude>amplitude*1.02)return false;
        const auto frequency=s.fit_hz+std::imag(derivative*std::conj(c))/
            (std::norm(c)*tau*duration);
        if(!std::isfinite(frequency)||std::abs(frequency-expected)>width+1e-6)return false;
        r.frequency_hz=frequency;
        auto phase=std::arg(c)+tau*s.fit_hz*static_cast<double>(s.fit_size-1)/s.rate;
        // Remove only the selected waveform's known punctuation sign. This
        // auxiliary phase never becomes the pure pilot's phase observation.
        const auto dwell_phase=local(seconds(r.sample));
        const auto inverted_end=punctuation_format==PunctuationFormat::auxiliary_single?.4:.276;
        if(s.timing_valid&&dwell_phase>=.244&&dwell_phase<inverted_end)phase-=std::numbers::pi;
        r.phase_radians=phase_wrap(phase);
        const auto noise=std::sqrt(residual/degrees);
        // Endpoint evaluation has greater variance than a center phasor. Use
        // the actual finite-window six-column Gram covariance, including the
        // quadratic envelope and noninteger carrier cycles.
        std::array<double,6> phase_gradient{},frequency_gradient{},covariance{};
        const auto norm=std::norm(c),numerator=std::imag(derivative*std::conj(c));
        for(unsigned j=0;j<6;++j) {
            const auto power=j/2;
            const auto q=power==0?1.:power==1?u:u*u;
            const auto dq=power==0?0.:power==1?1.:2*u;
            const auto direction=j%2?std::complex<double>{0,-1}:std::complex<double>{1,0};
            const auto dc=q*direction,dd=dq*direction;
            phase_gradient[j]=std::imag(dc*std::conj(c))/norm;
            frequency_gradient[j]=(std::imag(dd*std::conj(c)+derivative*std::conj(dc))/norm-
                numerator*2*std::real(dc*std::conj(c))/(norm*norm))/(tau*duration);
        }
        if(!solve6(gram,phase_gradient,covariance))return false;
        double phase_variance=0;
        for(unsigned j=0;j<6;++j)phase_variance+=phase_gradient[j]*covariance[j];
        if(!solve6(gram,frequency_gradient,covariance))return false;
        double frequency_variance=0;
        for(unsigned j=0;j<6;++j)frequency_variance+=frequency_gradient[j]*covariance[j];
        if(!std::isfinite(phase_variance)||!std::isfinite(frequency_variance)||
           phase_variance<=0||frequency_variance<=0)return false;
        const auto phase_noise=std::max(2e-6,8*noise*std::sqrt(phase_variance));
        const auto frequency_noise=8*noise*std::sqrt(frequency_variance);
        r.frequency_first_sample=s.fit_first;r.frequency_sample=r.sample;
        r.frequency_fit=raw_frequency_kernel?FrequencyFit::local_polynomial:FrequencyFit::conditioned_polynomial;
        double phase_process=0;
        if(raw_frequency_kernel&&!frequency_process_model(x,gram,covariance,noise,
                r.frequency_phase_diffusion_gain,r.frequency_process_uncertainty_hz,phase_process))return false;
        const auto radial=std::real(c1*std::conj(c0))/std::norm(c0);
        const auto curvature=2*std::imag(c2*std::conj(c0))/std::norm(c0)-2*radial*correction;
        // A downstream constant-frequency reference omits measured phase
        // acceleration. Include its half-window frequency span and linear
        // phase remainder. The fitted quadratic phase's third derivative is
        // bounded by M^3+3*M*|curvature|, M=|slope|+|curvature|/2. Endpoint
        // polynomial projection/remainder bounds supply the remaining terms;
        // deterministic model error must not be divided by sqrt(sample count).
        const auto slope_bound=std::abs(correction)+.5*std::abs(curvature);
        const auto third=slope_bound*slope_bound*slope_bound+3*slope_bound*std::abs(curvature);
        const auto local_phase_uncertainty=phase_noise+std::abs(curvature)/8+third/4;
        const auto local_frequency_uncertainty=std::max(1e-5,frequency_noise)+
            (std::abs(curvature)/2+third)/(tau*duration);
        r.phase_uncertainty_radians=local_phase_uncertainty+phase_process;
        if constexpr(std::is_same_v<Result,ToneReference>) {
            r.phase_process_uncertainty_radians=phase_process;
            r.metadata_phase_uncertainty_radians=local_phase_uncertainty;
            r.metadata_frequency_uncertainty_hz=local_frequency_uncertainty;
        }
        r.frequency_uncertainty_hz=local_frequency_uncertainty+r.frequency_process_uncertainty_hz;
        r.frequency_valid=r.phase_valid=true;
        r.timing_valid=s.timing_valid;r.dwell_uncertainty_seconds=s.timing_uncertainty;
        r.dwell_start_seconds=seconds(r.sample)-local(seconds(r.sample))/s.waveform_scale;
        if constexpr(std::is_same_v<Result,Reference>)attach_punctuation(r,s.actual_punctuation);
        output_amplitude=frontier_amplitude;return true;
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    std::optional<double> finish_fit() {
        auto& s=state;
        if(s.phase_model_looks==UINT64_MAX)return {};
        ++s.phase_model_looks;
        std::array<double,6> x{};
        if(!solve6(s.fit_gram,s.fit_rhs,x))return {};
        double explained=0;for(unsigned i=0;i<6;++i)explained+=x[i]*s.fit_rhs[i];
        const auto residual=std::max(0.,s.fit_energy-explained);
        const auto amplitude=std::hypot(x[0],x[1]);
        // The original raw-tone admission is untouched. Nuisance explained
        // energy never helps an absent helper pass this gate.
        if(!(amplitude>1e-9)||s.fit_energy<=0||explained<s.fit_energy*.90)return {};
        ToneReference r;double frontier_amplitude=0;
        if(!estimate_frame(x,s.fit_gram,residual,std::max<double>(1,s.fit_size-6),r,frontier_amplitude))return {};
        const auto frequency=r.frequency_hz;
        bool accepted=false;
        if(s.candidate) {
            const auto dt=static_cast<double>(r.sample-s.candidate->sample)/s.rate;
            auto error=phase_wrap(r.phase_radians-s.candidate->phase_radians-tau*s.candidate->frequency_hz*dt);
            // Prior to timing acquisition an observed reversal may change the
            // helper sign. It can never establish a payload phase convention.
            if(!s.timing_valid&&std::abs(std::abs(error)-std::numbers::pi)<.03)
                error=phase_wrap(error-std::numbers::pi);
            // A process allowance supports downstream oscillator inference;
            // it must not broaden this original raw CW/model consistency
            // check enough to admit an APSK or punctuation transition.
            const auto raw_phase=r.phase_uncertainty_radians-r.phase_process_uncertainty_radians;
            const auto previous_raw_phase=s.candidate->phase_uncertainty_radians-
                s.candidate->phase_process_uncertainty_radians;
            const auto raw_frequency=r.frequency_uncertainty_hz-r.frequency_process_uncertainty_hz;
            const auto previous_raw_frequency=s.candidate->frequency_uncertainty_hz-
                s.candidate->frequency_process_uncertainty_hz;
            const auto allowance=std::max(.01,6*(raw_phase+previous_raw_phase+tau*dt*previous_raw_frequency));
            if(std::abs(error)<allowance&&std::abs(r.frequency_hz-s.candidate->frequency_hz)<
               std::max(.05,6*(raw_frequency+previous_raw_frequency))) {
                s.reference=r;accepted=true;
                if(!s.update||!s.update->metadata) {s.update=r.publication(s.actual_punctuation);fuse_frequency(*s.update);}
                s.amplitude=frontier_amplitude;s.guess_hz=frequency;
            }
        }
        s.candidate=r;
        if(!accepted||!s.update||s.update->metadata||s.update->sample!=r.sample)return {};
        return residual;
    }
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void refine_accepted(double residual) {
        auto& s=state;
        // The Schur-conditioned input map has different phase/frequency
        // kernels. With a process model, keep both measured raw components
        // until that separate optional map has its own process enclosure.
        if(precision.phase_noise_radians_per_sqrt_second>0)return;
        if(!s.reference||!s.update||s.update->metadata||s.update->sample!=s.reference->sample)return;
        const auto& r=*s.reference;
        std::array<double,6> x{};
        bool used=false;double joint_residual=-1,joint_degrees=0,trace=-1;
        Reference refined;double refined_amplitude=0;
        if(condition_fit(residual,x,joint_residual,joint_degrees,trace)&&
           estimate_frame(x,s.fit_gram,joint_residual,joint_degrees,refined,refined_amplitude,false)) {
            if(refined.frequency_uncertainty_hz<=r.frequency_uncertainty_hz&&
               refined.phase_uncertainty_radians<=r.phase_uncertainty_radians&&
               std::abs(refined.frequency_hz-r.frequency_hz)<=r.frequency_uncertainty_hz+refined.frequency_uncertainty_hz&&
               std::abs(phase_wrap(refined.phase_radians-r.phase_radians))<=r.phase_uncertainty_radians+refined.phase_uncertainty_radians)
                used=true;
        }
        if(used&&s.update&&!s.update->metadata&&s.update->sample==r.sample) {s.update=std::move(refined);fuse_frequency(*s.update);}
    }
    void fit_sample(float sample,std::uint64_t index) {
        auto& s=state;
        if(!tone_time(seconds(index))) {s.fit_count=0;return;}
        if(!s.fit_count) {
            // Nine metadata symbols start at316ms. For the paired laboratory
            // waveform the final straight flank is only32ms; keep the normal
            // 40ms aperture elsewhere, and fit that flank in31.25ms. The
            // covariance and process uncertainty use the selected aperture.
            const auto t=local(seconds(index));
            const bool short_flank=s.timing_valid&&punctuation_format==PunctuationFormat::paired_legacy&&
                t>=.284&&t<.316;
            s.fit_size=std::max<std::size_t>(16,s.rate/(short_flank?32:25));
            s.fit_first=index;s.fit_gram={};s.fit_rhs={};s.fit_energy=0;
            s.fit_hz=s.guess_hz;s.fit_osc={1,0};s.fit_step=std::polar(1.,tau*s.fit_hz/s.rate);
            start_nuisance();
        }
        const auto u=static_cast<double>(s.fit_count)/s.fit_size-.5;
        const auto c=s.fit_osc.real(),v=s.fit_osc.imag();
        const std::array<double,6> basis{c,v,u*c,u*v,u*u*c,u*u*v};
        for(unsigned i=0;i<6;++i) {
            s.fit_rhs[i]+=sample*basis[i];
            for(unsigned j=i;j<6;++j)s.fit_gram[i][j]+=basis[i]*basis[j];
        }
        nuisance_sample(sample);
        s.fit_energy+=static_cast<double>(sample)*sample;s.fit_osc*=s.fit_step;
        if(++s.fit_count==s.fit_size) {
            for(unsigned i=0;i<6;++i)for(unsigned j=0;j<i;++j)s.fit_gram[i][j]=s.fit_gram[j][i];
            if(const auto accepted=finish_fit())refine_accepted(*accepted);
            s.fit_count=0;
        }
    }
    const Bin& bin(std::size_t age) const {
        const auto& s=state;return s.ring[(s.ring_position+s.ring.size()-1-age)%s.ring.size()];
    }
    std::complex<double> aligned(const Bin& b,double center) const {
        const auto t=(static_cast<double>(b.first)+.5*state.bin_size)/state.rate;
        return b.z*std::polar(1.,-tau*(state.guess_hz-state.basis_hz)*(t-center));
    }
    void find_punctuation() {
        auto& s=state;if(s.metadata_timing_hint||s.ring_count<32||!s.coarse_valid)return;
        const auto center=(static_cast<double>(bin(13).first)+.5*s.bin_size)/s.rate;
        if(s.punctuation>=0&&center-s.punctuation<.1)return;
        std::complex<double> left{},right{};double left_power=0,right_power=0;
        for(std::size_t age=18;age<=28;++age) {const auto z=aligned(bin(age),center);left+=z;left_power+=std::norm(z);}
        for(std::size_t age=0;age<=10;++age) {const auto z=aligned(bin(age),center);right+=z;right_power+=std::norm(z);}
        left/=11;right/=11;
        if(std::norm(left)<1e-12||std::norm(right)<1e-12||
           11*std::norm(left)<.998*left_power||11*std::norm(right)<.998*right_power||
           std::abs(std::abs(right)/std::abs(left)-1)>.03||std::abs(phase_wrap(std::arg(-right/left)))>.015)return;
        // A changing real-tone envelope also changes the carrier image in
        // each finite bin. Project the candidate reversal through the same
        // sampled carrier/Gram model as finish_bin; a scalar envelope average
        // biases timing with carrier phase and can corrupt dense metadata.
        struct ReversalBin {
            double begin;
            std::complex<double> carrier,alignment,observed;
        };
        std::array<ReversalBin,8> reversal_bins;
        const auto difference=s.guess_hz-s.basis_hz;
        const auto basis_step=std::polar(1.,tau*s.basis_hz/s.rate);
        const auto carrier_step=std::polar(1.,tau*s.guess_hz/s.rate);
        const auto sample_seconds=1./s.rate;
        const auto envelope_step=sample_seconds/.008;
        // The s.basis_hz-carrier oscillator restarts in every bin, so all
        // completed bins have this same actual finite Gram matrix.
        const auto determinant=s.bin_cc*s.bin_ss-s.bin_cs*s.bin_cs;
        if(determinant<=1e-12)return;
        for(std::size_t i=0;i<reversal_bins.size();++i) {
            const auto& b=bin(i+10);
            const auto begin=seconds(b.first);
            const auto cycles=std::fmod(static_cast<long double>(s.basis_hz)*b.first/s.rate,1.L);
            const auto phase=std::polar(1.,tau*static_cast<double>(cycles));
            reversal_bins[i]={begin,
                phase*std::polar(1.,tau*difference*(begin-center)),
                std::conj(phase)*std::polar(1.,-tau*difference*(begin+.5*static_cast<double>(s.bin_size)/s.rate-center)),
                aligned(b,center)};
        }
        const auto objective=[&](double crossing) {
            // The finite off-basis real-tone projection is not an ideal
            // complex mixer: it has both attenuation and a carrier image.
            // Fit the unknown RAW complex amplitude through that exact map,
            // rather than reusing an already projected plateau amplitude.
            std::array<std::complex<double>,8> cosine_column{},sine_column{};
            double aa=0,ab=0,bb=0,ar=0,br=0;
            for(std::size_t i=0;i<reversal_bins.size();++i) {
                const auto& b=reversal_bins[i];
                auto carrier=b.carrier,basis=std::complex<double>{1,0};
                auto position=(b.begin-crossing+.004)/.008;
                double ac=0,as=0,bc=0,bs=0;
                for(std::size_t j=0;j<s.bin_size;++j) {
                    const auto envelope=1-2*(punctuation_format==PunctuationFormat::auxiliary_single?
                        auxiliary_smooth(position):smooth(position));
                    const auto cosine=carrier.real()*envelope;
                    const auto sine=-carrier.imag()*envelope;
                    ac+=cosine*basis.real();as+=cosine*basis.imag();
                    bc+=sine*basis.real();bs+=sine*basis.imag();
                    carrier*=carrier_step;basis*=basis_step;position+=envelope_step;
                }
                const auto project=[&](double c,double v) {
                    return std::complex<double>{
                        (s.bin_ss*c-s.bin_cs*v)/determinant,
                        -(s.bin_cc*v-s.bin_cs*c)/determinant}*b.alignment;
                };
                const auto u=project(ac,as),v=project(bc,bs);
                cosine_column[i]=u;sine_column[i]=v;
                aa+=std::norm(u);bb+=std::norm(v);
                ab+=std::real(std::conj(u)*v);
                ar+=std::real(std::conj(u)*b.observed);
                br+=std::real(std::conj(v)*b.observed);
            }
            const auto normal_determinant=aa*bb-ab*ab;
            const auto scale=std::max({aa,bb,std::abs(ab)});
            if(!std::isfinite(normal_determinant)||!std::isfinite(scale)||!(scale>0)||
               normal_determinant<=256*std::numeric_limits<double>::epsilon()*scale*scale)
                return std::numeric_limits<double>::infinity();
            const auto a=(bb*ar-ab*br)/normal_determinant;
            const auto b=(aa*br-ab*ar)/normal_determinant;
            const auto amplitude_squared=a*a+b*b;
            if(!std::isfinite(amplitude_squared)||amplitude_squared<=1e-12)
                return std::numeric_limits<double>::infinity();
            double error=0;
            for(std::size_t i=0;i<reversal_bins.size();++i)
                error+=std::norm(reversal_bins[i].observed-
                    a*cosine_column[i]-b*sine_column[i]);
            return error/amplitude_squared;
        };
        double lo=center-.004,hi=center+.004;
        for(unsigned i=0;i<28;++i) {
            const auto a=lo+(hi-lo)/3,b=hi-(hi-lo)/3;
            if(objective(a)<objective(b))hi=b;else lo=a;
        }
        const auto crossing=.5*(lo+hi),error=objective(crossing);
        if(error>2e-4)return;
        const auto uncertainty=std::max(1./s.rate,.002*std::sqrt(error));
        if(s.punctuation>=0) {
            const auto intervals=std::round((crossing-s.punctuation)/.4);
            if(intervals>=1&&intervals<=3&&std::abs(crossing-s.punctuation-.4*intervals)<.0005&&
               std::abs(s.boundary_notch-(s.punctuation+.16))<.004) {
                s.timing_valid=true;s.anchor=crossing-.24;
                s.timing_uncertainty=std::max(uncertainty,std::abs(crossing-s.punctuation-.4*intervals));
                ++s.punctuation_count;
            } else {s.timing_valid=false;s.punctuation_count=1;}
        } else s.punctuation_count=1;
        s.punctuation=crossing;s.punctuation_uncertainty=uncertainty;
        if(s.punctuation_arithmetic_qualified && punctuation_numeric::environment() &&
           s.timing_valid && punctuation_generation && punctuation_generation!=UINT64_MAX && s.next) {
            // This fit used plateau/reversal bins 0..28. Keep its actual raw
            // support and date, not the much newer date of a carried tone fit.
            const auto first=bin(28).first,last=s.next-1;
            const auto position=(static_cast<long double>(crossing)-static_cast<long double>(first)/s.rate)*s.rate;
            const auto numeric=64*std::numeric_limits<double>::epsilon()*
                std::max({1.L,std::abs(static_cast<long double>(crossing)*s.rate),static_cast<long double>(first)});
            const auto radius=static_cast<long double>(uncertainty)*s.rate+numeric;
            if(std::isfinite(position) && std::isfinite(radius) && first<=last &&
               position>=radius && position+radius<=static_cast<long double>(last-first)+1) {
                const auto whole=std::floor(position);
                if(whole<=last-first) {
                    PunctuationObservation observation;
                    observation.generation=punctuation_generation;observation.sample=first+static_cast<std::uint64_t>(whole);
                    observation.fraction=static_cast<double>(position-whole);
                    observation.uncertainty_samples=std::nextafter(static_cast<double>(radius),std::numeric_limits<double>::infinity());
                    observation.first_sample=first;observation.last_sample=last;
                    // Validate the STORED rounded endpoints too. A fractional
                    // conversion that rounds to one, or an outward CI touching
                    // beyond support, declines provenance rather than exposing
                    // a malformed "known" observation.
                    const auto left=static_cast<long double>(observation.sample-first)+observation.fraction;
                    const auto right=static_cast<long double>(last-observation.sample)+1-observation.fraction;
                    if(observation.fraction>=0 && observation.fraction<1 &&
                       std::isfinite(observation.uncertainty_samples) &&
                       observation.uncertainty_samples<=left && observation.uncertainty_samples<=right) {
                        observation.known=true;s.actual_punctuation=observation;
                    }
                }
            }
        }
        s.boundary_notch=-1;s.boundary_level=1;
        // The preceding accepted candidate is before the reversal. Keeping it
        // allows the short post-reversal tone to validate the canonical phase
        // without requiring two additional 40 ms fits before metadata.
    }
    void recenter_basis() {
        auto& s=state;if(!s.pending_basis_hz)return;
        // Called after every consumer has processed the completed bin's last
        // sample. New fits therefore use only future PCM, conditionally on
        // this past basis choice. Capture indices and alpha ordinals never
        // restart; discard partial evidence and every mixed-basis phase fit.
        s.basis_hz=s.pending_basis_hz;s.pending_basis_hz=0;s.last_basis_sample=s.next;
        s.bin_step=std::polar(1.,-tau*s.basis_hz/s.rate);s.bin_osc={1,0};
        s.ring={};s.ring_count=s.ring_position=0;
        s.previous_bin={};s.increment_sum={};s.increments=0;
        s.coarse_valid=true;s.coarse_hz=s.guess_hz=s.basis_hz;
        s.fit_count=0;s.candidate.reset();s.reference.reset();
        s.timing_valid=false;s.punctuation=-1;s.boundary_notch=-1;s.boundary_level=1;
        s.amplitude=0;s.metadata_active=false;s.metadata_last_dwell=-1;
    }
    void finish_bin() {
        auto& s=state;
        const auto determinant=s.bin_cc*s.bin_ss-s.bin_cs*s.bin_cs;
        std::complex<double> z{};
        if(determinant>1e-12)z={(s.bin_ss*s.bin_sum.real()-s.bin_cs*(-s.bin_sum.imag()))/determinant,
            -(s.bin_cc*(-s.bin_sum.imag())-s.bin_cs*s.bin_sum.real())/determinant};
        // bin_sum uses exp(-i wt), hence its negative imaginary component is
        // the sine RHS; the returned phasor uses the conventional +i phase.
        const auto cycles=std::fmod(static_cast<long double>(s.basis_hz)*s.bin_first/s.rate,1.L);
        z*=std::polar(1.,-tau*static_cast<double>(cycles));
        if(std::norm(z)>1e-14&&std::norm(s.previous_bin)>1e-14) {
            const auto increment=z*std::conj(s.previous_bin);
            const auto delta=std::arg(increment);
            const auto expected_delta=tau*(s.guess_hz-s.basis_hz)*s.bin_size/s.rate;
            if((s.coarse_valid?std::abs(phase_wrap(delta-expected_delta))<.35:
                std::abs(delta)<=tau*(width+1)*s.bin_size/s.rate)&&
               std::abs(std::abs(z)/std::abs(s.previous_bin)-1)<.1) {
                s.increment_sum+=increment/std::abs(increment);++s.increments;
                if(s.increments>=16) {
                    const auto value=s.basis_hz+std::arg(s.increment_sum)*s.rate/(tau*s.bin_size);
                    if(std::abs(s.increment_sum)>s.increments*.995&&std::abs(value-expected)<=width+1e-6) {
                        s.coarse_hz=value;s.coarse_valid=true;if(!s.reference)s.guess_hz=value;
                    }
                    s.increment_sum={};s.increments=0;
                }
            } else {s.increment_sum={};s.increments=0;}
        }
        s.previous_bin=z;
        s.ring[s.ring_position]={z,s.bin_first};s.ring_position=(s.ring_position+1)%s.ring.size();
        s.ring_count=std::min(s.ring.size(),s.ring_count+1);
        // The reversal alone supplies precise timing. A following dwell ramp
        // must also have its independent amplitude notch at +160 ms before
        // another reversal is allowed to establish the 400 ms grid. Metadata
        // need not be a straight tone near that boundary.
        const auto bin_center=(static_cast<double>(s.bin_first)+.5*s.bin_size)/s.rate;
        if(s.punctuation>=0&&s.amplitude>0&&std::abs(bin_center-s.punctuation-.16)<.004) {
            const auto level=std::abs(z)/s.amplitude;
            if(level<.3&&level<s.boundary_level) {s.boundary_notch=bin_center;s.boundary_level=level;}
        }
        find_punctuation();
        s.bin_first+=s.bin_size;s.bin_count=0;s.bin_osc={1,0};s.bin_sum={};s.bin_cc=s.bin_ss=s.bin_cs=0;
    }
    void bin_sample(float sample) {
        auto& s=state;const auto c=s.bin_osc.real(),v=-s.bin_osc.imag();
        s.bin_sum+=static_cast<double>(sample)*s.bin_osc;
        s.bin_cc+=c*c;s.bin_ss+=v*v;s.bin_cs+=c*v;s.bin_osc*=s.bin_step;
        if(++s.bin_count==s.bin_size)finish_bin();
    }
    bool metadata_fit(double shift,std::array<Pair,vertices>& points,double& residual) const {
        const auto& s=state;
        const auto block=[&](Block bb,Block bd1,Block bd2,Block dd) {
            return matrix(add(add(add(bb,bd1,-shift),bd2,-shift),dd,shift*shift));
        };
        std::array<Matrix,vertices> diagonal{},inverse_diagonal{};
        std::array<Matrix,vertices-1> upper{};
        std::array<Pair,vertices> rhs{};
        for(std::size_t i=0;i<vertices;++i) {
            diagonal[i]=block(s.bb.diagonal[i],s.bd.diagonal[i],s.bd.diagonal[i],s.dd.diagonal[i]);
            rhs[i]={s.rhs[i][0]-shift*s.rhs_derivative[i][0],s.rhs[i][1]-shift*s.rhs_derivative[i][1]};
            if(i+1<vertices)upper[i]=block(s.bb.upper[i],s.bd.upper[i],s.bd.lower[i],s.dd.upper[i]);
        }
        points.front()={1,0};points.back()={1,0};
        rhs[1]=rhs[1]-upper[0]*points.front();rhs[vertices-2]=rhs[vertices-2]-upper[vertices-2]*points.back();
        for(std::size_t i=1;i+1<vertices;++i) {
            if(i>1) {
                const auto multiplier=upper[i-1]*inverse_diagonal[i-1];
                diagonal[i]=diagonal[i]-multiplier*upper[i-1];
                rhs[i]=rhs[i]-multiplier*rhs[i-1];
            }
            if(!inverse(diagonal[i],inverse_diagonal[i]))return false;
        }
        for(std::size_t i=vertices-2;i>0;--i) {
            auto value=rhs[i];if(i+1<vertices-1)value=value-upper[i]*points[i+1];
            points[i]=inverse_diagonal[i]*value;
            if(!std::isfinite(points[i][0])||!std::isfinite(points[i][1]))return false;
        }
        residual=s.metadata_energy;
        for(std::size_t i=0;i<vertices;++i) {
            const Pair b{s.rhs[i][0]-shift*s.rhs_derivative[i][0],s.rhs[i][1]-shift*s.rhs_derivative[i][1]};
            const auto a=block(s.bb.diagonal[i],s.bd.diagonal[i],s.bd.diagonal[i],s.dd.diagonal[i]);
            residual+=dot(points[i],a*points[i])-2*dot(points[i],b);
            if(i+1<vertices)residual+=2*dot(points[i],upper[i]*points[i+1]);
        }
        residual=std::max(0.,residual);return true;
    }
    void finish_metadata() {
        auto& s=state;s.metadata_active=false;
        if(s.metadata_samples<static_cast<std::size_t>(.079*s.rate/s.waveform_scale)||!s.reference)return;
        std::array<Pair,vertices> points{};double residual=0;
        double lo=-.0002,hi=.0002;
        for(unsigned i=0;i<24;++i) {
            const auto a=lo+(hi-lo)/3,b=hi-(hi-lo)/3;double ea=0,eb=0;
            if(!metadata_fit(a,points,ea)||!metadata_fit(b,points,eb))return;
            if(ea<eb)hi=b;else lo=a;
        }
        const auto shift=.5*(lo+hi);
        if(!metadata_fit(shift,points,residual)||s.metadata_energy<=0||residual/s.metadata_energy>1e-6)return;
        std::array<std::uint32_t,vertices-2> words{};
        std::array<bool,vertices-2> valid{};
        for(std::size_t i=0;i<words.size();++i) {
            const std::complex<double> p{points[i+1][0],points[i+1][1]};
            const auto radius=std::abs(p),phase=std::arg(p);
            const auto ring=std::lround((radius-.2)*127/.8);
            const auto sector=std::lround(phase*128/tau);
            if(ring<0||ring>127)continue;
            const auto word=(static_cast<std::uint32_t>(ring)<<7)|
                (static_cast<std::uint32_t>(sector)&127U);
            const auto ideal=apsk_point(word);
            if(std::abs(radius-std::abs(ideal))>.48*.8/127||
               std::abs(phase_wrap(phase-std::arg(ideal)))>.48*tau/128)continue;
            words[i]=word;valid[i]=true;
        }
        // The fixed record occupies all nine fitted symbols in this dwell.
        // Never assemble a record across a dwell boundary.
        const auto selected_key=std::span<const Crypto>(keys).first(std::min<std::size_t>(1,keys.size()));
        std::optional<Metadata> metadata;
        if(std::all_of(valid.begin(),valid.end(),[](bool value){return value;}))
            {
                std::optional<std::uint64_t> anchor;
                if(capture_epoch && std::abs(s.metadata_dwell-capture_anchor_sample_seconds)<=2.) {
                    const auto utc=*capture_epoch+s.metadata_dwell;
                    if(std::isfinite(utc)&&utc>=0&&utc<4294967296.)
                        anchor=static_cast<std::uint64_t>(std::llround(utc*65536.));
                }
                metadata=decode_metadata_packet(words,selected_key,anchor);
            }
        if(metadata) {
            // Metadata is unauthenticated and never grants timing authority.
            // The interpolation shift is only a local
            // nuisance fit; preserve the independent punctuation-derived grid.
            auto update=s.reference->publication(s.actual_punctuation);
            const auto frontier=s.next-1;
            const auto age=static_cast<double>(frontier-update.sample)/s.rate;
            update.frequency_sample=update.frequency_sample?update.frequency_sample:update.sample;
            update.phase_radians=phase_wrap(update.phase_radians+tau*update.frequency_hz*age);
            update.phase_uncertainty_radians+=tau*update.frequency_uncertainty_hz*age;
            // Retain the actual phase-fit observation date. Metadata samples
            // justify only this publication frontier, not a fresh phase fit.
            update.sample=frontier;update.metadata=std::move(metadata);
            attach_punctuation(update,s.actual_punctuation);
            update.timing_valid=s.timing_valid;
            update.dwell_start_seconds=s.metadata_dwell;
            update.dwell_uncertainty_seconds=s.timing_uncertainty;s.update=std::move(update);fuse_frequency(*s.update);
        }
    }
    void metadata_sample(float sample,std::uint64_t index) {
        auto& s=state;if(!s.timing_valid||!s.reference)return;
        const auto time=seconds(index);
        const auto period=.4/s.waveform_scale;
        const auto dwell=s.anchor+period*std::floor((time-s.anchor)/period),t=(time-dwell)*s.waveform_scale;
        if(s.metadata_active&&(dwell!=s.metadata_dwell||t>=.396))finish_metadata();
        if(t<.316||t>=.396)return;
        if(!s.metadata_active) {
            if(std::abs(s.metadata_last_dwell-dwell)<.1||t>.316+1.5*s.waveform_scale/s.rate)return;
            s.metadata_last_dwell=dwell;
            const auto& r=*s.reference;
            const auto age=time-seconds(r.sample);
            // Metadata is an optional local waveform decode, not a new phase,
            // frequency or UTC qualification. Its observed-coherence gate is
            // independent of the full process CI retained in every reference.
            if(age<0||age>.08||r.metadata_phase_uncertainty_radians>.0002||
               r.metadata_frequency_uncertainty_hz>.002)return;
            s.metadata_active=true;s.metadata_dwell=dwell;s.bb={};s.dd={};s.bd={};s.rhs={};s.rhs_derivative={};
            s.metadata_energy=0;s.metadata_samples=0;s.metadata_gain=s.amplitude;
            // Metadata returns to the applicable underlying auxiliary state,
            // which remains inverted for the single-reversal waveform.
            const auto sign=punctuation_format==PunctuationFormat::auxiliary_single?std::numbers::pi:0.;
            s.metadata_osc=std::polar(1.,r.phase_radians+tau*r.frequency_hz*age+sign);
            s.metadata_step=std::polar(1.,tau*r.frequency_hz/s.rate);
        }
        const auto position=(t-.316)/.008;
        const auto i=std::min<std::size_t>(vertices-2,static_cast<std::size_t>(std::floor(std::max(0.,position))));
        const auto u=position-i;
        const bool auxiliary=punctuation_format==PunctuationFormat::auxiliary_single;
        const auto h=auxiliary?auxiliary_smooth(u):smooth(u);
        const auto d=(auxiliary?auxiliary_smooth_derivative(u):smooth_derivative(u))*s.waveform_scale/.008;
        // The current auxiliary's8ms boundary gate overlaps the last4ms of
        // its metadata return. The nuisance shift moves the whole waveform,
        // so differentiate both gate and interpolation in physical seconds.
        const auto gate_position=(.4-t)/.008;
        const auto gate=auxiliary?smooth(gate_position):1.;
        const auto gate_derivative=auxiliary?
            -smooth_derivative(gate_position)*s.waveform_scale/.008:0.;
        const Pair v{s.metadata_gain*s.metadata_osc.real(),-s.metadata_gain*s.metadata_osc.imag()};
        const auto vv=outer(v,1);
        const std::array<double,2> b{gate*(1-h),gate*h},
            derivative{gate_derivative*(1-h)-gate*d,gate_derivative*h+gate*d};
        for(std::size_t j=0;j<2;++j) {
            const auto vertex=i+j;
            s.bb.diagonal[vertex]=add(s.bb.diagonal[vertex],vv,b[j]*b[j]);
            s.dd.diagonal[vertex]=add(s.dd.diagonal[vertex],vv,derivative[j]*derivative[j]);
            s.bd.diagonal[vertex]=add(s.bd.diagonal[vertex],vv,b[j]*derivative[j]);
            for(unsigned k=0;k<2;++k) {
                s.rhs[vertex][k]+=sample*b[j]*v[k];
                s.rhs_derivative[vertex][k]+=sample*derivative[j]*v[k];
            }
        }
        s.bb.upper[i]=add(s.bb.upper[i],vv,b[0]*b[1]);
        s.dd.upper[i]=add(s.dd.upper[i],vv,derivative[0]*derivative[1]);
        s.bd.upper[i]=add(s.bd.upper[i],vv,b[0]*derivative[1]);
        s.bd.lower[i]=add(s.bd.lower[i],vv,b[1]*derivative[0]);
        s.metadata_energy+=static_cast<double>(sample)*sample;++s.metadata_samples;s.metadata_osc*=s.metadata_step;
    }
};
Receiver::Receiver(double expected,double width,std::vector<Crypto> keys,
                   std::optional<OccupiedBand> ordinary_band,FrequencyPrecisionOptions precision,
                   PunctuationFormat punctuation) {
    if(!std::isfinite(expected)||expected<=0||!std::isfinite(width)||width<0||width>100||keys.size()>8)
        throw Error("invalid Helper Carrier precision band or receive-key count");
    if(!std::isfinite(precision.maximum_audio_clock_ppm)||precision.maximum_audio_clock_ppm<0||
       !std::isfinite(precision.phase_noise_radians_per_sqrt_second)||precision.phase_noise_radians_per_sqrt_second<0)
        throw Error("invalid Helper Carrier reference uncertainty model");
    // Do not inherit an unbounded caller vector capacity into the workspace.
    std::vector<Crypto> compact;
    if(!keys.empty())compact.push_back(keys.front());
    // Account for both the retained metadata and a replacement decoded before
    // it is moved into that slot. Match the codec's bounded append sequence,
    // rather than assuming a particular standard library string capacity.
    std::size_t metadata_heap=0;
    {
        std::string callsign;
        for(unsigned i=0;i<19;++i)callsign+='X';
        metadata_heap=2*(callsign.capacity()+1);
    }
    // Includes the finite-aperture process-kernel scratch. Focused compiler
    // stack-usage validation checks this bound for the qualified toolchain.
    constexpr std::size_t scratch_bound=3072;
    if(sizeof(*this)+sizeof(Impl)+compact.capacity()*sizeof(Crypto)+scratch_bound+metadata_heap>workspace_bound)
        throw Error("Helper Carrier precision receiver exceeds its bounded workspace");
    impl_=std::make_unique<Impl>(expected,width,std::move(compact),ordinary_band,precision,punctuation);
}
Receiver::~Receiver()=default;
Receiver::Receiver(Receiver&&) noexcept=default;
Receiver& Receiver::operator=(Receiver&&) noexcept=default;
void Receiver::reset() {
    impl_->reset_state();impl_->bump_optional_generation();
    if(impl_->optional_frequency)impl_->optional_frequency->reset_ingest(0);
}
void Receiver::push(std::span<const float> samples,std::uint32_t rate,std::uint64_t first) {
    auto& s=impl_->state;
    if(!rate)throw Error("invalid Helper Carrier precision sample rate");
    if(s.rate!=rate||(s.rate&&s.next!=first))impl_->configure(rate,first);
    if(!punctuation_numeric::environment()) {
        // A bin can outlive this call. Do not later publish a marker whose
        // evidence was accumulated in an unsupported numeric environment.
        // Only explicit reset/reconfiguration rebuilds clean provider history;
        // ordinary frequency measurements remain on their existing path.
        s.punctuation_arithmetic_qualified=false;s.actual_punctuation={};
        if(s.update)s.update->punctuation={};
    }
    if(impl_->optional_frequency)impl_->optional_frequency->push(samples,rate,first);
    for(const auto sample:samples) {
        if(!std::isfinite(sample)||s.next==std::numeric_limits<std::uint64_t>::max()) {reset();return;}
        const auto index=s.next++;
        if(s.timing_valid&&impl_->seconds(index)-s.punctuation>.85) {
            s.timing_valid=false;s.candidate.reset();
        }
        impl_->bin_sample(sample);impl_->fit_sample(sample,index);impl_->metadata_sample(sample,index);
        if(!s.bin_count)impl_->recenter_basis();
    }
}
std::optional<Reference> Receiver::take_update() {auto out=std::move(impl_->state.update);impl_->state.update.reset();return out;}
std::size_t Receiver::optional_frequency_workspace_required() noexcept {
    return sizeof(FrequencyProfile)+optional_frequency_worker_scratch_bound;
}
FrequencyWorkGeometry Receiver::optional_frequency_geometry(double expected,double constructor_width,
    const FrequencyWorkOptions& options,FrequencyPrecisionOptions precision) noexcept {
    FrequencyWorkGeometry result;
    result.heap_bytes=sizeof(FrequencyProfile);result.worker_scratch_bytes=optional_frequency_worker_scratch_bound;
    result.required_bytes=result.heap_bytes+result.worker_scratch_bytes;
    const auto width=options.physical_prior_half_width_hz.value_or(constructor_width);
    if(!std::isfinite(constructor_width)||constructor_width<0||!std::isfinite(width)||width<0||width>constructor_width)
        return result;
    auto layout=FrequencyProfile::geometry(expected,width,options.sample_rate,
        precision.phase_noise_radians_per_sqrt_second,precision.maximum_audio_clock_ppm);
    layout.heap_bytes=result.heap_bytes;layout.worker_scratch_bytes=result.worker_scratch_bytes;
    layout.required_bytes=result.required_bytes;
    if(!layout.supported)return layout;
    if(!options.coherent_phase_model)layout.decline=FrequencyWorkDecline::coherence_unavailable;
    else if(options.workspace_grant<layout.required_bytes)layout.decline=FrequencyWorkDecline::grant_unavailable;
    else layout.eligible=true;
    return layout;
}
bool Receiver::set_optional_frequency_work(const FrequencyWorkOptions& options) {
    release_optional_frequency_work();
    if(impl_->optional_generation==UINT64_MAX) {
        impl_->optional_decline=FrequencyWorkDecline::generation_changed;return false;
    }
    if(options.workspace_grant<optional_frequency_workspace_required()) {
        impl_->optional_decline=FrequencyWorkDecline::grant_unavailable;return false;
    }
    if(!options.coherent_phase_model) {
        impl_->optional_decline=FrequencyWorkDecline::coherence_unavailable;return false;
    }
    const auto width=options.physical_prior_half_width_hz.value_or(impl_->width);
    if(!std::isfinite(width)||width<0||width>impl_->width||!options.sample_rate||
       impl_->precision.maximum_audio_clock_ppm>1000) {
        impl_->optional_decline=FrequencyWorkDecline::unsupported_geometry;return false;
    }
    try {
        auto profile=std::make_unique<FrequencyProfile>(impl_->expected,width,options.sample_rate,
            impl_->precision.phase_noise_radians_per_sqrt_second,impl_->precision.maximum_audio_clock_ppm);
        profile->set_ordinal(impl_->optional_ordinal);
        if(impl_->state.rate==options.sample_rate)profile->reset_ingest(impl_->state.next);
        impl_->optional_frequency=std::move(profile);
        impl_->optional_decline=FrequencyWorkDecline::warming_aperture;return true;
    } catch(const Error&) {
        impl_->optional_decline=FrequencyWorkDecline::unsupported_geometry;return false;
    } catch(const std::bad_alloc&) {
        impl_->optional_decline=FrequencyWorkDecline::grant_unavailable;return false;
    }
}
void Receiver::release_optional_frequency_work() {
    if(impl_->optional_frequency)impl_->optional_ordinal=impl_->optional_frequency->ordinal();
    impl_->optional_frequency.reset();impl_->bump_optional_generation();
    impl_->optional_decline=FrequencyWorkDecline::grant_unavailable;
}
void Receiver::clear_optional_frequency_history() {
    impl_->bump_optional_generation();
    if(impl_->optional_frequency) {
        impl_->optional_frequency->cancel();impl_->optional_frequency->reset_ingest(impl_->state.next);
    }
}
FrequencyWorkDescriptor Receiver::optional_frequency_work() const {
    if(impl_->optional_frequency)return impl_->optional_frequency->descriptor(impl_->optional_generation);
    FrequencyWorkDescriptor d;d.required_bytes=optional_frequency_workspace_required();
    d.generation=impl_->optional_generation;d.look_ordinal=impl_->optional_ordinal;
    d.decline=impl_->optional_decline;return d;
}
bool Receiver::prepare_optional_frequency(std::uint64_t revision) {
    return impl_->optional_frequency&&impl_->optional_frequency->prepare(revision,impl_->optional_generation);
}
FrequencyWorkAdvance Receiver::advance_optional_frequency(std::size_t maximum) {
    return impl_->optional_frequency?impl_->optional_frequency->advance(maximum):FrequencyWorkAdvance{};
}
bool Receiver::commit_optional_frequency(const FrequencyPresenceCertificate& presence) {
    if(!impl_->optional_frequency||!impl_->optional_frequency->commit(presence,impl_->optional_generation))return false;
    const auto& selected=*impl_->optional_frequency->qualified();
    auto& update=impl_->state.update;
    if(update&&update->sample>=selected.sample)impl_->fuse_frequency(*update);
    else if(!update)update=selected;
    return true;
}

struct AuxiliaryMetadataReceiver::Impl {
    std::uint32_t rate;
    double expected_auxiliary,spacing,expected_low;
    Receiver discovery,decoder;
    std::optional<PayloadFilter> notch;
    // Capture ownership is disjoint from the joined optional worker. Only
    // prepare copies capture PCM/dates into the prepaid immutable work ring.
    std::vector<float> capture,raw;
    std::uint64_t capture_first,capture_next,generation=1;
    std::optional<Reference> committed;
    PunctuationObservation committed_punctuation;
    std::array<float,4096> filtered{},corrected{};
    std::uint64_t prepared_first=0,prepared_end=0,prepared_generation=0,worker_generation=0;
    std::uint64_t first_raw,next_raw,filtered_next,last_dwell_end=0;
    double last_nominal_dwell=-1,last_boundary_attempt=-1;
    std::uint64_t boundary_support_first=0,boundary_support_last=0;
    std::optional<Reference> grid,update;
    PunctuationObservation pending_punctuation;
    std::uint64_t punctuation_generation=0,punctuation_sample=0;
    Impl(std::uint32_t r,double auxiliary,double pilot,std::vector<Crypto> keys,
         FrequencyPrecisionOptions precision,std::uint64_t first,std::optional<double> first_utc,double low)
        :rate(r),expected_auxiliary(auxiliary),spacing(pilot-auxiliary),expected_low(low),
         discovery(auxiliary,20,{},std::nullopt,precision,PunctuationFormat::auxiliary_single),
         decoder(auxiliary,20,std::move(keys),std::nullopt,precision,PunctuationFormat::auxiliary_single),
         notch(),capture(static_cast<std::size_t>(r)*84/100),
         raw(capture.size()),capture_first(first),capture_next(first),
         first_raw(first),next_raw(first),filtered_next(first) {
        make_notch();filtered_next+=notch->delay();
        if(first_utc) {
            decoder.impl_->capture_anchor_sample_seconds=static_cast<double>(first)/rate;
            decoder.impl_->capture_epoch=*first_utc-decoder.impl_->capture_anchor_sample_seconds;
        }
    }
    float sample(std::uint64_t at) const {return raw[at%raw.size()];}
    template<std::size_t N> static bool solve(std::array<std::array<double,N>,N> a,
            std::array<double,N> b,std::array<double,N>& x) {
        for(std::size_t i=0;i<N;++i) {
            auto pivot=i;for(auto j=i+1;j<N;++j)if(std::abs(a[j][i])>std::abs(a[pivot][i]))pivot=j;
            if(!(std::abs(a[pivot][i])>1e-12))return false;
            std::swap(a[i],a[pivot]);std::swap(b[i],b[pivot]);
            for(auto j=i+1;j<N;++j) {
                const auto q=a[j][i]/a[i][i];
                for(auto k=i+1;k<N;++k)a[j][k]-=q*a[i][k];
                b[j]-=q*b[i];
            }
        }
        for(auto i=N;i-->0;) {
            auto value=b[i];for(auto j=i+1;j<N;++j)value-=a[i][j]*x[j];
            x[i]=value/a[i][i];if(!std::isfinite(x[i]))return false;
        }
        return true;
    }
    void make_notch() {
        const std::array<PayloadFilter::NotchBand,2> bands{{{expected_auxiliary+spacing,100},{expected_low,100}}};
        notch.emplace(rate,std::span<const PayloadFilter::NotchBand>(bands).first(expected_low>0?2:1));
    }
    struct Pilots {
        double auxiliary=0,high=0,low=0;
        std::complex<double> high_amplitude{},low_amplitude{};
    };
    // Revisit only a straight flank in the current dwell. Each tone has an
    // independently fitted phasor and local frequency slope. No phase history
    // or decoded metadata is needed to remove the pure pilots.
    template<std::size_t N> bool fit_tones(std::uint64_t first,std::uint64_t end,Pilots& result) const {
        std::array<double,N> frequency{};
        frequency[0]=result.auxiliary;frequency[1]=result.high;
        if constexpr(N==3)frequency[2]=result.low;
        const auto duration=static_cast<double>(end-first)/rate;
        for(unsigned iteration=0;iteration<3;++iteration) {
            std::array<std::array<double,4*N>,4*N> gram{};
            std::array<double,4*N> rhs{},x{};
            std::array<std::complex<double>,N> phase{},step{};phase.fill({1,0});
            for(std::size_t tone=0;tone<N;++tone)step[tone]=std::polar(1.,tau*frequency[tone]/rate);
            for(auto at=first;at<end;++at) {
                const auto u=static_cast<double>(at-first)/(end-first)-.5;
                std::array<double,4*N> basis{};
                for(std::size_t tone=0;tone<N;++tone) {
                    basis[4*tone]=phase[tone].real();basis[4*tone+1]=phase[tone].imag();
                    basis[4*tone+2]=u*phase[tone].real();basis[4*tone+3]=u*phase[tone].imag();
                    phase[tone]*=step[tone];
                }
                const auto value=sample(at);
                for(std::size_t i=0;i<4*N;++i) {
                    rhs[i]+=value*basis[i];
                    for(std::size_t j=0;j<4*N;++j)gram[i][j]+=basis[i]*basis[j];
                }
            }
            if(!solve(gram,rhs,x))return false;
            for(std::size_t tone=0;tone<N;++tone) {
                const std::complex<double> amplitude{x[4*tone],-x[4*tone+1]},slope{x[4*tone+2],-x[4*tone+3]};
                if(std::norm(amplitude)<1e-12)return false;
                const auto correction=std::imag(slope*std::conj(amplitude))/(std::norm(amplitude)*tau*duration);
                if(!std::isfinite(correction)||std::abs(correction)>2)return false;
                frequency[tone]+=correction;
            }
        }
        std::array<std::array<double,2*N>,2*N> gram{};
        std::array<double,2*N> rhs{},x{};
        std::array<std::complex<double>,N> phase{},step{};phase.fill({1,0});
        for(std::size_t tone=0;tone<N;++tone)step[tone]=std::polar(1.,tau*frequency[tone]/rate);
        double energy=0;
        for(auto at=first;at<end;++at) {
            std::array<double,2*N> basis{};
            for(std::size_t tone=0;tone<N;++tone) {
                basis[2*tone]=phase[tone].real();basis[2*tone+1]=phase[tone].imag();phase[tone]*=step[tone];
            }
            const auto value=sample(at);energy+=value*value;
            for(std::size_t i=0;i<2*N;++i) {
                rhs[i]+=value*basis[i];for(std::size_t j=0;j<2*N;++j)gram[i][j]+=basis[i]*basis[j];
            }
        }
        if(!solve(gram,rhs,x)||!(energy>0))return false;
        double explained=0;for(std::size_t i=0;i<2*N;++i)explained+=x[i]*rhs[i];
        if(explained<.9*energy||std::abs(frequency[0]-expected_auxiliary)>20)return false;
        result.auxiliary=frequency[0];result.high=frequency[1];result.high_amplitude={x[2],-x[3]};
        if constexpr(N==3) {result.low=frequency[2];result.low_amplitude={x[4],-x[5]};}
        return true;
    }
    bool fit(std::uint64_t first,std::uint64_t end,Pilots& result) const {
        return expected_low>0?fit_tones<3>(first,end,result):fit_tones<2>(first,end,result);
    }
    double pure_pilots(const Pilots& pilots,std::uint64_t fit_first,std::uint64_t at) const {
        const auto seconds=static_cast<double>((static_cast<long double>(at)-fit_first)/rate);
        return std::real(pilots.high_amplitude*std::polar(1.,tau*pilots.high*seconds)+
            pilots.low_amplitude*std::polar(1.,tau*pilots.low*seconds));
    }
    void raw_boundary() {
        auto& state=discovery.impl_->state;
        if(state.punctuation<0)return;
        const auto center=state.punctuation+.16;
        if(std::abs(state.boundary_notch-center)<1./rate&&state.boundary_level<=.1)return;
        if(std::abs(last_boundary_attempt-center)<.1)return;
        const auto position=static_cast<long double>(center)*rate;
        const auto begin=std::ceil(position-.0005L*rate),end=std::floor(position);
        const auto plateau_begin=std::ceil(position-.10L*rate),plateau_end=std::ceil(position-.096L*rate);
        const auto fit_first=std::ceil(position-.36L*rate),fit_end=std::ceil(position-.20L*rate);
        const auto retained=std::max(first_raw,next_raw>raw.size()?next_raw-raw.size():0);
        if(!std::isfinite(position)||fit_first<retained||begin<retained||end>next_raw||
           end-begin<8||plateau_end-plateau_begin<8||fit_end<=fit_first)return;
        last_boundary_attempt=center; // At most one complete bounded fit per observed dwell.
        Pilots pilots{state.guess_hz,state.guess_hz+spacing,
            expected_low>0?expected_low+(state.guess_hz-expected_auxiliary):0};
        if(!fit(static_cast<std::uint64_t>(fit_first),static_cast<std::uint64_t>(fit_end),pilots))return;
        // Pure pilots can remain continuous at the auxiliary gate. Remove
        // their independently fitted current-dwell phasors before witnessing
        // its zero. Test the two permitted pilot envelopes; both hypotheses
        // retain the same reversal/cadence and strict auxiliary RMS guard.
        double best=1;
        for(const bool gated:{false,true}) {
            double energy=0,plateau=0;
            const auto residual=[&](std::uint64_t at) {
                const auto envelope=gated?smooth((center-static_cast<double>(at)/rate)/.004):1.;
                return static_cast<double>(sample(at))-envelope*pure_pilots(pilots,static_cast<std::uint64_t>(fit_first),at);
            };
            for(auto at=static_cast<std::uint64_t>(begin);at<static_cast<std::uint64_t>(end);++at) {
                const auto value=residual(at);energy+=value*value;
            }
            for(auto at=static_cast<std::uint64_t>(plateau_begin);at<static_cast<std::uint64_t>(plateau_end);++at) {
                const auto value=residual(at);plateau+=value*value;
            }
            const auto ratio=plateau>0?energy/(end-begin)/(plateau/(plateau_end-plateau_begin)):1.L;
            best=std::min(best,static_cast<double>(ratio));
        }
        if(best<=.01) {
            state.boundary_notch=center;state.boundary_level=std::sqrt(best);
            boundary_support_first=static_cast<std::uint64_t>(fit_first);
            boundary_support_last=static_cast<std::uint64_t>(end)-1;
        }
    }
    void restart_worker() {
        // Retire old filter storage before rebuilding; the prepaid grant need
        // never hold two filters. This allocation is optional worker work.
        if(worker_generation||!notch) {notch.reset();make_notch();}
        discovery.reset();decoder.reset();grid.reset();update.reset();
        pending_punctuation={};punctuation_generation=punctuation_sample=0;
        first_raw=next_raw=prepared_first;filtered_next=first_raw+notch->delay();
        last_dwell_end=0;last_nominal_dwell=last_boundary_attempt=-1;
        boundary_support_first=boundary_support_last=0;worker_generation=prepared_generation;
    }
    void observe_punctuation() {
        auto observed=discovery.impl_->state.actual_punctuation;
        if(!observed.known||!observed.generation||
           (observed.generation==punctuation_generation&&observed.sample==punctuation_sample))return;
        // Filter output dates are already raw-center coordinates. Preserve
        // the fitted edge date/CI, but include EVERY raw FIR dependency in
        // its immutable support and wait until that support actually exists.
        const auto delay=notch->delay();
        if(observed.first_sample<delay||observed.last_sample>UINT64_MAX-delay)return;
        observed.first_sample-=delay;observed.last_sample+=delay;
        if(boundary_support_last) {
            observed.first_sample=std::min(observed.first_sample,boundary_support_first);
            observed.last_sample=std::max(observed.last_sample,boundary_support_last);
        }
        if(observed.first_sample<first_raw||observed.last_sample>=next_raw)return;
        pending_punctuation=observed;punctuation_generation=observed.generation;punctuation_sample=observed.sample;
    }
    bool advance(std::size_t credit) {
        if(!prepared_generation||!credit)return false;
        credit=std::min(credit,filtered.size());
        if(worker_generation!=prepared_generation||next_raw<prepared_first||next_raw>prepared_end)
            restart_worker();
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(credit,prepared_end-next_raw));
        for(std::size_t i=0;i<count;++i)corrected[i]=sample(next_raw+i);
        next_raw+=count;first_raw=prepared_first;
        if(count) {
            raw_boundary();
            const auto produced=notch->push(std::span<const float>(corrected).first(count),filtered);
            if(produced)discovery.push(std::span<const float>(filtered).first(produced),rate,filtered_next);
            filtered_next+=produced;
            observe_punctuation();
            if(auto current=discovery.take_update();current&&current->timing_valid&&current->frequency_valid)
                grid=std::move(current);
        }
        return decode_latest()||count;
    }
    bool decode_latest() {
        if(!grid||!grid->timing_valid||!grid->frequency_valid||!next_raw||
           next_raw-1<grid->sample||next_raw-1-grid->sample>rate)return false;
        const auto frontier=static_cast<double>(next_raw)/rate;
        auto dwell=grid->dwell_start_seconds+.4*std::floor((frontier-grid->dwell_start_seconds)/.4);
        if(dwell+.4>frontier)dwell-=.4;
        if(dwell<0)return false;
        // The fitted clock moves a physical end by a few samples. Identify
        // attempts on the nominal waveform calendar instead, so updated grid
        // or spacing estimates cannot replay the same completed dwell.
        const auto nominal_dwell=dwell;
        if(last_nominal_dwell>=0&&std::llround((nominal_dwell-last_nominal_dwell)/.4)<=0)return false;
        auto first=static_cast<std::uint64_t>(std::ceil(dwell*rate));
        auto end=static_cast<std::uint64_t>(std::ceil((dwell+.4)*rate));
        const auto retained=std::max(first_raw,next_raw>raw.size()?next_raw-raw.size():0);
        if(end>next_raw||end<=last_dwell_end||first<retained)return false;
        const auto fit_first=static_cast<std::uint64_t>(std::ceil((dwell+.04)*rate));
        const auto fit_end=static_cast<std::uint64_t>(std::ceil((dwell+.20)*rate));
        Pilots pilots{grid->frequency_hz,grid->frequency_hz+spacing,
            expected_low>0?expected_low+(grid->frequency_hz-expected_auxiliary):0};
        if(fit_end<=fit_first||!fit(fit_first,fit_end,pilots)) {
            last_dwell_end=end;last_nominal_dwell=nominal_dwell;return true;
        }
        // RF offset is common to both tones. Their measured spacing supplies
        // the local waveform clock without decoding metadata or assigning
        // that clock any receiver-assistance authority. A scalar timing shift
        // cannot fit clock stretch across the eight metadata transitions.
        const auto auxiliary=pilots.auxiliary,pilot=pilots.high;
        const auto scale=(pilot-auxiliary)/spacing;
        if(!std::isfinite(scale)||std::abs(scale-1)>.001) {
            last_dwell_end=end;last_nominal_dwell=nominal_dwell;return true;
        }
        const auto period=.4/scale;
        const auto anchor=grid->dwell_start_seconds+.24-.24/scale;
        dwell=anchor+period*std::floor((frontier-anchor)/period);
        if(dwell+period>frontier)dwell-=period;
        if(dwell<0)return true;
        first=static_cast<std::uint64_t>(std::ceil(dwell*rate));
        end=static_cast<std::uint64_t>(std::ceil((dwell+period)*rate));
        if(end>next_raw||end<=last_dwell_end||first<retained||fit_first<first||fit_end>end)return true;
        last_dwell_end=end;last_nominal_dwell=nominal_dwell;
        decoder.reset();decoder.push({},rate,first);
        auto& state=decoder.impl_->state;
        // Private metadata decoding may use the independently observed grid.
        // None of these conditioned tone/phase fits is exposed as assistance.
        state.metadata_timing_hint=true;state.timing_valid=true;state.anchor=dwell;
        state.waveform_scale=scale;
        state.timing_uncertainty=grid->dwell_uncertainty_seconds;state.punctuation=dwell;
        state.coarse_valid=true;state.coarse_hz=state.guess_hz=auxiliary;
        for(auto at=first;at<end;) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(corrected.size(),end-at));
            auto oscillator=std::polar(1.,tau*pilot*static_cast<double>(
                (static_cast<long double>(at)-fit_first)/rate));
            const auto step=std::polar(1.,tau*pilot/rate);
            auto low_oscillator=std::polar(1.,tau*pilots.low*static_cast<double>(
                (static_cast<long double>(at)-fit_first)/rate));
            const auto low_step=std::polar(1.,tau*pilots.low/rate);
            for(std::size_t i=0;i<count;++i) {
                const auto t=(static_cast<double>(at+i)/rate-dwell)*scale;
                const auto envelope=smooth(std::min(t,.4-t)/.004);
                corrected[i]=static_cast<float>(sample(at+i)-envelope*std::real(
                    pilots.high_amplitude*oscillator+pilots.low_amplitude*low_oscillator));
                oscillator*=step;low_oscillator*=low_step;
            }
            decoder.push(std::span<const float>(corrected).first(count),rate,at);at+=count;
            if(auto decoded=decoder.take_update();decoded&&decoded->metadata) {
                decoded->sample=next_raw-1;decoded->frequency_hz=auxiliary;
                decoded->frequency_valid=decoded->phase_valid=decoded->timing_valid=false;
                decoded->phase_observation_known=false;decoded->phase_observation_sample=0;
                decoded->punctuation={};update=std::move(decoded);
            }
        }
        return true;
    }
};
std::size_t AuxiliaryMetadataReceiver::workspace_required(std::uint32_t rate) {
    if(rate<1000||rate>96000)throw Error("optional Helper metadata requires1..96kHz PCM");
    const auto taps=static_cast<std::size_t>(std::ceil(.04*rate))|1U;
    // Includes the complete-dwell three-tone Gram fit and its callers. GCC
    // -O3 stack-usage accounting measures 4176 bytes on the deepest owned
    // chain; external runtime/provider stacks retain their existing boundary.
    constexpr std::size_t worker_scratch=5120;
    return sizeof(Impl)+2*(static_cast<std::size_t>(rate)*84/100)*sizeof(float)+
        taps*(sizeof(double)+sizeof(float))+2*Receiver::workspace_bound+worker_scratch;
}
AuxiliaryMetadataReceiver::AuxiliaryMetadataReceiver(std::uint32_t rate,double auxiliary,double pilot,
        std::vector<Crypto> keys,FrequencyPrecisionOptions precision,std::size_t grant,std::uint64_t first,
        std::optional<double> first_utc,double low) {
    const auto required=workspace_required(rate);
    if(grant<required)throw Error("optional Helper metadata workspace was not prepaid");
    if((first_utc&&!std::isfinite(*first_utc))||!std::isfinite(auxiliary)||!std::isfinite(pilot)||pilot<=auxiliary||
       !std::isfinite(low)||low<0||(low>0&&(low<=100||low+200>=auxiliary))||
       first>UINT64_MAX-static_cast<std::size_t>(std::ceil(.04*rate)))
        throw Error("invalid optional Helper metadata geometry");
    impl_=std::make_unique<Impl>(rate,auxiliary,pilot,std::move(keys),precision,first,first_utc,low);
    if(sizeof(*this)+sizeof(Impl)+(impl_->capture.capacity()+impl_->raw.capacity())*sizeof(float)+impl_->notch->allocated_bytes()+
       2*Receiver::workspace_bound>grant)throw Error("optional Helper metadata exceeds its workspace grant");
}
AuxiliaryMetadataReceiver::~AuxiliaryMetadataReceiver()=default;
AuxiliaryMetadataReceiver::AuxiliaryMetadataReceiver(AuxiliaryMetadataReceiver&&) noexcept=default;
AuxiliaryMetadataReceiver& AuxiliaryMetadataReceiver::operator=(AuxiliaryMetadataReceiver&&) noexcept=default;
void AuxiliaryMetadataReceiver::set_capture_utc(std::uint64_t sample,std::optional<double> utc) {
    if(utc&&!std::isfinite(*utc))throw Error("invalid auxiliary capture UTC");
    impl_->decoder.impl_->capture_anchor_sample_seconds=static_cast<double>(sample)/impl_->rate;
    impl_->decoder.impl_->capture_epoch=utc?std::optional<double>{*utc-impl_->decoder.impl_->capture_anchor_sample_seconds}:std::nullopt;
}
void AuxiliaryMetadataReceiver::push(std::span<const float> samples,std::uint64_t first) {
    auto& s=*impl_;
    if(first!=s.capture_next||samples.size()>UINT64_MAX-first)
        throw Error("optional Helper metadata capture is discontinuous");
    for(const auto value:samples) {
        if(!std::isfinite(value))throw Error("optional Helper metadata contains a nonfinite sample");
        s.capture[s.capture_next%s.capture.size()]=value;++s.capture_next;
    }
}
void AuxiliaryMetadataReceiver::reset(std::uint64_t first) {
    auto& s=*impl_;
    if(s.generation==UINT64_MAX||first>UINT64_MAX-static_cast<std::size_t>(std::ceil(.04*s.rate)))
        throw Error("optional Helper metadata reset coordinate exhausted");
    ++s.generation;s.capture_first=s.capture_next=first;s.committed.reset();s.committed_punctuation={};
}
bool AuxiliaryMetadataReceiver::prepare() {
    auto& s=*impl_;
    const auto first=std::max(s.capture_first,s.capture_next>s.capture.size()?s.capture_next-s.capture.size():0);
    if(first==s.capture_next||
       (s.prepared_generation==s.generation&&s.prepared_first==first&&s.prepared_end==s.capture_next))return false;
    for(auto at=first;at<s.capture_next;++at)s.raw[at%s.raw.size()]=s.capture[at%s.capture.size()];
    s.prepared_first=first;s.prepared_end=s.capture_next;s.prepared_generation=s.generation;
    s.update.reset();s.pending_punctuation={};return true;
}
std::size_t AuxiliaryMetadataReceiver::pending_samples() const {
    const auto& s=*impl_;
    if(!s.prepared_generation)return 0;
    const auto first=s.worker_generation!=s.prepared_generation||s.next_raw<s.prepared_first||s.next_raw>s.prepared_end?
        s.prepared_first:s.next_raw;
    return static_cast<std::size_t>(s.prepared_end-first);
}
bool AuxiliaryMetadataReceiver::advance(std::size_t credit) {return impl_->advance(credit);}
bool AuxiliaryMetadataReceiver::commit() {
    auto& s=*impl_;
    auto update=std::move(s.update);s.update.reset();
    const auto punctuation=s.pending_punctuation;s.pending_punctuation={};
    if(s.prepared_generation!=s.generation)return false;
    bool accepted=false;
    if(update&&update->sample>=s.capture_first&&update->sample<s.capture_next) {
        s.committed=std::move(update);accepted=true;
    }
    if(punctuation.known&&punctuation.first_sample>=s.capture_first&&punctuation.last_sample<s.capture_next) {
        s.committed_punctuation=punctuation;accepted=true;
    }
    return accepted;
}
bool AuxiliaryMetadataReceiver::advance() {
    prepare();const auto worked=advance(4096);commit();return worked;
}
PunctuationObservation AuxiliaryMetadataReceiver::take_punctuation_update() {
    const auto out=impl_->committed_punctuation;impl_->committed_punctuation={};return out;
}
std::optional<Reference> AuxiliaryMetadataReceiver::take_update() {
    auto out=std::move(impl_->committed);impl_->committed.reset();return out;
}

}

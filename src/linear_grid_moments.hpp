#pragma once
#include "scheduled_linear_grid_fft.hpp"
#include "pattern_correlator_batch.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
namespace datapump::modem::detail::scheduled_linear_grid {
namespace fft=scheduled_linear_grid_fft;
using Wide=fft::Wide;using Pair=std::array<Wide,2>;
// Three quadratic weights remain distinct; no interpolated samples are treated
// as independent noise. Plane order: aY,bY,a²K,2abK,b²K,a²T,2abT,b²T.
class Moments {
    std::size_t planes_,current_=0;bool have_=false,finished_=false,seen_=false;
    std::uint64_t last_physical_=0,current_count_=0;
    std::array<Wide,8> sum_{},correction_{};std::array<double,8> mass_{};long double current_energy_=0;
    scheduled_acquisition_certificate::Interval bounded_energy_;
    std::vector<fft::InputRow> rows_;
    std::vector<long double> energy_;
    std::vector<fft::InputRadius> radius_;
    std::vector<scheduled_acquisition_certificate::Interval> bounded_energy_rows_;
    std::vector<std::uint64_t> count_;
    static void add_scalar(double x,double& s,double& c){const auto n=s+x;c+=(std::abs(s)>=std::abs(x)?(s-n)+x:(x-n)+s);s=n;}
    void add_plane(std::size_t p,Wide z){mass_[p]=scheduled_acquisition_certificate::add(mass_[p],scheduled_acquisition_certificate::norm(z));auto re=sum_[p].real(),im=sum_[p].imag(),cr=correction_[p].real(),ci=correction_[p].imag();
        add_scalar(z.real(),re,cr);add_scalar(z.imag(),im,ci);sum_[p]={re,im};correction_[p]={cr,ci};}
    void flush(){if(!have_)return;for(std::size_t p=0;p<planes_;++p){rows_[current_][p]=sum_[p]+correction_[p];
            fft::require(std::isfinite(rows_[current_][p].real())&&std::isfinite(rows_[current_][p].imag()),"linear grid moment overflow");}
        energy_[current_]=current_energy_;bounded_energy_rows_[current_]=bounded_energy_;count_[current_]=current_count_;
        namespace cert=scheduled_acquisition_certificate;
        const auto n=cert::add(cert::mul(128,static_cast<double>(current_count_)),256);
        const auto g=n<SIZE_MAX?cert::gamma(static_cast<std::size_t>(n),std::numeric_limits<double>::epsilon()/2):cert::infinity;
        for(std::size_t p=0;p<planes_;++p)radius_[current_][p]=cert::add(cert::mul(g,mass_[p]),cert::mul(n,std::numeric_limits<double>::denorm_min()));
        sum_={};correction_={};mass_={};bounded_energy_={};current_energy_=0;current_count_=0;}
public:
    Moments(std::size_t cells,bool gram,std::size_t budget):planes_(gram?8:2) {
        const auto needed=fft::add(sizeof(*this)+4096,fft::mul(cells,sizeof(fft::InputRow)+sizeof(fft::InputRadius)+sizeof(scheduled_acquisition_certificate::Interval)+sizeof(long double)+sizeof(std::uint64_t)));
        fft::require(cells&&needed<=budget,"linear grid moments exceed workspace");rows_.resize(cells);radius_.resize(cells);bounded_energy_rows_.resize(cells);energy_.resize(cells);count_.resize(cells);
        fft::require(working_bytes()<=budget,"linear grid actual moments exceed workspace");}
    ~Moments(){if(!rows_.empty())OPENSSL_cleanse(rows_.data(),rows_.size()*sizeof(rows_[0]));}
    std::size_t working_bytes()const{return sizeof(*this)+640+rows_.capacity()*sizeof(rows_[0])+radius_.capacity()*sizeof(radius_[0])+bounded_energy_rows_.capacity()*sizeof(bounded_energy_rows_[0])+energy_.capacity()*sizeof(long double)+count_.capacity()*sizeof(std::uint64_t);}
    // u is RELATIVE node coordinate, not a floating-point absolute UTC value.
    // The caller proves its source/physical map, coordinate uncertainty and
    // origin-grid identity. Nonobserved samples still advance continuity.
    void add(std::uint64_t physical,long double u,const CorrelationProjection& p,double gain,bool observed=true) {
        fft::require(!finished_&&std::isfinite(u)&&u>=0&&u<rows_.size(),"linear grid coordinate outside borrowed extent");
        fft::require(!seen_||physical>last_physical_,"linear grid repeated/reversed physical sample");seen_=true;last_physical_=physical;
        const auto cell=static_cast<std::size_t>(std::floor(u));const auto t=static_cast<double>(u-cell);
        fft::require(t>=0&&t<1&&std::isfinite(gain)&&gain>=0&&gain<=1,"linear grid weight/gain invalid");
        if(have_)fft::require(cell>=current_,"linear grid source map rewound");
        if(!have_||cell!=current_){flush();current_=cell;have_=true;}
        if(!observed||gain<=0)return;
        fft::require(std::isfinite(p.xc)&&std::isfinite(p.xs)&&std::isfinite(p.cc)&&std::isfinite(p.ss)&&std::isfinite(p.cs)&&std::isfinite(p.energy)&&p.energy>=0,
            "linear grid original projection invalid");
        // The producer is an original real PCM sample projected on a bounded
        // oscillator. This domain also bounds amplification of underflowed
        // intermediate gain/weights in the preparation certificate.
        fft::require(p.cc>=0&&p.ss>=0&&p.cc<=4&&p.ss<=4&&std::abs(p.cs)<=4&&
            std::abs(p.xc)<=0x1p128&&std::abs(p.xs)<=0x1p128&&p.energy<=0x1p256,
            "linear grid projection outside physical producer bounds");
        const auto a=1-t,b=t,g2=gain*gain;const Wide y{p.xc*gain,p.xs*gain};
        add_plane(0,a*y);add_plane(1,b*y);
        if(planes_==8){const Wide k{p.cc-p.ss,2*p.cs},trace{p.cc+p.ss,0};
            const std::array<double,3>w{a*a,2*a*b,b*b};for(unsigned j=0;j<3;++j){add_plane(2+j,w[j]*g2*k);add_plane(5+j,w[j]*g2*trace);}}
        current_energy_+=p.energy;bounded_energy_=scheduled_acquisition_certificate::plus(bounded_energy_,scheduled_acquisition_certificate::Interval::point(p.energy));fft::require(std::isfinite(current_energy_),"linear grid energy overflow");
        fft::require(current_count_!=UINT64_MAX,"linear grid sample count overflow");++current_count_;
    }
    void finish(){if(!finished_){flush();finished_=true;}}
    std::span<const fft::InputRow> rows()const{fft::require(finished_,"linear grid moments not sealed");return rows_;}
    std::span<const long double> energy()const{fft::require(finished_,"linear grid energy not sealed");return energy_;}
    std::span<const fft::InputRadius> radii()const{fft::require(finished_,"linear grid errors not sealed");return radius_;}
    std::span<const scheduled_acquisition_certificate::Interval> energy_bounds()const{fft::require(finished_,"linear grid errors not sealed");return bounded_energy_rows_;}
    std::span<const std::uint64_t> count()const{fft::require(finished_,"linear grid count not sealed");return count_;}
};
struct Cell {
    Pair left{},right{};
    // Supremum ORIGINAL limited-row minus endpoint chord, including control
    // and native arithmetic error. Infinite/unsupported requires Original.
    std::array<double,2> residual{scheduled_acquisition_certificate::infinity,scheduled_acquisition_certificate::infinity};
    bool observed=true,supported=false;
};
inline fft::TemplateRow row(const Cell& c){fft::TemplateRow out{};if(!c.observed||!c.supported)return out;
    for(unsigned bit=0;bit<2;++bit){const auto a=c.left[bit],b=c.right[bit];out[8*bit]=a;out[8*bit+1]=b;
        out[8*bit+2]=a*a;out[8*bit+3]=a*b;out[8*bit+4]=b*b;out[8*bit+5]={std::norm(a),0};
        out[8*bit+6]={std::real(a*std::conj(b)),0};out[8*bit+7]={std::norm(b),0};}return out;}
inline std::array<CorrelationFit,2> fit(const fft::Output& x,double energy,std::uint64_t count,bool gram=true){std::array<CorrelationFit,2> out;
    for(unsigned bit=0;bit<2;++bit){const auto d=x[8*bit]+x[8*bit+1];Wide k{},t{};
        if(gram){k=x[8*bit+2]+x[8*bit+3]+x[8*bit+4];t=x[8*bit+5]+x[8*bit+6]+x[8*bit+7];}
        out[bit]={d.real(),d.imag(),(t.real()+k.real())/2,(t.real()-k.real())/2,k.imag()/2,energy,count};}return out;}
} // namespace datapump::modem::detail::scheduled_linear_grid
namespace datapump::modem::detail::scheduled_linear_grid {
inline fft::TemplateRadius product_radii(const Cell& c){fft::TemplateRadius out{};if(!c.observed||!c.supported)return out;
    namespace cert=scheduled_acquisition_certificate;
    for(unsigned bit=0;bit<2;++bit){const auto a=c.left[bit],b=c.right[bit];
        out[8*bit+2]=cert::complex_preproduct_radius(a,a);out[8*bit+3]=cert::complex_preproduct_radius(a,b);
        out[8*bit+4]=cert::complex_preproduct_radius(b,b);out[8*bit+5]=cert::norm_preproduct_radius(a);
        out[8*bit+6]=cert::complex_preproduct_radius(a,std::conj(b));out[8*bit+7]=cert::norm_preproduct_radius(b);}return out;}
struct Observation {long double energy=0;std::uint64_t count=0;scheduled_acquisition_certificate::Interval bounded_energy;};
// Prefixes are key/bit independent. Exclude quiet cells explicitly, never by
// testing the private coefficient. Unsupported cells require ORIGINAL addback.
class ObservationPrefix {
    std::vector<long double> energy_;std::vector<std::uint64_t> count_;
    std::vector<scheduled_acquisition_certificate::Interval> bounded_;
public:
    static std::size_t reservation(std::size_t cells){return fft::add(sizeof(ObservationPrefix)+4096,fft::mul(fft::add(cells,1),sizeof(long double)+sizeof(std::uint64_t)+sizeof(scheduled_acquisition_certificate::Interval)));}
    explicit ObservationPrefix(const Moments& m,std::size_t budget=SIZE_MAX){
        fft::require(reservation(m.count().size())<=budget,"linear grid prefix exceeds workspace");const auto e=m.energy();const auto c=m.count();const auto b=m.energy_bounds();
        energy_.resize(e.size()+1);count_.resize(e.size()+1);bounded_.resize(e.size()+1);
        for(std::size_t i=0;i<e.size();++i){energy_[i+1]=energy_[i]+e[i];fft::require(c[i]<=UINT64_MAX-count_[i],"linear grid prefix count overflow");
            count_[i+1]=count_[i]+c[i];bounded_[i+1]=scheduled_acquisition_certificate::plus(bounded_[i],b[i]);}}
    using Run=std::pair<std::size_t,std::size_t>;
    static std::size_t run_reservation(std::size_t cells){return fft::add(4096,fft::mul(1+cells/2,sizeof(Run)));}
    static std::vector<Run> runs(std::span<const Cell> cells,std::size_t budget=SIZE_MAX){fft::require(run_reservation(cells.size())<=budget,"linear grid runs exceed workspace");std::vector<Run> out;out.reserve(1+cells.size()/2);std::size_t first=0;
        while(first<cells.size()){if(!cells[first].observed||!cells[first].supported){++first;continue;}auto end=first+1;
            while(end<cells.size()&&cells[end].observed&&cells[end].supported)++end;out.emplace_back(first,end);first=end;}return out;}
    Observation query(std::span<const Run> runs,std::size_t origin)const{Observation out;
        for(const auto [first,end]:runs){fft::require(first<=end&&origin<=SIZE_MAX-end&&origin+end<energy_.size(),"linear grid observation query outside input");
            out.energy+=energy_[origin+end]-energy_[origin+first];out.count+=count_[origin+end]-count_[origin+first];
            out.bounded_energy=scheduled_acquisition_certificate::plus(out.bounded_energy,
                scheduled_acquisition_certificate::energy_slice(bounded_[origin+end],bounded_[origin+first]));}return out;}
    std::size_t working_bytes()const{return sizeof(*this)+384+energy_.capacity()*sizeof(long double)+count_.capacity()*sizeof(std::uint64_t)+bounded_.capacity()*sizeof(bounded_[0]);}
};
inline scheduled_acquisition_certificate::FitCertificate fft_certificate(const fft::Work& work,const Observation& observed,const fft::Output& raw){
    namespace cert=scheduled_acquisition_certificate;cert::FitCertificate out;out.valid=work.certified;
    out.energy_lower=std::max(0.,observed.bounded_energy.lo);out.energy_upper=std::max(0.,observed.bounded_energy.hi);
    for(unsigned bit=0;bit<2;++bit){out.dot_radius[bit]=cert::add(cert::add(work.radius[8*bit],work.radius[8*bit+1]),
            cert::add(cert::mul(cert::gamma(4,std::numeric_limits<double>::epsilon()/2),cert::add(cert::norm(raw[8*bit]),cert::norm(raw[8*bit+1]))),16*std::numeric_limits<double>::denorm_min()));
        double k=0,t=0;for(unsigned p=2;p<5;++p)k=cert::add(k,work.radius[8*bit+p]);for(unsigned p=5;p<8;++p)t=cert::add(t,work.radius[8*bit+p]);
        double mass=0;for(unsigned p=2;p<8;++p)mass=cert::add(mass,cert::norm(raw[8*bit+p]));
        out.gram_radius[bit]=cert::add(cert::mul(.5,cert::add(k,t)),cert::add(cert::mul(cert::gamma(32,std::numeric_limits<double>::epsilon()/2),mass),64*std::numeric_limits<double>::denorm_min()));out.valid=out.valid&&std::isfinite(out.dot_radius[bit])&&std::isfinite(out.gram_radius[bit]);}
    return out;
}
} // namespace datapump::modem::detail::scheduled_linear_grid

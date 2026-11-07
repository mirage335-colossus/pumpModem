#include "datapump/triad_modem_v2.hpp"
#include "datapump/pattern_pulse.hpp"
#include "search_fft.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <limits>
#include <initializer_list>
#include <map>
#include <mutex>
#include <numbers>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

namespace datapump::triad::v2 {
namespace {
constexpr double tau=2*std::numbers::pi;
constexpr unsigned oversampling=4;
using Chip=std::array<Complex,3>;
using Alternatives=std::array<Chip,2>;
void require(bool condition,const char* message){if(!condition)throw Error(message);}
std::size_t multiply(std::size_t a,std::size_t b){require(!a||b<=std::numeric_limits<std::size_t>::max()/a,"triad v2 size overflow");return a*b;}
std::size_t total(std::initializer_list<std::size_t> values){std::size_t result=0;for(auto value:values){require(value<=std::numeric_limits<std::size_t>::max()-result,"triad v2 workspace sum overflow");result+=value;}return result;}
unsigned workers(unsigned requested,std::size_t count){return static_cast<unsigned>(std::min<std::size_t>(std::max(1U,requested),std::max<std::size_t>(1,count)));}
template<class Work>void parallel(std::size_t count,unsigned requested,Work work){
    std::atomic<std::size_t> next{0};std::exception_ptr error;std::mutex mutex;
    auto run=[&](unsigned worker){try{for(;;){const auto index=next.fetch_add(1);if(index>=count)break;work(index,worker);}}catch(...){next.store(count);std::lock_guard guard(mutex);if(!error)error=std::current_exception();}};
    std::vector<std::jthread> pool;const auto n=workers(requested,count);
    for(unsigned i=1;i<n;++i){pool.emplace_back(run,i);}
    run(0);
    for(auto& thread:pool){thread.join();}
    if(error)std::rethrow_exception(error);
}
Crypto child(const Crypto& key,const char* purpose){const auto label=std::string("datapump/experimental-triad/v2/")+purpose;return Crypto(key.mac(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(label.data()),label.size())));}
double smooth(double x){x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x));}
double envelope(double time,const Format& f,const Geometry& g,std::size_t frames){
    const auto duration=(static_cast<double>(frames-1)*(f.burst_wait_frames+1)+1)*g.frame_seconds;
    if(!(time>=0)||time>=duration)return 0;
    const auto period=(f.burst_wait_frames+1)*g.frame_seconds;
    const auto frame=static_cast<std::size_t>(std::floor(time/period));
    const auto local=time-static_cast<double>(frame)*period;
    if(local>=g.frame_seconds)return 0;
    double gain=1;
    const auto edge=std::max(f.ramp_seconds,8/g.fine_chip_rate_hz);
    if(f.burst_wait_frames){gain*=smooth(local/edge)*smooth((g.frame_seconds-local)/edge);}
    else {gain*=smooth(time/edge)*smooth((duration-time)/edge);}
    if(f.fhss){const auto position=std::fmod(time,f.dwell_seconds);const auto distance=std::min(position,f.dwell_seconds-position);gain*=smooth((distance-f.quiet_seconds)/f.ramp_seconds);}
    return gain;
}
template<class Consume>void eligible_intervals(const Format& f,const Geometry& g,double start,Consume consume){
    const auto endpoint=8/g.fine_chip_rate_hz+std::max(f.margin_seconds,f.ramp_seconds);
    const auto first=start+endpoint,last=start+g.frame_seconds-endpoint;
    if(last<first)return;
    if(!f.fhss){consume(first,last);return;}
    const auto guard=f.quiet_seconds+f.ramp_seconds+f.margin_seconds+8/g.fine_chip_rate_hz;
    for(unsigned dwell=0;dwell<g.dwells_per_frame;++dwell){
        const auto boundary=start+dwell*f.dwell_seconds;
        const auto low=std::max(first,boundary+guard),high=std::min(last,boundary+f.dwell_seconds-guard);
        if(high>=low)consume(low,high);
    }
}
std::pair<std::size_t,std::size_t> eligible_bounds(const Format& f,const Geometry& g){
    std::size_t lower=0,upper=0;
    eligible_intervals(f,g,0,[&](double first,double last){
        const auto width=(last-first)*g.fine_chip_rate_hz;
        require(std::isfinite(width)&&width>=0&&width<=10000000,"triad v2 eligible interval exceeds cell cap");
        const auto count=static_cast<std::size_t>(std::floor(width));
        require(upper<=30000000-count-2,"triad v2 eligible calendar exceeds cell cap");
        lower+=count;upper+=count+2;
    });
    return {lower,upper};
}
std::vector<std::size_t> eligible(const Format& f,const Geometry& g,double start){
    const auto bound=eligible_bounds(f,g).second;
    std::vector<std::size_t> result;result.reserve(bound);
    eligible_intervals(f,g,start,[&](double low,double high){
        const auto first_real=std::max(0.,std::ceil(low*g.fine_chip_rate_hz-.5));
        const auto last_real=std::floor(high*g.fine_chip_rate_hz-.5);
        require(std::isfinite(first_real)&&std::isfinite(last_real)&&last_real<static_cast<double>(std::numeric_limits<std::size_t>::max()),"triad v2 fine calendar coordinate overflow");
        if(last_real<first_real)return;
        const auto first=static_cast<std::size_t>(first_real),last=static_cast<std::size_t>(last_real);
        for(auto index=first;;++index){
            const auto time=(static_cast<double>(index)+.5)/g.fine_chip_rate_hz;
            if(time>=low&&time<=high)result.push_back(index);
            if(index==last)break;
        }
    });
    require(result.size()<=bound,"triad v2 eligible calendar exceeded bound");
    return result;
}
std::uint64_t addressed_epoch(const Format& f,const Geometry& g,unsigned frame){
    const auto elapsed=static_cast<long double>(frame)*(f.burst_wait_frames+1)*g.frame_seconds;
    require(elapsed>=0&&elapsed<=std::numeric_limits<std::uint64_t>::max()-f.epoch,"triad v2 frame epoch overflow");
    return f.epoch+static_cast<std::uint64_t>(std::floor(elapsed));
}
std::uint64_t shifted_epoch(std::uint64_t epoch,std::int64_t offset){
    if(offset<0){const auto magnitude=static_cast<std::uint64_t>(-(offset+1))+1;require(magnitude<=epoch,"triad v2 epoch underflow");return epoch-magnitude;}
    require(static_cast<std::uint64_t>(offset)<=std::numeric_limits<std::uint64_t>::max()-epoch,"triad v2 epoch overflow");return epoch+static_cast<std::uint64_t>(offset);
}
std::vector<std::uint8_t> masks(const Format& f,const Crypto& key,std::size_t count){
    std::vector<std::uint8_t> result(count);if(!f.encrypt)return result;
    constexpr std::string_view context="datapump/experimental-triad/v2/payload";
    const auto payload=key.confidentiality_child(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(context.data()),context.size()));
    const auto stream=payload.confidentiality_stream(f.epoch,0,(count+7)/8);
    for(std::size_t bit=0;bit<count;++bit)result[bit]=(stream[bit/8]>>(bit%8))&1U;
    return result;
}
std::vector<std::size_t> permutation(std::size_t size,const Crypto& key,std::uint64_t epoch,std::uint64_t offset){
    std::vector<std::size_t> result(size);std::iota(result.begin(),result.end(),0);
    if(size<2)return result;
    // A fixed isolated seed address owns this bit's variable rejection draws.
    const auto seed=key.stream(StreamPurpose::Dsss,epoch,offset,32);const Crypto draws(seed);
    const auto budget=multiply(size+64,8);const auto bytes=draws.stream(StreamPurpose::Dsss,0,0,budget);std::size_t cursor=0;
    auto next=[&](){require(cursor+4<=bytes.size(),"triad v2 permutation rejection budget exhausted");std::uint32_t value=0;for(unsigned j=0;j<4;++j)value=(value<<8)|bytes[cursor++];return value;};
    for(auto i=size-1;i>0;--i){const auto bound=static_cast<std::uint64_t>(i)+1;const auto limit=(std::uint64_t{1}<<32)/bound*bound;std::uint32_t value;do{value=next();}while(value>=limit);std::swap(result[i],result[value%bound]);}
    return result;
}
std::vector<Alternatives> patterns(const Format& f,const Geometry& g,const Crypto& key,const std::vector<Cell>& cells){
    const auto inner_key=child(key,"inner-patterns"),outer_key=child(key,"carrier-outer-rotations");
    std::vector<Alternatives> result(cells.size());std::vector<Bytes> inner_banks,outer_banks;
    const auto frames=cells.back().frame+1;
    inner_banks.resize(multiply(frames,f.bits_per_frame));outer_banks.resize(inner_banks.size());
    for(std::size_t bit=0;bit<inner_banks.size();++bit){const auto frame=static_cast<unsigned>(bit/f.bits_per_frame);const auto local=bit%f.bits_per_frame;const auto epoch=addressed_epoch(f,g,frame);
        inner_banks[bit]=inner_key.stream(StreamPurpose::Scrambler,epoch,local*(32ULL<<20),multiply(g.inner_coefficients_per_bit,8));
        outer_banks[bit]=outer_key.stream(StreamPurpose::Dsss,epoch,local*(32ULL<<20),multiply(g.fine_coefficients_per_bit,3));}
    constexpr std::array<std::array<unsigned,3>,6> permutations{{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}}};
    const std::array<double,3> amplitudes{std::sqrt(.4),1,std::sqrt(1.6)};
    for(std::size_t i=0;i<cells.size();++i){const auto& cell=cells[i];const auto& ib=inner_banks[cell.bit];const auto& ob=outer_banks[cell.bit];const auto offset=cell.logical_inner*8;const auto& perm=permutations[ib[offset]%6];
        const auto outer_index=cell.logical_inner*f.added_symbols+cell.added_index;
        for(unsigned b=0;b<2;++b)for(unsigned c=0;c<3;++c){const auto phase=(ib[offset+1+b*3+c]%8)*std::numbers::pi/4+(f.added_symbols>1?ob[outer_index*3+c]%4:0)*std::numbers::pi/2;result[i][b][c]=std::polar(amplitudes[perm[(c+b)%3]],phase);}}
    return result;
}
template<class T>std::complex<T> interpolate(std::span<const std::complex<T>> samples,double position){
    if(!(position>=0)||position>=static_cast<double>(samples.size()))return {};
    const auto index=static_cast<std::size_t>(position);const auto fraction=static_cast<T>(position-static_cast<double>(index));
    return samples[index]*(T{1}-fraction)+(index+1<samples.size()?samples[index+1]*fraction:std::complex<T>{});
}
// Account for the higher-order observables before any arithmetic takes
// place. Matched-filter interpolation has a conservative amplitude gain16;
// powers/products and their FFT mask contractions require polynomial degree4
// for differential/amplitude modes, degree2 for coherent mode.
template<class T>bool higher_order_safe(std::span<const Complex> samples,std::size_t fft_bound,Receiver receiver){
    double peak=0;
    for(const auto value:samples)peak=std::max({peak,std::abs(value.real()),std::abs(value.imag())});
    if(peak==0)return true;
    const auto degree=receiver==Receiver::coherent?2.:4.;
    const auto n=2*static_cast<long double>(fft_bound);
    const auto upper=std::pow(static_cast<long double>(std::numeric_limits<T>::max())/(4096*n*n),1.L/degree)/16;
    const auto lower=16*std::pow(static_cast<long double>(std::numeric_limits<T>::min()),1.L/degree);
    return peak>=lower&&peak<=upper;
}
template<class T>void require_finite(const std::vector<std::complex<T>>& values){
    for(const auto value:values)require(std::isfinite(value.real())&&std::isfinite(value.imag()),"triad v2 nonfinite DSP intermediate");
}
struct Segment{std::size_t bit=0;std::vector<std::size_t> cells;};
struct WorkspaceBound{std::size_t preparation=0,persistent=0;};
WorkspaceBound preparation_bound(const Format& f,const Geometry& g,const SearchPrior& p,
    std::size_t bit_count,std::size_t frequency_count){
    const auto frames=1+(bit_count-1)/f.bits_per_frame;
    const auto full_bits=multiply(frames,f.bits_per_frame);
    const auto cells=multiply(frames,g.payload_cells_per_frame);
    auto sections_per_bit=g.fine_coefficients_per_bit;
    if(p.independent_hop_phase)sections_per_bit=std::min<std::size_t>(sections_per_bit,g.dwells_per_frame);
    else if(p.segment_seconds==0)sections_per_bit=1;
    else {const auto count=std::ceil(g.frame_seconds/p.segment_seconds)+2;
        if(count<static_cast<double>(sections_per_bit))sections_per_bit=static_cast<std::size_t>(count);}
    const auto section_count=std::min(cells,multiply(full_bits,sections_per_bit));
    const auto cell_storage=multiply(cells,sizeof(Cell));
    // Geometric vector growth is bounded by twice the final element count.
    // Map-node128 includes the key/value, links and allocator bookkeeping.
    const auto section_storage=total({multiply(section_count,2*sizeof(Segment)),multiply(cells,4*sizeof(std::size_t))});
    const auto bit_storage=total({multiply(section_count,2*sizeof(std::size_t)),multiply(bit_count,64)});
    const auto epoch_metadata=multiply(p.epoch_offsets_seconds.size(),sizeof(std::vector<Alternatives>));
    const auto epoch_storage=multiply(multiply(cells,sizeof(Alternatives)),p.epoch_offsets_seconds.size());
    const auto frequency_storage=multiply(frequency_count,160);
    const auto candidates=multiply(eligible_bounds(f,g).second,sizeof(std::size_t));
    const auto permutation_storage=total({multiply(g.payload_cells_per_frame,sizeof(std::size_t)),
        multiply(g.fine_coefficients_per_bit+64,8),multiply(f.bits_per_frame,sizeof(std::vector<std::size_t>))});
    const auto layout_scratch=total({candidates,permutation_storage});
    const auto pattern_streams=total({multiply(cells/f.added_symbols,8),multiply(cells,3),multiply(full_bits,2*sizeof(Bytes))});
    const auto fixed=std::size_t{65536}; // Crypto contexts, small vectors, seeds, allocation metadata.
    const auto layout_peak=total({cell_storage,layout_scratch});
    const auto section_peak=total({cell_storage,section_storage,multiply(section_count,sizeof(Segment)+128)});
    const auto persistent=total({cell_storage,section_storage,bit_storage,epoch_metadata,epoch_storage,frequency_storage,fixed});
    // One addressed layout and its permutation scratch coexist with the
    // already accumulated epoch bank and one inner/outer stream preparation.
    const auto epoch_peak=total({persistent,cell_storage,layout_scratch,pattern_streams});
    return {total({std::max({layout_peak,section_peak,epoch_peak}),fixed}),persistent};
}
std::vector<Segment> segments(const Format& f,const std::vector<Cell>& cells,const SearchPrior& p){
    std::map<std::pair<std::size_t,std::size_t>,std::size_t> indices;std::vector<Segment> result;
    for(std::size_t index=0;index<cells.size();++index){const auto& cell=cells[index];std::size_t section=0;
        if(p.independent_hop_phase)section=static_cast<std::size_t>(std::floor(cell.center_seconds/f.dwell_seconds));
        else if(p.segment_seconds>0)section=static_cast<std::size_t>(std::floor(cell.center_seconds/p.segment_seconds));
        const auto identity=std::pair(cell.bit,section);auto [it,inserted]=indices.emplace(identity,result.size());if(inserted)result.push_back({cell.bit,{}});result[it->second].cells.push_back(index);}
    // A short final section is folded into its preceding section for the same
    // bit only when no independent-hop phase boundary would be crossed.
    if(!p.independent_hop_phase){for(std::size_t i=0;i<result.size();++i)if(result[i].cells.size()<3){for(std::size_t j=i;j>0;--j)if(result[j-1].bit==result[i].bit){result[j-1].cells.insert(result[j-1].cells.end(),result[i].cells.begin(),result[i].cells.end());result[i].cells.clear();break;}}}
    return result;
}
} // namespace

Geometry geometry(const Format& f){
    require(std::isfinite(f.inner_rate_hz)&&f.inner_rate_hz>0&&f.inner_rate_hz<=30000000,"triad v2 invalid inner Rate");
    require(f.added_symbols>=1&&f.added_symbols<=1000,"triad v2 invalid Added Symbols factor");
    require(f.sample_rate>=64,"triad v2 invalid sample rate");
    require(f.minimum_inner_coefficients>=64&&f.minimum_inner_coefficients<=1048576,"triad v2 requires at least64 inner coefficients");
    require(f.bits_per_frame>=1&&f.bits_per_frame<=256&&f.minimum_dwells>=20&&f.minimum_dwells<=1000000,"triad v2 invalid frame diversity");
    require(std::isfinite(f.minimum_bit_energy_seconds)&&f.minimum_bit_energy_seconds>=0,"triad v2 invalid energy duration");
    require(std::isfinite(f.dwell_seconds)&&std::abs(f.dwell_seconds-.4)<1e-12,"triad v2 dwell must be400ms");
    require(std::isfinite(f.quiet_seconds)&&f.quiet_seconds>=0&&std::isfinite(f.ramp_seconds)&&f.ramp_seconds>0&&std::isfinite(f.margin_seconds)&&f.margin_seconds>=.001,"triad v2 invalid envelope margins");
    require(f.burst_wait_frames<=1000,"triad v2 burst wait exceeds limit");
    Geometry g;g.inner_chip_rate_hz=f.inner_rate_hz/6;g.fine_chip_rate_hz=g.inner_chip_rate_hz*f.added_symbols;
    g.carrier_spacing_hz=1.25*g.fine_chip_rate_hz;g.occupied_bandwidth_hz=3*g.carrier_spacing_hz;
    require(f.sample_rate>=4*g.fine_chip_rate_hz&&f.sample_rate>1.1*g.occupied_bandwidth_hz,"triad v2 sample rate cannot contain shaped carriers");
    require(f.minimum_dwells*f.dwell_seconds*f.sample_rate<=100000000&&f.minimum_dwells*f.dwell_seconds*g.fine_chip_rate_hz<=10000000,"triad v2 minimum frame exceeds sample/cell cap");
    if(f.fhss){require(f.quiet_seconds+f.ramp_seconds+f.margin_seconds+8/g.fine_chip_rate_hz<f.dwell_seconds/2,"triad v2 FHSS pulse support cannot fit quiet/ramp margins");
        Geometry probe=g;probe.frame_seconds=20*f.dwell_seconds;probe.dwells_per_frame=20;require(eligible_bounds(f,probe).first>0,"triad v2 FHSS aperture cannot guarantee global fine cells");}
    const auto required_real=std::ceil(f.minimum_bit_energy_seconds*g.inner_chip_rate_hz-1e-10);
    require(std::isfinite(required_real)&&required_real<=1048576/f.added_symbols,"triad v2 requested energy exceeds pattern cap");
    const auto required=std::max<std::size_t>(f.minimum_inner_coefficients,static_cast<std::size_t>(std::max(0.,required_real)));
    require(required<=1048576/f.added_symbols,"triad v2 fine pattern exceeds1048576-cell cap");
    const auto required_cells=multiply(multiply(required,f.added_symbols),f.bits_per_frame);
    require(required_cells<=10000000,"triad v2 requested frame exceeds cell cap");
    g.dwells_per_frame=f.minimum_dwells;
    const auto duty=f.fhss?std::max(.001,1-2*(f.quiet_seconds+f.ramp_seconds+f.margin_seconds+8/g.fine_chip_rate_hz)/f.dwell_seconds):1.;
    const auto initial_real=std::ceil((static_cast<double>(required_cells)/g.fine_chip_rate_hz/duty+2*(8/g.fine_chip_rate_hz+std::max(f.margin_seconds,f.ramp_seconds)))/f.dwell_seconds);
    require(std::isfinite(initial_real)&&initial_real<=1000000,"triad v2 requested duration exceeds dwell cap");
    const auto initial=static_cast<unsigned>(initial_real);
    g.dwells_per_frame=std::max(g.dwells_per_frame,initial);
    for(;;){require(g.dwells_per_frame<=1000000,"triad v2 frame exceeds dwell cap");g.frame_seconds=g.dwells_per_frame*f.dwell_seconds;
        require(g.frame_seconds*f.sample_rate<=100000000&&g.frame_seconds*g.fine_chip_rate_hz<=10000000,"triad v2 frame exceeds sample/cell cap");
        const auto available=eligible_bounds(f,g).first;if(available>=required_cells){g.inner_coefficients_per_bit=available/(f.bits_per_frame*f.added_symbols);break;}++g.dwells_per_frame;}
    g.fine_coefficients_per_bit=multiply(g.inner_coefficients_per_bit,f.added_symbols);
    require(g.fine_coefficients_per_bit<=1048576,"triad v2 fine pattern exceeds1048576-cell cap");
    g.payload_cells_per_frame=multiply(g.fine_coefficients_per_bit,f.bits_per_frame);
    g.fine_cells_per_frame=static_cast<std::size_t>(std::ceil(g.frame_seconds*g.fine_chip_rate_hz));
    require(g.fine_cells_per_frame<=10000000,"triad v2 frame exceeds fine-cell cap");
    g.payload_energy_seconds_per_bit=static_cast<double>(g.fine_coefficients_per_bit)/g.fine_chip_rate_hz;
    const auto extent=g.frame_seconds*f.sample_rate;require(std::isfinite(extent)&&extent<=100000000,"triad v2 frame exceeds sample cap");g.frame_samples=static_cast<std::size_t>(std::llround(extent));return g;
}
std::vector<Cell> layout(const Format& f,std::size_t bit_count,const Crypto& key){
    const auto g=geometry(f);require(bit_count>=1&&bit_count<=65536,"triad v2 invalid local bit count");
    const auto frames=1+(bit_count-1)/f.bits_per_frame;
    const auto duration=(static_cast<double>(frames-1)*(f.burst_wait_frames+1)+1)*g.frame_seconds;
    require(duration*f.sample_rate<=100000000,"triad v2 source calendar exceeds sample cap");
    require(multiply(frames,g.payload_cells_per_frame)<=10000000,"triad v2 layout exceeds cell cap");
    const auto perm_key=child(key,"whole-fine-cell-permutation");std::vector<Cell> result;result.reserve(multiply(frames,g.payload_cells_per_frame));
    for(unsigned frame=0;frame<frames;++frame){const auto start=static_cast<double>(frame)*(f.burst_wait_frames+1)*g.frame_seconds;const auto candidates=eligible(f,g,start);
        require(candidates.size()>=g.payload_cells_per_frame,"triad v2 fractional frame calendar lacks required cells");
        std::vector<std::vector<std::size_t>> maps(f.bits_per_frame);
        for(unsigned bit=0;bit<f.bits_per_frame;++bit){if(f.added_interleave&&f.added_symbols>1)maps[bit]=permutation(g.fine_coefficients_per_bit,perm_key,addressed_epoch(f,g,frame),static_cast<std::uint64_t>(bit)*32);}
        for(std::size_t position=0;position<g.payload_cells_per_frame;++position){const auto selected=std::min(candidates.size()-1,static_cast<std::size_t>((static_cast<long double>(position)+.5L)*candidates.size()/g.payload_cells_per_frame));
            const auto fine_index=candidates[selected],local_bit=position%f.bits_per_frame,rank=position/f.bits_per_frame;
            const auto logical=maps[local_bit].empty()?rank:maps[local_bit][rank];const auto time=(static_cast<double>(fine_index)+.5)/g.fine_chip_rate_hz;
            result.push_back({time,fine_index,static_cast<std::size_t>(frame)*f.bits_per_frame+local_bit,frame,static_cast<unsigned>(std::floor((time-start)/f.dwell_seconds)),logical/f.added_symbols,static_cast<unsigned>(logical%f.added_symbols)});}}
    return result;
}
Waveform transmit(std::span<const std::uint8_t> bits,const Format& f,const Crypto& key){
    const auto g=geometry(f);for(auto bit:bits)require(bit<=1,"triad v2 source contains a non-bit");Waveform result;result.cells=layout(f,bits.size(),key);result.source_bits=bits.size();
    const auto frames=1+(bits.size()-1)/f.bits_per_frame;const auto duration=(static_cast<double>(frames-1)*(f.burst_wait_frames+1)+1)*g.frame_seconds;
    require(duration*f.sample_rate<=100000000,"triad v2 waveform exceeds sample cap");result.source_end_sample=static_cast<std::size_t>(std::llround(duration*f.sample_rate));result.samples.resize(result.source_end_sample);
    result.payload_energy_seconds_per_bit=g.payload_energy_seconds_per_bit;const auto bank=patterns(f,g,key,result.cells);const auto mask=masks(f,key,bits.size());
    const auto pad=child(key,"padding").stream(StreamPurpose::Data,f.epoch,0,(frames*f.bits_per_frame+7)/8);const auto cover_key=child(key,"cover");
    const auto all_cells=static_cast<std::size_t>(std::ceil(duration*g.fine_chip_rate_hz));const auto cover=cover_key.stream(StreamPurpose::Scrambler,f.epoch,0,multiply(all_cells,4));
    constexpr std::array<std::array<unsigned,3>,6> permutations{{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}}};const std::array<double,3> amplitudes{std::sqrt(.4),1,std::sqrt(1.6)};
    std::size_t payload=0;const auto chip_samples=f.sample_rate/g.fine_chip_rate_hz;constexpr double gain=.5773502691896257645;
    for(std::size_t fine=0;fine<all_cells;++fine){const auto center_seconds=(static_cast<double>(fine)+.5)/g.fine_chip_rate_hz;
        const auto period=(f.burst_wait_frames+1)*g.frame_seconds;const auto local=std::fmod(center_seconds,period);if(local>=g.frame_seconds)continue;
        Chip values{};if(payload<result.cells.size()&&result.cells[payload].fine_index==fine){const auto bit=result.cells[payload].bit;const auto choice=bit<bits.size()?bits[bit]^mask[bit]:(pad[bit/8]>>(bit%8))&1U;values=bank[payload][choice];++payload;}
        else{const auto offset=fine*4;const auto& perm=permutations[cover[offset]%6];for(unsigned c=0;c<3;++c)values[c]=std::polar(amplitudes[perm[c]],(cover[offset+1+c]%8)*std::numbers::pi/4);}
        const auto center=center_seconds*f.sample_rate;const auto first=std::max(0.,std::ceil(center-8*chip_samples));const auto end=std::min(static_cast<double>(result.samples.size()),std::floor(center+8*chip_samples)+1);
        for(auto sample=static_cast<std::size_t>(first);static_cast<double>(sample)<end;++sample){const auto pulse=modem::pattern_pulse((static_cast<double>(sample)-center)/chip_samples)*gain;for(unsigned c=0;c<3;++c){const auto phase=tau*(static_cast<double>(c)-1)*g.carrier_spacing_hz*static_cast<double>(sample)/f.sample_rate;result.samples[sample]+=values[c]*pulse*std::polar(1.,phase);}}}
    require(payload==result.cells.size(),"triad v2 payload calendar was not fully synthesized");
    for(std::size_t sample=0;sample<result.samples.size();++sample){result.samples[sample]*=envelope(static_cast<double>(sample)/f.sample_rate,f,g,frames);const auto power=std::norm(result.samples[sample]);result.mean_power+=power/static_cast<double>(result.samples.size());result.peak_power=std::max(result.peak_power,power);}return result;
}

namespace {
template<class T,class Plan>std::array<std::vector<std::complex<T>>,3> channelize(
    std::span<const Complex> samples,const Format& f,const Geometry& g,double clock,double center,
    std::size_t nraw,std::size_t nretained,const Plan& plan,unsigned requested){
    const auto rate=1+clock*1e-6;const auto chip_samples=f.sample_rate/(g.fine_chip_rate_hz*rate);
    const auto half=static_cast<std::size_t>(std::ceil(8*chip_samples));
    std::vector<std::complex<T>> filter(nraw);double energy=0,l1=0;
    for(std::size_t j=0;j<=2*half;++j){const auto value=modem::pattern_pulse((static_cast<double>(j)-static_cast<double>(half))/chip_samples);filter[j]={static_cast<T>(value),0};energy+=value*value;l1+=std::abs(value);}
    require(energy>0&&l1/energy<=8,"triad v2 matched-filter gain exceeds arithmetic bound");
    for(auto& value:filter){value/=static_cast<T>(energy);}
    modem::detail::search_fft::fft(filter,false,plan);
    std::array<std::vector<std::complex<T>>,3> rich;
    parallel(3,requested,[&](std::size_t carrier,unsigned){std::vector<std::complex<T>> work(nraw);const auto frequency=center+(static_cast<double>(carrier)-1)*g.carrier_spacing_hz*rate;
        const auto step=std::polar(1.,-tau*frequency/f.sample_rate);Complex phase{1,0};
        for(std::size_t j=0;j<samples.size();++j){const auto value=samples[j]*phase;work[j]={static_cast<T>(value.real()),static_cast<T>(value.imag())};phase*=step;if((j&4095U)==0)phase/=std::abs(phase);}
        modem::detail::search_fft::fft(work,false,plan);for(std::size_t j=0;j<nraw;++j)work[j]*=filter[j];modem::detail::search_fft::fft(work,true,plan);
        rich[carrier].resize(nretained);const auto stride=static_cast<double>(f.sample_rate)/(oversampling*g.fine_chip_rate_hz*rate);
        for(std::size_t j=0;j<nretained;++j){rich[carrier][j]=interpolate<T>(work,static_cast<double>(half)+static_cast<double>(j)*stride);}
        require_finite(rich[carrier]);
    });return rich;
}
template<class T,class Plan>Reception run(std::span<const Complex> samples,std::size_t bit_count,
    const Format& f,const Geometry& g,const Crypto& key,const SearchPrior& p,Receiver receiver,
    const std::vector<Cell>& cells,const std::vector<Segment>& sections,
    const std::map<double,std::vector<double>>& frequency_groups,std::size_t logical_frequency_count,const WorkspaceBound& memory){
    Reception result;result.working_bytes=memory.preparation;result.score=-std::numeric_limits<double>::infinity();result.arithmetic_used=std::is_same_v<T,float>?Arithmetic::fp32:Arithmetic::fp64;
    const auto frames=1+(bit_count-1)/f.bits_per_frame;const auto duration=(static_cast<double>(frames-1)*(f.burst_wait_frames+1)+1)*g.frame_seconds;
    const auto components=receiver==Receiver::differential?2U:3U;const auto epochs=p.epoch_offsets_seconds.size();
    const auto tasks=multiply(multiply(epochs,bit_count),2);const auto threads=workers(p.threads,tasks);result.threads_used=threads;
    const auto reference_extent=multiply(cells.back().fine_index,oversampling)+oversampling/2+1;
    std::vector<std::vector<std::size_t>> bit_sections(bit_count);std::vector<double> bit_weights(bit_count);
    std::vector<std::size_t> section_counts(bit_count);
    for(const auto& section:sections)if(section.bit<bit_count&&section.cells.size()>=3)++section_counts[section.bit];
    for(std::size_t bit=0;bit<bit_count;++bit)bit_sections[bit].reserve(section_counts[bit]);
    for(std::size_t s=0;s<sections.size();++s)if(sections[s].bit<bit_count&&sections[s].cells.size()>=3){bit_sections[sections[s].bit].push_back(s);bit_weights[sections[s].bit]+=static_cast<double>(sections[s].cells.size());}
    for(auto weight:bit_weights)require(weight>0,"triad v2 score segments contain no usable evidence for a source bit");
    const auto patterns_bytes=multiply(multiply(cells.size(),sizeof(Alternatives)),epochs);
    require(patterns_bytes<p.memory_limit_bytes,"triad v2 epoch template bank exceeds memory cap");
    std::vector<std::vector<Alternatives>> epoch_patterns(epochs);
    for(std::size_t e=0;e<epochs;++e){Format addressed=f;addressed.epoch=shifted_epoch(f.epoch,p.epoch_offsets_seconds[e]);const auto addressed_cells=layout(addressed,bit_count,key);epoch_patterns[e]=patterns(addressed,g,key,addressed_cells);}
    std::atomic<std::size_t> fft_counter{0};
    for(const auto clock:p.clock_offsets_ppm){const auto rate=1+clock*1e-6;const auto dt=1/(oversampling*g.fine_chip_rate_hz*rate);
        const auto nretained=static_cast<std::size_t>(std::ceil(static_cast<double>(samples.size())/f.sample_rate/dt))+1;
        const auto lower=static_cast<long long>(std::floor(p.arrival_min_seconds/dt));const auto upper=static_cast<long long>(std::ceil(p.arrival_max_seconds/dt));
        const auto requested_step=p.timing_step_seconds>0?p.timing_step_seconds:dt;const auto stride=std::max<long long>(1,static_cast<long long>(std::floor(requested_step/dt+1e-8)));
        require(upper>=lower&&static_cast<long double>(upper)-lower<=static_cast<long double>(p.maximum_hypotheses)*stride,"triad v2 arrival grid exceeds hypothesis cap");
        const auto declared_times=static_cast<std::size_t>((upper-lower)/stride)+1+static_cast<std::size_t>((upper-lower)%stride!=0);
        require(multiply(multiply(multiply(logical_frequency_count,declared_times),epochs),p.clock_offsets_ppm.size())<=p.maximum_hypotheses,
                "triad v2 declared search exceeds hypothesis cap");
        const auto arrival_bytes=multiply(declared_times,32);
        require(total({memory.persistent,arrival_bytes})<=p.memory_limit_bytes,"triad v2 arrival workspace exceeds memory cap");
        result.working_bytes=std::max(result.working_bytes,total({memory.persistent,arrival_bytes}));
        std::vector<std::size_t> lags;std::vector<double> times;
        lags.reserve(declared_times);times.reserve(declared_times);
        auto append=[&](long long lag){const auto time=static_cast<double>(lag)*dt;if(lag>=0&&time+duration/rate<=static_cast<double>(samples.size())/f.sample_rate+.5/f.sample_rate){lags.push_back(static_cast<std::size_t>(lag));times.push_back(time);}};
        for(auto lag=lower;lag<=upper;){append(lag);if(upper-lag<stride)break;lag+=stride;}
        if((upper-lower)%stride)append(upper);
        if(lags.empty()){continue;}
        const auto ntimes=lags.size();result.complete_observation=true;
        const auto potential=multiply(multiply(multiply(logical_frequency_count,ntimes),epochs),p.clock_offsets_ppm.size());require(potential<=p.maximum_hypotheses,"triad v2 declared search exceeds hypothesis cap");
        const auto half=static_cast<std::size_t>(std::ceil(8*f.sample_rate/(g.fine_chip_rate_hz*rate)));
        const auto nraw=std::bit_ceil(samples.size()+2*half+1);const auto ncorr=std::bit_ceil(nretained+reference_extent+1);const auto maximum=std::max(nraw,ncorr);
        for(const auto& [center,frequencies]:frequency_groups){const auto nfrequencies=receiver==Receiver::coherent?frequencies.size():std::size_t{1};
            const auto entries=multiply(multiply(multiply(tasks,nfrequencies),ntimes),sizeof(double));
            const auto denominator_entries=multiply(multiply(multiply(sections.size(),components),ntimes),sizeof(double));
            const auto working=total({memory.persistent,multiply(maximum,sizeof(std::complex<T>)),arrival_bytes,
                multiply(nraw,4*sizeof(std::complex<T>)),multiply(nretained,3*sizeof(std::complex<T>)),
                multiply(multiply(ncorr,components),multiply(sizeof(std::complex<T>),nfrequencies+(receiver==Receiver::amplitude_only?2:1))),
                multiply(multiply(ncorr,threads),2*sizeof(std::complex<T>)),entries,
                multiply(denominator_entries,receiver==Receiver::amplitude_only?2:1)});
            require(working<=p.memory_limit_bytes,"triad v2 rich workspace exceeds memory cap");result.working_bytes=std::max(result.working_bytes,working);
            result.threads_used=std::max({result.threads_used,workers(p.threads,3),workers(p.threads,multiply(nfrequencies,components))});
            Plan plan(maximum);auto rich=channelize<T>(samples,f,g,clock,center,nraw,nretained,plan,p.threads);fft_counter+=7;
            ++result.frequency_banks;result.retained_samples=std::max(result.retained_samples,multiply(nretained,3));result.projection_values+=multiply(nretained,3);++result.coarse_hypotheses;
            std::vector<std::vector<std::complex<T>>> input_spectra(multiply(nfrequencies,components),std::vector<std::complex<T>>(ncorr));
            std::vector<std::vector<std::complex<T>>> powers(components,std::vector<std::complex<T>>(ncorr)),squares;
            if(receiver==Receiver::amplitude_only)squares.assign(components,std::vector<std::complex<T>>(ncorr));
            for(unsigned c=0;c<components;++c){for(std::size_t j=0;j<nretained;++j){const auto observed=receiver==Receiver::differential?rich[c][j]*std::conj(rich[c+1][j]):rich[c][j];const auto power=std::norm(observed);powers[c][j]={power,0};if(receiver==Receiver::amplitude_only)squares[c][j]={power*power,0};}
                modem::detail::search_fft::fft(powers[c],false,plan);require_finite(powers[c]);++fft_counter;if(receiver==Receiver::amplitude_only){modem::detail::search_fft::fft(squares[c],false,plan);require_finite(squares[c]);++fft_counter;}}
            parallel(input_spectra.size(),p.threads,[&](std::size_t row,unsigned){const auto fi=row/components;const auto c=static_cast<unsigned>(row%components);auto& work=input_spectra[row];const auto residual=receiver==Receiver::coherent?frequencies[fi]-center:0.;const auto step=std::polar(1.,-tau*residual*dt);Complex phase{1,0};
                for(std::size_t j=0;j<nretained;++j){Complex observed;if(receiver==Receiver::differential)observed=static_cast<Complex>(rich[c][j])*std::conj(static_cast<Complex>(rich[c+1][j]));else if(receiver==Receiver::amplitude_only)observed={static_cast<double>(std::norm(rich[c][j])),0};else observed=static_cast<Complex>(rich[c][j])*phase;
                    work[j]={static_cast<T>(observed.real()),static_cast<T>(observed.imag())};phase*=step;if((j&4095U)==0)phase/=std::abs(phase);}
                modem::detail::search_fft::fft(work,false,plan);require_finite(work);++fft_counter;});
            rich={};
            std::vector<double> denominator(denominator_entries/sizeof(double)),second_denominator(receiver==Receiver::amplitude_only?denominator.size():0);
            std::vector<std::vector<std::complex<T>>> references(threads,std::vector<std::complex<T>>(ncorr)),products(threads,std::vector<std::complex<T>>(ncorr));
            parallel(sections.size(),threads,[&](std::size_t section,unsigned worker){const auto& indices=sections[section].cells;if(indices.size()<3||sections[section].bit>=bit_count)return;auto& reference=references[worker];auto& product=products[worker];std::fill(reference.begin(),reference.end(),std::complex<T>{});
                for(auto cell:indices){reference[cells[cell].fine_index*oversampling+oversampling/2]={1,0};}
                modem::detail::search_fft::fft(reference,false,plan);++fft_counter;
                for(unsigned c=0;c<components;++c){for(std::size_t j=0;j<ncorr;++j)product[j]=powers[c][j]*std::conj(reference[j]);modem::detail::search_fft::fft(product,true,plan);++fft_counter;const auto offset=(section*components+c)*ntimes;
                    for(std::size_t t=0;t<ntimes;++t)denominator[offset+t]=std::max(0.,static_cast<double>(product[lags[t]].real()));
                    if(receiver==Receiver::amplitude_only){for(std::size_t j=0;j<ncorr;++j)product[j]=squares[c][j]*std::conj(reference[j]);modem::detail::search_fft::fft(product,true,plan);++fft_counter;for(std::size_t t=0;t<ntimes;++t)second_denominator[offset+t]=std::max(0.,static_cast<double>(product[lags[t]].real()));}}});
            powers.clear();squares.clear();std::vector<double> scores(entries/sizeof(double));
            parallel(tasks,threads,[&](std::size_t task,unsigned worker){const auto candidate=static_cast<unsigned>(task%2);const auto bit=(task/2)%bit_count;const auto epoch=task/(2*bit_count);const auto& bank=epoch_patterns[epoch];auto& reference=references[worker];auto& product=products[worker];
                for(auto section:bit_sections[bit]){const auto& indices=sections[section].cells;const auto n=indices.size();for(unsigned c=0;c<components;++c){double mean=0;if(receiver==Receiver::amplitude_only){for(auto index:indices)mean+=std::norm(bank[index][candidate][c])/static_cast<double>(n);}
                    std::fill(reference.begin(),reference.end(),std::complex<T>{});double reference_energy=0;
                    for(auto index:indices){Complex value;if(receiver==Receiver::differential)value=bank[index][candidate][c]*std::conj(bank[index][candidate][c+1]);else if(receiver==Receiver::amplitude_only)value={std::norm(bank[index][candidate][c])-mean,0};else value=bank[index][candidate][c];
                        reference_energy+=std::norm(value);reference[cells[index].fine_index*oversampling+oversampling/2]={static_cast<T>(value.real()),static_cast<T>(value.imag())};}
                    if(!(reference_energy>0)){continue;}
                    modem::detail::search_fft::fft(reference,false,plan);++fft_counter;
                    for(std::size_t fi=0;fi<nfrequencies;++fi){const auto& input=input_spectra[fi*components+c];for(std::size_t j=0;j<ncorr;++j)product[j]=input[j]*std::conj(reference[j]);modem::detail::search_fft::fft(product,true,plan);++fft_counter;
                        const auto score_offset=(task*nfrequencies+fi)*ntimes,denominator_offset=(section*components+c)*ntimes;
                        for(std::size_t t=0;t<ntimes;++t){const auto observation=static_cast<Complex>(product[lags[t]]);auto observed_energy=denominator[denominator_offset+t];double cross_power=std::norm(observation);const auto baseline=receiver==Receiver::amplitude_only?.5/static_cast<double>(n-1):1./static_cast<double>(n);
                            if(receiver==Receiver::amplitude_only){observed_energy=std::max(0.,second_denominator[denominator_offset+t]-observed_energy*observed_energy/static_cast<double>(n));const auto positive=std::max(0.,observation.real());cross_power=positive*positive;}
                            const auto normalized=observed_energy>0?(cross_power/(observed_energy*reference_energy)-baseline)/(1-baseline):0.;require(std::isfinite(normalized),"triad v2 nonfinite score");scores[score_offset+t]+=static_cast<double>(n)*normalized/(components*bit_weights[bit]);}}}}});
            result.hypotheses+=multiply(multiply(epochs,frequencies.size()),ntimes);result.refined_hypotheses+=multiply(multiply(epochs,nfrequencies),ntimes);
            for(std::size_t e=0;e<epochs;++e)for(std::size_t fi=0;fi<nfrequencies;++fi)for(std::size_t t=0;t<ntimes;++t){double average=0,margin=0;for(std::size_t bit=0;bit<bit_count;++bit){const auto task=(e*bit_count+bit)*2;const auto zero=scores[(task*nfrequencies+fi)*ntimes+t],one=scores[((task+1)*nfrequencies+fi)*ntimes+t];average+=std::max(zero,one)/static_cast<double>(bit_count);margin+=std::abs(one-zero)/static_cast<double>(bit_count);}
                if(average>result.score){result.score=average;result.margin=margin;result.bits.resize(bit_count);result.bit_scores.resize(bit_count);result.bit_margins.resize(bit_count);for(std::size_t bit=0;bit<bit_count;++bit){const auto task=(e*bit_count+bit)*2;const auto zero=scores[(task*nfrequencies+fi)*ntimes+t],one=scores[((task+1)*nfrequencies+fi)*ntimes+t];result.bits[bit]=one>zero;result.bit_scores[bit]=std::max(zero,one);result.bit_margins[bit]=std::abs(one-zero);}result.frequency_hz=receiver==Receiver::coherent?frequencies[fi]:center;result.timing_seconds=times[t];result.clock_ppm=clock;result.epoch_offset_seconds=p.epoch_offsets_seconds[e];}}
        }
    }
    result.fft_count=fft_counter.load();if(!result.complete_observation){result.score=0;return result;}
    Format addressed=f;addressed.epoch=shifted_epoch(f.epoch,result.epoch_offset_seconds);const auto mask=masks(addressed,key,bit_count);for(std::size_t bit=0;bit<bit_count;++bit)result.bits[bit]^=mask[bit];
    result.accepted=std::all_of(result.bit_scores.begin(),result.bit_scores.end(),[&](double score){return score>=p.minimum_score;})&&std::all_of(result.bit_margins.begin(),result.bit_margins.end(),[&](double margin){return margin>=p.minimum_margin;});return result;
}
} // namespace

Reception receive(std::span<const Complex> samples,std::size_t bit_count,const Format& f,const Crypto& key,const SearchPrior& p,Receiver receiver){
    const auto started=std::chrono::steady_clock::now();const auto cpu=std::clock();const auto g=geometry(f);
    require(bit_count>=1&&bit_count<=65536,"triad v2 invalid local bit count");require(!samples.empty(),"triad v2 empty capture");
    require(receiver==Receiver::coherent||receiver==Receiver::differential||receiver==Receiver::amplitude_only,"triad v2 invalid receiver");
    require(p.arithmetic==Arithmetic::automatic||p.arithmetic==Arithmetic::fp32||p.arithmetic==Arithmetic::fp64,"triad v2 invalid arithmetic");
    require(p.threads>=1&&p.threads<=256,"triad v2 invalid worker count");
    require(std::isfinite(p.frequency_half_width_hz)&&p.frequency_half_width_hz>=0&&std::isfinite(p.frequency_step_hz)&&p.frequency_step_hz>=0,"triad v2 invalid frequency prior");
    require(std::isfinite(p.arrival_min_seconds)&&std::isfinite(p.arrival_max_seconds)&&p.arrival_min_seconds<=p.arrival_max_seconds&&std::abs(p.arrival_min_seconds)<=86400&&std::abs(p.arrival_max_seconds)<=86400,"triad v2 invalid arrival prior");
    require(std::isfinite(p.timing_step_seconds)&&p.timing_step_seconds>=0&&p.timing_step_seconds<=86400&&std::isfinite(p.segment_seconds)&&(p.segment_seconds==0||p.segment_seconds>=1e-6),"triad v2 invalid time resolution");
    require(std::isfinite(p.minimum_score)&&p.minimum_score>=0&&p.minimum_score<=1&&std::isfinite(p.minimum_margin)&&p.minimum_margin>=0&&p.minimum_margin<=2,"triad v2 invalid empirical gate");
    require(!p.clock_offsets_ppm.empty()&&!p.epoch_offsets_seconds.empty(),"triad v2 empty prior grid");
    for(const auto value:samples)require(std::isfinite(value.real())&&std::isfinite(value.imag()),"triad v2 nonfinite capture");
    for(auto clock:p.clock_offsets_ppm)require(std::isfinite(clock)&&std::abs(clock)<=10000,"triad v2 invalid clock prior");
    const auto frames=1+(bit_count-1)/f.bits_per_frame;
    const auto duration=(static_cast<double>(frames-1)*(f.burst_wait_frames+1)+1)*g.frame_seconds;
    require(duration*f.sample_rate<=100000000,"triad v2 source calendar exceeds sample cap");
    const auto coherence=p.independent_hop_phase?f.dwell_seconds:(p.segment_seconds>0?std::min(p.segment_seconds,duration):duration);
    const auto step=p.frequency_step_hz>0?p.frequency_step_hz:1/(4*coherence);const auto half=std::ceil(p.frequency_half_width_hz/step);
    require(std::isfinite(step)&&step>0&&half<=1000000,"triad v2 frequency grid exceeds cap");const auto count=static_cast<std::size_t>(half)*2+1;
    require(count<=p.maximum_hypotheses,"triad v2 frequency grid exceeds hypothesis cap");
    const auto memory=preparation_bound(f,g,p,bit_count,count);
    require(memory.preparation<=p.memory_limit_bytes,"triad v2 preparation workspace exceeds memory cap");
    const auto cells=layout(f,bit_count,key);const auto parts=segments(f,cells,p);
    const auto maximum_clock=1+*std::max_element(p.clock_offsets_ppm.begin(),p.clock_offsets_ppm.end())*1e-6;
    require(half*step+.5*g.occupied_bandwidth_hz*maximum_clock<.49*f.sample_rate,"triad v2 capture bandwidth misses declared CFO hull");
    std::map<double,std::vector<double>> groups;const auto coarse_step=g.fine_chip_rate_hz/8;
    for(std::size_t index=0;index<count;++index){const auto frequency=(static_cast<double>(index)-half)*step;const auto coarse=std::round(frequency/coarse_step)*coarse_step;groups[coarse].push_back(frequency);}
    const auto largest=std::bit_ceil(samples.size()+static_cast<std::size_t>(std::ceil(16*f.sample_rate/(g.fine_chip_rate_hz*.99)))+1);
    const auto float_safe=modem::detail::search_fft::float_input_safe(samples,largest)&&higher_order_safe<float>(samples,largest,receiver);
    auto arithmetic=p.arithmetic;if(arithmetic==Arithmetic::automatic)arithmetic=float_safe?Arithmetic::fp32:Arithmetic::fp64;
    require(arithmetic!=Arithmetic::fp64||higher_order_safe<double>(samples,largest,receiver),"triad v2 FP64 capture outside safe higher-order envelope");
    Reception result;if(arithmetic==Arithmetic::fp64)result=run<double,modem::detail::search_fft::DoublePlan>(samples,bit_count,f,g,key,p,receiver,cells,parts,groups,count,memory);
    else{require(float_safe,"triad v2 FP32 capture outside safe higher-order FFT envelope");result=run<float,modem::detail::search_fft::FloatPlan>(samples,bit_count,f,g,key,p,receiver,cells,parts,groups,count,memory);}
    result.wall_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();result.cpu_seconds=static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC;return result;
}
const char* receiver_name(Receiver receiver){switch(receiver){case Receiver::coherent:return "coherent";case Receiver::differential:return "differential";case Receiver::amplitude_only:return "amplitude-only";}return "unknown";}
} // namespace datapump::triad::v2

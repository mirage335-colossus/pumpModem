#include "datapump/triad_modem.hpp"
#include "datapump/pattern_pulse.hpp"
#include "search_fft.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <limits>
#include <mutex>
#include <numbers>
#include <numeric>
#include <string>
#include <thread>
#include <type_traits>

namespace datapump::triad {
namespace {
constexpr double tau = 2 * std::numbers::pi;
using Chip = std::array<Complex,3>;
using Alternatives = std::array<Chip,2>;
void require(bool condition, const char* message) { if(!condition) throw Error(message); }
std::size_t product(std::size_t a,std::size_t b) {
    require(!a || b<=std::numeric_limits<std::size_t>::max()/a,"triad workspace extent overflow");
    return a*b;
}
unsigned worker_count(unsigned requested,std::size_t count) {
    return static_cast<unsigned>(std::min<std::size_t>(std::max(1U,requested),std::max<std::size_t>(1,count)));
}
template<class Work> void parallel(std::size_t count,unsigned requested,Work work) {
    const auto workers=worker_count(requested,count);
    std::atomic<std::size_t> next{0}; std::exception_ptr error; std::mutex lock;
    auto run=[&](unsigned worker) {
        try { for(;;) { auto i=next.fetch_add(1); if(i>=count)break; work(i,worker); } }
        catch(...) { next.store(count);std::lock_guard guard(lock);if(!error)error=std::current_exception(); }
    };
    std::vector<std::jthread> pool;
    for(unsigned i=1;i<workers;++i)pool.emplace_back(run,i);
    run(0);for(auto& thread:pool)thread.join();if(error)std::rethrow_exception(error);
}
std::uint64_t frame_epoch(const Format& f,unsigned frame) {
    const auto elapsed=static_cast<long double>(frame)*(f.burst_wait_frames+1)*f.dwells_per_frame*f.dwell_seconds;
    require(elapsed>=0 && elapsed<=std::numeric_limits<std::uint64_t>::max()-f.epoch,"triad epoch overflow");
    return f.epoch+static_cast<std::uint64_t>(std::floor(elapsed));
}
std::vector<std::uint8_t> masks(const Format& f,const Crypto& key,std::size_t bits) {
    std::vector<std::uint8_t> result(bits,0);if(!f.encrypt)return result;
    const auto child=experiment_key(key,"payload");
    const auto bytes=child.stream(StreamPurpose::Data,f.epoch,0,(bits+7)/8);
    for(std::size_t i=0;i<bits;++i)result[i]=(bytes[i/8]>>(i%8))&1U;
    return result;
}
std::vector<Alternatives> patterns(const Format& f,const Crypto& key,const std::vector<Cell>& cells) {
    const auto g=geometry(f); const auto groups=g.cells_per_dwell/(f.bits_per_frame*f.added_symbols);
    const auto bases=static_cast<std::size_t>(groups)*f.dwells_per_frame;
    const auto child=experiment_key(key,"patterns");
    const auto added=experiment_key(key,"added-symbols");
    std::vector<Alternatives> result(cells.size());
    unsigned frame=std::numeric_limits<unsigned>::max();Bytes bytes,rotations;
    constexpr std::array<std::array<unsigned,3>,6> permutations{{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}}};
    const std::array<double,3> amplitudes{std::sqrt(.4),1,std::sqrt(1.6)};
    for(std::size_t i=0;i<cells.size();++i) {
        const auto& cell=cells[i];
        if(cell.frame!=frame) {
            frame=cell.frame;const auto epoch=frame_epoch(f,frame);
            bytes=child.stream(StreamPurpose::Scrambler,epoch,0,product(product(bases,f.bits_per_frame),8));
            rotations=added.stream(StreamPurpose::Dsss,epoch,0,product(product(bases,f.bits_per_frame),f.added_symbols));
        }
        const auto base=(cell.bit%f.bits_per_frame)*bases+cell.logical_cell;
        const auto offset=base*8;
        const auto& permutation=permutations[bytes[offset]%6];
        const auto outer=f.added_symbols>1?rotations[base*f.added_symbols+cell.added_index]%4:0;
        for(unsigned bit=0;bit<2;++bit)for(unsigned c=0;c<3;++c) {
            const auto amplitude=amplitudes[permutation[(c+bit)%3]];
            const auto phase=(bytes[offset+1+3*bit+c]%8)*std::numbers::pi/4+outer*std::numbers::pi/2;
            result[i][bit][c]=std::polar(amplitude,phase);
        }
    }
    return result;
}
Complex interpolate(std::span<const Complex> samples,double position) {
    if(!(position>=0) || position>=static_cast<double>(samples.size()))return {};
    const auto i=static_cast<std::size_t>(position);const auto fraction=position-i;
    return samples[i]*(1-fraction)+(i+1<samples.size()?samples[i+1]*fraction:Complex{});
}
template<class T> std::complex<T> spectral(std::span<const std::complex<T>> values,double bin) {
    const auto size=static_cast<long long>(values.size());
    const auto lower=static_cast<long long>(std::floor(bin));const auto fraction=static_cast<T>(bin-lower);
    auto wrap=[&](long long n) {n%=size;if(n<0)n+=size;return static_cast<std::size_t>(n);};
    return values[wrap(lower)]*(1-fraction)+values[wrap(lower+1)]*fraction;
}
struct Candidate {std::vector<std::uint8_t> bits;std::vector<double> scores,margins;double score=0,margin=0;};
template<class T> Candidate score_bank(std::span<const std::complex<T>> observations,
    const std::vector<Cell>& cells,const std::vector<Alternatives>& bank,
    std::size_t bit_count,unsigned frame_bits,const SearchPrior& p,Receiver receiver) {
    Candidate result;result.bits.resize(bit_count);result.scores.resize(bit_count);result.margins.resize(bit_count);
    std::vector<std::array<double,2>> totals(bit_count);std::vector<double> weights(bit_count);
    // Segments never cross a physical dwell (including FHSS boundaries).
    std::size_t begin=0;
    while(begin<cells.size()) {
        auto end=begin+1;while(end<cells.size()&&cells[end].frame==cells[begin].frame&&cells[end].dwell==cells[begin].dwell)++end;
        const auto first_bit=static_cast<std::size_t>(cells[begin].frame)*frame_bits;
        for(auto bit=first_bit;bit<std::min(bit_count,first_bit+frame_bits);++bit) {
            std::vector<std::size_t> selected;
            for(auto i=begin;i<end;++i)if(cells[i].bit==bit)selected.push_back(i);
            for(std::size_t first=0;first<selected.size();) {
                const auto remaining=selected.size()-first;
                const auto n=remaining<=p.segment_cells+2?remaining:static_cast<std::size_t>(p.segment_cells);if(n<3)break;
                for(unsigned candidate=0;candidate<2;++candidate) {
                    double sum=0;unsigned used=0;
                    const auto components=receiver==Receiver::differential?2U:3U;
                    for(unsigned component=0;component<components;++component) {
                        Complex cross{};double observed_energy=0,reference_energy=0;
                        if(receiver==Receiver::amplitude_only) {
                            double sx=0,sy=0,sxx=0,syy=0,sxy=0;
                            for(std::size_t k=0;k<n;++k) {
                                const auto index=selected[first+k];
                                const auto x=static_cast<double>(std::norm(observations[index*3+component]));
                                const auto y=std::norm(bank[index][candidate][component]);
                                sx+=x;sy+=y;sxx+=x*x;syy+=y*y;sxy+=x*y;
                            }
                            const auto vx=sxx-sx*sx/n,vy=syy-sy*sy/n,xy=sxy-sx*sy/n;
                            if(vx>1e-30&&vy>1e-30) {const auto corr=xy/std::sqrt(vx*vy);sum+=std::max(0.,corr)*std::max(0.,corr);++used;}
                        } else {
                            for(std::size_t k=0;k<n;++k) {
                                const auto index=selected[first+k];Complex observed,reference;
                                if(receiver==Receiver::differential) {
                                    observed=static_cast<Complex>(observations[index*3+component])*std::conj(static_cast<Complex>(observations[index*3+component+1]));
                                    reference=bank[index][candidate][component]*std::conj(bank[index][candidate][component+1]);
                                } else {observed=static_cast<Complex>(observations[index*3+component]);reference=bank[index][candidate][component];}
                                cross+=observed*std::conj(reference);observed_energy+=std::norm(observed);reference_energy+=std::norm(reference);
                            }
                            if(observed_energy>1e-30&&reference_energy>1e-30) {sum+=std::norm(cross)/(observed_energy*reference_energy);++used;}
                        }
                    }
                    const auto baseline=receiver==Receiver::amplitude_only?.5/static_cast<double>(n-1):1./n;
                    const auto normalized=used?(sum/used-baseline)/(1-baseline):0.;
                    totals[bit][candidate]+=n*normalized;
                }
                weights[bit]+=n;first+=n;
            }
        }
        begin=end;
    }
    for(std::size_t bit=0;bit<bit_count;++bit) {
        if(weights[bit]) {totals[bit][0]/=weights[bit];totals[bit][1]/=weights[bit];}
        result.bits[bit]=totals[bit][1]>totals[bit][0];
        result.scores[bit]=std::max(totals[bit][0],totals[bit][1]);
        result.margins[bit]=std::abs(totals[bit][1]-totals[bit][0]);
        result.score+=result.scores[bit]/bit_count;result.margin+=result.margins[bit]/bit_count;
    }
    return result;
}
template<class T,class Plan> Reception search(std::span<const Complex> samples,std::size_t bit_count,
    const Format& f,const Crypto& key,const SearchPrior& prior,Receiver receiver,const Geometry& g,
    const std::vector<Cell>& cells,std::size_t nfft,std::size_t frequency_count,double frequency_step) {
    const auto frame_count=1+(bit_count-1)/f.bits_per_frame;
    const auto duration=(static_cast<double>(frame_count-1)*(f.burst_wait_frames+1)+1)*g.frame_seconds;
    const auto complete=[&](double clock,double timing) {
        const auto endpoint=timing+duration/(1+clock*1e-6);
        return timing>=0&&endpoint<=static_cast<double>(samples.size())/f.sample_rate+.5/f.sample_rate;
    };
    bool any_complete=false;
    for(auto clock:prior.clock_offsets_ppm)for(auto timing:prior.timing_offsets_seconds)any_complete|=complete(clock,timing);
    if(!any_complete) {Reception incomplete;incomplete.arithmetic_used=std::is_same_v<T,float>?Arithmetic::fp32:Arithmetic::fp64;return incomplete;}
    const auto workers=worker_count(prior.threads,cells.size());
    const auto fft_bytes=product(product(nfft,workers+1),sizeof(std::complex<T>));
    const auto one_bank_bytes=product(product(cells.size(),3),sizeof(std::complex<T>))+product(bit_count,2*sizeof(double)+sizeof(std::uint8_t))+sizeof(Candidate);
    const auto base_bytes=fft_bytes+product(cells.size(),sizeof(Cell)+sizeof(Alternatives))+product(bit_count,96)+product(product(cells.size(),workers),sizeof(std::size_t));
    require(prior.memory_limit_bytes>base_bytes && one_bank_bytes<=prior.memory_limit_bytes-base_bytes,
            "triad memory cap cannot fit one projection bank");
    const auto batch_size=std::min(frequency_count,(prior.memory_limit_bytes-base_bytes)/one_bank_bytes);
    const auto frequency_first=-.5*static_cast<double>(frequency_count-1)*frequency_step;
    Plan plan(nfft);
    std::vector<std::vector<std::complex<T>>> scratch(workers,std::vector<std::complex<T>>(nfft));
    Reception result;result.score=-std::numeric_limits<double>::infinity();
    result.frequency_banks=frequency_count;result.threads_used=workers;result.complete_observation=true;
    result.working_bytes=base_bytes+product(batch_size,one_bank_bytes);
    result.arithmetic_used=std::is_same_v<T,float>?Arithmetic::fp32:Arithmetic::fp64;
    for(const auto epoch_offset:prior.epoch_offsets_seconds) {
        Format addressed=f;
        if(epoch_offset<0) {require(static_cast<std::uint64_t>(-(epoch_offset+1))+1<=f.epoch,"triad negative epoch underflow");addressed.epoch-=static_cast<std::uint64_t>(-(epoch_offset+1))+1;}
        else {require(static_cast<std::uint64_t>(epoch_offset)<=std::numeric_limits<std::uint64_t>::max()-f.epoch,"triad epoch overflow");addressed.epoch+=static_cast<std::uint64_t>(epoch_offset);}
        const auto bank=patterns(addressed,key,cells);
        for(const auto clock:prior.clock_offsets_ppm)for(const auto timing:prior.timing_offsets_seconds) {
            if(!complete(clock,timing))continue;
            const auto rate=1+clock*1e-6;const auto chip_samples=static_cast<double>(f.sample_rate)/(g.chip_rate_hz*rate);
            for(std::size_t start=0;start<frequency_count;start+=batch_size) {
                const auto count=std::min(batch_size,frequency_count-start);
                std::vector<std::complex<T>> projected(product(product(count,cells.size()),3));
                parallel(cells.size(),workers,[&](std::size_t index,unsigned worker) {
                    auto& fft=scratch[worker];const auto center_seconds=cells[index].center_seconds/rate+timing;
                    const auto center=center_seconds*f.sample_rate;
                    double energy=0;
                    for(std::size_t j=0;j<nfft;++j) {
                        const auto offset=j<=nfft/2?static_cast<double>(j):static_cast<double>(j)-nfft;
                        const auto pulse=modem::pattern_pulse(offset/chip_samples);energy+=pulse*pulse;
                        const auto value=pulse*interpolate(samples,center+offset);
                        fft[j]={static_cast<T>(value.real()),static_cast<T>(value.imag())};
                    }
                    modem::detail::search_fft::fft(fft,false,plan);
                    for(std::size_t local=0;local<count;++local) {
                        const auto frequency=frequency_first+(start+local)*frequency_step;
                        for(unsigned carrier=0;carrier<3;++carrier) {
                            const auto carrier_frequency=(static_cast<double>(carrier)-1)*g.carrier_spacing_hz;
                            const auto bin=(frequency+carrier_frequency*rate)*nfft/f.sample_rate;
                            auto observation=spectral<T>(fft,bin)/static_cast<T>(energy);
                            const auto phase=-tau*(frequency*center_seconds+carrier_frequency*cells[index].center_seconds);
                            const auto rotation=std::polar(1.,std::remainder(phase,tau));
                            observation*=std::complex<T>(static_cast<T>(rotation.real()),static_cast<T>(rotation.imag()));
                            projected[(local*cells.size()+index)*3+carrier]=observation;
                        }
                    }
                });
                result.fft_count+=cells.size();result.projection_values+=product(product(count,cells.size()),3);
                std::vector<Candidate> scored(count);
                parallel(count,prior.threads,[&](std::size_t local,unsigned) {
                    const auto row=std::span<const std::complex<T>>(projected).subspan(local*cells.size()*3,cells.size()*3);
                    scored[local]=score_bank<T>(row,cells,bank,bit_count,f.bits_per_frame,prior,receiver);
                });
                for(std::size_t local=0;local<count;++local) {
                    ++result.hypotheses;auto& candidate=scored[local];
                    if(candidate.score>result.score) {
                        result.score=candidate.score;result.margin=candidate.margin;result.bits=std::move(candidate.bits);
                        result.bit_scores=std::move(candidate.scores);result.bit_margins=std::move(candidate.margins);
                        result.frequency_hz=frequency_first+(start+local)*frequency_step;result.timing_seconds=timing;
                        result.clock_ppm=clock;result.epoch_offset_seconds=epoch_offset;
                    }
                }
            }
        }
    }
    Format decoded=f;
    if(result.epoch_offset_seconds<0)decoded.epoch-=static_cast<std::uint64_t>(-(result.epoch_offset_seconds+1))+1;
    else decoded.epoch+=static_cast<std::uint64_t>(result.epoch_offset_seconds);
    const auto mask=masks(decoded,key,bit_count);for(std::size_t bit=0;bit<bit_count;++bit)result.bits[bit]^=mask[bit];
    // Every locally configured source bit must have evidence. These empirical
    // gates are not calibrated probabilities or a false-alarm certificate.
    result.accepted=result.complete_observation &&
        std::all_of(result.bit_scores.begin(),result.bit_scores.end(),[&](double score){return score>=prior.minimum_score;}) &&
        std::all_of(result.bit_margins.begin(),result.bit_margins.end(),[&](double margin){return margin>=prior.minimum_margin;});
    return result;
}
} // namespace

Crypto experiment_key(const Crypto& key,const char* purpose) {
    require(purpose&&*purpose,"triad experiment key requires a domain");
    const auto text=std::string("datapump/experimental-triad/v1/")+purpose;
    return Crypto(key.mac(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),text.size())));
}
Geometry geometry(const Format& f) {
    require(std::isfinite(f.aggregate_rate_hz)&&f.aggregate_rate_hz>=600&&f.aggregate_rate_hz<=360000,
            "triad experimental aggregate rate must be between 600 and 360000 Hz");
    require(f.sample_rate>=64,"triad sample rate is too low");
    require(f.dwells_per_frame>=20&&f.dwells_per_frame<=10000,"triad requires at least twenty dwells");
    require(std::isfinite(f.dwell_seconds)&&std::abs(f.dwell_seconds-.4)<1e-12,"triad dwell must be 400 ms");
    require(f.bits_per_frame>=1&&f.bits_per_frame<=256,"triad invalid bits per frame");
    require(f.added_symbols>=1&&f.added_symbols<=1024,"triad invalid Added Symbols factor");
    require(f.burst_wait_frames<=1000,"triad burst wait exceeds experimental limit");
    require(std::isfinite(f.ramp_seconds)&&f.ramp_seconds>=.005&&std::isfinite(f.margin_seconds)&&f.margin_seconds>=.01,
            "triad requires finite ramp and timing margins");
    Geometry g;g.chip_rate_hz=f.aggregate_rate_hz/6;g.carrier_spacing_hz=1.25*g.chip_rate_hz;
    g.occupied_bandwidth_hz=3*g.carrier_spacing_hz;
    const auto dwell_chips=f.dwell_seconds*g.chip_rate_hz;
    require(std::abs(dwell_chips-std::round(dwell_chips))<1e-8,
            "triad covered format requires Rate to be an integer multiple of 15 Hz");
    g.frame_seconds=f.dwells_per_frame*f.dwell_seconds;
    require(g.frame_seconds>=8,"triad frame must span at least eight seconds");
    require(f.sample_rate>=4*g.chip_rate_hz && f.sample_rate>1.1*g.occupied_bandwidth_hz,
            "triad sample rate cannot contain the shaped carriers");
    const auto available=(f.dwell_seconds-2*(f.ramp_seconds+f.margin_seconds))*g.chip_rate_hz-16;
    require(available>=0,"triad pulse support cannot fit dwell margins");
    const auto cells=static_cast<unsigned>(std::floor(available+1e-8))+1;
    const auto unit=f.bits_per_frame*f.added_symbols;
    g.cells_per_dwell=cells/unit*unit;
    require(g.cells_per_dwell>=3*unit,"triad needs at least three pattern groups per bit per dwell");
    const auto sample_extent=static_cast<long double>(g.frame_seconds)*f.sample_rate;
    require(sample_extent<=std::numeric_limits<std::size_t>::max(),"triad frame sample overflow");
    g.frame_samples=static_cast<std::size_t>(std::llround(sample_extent));return g;
}
std::vector<Cell> layout(const Format& f,std::size_t bit_count,const Crypto&) {
    const auto g=geometry(f);require(bit_count>0&&bit_count<=65536,"triad study bit count must be 1..65536");
    const auto frames=1+(bit_count-1)/f.bits_per_frame;
    const auto total=product(product(frames,f.dwells_per_frame),g.cells_per_dwell);
    require(total<=10000000,"triad layout exceeds experimental cell cap");
    std::vector<Cell> result;result.reserve(total);
    const auto groups=g.cells_per_dwell/(f.bits_per_frame*f.added_symbols);
    for(unsigned frame=0;frame<frames;++frame)for(unsigned dwell=0;dwell<f.dwells_per_frame;++dwell) {
        const auto start=(static_cast<double>(frame)*(f.burst_wait_frames+1)*f.dwells_per_frame+dwell)*f.dwell_seconds;
        const auto first=f.ramp_seconds+f.margin_seconds+8/g.chip_rate_hz;
        for(unsigned slot=0;slot<g.cells_per_dwell;++slot) {
            const auto logical=f.added_interleave?(slot% (g.cells_per_dwell/f.added_symbols))*f.added_symbols+slot/(g.cells_per_dwell/f.added_symbols):slot;
            const auto bit=(logical/f.added_symbols+dwell)%f.bits_per_frame;
            const auto group=logical/(f.added_symbols*f.bits_per_frame);
            result.push_back({start+first+slot/g.chip_rate_hz,static_cast<std::size_t>(frame)*f.bits_per_frame+bit,
                frame,dwell,slot,dwell*groups+group,logical%f.added_symbols});
        }
    }
    return result;
}
Waveform transmit(std::span<const std::uint8_t> bits,const Format& f,const Crypto& key) {
    const auto g=geometry(f);for(auto bit:bits)require(bit<=1,"triad source contains a non-bit value");
    Waveform result;result.source_bits=bits.size();result.cells=layout(f,bits.size(),key);
    const auto frames=1+(bits.size()-1)/f.bits_per_frame;
    const auto duration=(static_cast<double>(frames-1)*(f.burst_wait_frames+1)+1)*g.frame_seconds;
    require(duration*f.sample_rate<=100000000,"triad sampled waveform exceeds experimental sample cap");
    result.source_end_sample=static_cast<std::size_t>(std::llround(duration*f.sample_rate));result.samples.resize(result.source_end_sample);
    const auto bank=patterns(f,key,result.cells);const auto mask=masks(f,key,bits.size());
    const auto pad=experiment_key(key,"padding").stream(StreamPurpose::Data,f.epoch,0,(frames*f.bits_per_frame+7)/8);
    const auto chip_samples=f.sample_rate/g.chip_rate_hz;constexpr double gain=.5773502691896257645;
    const auto chips_per_dwell=static_cast<std::size_t>(std::llround(f.dwell_seconds*g.chip_rate_hz));
    const auto chips_per_frame=product(chips_per_dwell,f.dwells_per_frame);
    const auto first_payload=(f.ramp_seconds+f.margin_seconds)*g.chip_rate_hz+8;
    const auto first_payload_integer=static_cast<std::size_t>(std::floor(first_payload+1e-10));
    const auto raw_chip_phase=first_payload-static_cast<double>(first_payload_integer);
    const auto chip_phase=std::abs(raw_chip_phase)<1e-9?0.:raw_chip_phase;
    const auto cover_key=experiment_key(key,"cover");
    constexpr std::array<std::array<unsigned,3>,6> permutations{{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}}};
    const std::array<double,3> amplitudes{std::sqrt(.4),1,std::sqrt(1.6)};
    for(unsigned frame=0;frame<frames;++frame) {
        const auto frame_start=static_cast<double>(frame)*(f.burst_wait_frames+1)*g.frame_seconds;
        const auto frame_end=frame_start+g.frame_seconds;
        const auto cover=cover_key.stream(StreamPurpose::Scrambler,frame_epoch(f,frame),0,product(chips_per_frame,4));
        for(std::size_t chip=0;chip<chips_per_frame;++chip) {
            const auto dwell=chip/chips_per_dwell;
            const auto local=chip%chips_per_dwell;
            Chip values{};
            if(local>=first_payload_integer && local-first_payload_integer<g.cells_per_dwell) {
                const auto slot=local-first_payload_integer;
                const auto i=(static_cast<std::size_t>(frame)*f.dwells_per_frame+dwell)*g.cells_per_dwell+slot;
                const auto bit_index=result.cells[i].bit;
                const auto bit=bit_index<bits.size()?bits[bit_index]^mask[bit_index]:(pad[bit_index/8]>>(bit_index%8))&1U;
                values=bank[i][bit];
            } else {
                const auto offset=chip*4;
                const auto& permutation=permutations[cover[offset]%6];
                for(unsigned carrier=0;carrier<3;++carrier)
                    values[carrier]=std::polar(amplitudes[permutation[carrier]],(cover[offset+1+carrier]%8)*std::numbers::pi/4);
            }
            const auto center=(frame_start+(static_cast<double>(chip)+chip_phase)/g.chip_rate_hz)*f.sample_rate;
            const auto earliest=f.burst_wait_frames?frame_start*f.sample_rate:0.;
            const auto latest=f.burst_wait_frames?frame_end*f.sample_rate:static_cast<double>(result.samples.size());
            const auto first=std::max(earliest,std::ceil(center-8*chip_samples));
            const auto end=std::min(latest,std::floor(center+8*chip_samples)+1);
            for(auto sample=static_cast<std::size_t>(first);sample<end;++sample) {
                const auto pulse=modem::pattern_pulse((static_cast<double>(sample)-center)/chip_samples)*gain;
                for(unsigned carrier=0;carrier<3;++carrier) {
                    const auto phase=tau*(static_cast<double>(carrier)-1)*g.carrier_spacing_hz*static_cast<double>(sample)/f.sample_rate;
                    result.samples[sample]+=values[carrier]*pulse*std::polar(1.,phase);
                }
            }
        }
    }
    // One transmitter envelope, applied before propagation. No per-dwell gate
    // exists without FHSS. Burst waits remain literal silence; endpoint ramps
    // avoid truncating the continuous cover at an on-air transmission edge.
    for(std::size_t sample=0;sample<result.samples.size();++sample) {
        const auto time=static_cast<double>(sample)/f.sample_rate;
        double position=time,length=duration;
        if(f.fhss) {position=std::fmod(time,f.dwell_seconds);length=f.dwell_seconds;}
        else if(f.burst_wait_frames) {position=std::fmod(time,(f.burst_wait_frames+1)*g.frame_seconds);length=g.frame_seconds;}
        auto envelope=1.;
        if(position<f.ramp_seconds)envelope=.5-.5*std::cos(std::numbers::pi*position/f.ramp_seconds);
        if(position>length-f.ramp_seconds)envelope=std::min(envelope,.5-.5*std::cos(std::numbers::pi*std::max(0.,length-position)/f.ramp_seconds));
        result.samples[sample]*=envelope;
    }
    for(const auto value:result.samples) {result.mean_power+=std::norm(value)/result.samples.size();result.peak_power=std::max(result.peak_power,std::norm(value));}
    return result;
}
Reception receive(std::span<const Complex> samples,std::size_t bit_count,
    const Format& f,const Crypto& key,const SearchPrior& prior,Receiver receiver) {
    const auto started=std::chrono::steady_clock::now();const auto cpu=std::clock();
    const auto g=geometry(f);
    require(bit_count>0&&bit_count<=65536,"triad study bit count must be 1..65536");
    const auto expected_cells=product(product(1+(bit_count-1)/f.bits_per_frame,f.dwells_per_frame),g.cells_per_dwell);
    require(expected_cells<=prior.memory_limit_bytes/(sizeof(Cell)+sizeof(Alternatives)+3*sizeof(Complex)),
            "triad memory cap cannot fit layout and one observation bank");
    const auto cells=layout(f,bit_count,key);
    require(!samples.empty(),"triad receiver has no capture");
    require(std::isfinite(prior.frequency_half_width_hz)&&prior.frequency_half_width_hz>=0,"triad invalid frequency search bound");
    for(const auto value:samples)require(std::isfinite(value.real())&&std::isfinite(value.imag()),"triad capture contains a nonfinite sample");
    require(std::isfinite(prior.minimum_score)&&prior.minimum_score>=0&&prior.minimum_score<=1&&
            std::isfinite(prior.minimum_margin)&&prior.minimum_margin>=0&&prior.minimum_margin<=2,
            "triad invalid empirical acceptance gate");
    require(receiver==Receiver::differential||receiver==Receiver::segmented_complex||receiver==Receiver::amplitude_only,
            "triad invalid receiver algorithm");
    require(prior.arithmetic==Arithmetic::automatic||prior.arithmetic==Arithmetic::fp32||prior.arithmetic==Arithmetic::fp64,
            "triad invalid search arithmetic");
    require(prior.segment_cells>=3&&prior.segment_cells<=4096,"triad invalid score segment length");
    require(prior.threads>=1&&prior.threads<=256,"triad invalid worker count");
    require(!prior.timing_offsets_seconds.empty()&&!prior.clock_offsets_ppm.empty()&&!prior.epoch_offsets_seconds.empty(),"triad empty search grid");
    double minimum_rate=1;
    for(auto clock:prior.clock_offsets_ppm) {require(std::isfinite(clock)&&std::abs(clock)<=10000,"triad invalid clock hypothesis");minimum_rate=std::min(minimum_rate,1+clock*1e-6);}
    for(auto time:prior.timing_offsets_seconds)require(std::isfinite(time)&&std::abs(time)<=86400,"triad invalid timing hypothesis");
    const auto step=prior.frequency_step_hz==0?g.chip_rate_hz/8:prior.frequency_step_hz;
    require(std::isfinite(step)&&step>0,"triad invalid frequency search step");
    const auto half=std::ceil(prior.frequency_half_width_hz/step);
    require(half<=1000000,"triad frequency bank exceeds experimental cap");
    const auto frequency_count=static_cast<std::size_t>(half)*2+1;
    const auto hypotheses=product(product(product(frequency_count,prior.timing_offsets_seconds.size()),prior.clock_offsets_ppm.size()),prior.epoch_offsets_seconds.size());
    require(hypotheses<=prior.maximum_hypotheses,"triad declared search exceeds hypothesis cap");
    // Full declared CFO hull, shaped occupied band and clock uncertainty must
    // fit capture. No modulo-frequency alias or simulator-truth centering.
    const auto maximum_rate=1+*std::max_element(prior.clock_offsets_ppm.begin(),prior.clock_offsets_ppm.end())*1e-6;
    require(half*step+.5*g.occupied_bandwidth_hz*maximum_rate<.49*f.sample_rate,
            "triad capture bandwidth does not contain declared frequency search");
    const auto support=std::ceil(16*f.sample_rate/(g.chip_rate_hz*minimum_rate));
    require(support<static_cast<double>(std::numeric_limits<std::size_t>::max()/2),"triad FFT support overflow");
    const auto nfft=std::bit_ceil(static_cast<std::size_t>(support));
    auto arithmetic=prior.arithmetic;
    if(arithmetic==Arithmetic::automatic)arithmetic=modem::detail::search_fft::float_input_safe(samples,nfft)?Arithmetic::fp32:Arithmetic::fp64;
    Reception result;
    if(arithmetic==Arithmetic::fp64)result=search<double,modem::detail::search_fft::DoublePlan>(samples,bit_count,f,key,prior,receiver,g,cells,nfft,frequency_count,step);
    else {require(modem::detail::search_fft::float_input_safe(samples,nfft),"triad FP32 capture outside safe FFT envelope");result=search<float,modem::detail::search_fft::FloatPlan>(samples,bit_count,f,key,prior,receiver,g,cells,nfft,frequency_count,step);}
    result.wall_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    result.cpu_seconds=static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC;return result;
}
const char* receiver_name(Receiver r) {switch(r) {case Receiver::differential:return "differential";case Receiver::segmented_complex:return "segmented-complex";case Receiver::amplitude_only:return "amplitude-only";}return "unknown";}
const char* arithmetic_name(Arithmetic a) {switch(a) {case Arithmetic::automatic:return "automatic";case Arithmetic::fp32:return "fp32";case Arithmetic::fp64:return "fp64";}return "unknown";}
} // namespace datapump::triad

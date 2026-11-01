#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_statistics.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <limits>
#include <mutex>
#include <numbers>
#include <numeric>
#include <initializer_list>
#include <thread>

namespace datapump::triad::v3 {
namespace {
constexpr std::size_t maximum_columns=16;
constexpr long double tau=2*std::numbers::pi_v<long double>;
void require(bool condition,const char* message){if(!condition)throw Error(message);}
std::uint64_t checked_add(std::uint64_t a,std::uint64_t b){require(b<=UINT64_MAX-a,"triad v3 sample coordinate overflow");return a+b;}
std::size_t checked_multiply(std::size_t a,std::size_t b){require(!a||b<=SIZE_MAX/a,"triad v3 workspace extent overflow");return a*b;}
std::size_t checked_sum(std::initializer_list<std::size_t> values){std::size_t result=0;for(auto value:values){require(value<=SIZE_MAX-result,"triad v3 workspace sum overflow");result+=value;}return result;}
unsigned worker_count(unsigned requested,std::size_t count){return static_cast<unsigned>(std::min<std::size_t>(std::max(1U,requested),std::max<std::size_t>(1,count)));}
template<class Work>void parallel(std::size_t count,unsigned requested,Work work){
    std::atomic<std::size_t> next{0};std::exception_ptr error;std::mutex mutex;
    auto run=[&](unsigned worker){try{for(;;){const auto index=next.fetch_add(1);if(index>=count)break;work(index,worker);}}catch(...){next.store(count);std::lock_guard lock(mutex);if(!error)error=std::current_exception();}};
    const auto workers=worker_count(requested,count);std::vector<std::jthread> threads;
    for(unsigned worker=1;worker<workers;++worker)threads.emplace_back(run,worker);
    run(0);for(auto& thread:threads)thread.join();if(error)std::rethrow_exception(error);
}
std::uint64_t epoch_with_offset(std::uint64_t epoch,std::int64_t offset){
    if(offset<0){const auto magnitude=static_cast<std::uint64_t>(-(offset+1))+1;require(magnitude<=epoch,"triad v3 candidate epoch underflow");return epoch-magnitude;}
    return checked_add(epoch,static_cast<std::uint64_t>(offset));
}
void finite(Complex value){require(std::isfinite(value.real())&&std::isfinite(value.imag()),"triad v3 nonfinite projection input");}
struct Sum {
    double value=0,error=0;
    void add(double term){const auto adjusted=term-error;const auto next=value+adjusted;error=(next-value)-adjusted;value=next;require(std::isfinite(value),"triad v3 projection accumulator overflow");}
};
struct Fit {
    std::size_t rank=0;
    double projection=0,input=0;
    std::vector<Complex> coefficients;
};
// Append rows with complex Givens rotations. z remains linear in observed
// samples; no noisy carrier products or per-row squares enter the detector.
struct RowQr {
    std::size_t columns=0;
    std::array<Complex,maximum_columns*maximum_columns> r{};
    std::array<Complex,maximum_columns> z{};
    Sum input;
    std::uint64_t rows=0;
    explicit RowQr(std::size_t count=0):columns(count){}
    void append(std::span<const Complex> row,Complex observed){
        finite(observed);std::array<Complex,maximum_columns> work{};
        for(std::size_t j=0;j<columns;++j){finite(row[j]);work[j]=row[j];}
        auto rhs=observed;
        for(std::size_t j=0;j<columns;++j){
            const auto diagonal=r[j*maximum_columns+j],below=work[j];
            const auto a=std::abs(diagonal),b=std::abs(below);
            if(b==0)continue;
            const auto length=std::hypot(a,b);require(std::isfinite(length),"triad v3 QR magnitude overflow");
            const auto phase=a>0?diagonal/a:Complex{1,0};
            const auto cosine=a/length;const auto sine=phase*std::conj(below)/length;
            for(std::size_t k=j;k<columns;++k){const auto old=r[j*maximum_columns+k];r[j*maximum_columns+k]=cosine*old+sine*work[k];work[k]=-std::conj(sine)*old+cosine*work[k];}
            const auto old=z[j];z[j]=cosine*old+sine*rhs;rhs=-std::conj(sine)*old+cosine*rhs;
        }
        const auto energy=std::norm(observed);require(std::isfinite(energy),"triad v3 sample energy overflow");input.add(energy);++rows;
    }
    Fit finish(double tolerance)const{
        Fit result;result.input=input.value;result.coefficients.assign(columns,{});
        if(!rows||!columns)return result;
        // Rank revelation on the small R, using pivoted twice-reorthogonalized
        // QR. This avoids forming A^H A and keeps rank selection independent y.
        auto v=r;std::array<Complex,maximum_columns*maximum_columns> q{},upper{};
        std::array<Complex,maximum_columns> coordinates{};
        std::array<std::size_t,maximum_columns> permutation{};
        std::iota(permutation.begin(),permutation.end(),0);
        double initial=0;
        for(std::size_t c=0;c<columns;++c){double norm=0;for(std::size_t row=0;row<columns;++row)norm+=std::norm(v[row*maximum_columns+c]);initial=std::max(initial,std::sqrt(norm));}
        if(initial==0)return result;
        for(std::size_t k=0;k<columns;++k){
            auto pivot=k;double best=-1;
            for(std::size_t c=k;c<columns;++c){double norm=0;for(std::size_t row=0;row<columns;++row)norm+=std::norm(v[row*maximum_columns+c]);if(norm>best){best=norm;pivot=c;}}
            if(!(best>0)||std::sqrt(best)<=initial*tolerance)break;
            if(pivot!=k){for(std::size_t row=0;row<columns;++row)std::swap(v[row*maximum_columns+k],v[row*maximum_columns+pivot]);for(std::size_t row=0;row<k;++row)std::swap(upper[row*maximum_columns+k],upper[row*maximum_columns+pivot]);std::swap(permutation[k],permutation[pivot]);}
            const auto length=std::sqrt(best);upper[k*maximum_columns+k]=length;
            for(std::size_t row=0;row<columns;++row)q[row*maximum_columns+k]=v[row*maximum_columns+k]/length;
            for(std::size_t row=0;row<columns;++row)coordinates[k]+=std::conj(q[row*maximum_columns+k])*z[row];
            result.projection+=std::norm(coordinates[k]);++result.rank;
            for(std::size_t c=k+1;c<columns;++c){for(unsigned pass=0;pass<2;++pass){Complex dot{};for(std::size_t row=0;row<columns;++row)dot+=std::conj(q[row*maximum_columns+k])*v[row*maximum_columns+c];upper[k*maximum_columns+c]+=dot;for(std::size_t row=0;row<columns;++row)v[row*maximum_columns+c]-=q[row*maximum_columns+k]*dot;}}
        }
        std::array<Complex,maximum_columns> solution{};
        for(auto k=result.rank;k>0;--k){const auto row=k-1;auto rhs=coordinates[row];for(std::size_t c=row+1;c<result.rank;++c)rhs-=upper[row*maximum_columns+c]*solution[c];solution[row]=rhs/upper[row*maximum_columns+row];}
        for(std::size_t c=0;c<result.rank;++c)result.coefficients[permutation[c]]=solution[c];
        require(std::isfinite(result.projection),"triad v3 nonfinite projected energy");
        // QR roundoff can put explained energy a few ulps above input energy.
        require(result.projection<=result.input+1e-9*std::max(result.input,std::numeric_limits<double>::min()),"triad v3 QR violates projection energy bound");
        result.projection=std::min(result.projection,result.input);return result;
    }
};
std::size_t column_count(const Candidate& candidate){return checked_multiply(candidate.channel.taps.size(),candidate.channel.linear_gain?2:1);}
} // namespace

CandidateStatistics project_block(std::span<const Complex> observations,
    std::span<const Complex> basis,std::size_t columns,double noise,double tolerance){
    require(columns>0&&columns<=maximum_columns,"triad v3 invalid block projection rank");
    require(basis.size()==checked_multiply(observations.size(),columns),"triad v3 block projection extent mismatch");
    require(noise>0&&std::isfinite(noise)&&tolerance>0&&tolerance<1&&std::isfinite(tolerance),"triad v3 invalid block projection variance/tolerance");
    RowQr qr(columns);std::array<Complex,maximum_columns> row{};
    for(std::size_t sample=0;sample<observations.size();++sample){for(std::size_t column=0;column<columns;++column)row[column]=basis[column*observations.size()+sample];qr.append(std::span(row).first(columns),observations[sample]);}
    const auto fit=qr.finish(tolerance);CandidateStatistics result;result.projection_energy=fit.projection;result.input_energy=fit.input;result.residual_energy=std::max(0.,fit.input-fit.projection);result.normalized_score=fit.projection/noise;result.rank=fit.rank;result.rows=observations.size();result.blocks=1;result.rank_deficient_blocks=fit.rank<columns;result.last_channel_coefficients=fit.coefficients;return result;
}

void shaped_basis(PatternSource& source,const Candidate& candidate,double sample_rate,
                  std::uint64_t first_sample,std::size_t rows,std::span<Complex> output){
    const auto columns=column_count(candidate);require(output.size()==checked_multiply(columns,rows),"triad v3 shaped basis extent mismatch");
    require(std::isfinite(sample_rate)&&sample_rate>0,"triad v3 invalid basis sample rate");
    const auto rate=1+candidate.clock_ppm*1e-6;
    for(std::size_t row=0;row<rows;++row){const auto index=checked_add(first_sample,row);const auto time=static_cast<long double>(index)/sample_rate;
        const auto carrier_phase=std::remainder(tau*(candidate.frequency_hz*time+.5L*candidate.frequency_drift_hz_per_second*time*time),tau);
        const auto common=std::polar(1.,static_cast<double>(carrier_phase));
        for(std::size_t tap=0;tap<candidate.channel.taps.size();++tap){const auto& path=candidate.channel.taps[tap];
            // Stay in sample coordinates. Dividing an integral sample by Fs and
            // multiplying back can cross a finite RRC support endpoint.
            const auto receiver_coordinate=static_cast<long double>(index)-
                static_cast<long double>(candidate.arrival_seconds+path.delay_seconds)*sample_rate;
            const auto coordinate=receiver_coordinate*rate*(static_cast<long double>(source.format().sample_rate)/sample_rate);
            const auto phase=std::remainder(tau*path.doppler_hz*time,tau);
            const auto value=source.sample_at(coordinate)*common*std::polar(1.,static_cast<double>(phase));
            finite(value);const auto column=tap*(candidate.channel.linear_gain?2:1);output[column*rows+row]=value;
            if(candidate.channel.linear_gain){const auto coordinate_gain=(static_cast<double>(time)-candidate.channel.gain_reference_seconds)/candidate.channel.gain_scale_seconds;output[(column+1)*rows+row]=value*coordinate_gain;finite(output[(column+1)*rows+row]);}
        }
    }
}

struct StreamingReceiver::Impl {
    ReceiverConfig config;
    Geometry shape;
    struct State {
        std::unique_ptr<PatternSource> source;
        RowQr qr;
        CandidateStatistics stats;
        Sum explained,residual,input;
        explicit State(std::size_t columns):qr(columns){}
    };
    std::vector<State> states;
    std::vector<std::vector<Complex>> scratch;
    ReceiverReport report;
    std::uint64_t next_input=0,observation_end=0,block_first=0,block_rows=0;
    std::size_t workspace=0,maximum_basis=0;
    bool finished=false;
    double wall=0,cpu=0;
    explicit Impl(const ReceiverConfig& value,const Crypto& key):shape(v3::geometry(value.format)){
        const auto minimum_state=checked_sum({shape.source_workspace_bytes,8192});
        require(value.candidates.size()<=value.memory_limit_bytes/std::max<std::size_t>(1,minimum_state),"triad v3 candidate preparation exceeds memory cap");
        for(const auto& candidate:value.candidates){
            require(!candidate.channel.taps.empty()&&candidate.channel.taps.size()<=maximum_columns/2,"triad v3 invalid tap count");
        }
        config=value;
        require(!config.candidates.empty(),"triad v3 empty candidate bank");
        require(config.threads>=1&&config.threads<=256,"triad v3 invalid worker count");
        require(config.block_samples>0&&config.maximum_chunk_samples>0,"triad v3 zero receiver block/chunk");
        require(config.complex_noise_variance>0&&std::isfinite(config.complex_noise_variance),"triad v3 invalid complex noise variance");
        require(config.family_false_admission_probability>0&&config.family_false_admission_probability<1&&std::isfinite(config.family_false_admission_probability),"triad v3 invalid family false-admission budget");
        require(std::isfinite(config.log_family_hypotheses)&&config.log_family_hypotheses>=std::log(static_cast<double>(config.candidates.size())),"triad v3 family schedule omits bank candidates");
        require(std::isfinite(config.minimum_score_margin)&&config.minimum_score_margin>=0,"triad v3 invalid score margin");
        require(std::isfinite(config.rank_relative_tolerance)&&config.rank_relative_tolerance>0&&config.rank_relative_tolerance<1,"triad v3 invalid rank tolerance");
        require(std::isfinite(config.minimum_observed_fraction)&&config.minimum_observed_fraction>0&&config.minimum_observed_fraction<=1,"triad v3 invalid observed fraction");
        long double last=0;bool zero=false,one=false;
        for(const auto& candidate:config.candidates){require(candidate.source_bit<=1,"triad v3 nonbinary candidate");zero|=candidate.source_bit==0;one|=candidate.source_bit==1;
            require(std::isfinite(candidate.arrival_seconds)&&candidate.arrival_seconds>=0,"triad v3 negative/nonfinite capture arrival");
            require(std::isfinite(candidate.frequency_hz)&&std::isfinite(candidate.frequency_drift_hz_per_second)&&std::isfinite(candidate.clock_ppm)&&std::abs(candidate.clock_ppm)<=10000,"triad v3 invalid candidate oscillator");
            require(!candidate.channel.taps.empty()&&candidate.channel.taps.size()<=maximum_columns/2,"triad v3 invalid tap count");
            require(!candidate.channel.linear_gain||(std::isfinite(candidate.channel.gain_reference_seconds)&&std::isfinite(candidate.channel.gain_scale_seconds)&&candidate.channel.gain_scale_seconds>0),"triad v3 invalid gain basis");
            maximum_basis=std::max(maximum_basis,column_count(candidate));double delay=0;
            for(const auto& tap:candidate.channel.taps){require(std::isfinite(tap.delay_seconds)&&tap.delay_seconds>=0&&std::isfinite(tap.doppler_hz),"triad v3 invalid channel tap");delay=std::max(delay,tap.delay_seconds);}
            const auto endpoint=static_cast<long double>(candidate.arrival_seconds+delay)+static_cast<long double>(shape.source_samples)/config.format.sample_rate/(1+candidate.clock_ppm*1e-6);
            last=std::max(last,endpoint*config.format.sample_rate);epoch_with_offset(config.format.epoch,candidate.epoch_offset_seconds);
        }
        require(zero&&one,"triad v3 binary reception needs both bit hypotheses");
        require(last<=UINT64_MAX,"triad v3 observation coordinate overflow");
        if(config.observation_samples==0){const auto endpoint=static_cast<std::uint64_t>(std::ceil(last));require(endpoint>=config.observation_first_sample,"triad v3 invalid observation calendar");config.observation_samples=endpoint-config.observation_first_sample;}
        require(config.observation_samples>0,"triad v3 empty observation calendar");
        observation_end=checked_add(config.observation_first_sample,config.observation_samples);
        for(const auto& candidate:config.candidates){double delay=0;for(const auto& tap:candidate.channel.taps)delay=std::max(delay,tap.delay_seconds);
            const auto first=candidate.arrival_seconds*config.format.sample_rate;
            const auto end=(candidate.arrival_seconds+delay+static_cast<double>(shape.source_samples)/config.format.sample_rate/(1+candidate.clock_ppm*1e-6))*config.format.sample_rate;
            require(static_cast<long double>(first)+.5L>=config.observation_first_sample&&end<=static_cast<long double>(observation_end)+.5,"triad v3 observation omits candidate waveform support");}
        const auto workers=worker_count(config.threads,config.candidates.size());
        const auto tile=std::min(config.block_samples,config.maximum_chunk_samples);
        workspace=checked_sum({65536,checked_multiply(config.candidates.size(),checked_sum({shape.source_workspace_bytes,8192,checked_multiply(maximum_basis*maximum_basis,4*sizeof(Complex))})),checked_multiply(workers,checked_sum({checked_multiply(checked_multiply(maximum_basis,tile),sizeof(Complex)),4*maximum_columns*maximum_columns*sizeof(Complex)}))});
        require(workspace<=config.memory_limit_bytes,"triad v3 receiver workspace exceeds memory cap");
        states.reserve(config.candidates.size());
        for(const auto& candidate:config.candidates){Format addressed=config.format;addressed.epoch=epoch_with_offset(addressed.epoch,candidate.epoch_offset_seconds);states.emplace_back(column_count(candidate));states.back().source=std::make_unique<PatternSource>(addressed,key,candidate.source_bit);require(states.back().source->working_bytes()<=shape.source_workspace_bytes,"triad v3 source exceeds declared workspace bound");}
        scratch.assign(workers,std::vector<Complex>(checked_multiply(maximum_basis,tile)));
        block_first=config.observation_first_sample;report.working_bytes=workspace;report.threads_used=workers;
        report.analytic_null_supported=config.known_white_noise;report.arithmetic_used=Arithmetic::fp64;
    }
    void finalize_block(){
        if(!block_rows)return;
        parallel(states.size(),config.threads,[&](std::size_t index,unsigned){auto& state=states[index];const auto fit=state.qr.finish(config.rank_relative_tolerance);
            state.explained.add(fit.projection);state.input.add(fit.input);state.residual.add(std::max(0.,fit.input-fit.projection));
            state.stats.rank=checked_add(state.stats.rank,fit.rank);state.stats.rows=checked_add(state.stats.rows,state.qr.rows);++state.stats.blocks;
            if(fit.rank<state.qr.columns)++state.stats.rank_deficient_blocks;
            state.stats.last_channel_coefficients=fit.coefficients;state.qr=RowQr(state.qr.columns);
        });
        ++report.blocks;block_first=checked_add(block_first,block_rows);block_rows=0;
    }
    void consume(std::uint64_t first,std::span<const Complex> values,std::span<const std::uint8_t> validity){
        require(!finished,"triad v3 input after receiver finish");require(first==next_input,"triad v3 receiver input is not contiguous");
        require(validity.empty()||validity.size()==values.size(),"triad v3 validity extent mismatch");
        const auto end=checked_add(first,values.size());next_input=end;report.consumed_samples=checked_add(report.consumed_samples,values.size());
        std::size_t position=0;
        while(position<values.size()){
            const auto coordinate=checked_add(first,position);
            if(coordinate<config.observation_first_sample){const auto skip=std::min<std::uint64_t>(values.size()-position,config.observation_first_sample-coordinate);position+=static_cast<std::size_t>(skip);continue;}
            if(coordinate>=observation_end)break;
            const auto remaining=std::min<std::uint64_t>(observation_end-coordinate,config.block_samples-block_rows);
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>({remaining,values.size()-position,config.maximum_chunk_samples}));
            const auto observed=values.subspan(position,count);const auto mask=validity.empty()?std::span<const std::uint8_t>{}:validity.subspan(position,count);
            std::uint64_t present=0;for(std::size_t row=0;row<count;++row){if(mask.empty()||mask[row]){finite(observed[row]);++present;}}
            report.scored_samples=checked_add(report.scored_samples,present);report.missing_samples=checked_add(report.missing_samples,count-present);report.all_samples_observed&=present==count;
            parallel(states.size(),config.threads,[&](std::size_t index,unsigned worker){auto& state=states[index];auto candidate=config.candidates[index];
                if(candidate.channel.linear_gain){const auto length=std::min<std::uint64_t>(config.block_samples,observation_end-block_first);candidate.channel.gain_reference_seconds=(static_cast<double>(block_first)+.5*static_cast<double>(length))/config.format.sample_rate;candidate.channel.gain_scale_seconds=static_cast<double>(length)/config.format.sample_rate;}
                const auto columns=state.qr.columns;auto matrix=std::span(scratch[worker]).first(checked_multiply(columns,count));shaped_basis(*state.source,candidate,config.format.sample_rate,coordinate,count,matrix);
                std::array<Complex,maximum_columns> row_values{};for(std::size_t row=0;row<count;++row){if(!mask.empty()&&!mask[row])continue;for(std::size_t column=0;column<columns;++column)row_values[column]=matrix[column*count+row];state.qr.append(std::span(row_values).first(columns),observed[row]);}
            });
            for(const auto& state:states)report.basis_values=checked_add(report.basis_values,checked_multiply(state.qr.columns,count));
            block_rows+=count;position+=count;if(block_rows==config.block_samples||coordinate+count==observation_end)finalize_block();
        }
    }
    ReceiverReport finish(){
        if(finished){return report;}
        finalize_block();finished=true;
        report.complete_observation=next_input>=observation_end;
        report.candidates.reserve(states.size());double best=-std::numeric_limits<double>::infinity();
        for(std::size_t index=0;index<states.size();++index){auto& state=states[index];state.stats.projection_energy=state.explained.value;state.stats.residual_energy=state.residual.value;state.stats.input_energy=state.input.value;state.stats.normalized_score=state.explained.value/config.complex_noise_variance;
            require(std::isfinite(state.stats.normalized_score),"triad v3 normalized score overflow");if(state.stats.rank){
                const auto threshold=config.conservative_threshold?
                    statistics::conservative_null_threshold(state.stats.rank,config.log_family_hypotheses,config.family_false_admission_probability):
                    statistics::gamma_null_threshold(state.stats.rank,config.log_family_hypotheses,config.family_false_admission_probability);
                state.stats.null_threshold=threshold.score;state.stats.threshold_conservative=threshold.conservative;
                report.any_threshold_conservative|=threshold.conservative;
            }else{state.stats.null_threshold=std::numeric_limits<double>::infinity();}
            const auto excess=state.stats.normalized_score-state.stats.null_threshold;if(excess>best){best=excess;report.best_candidate=index;}report.candidates.push_back(state.stats);
        }
        const auto& selected=report.candidates[report.best_candidate];report.source_bit=config.candidates[report.best_candidate].source_bit;report.score=selected.normalized_score;report.threshold=selected.null_threshold;
        double other=-std::numeric_limits<double>::infinity();double score_scale=std::max(1.,std::abs(report.score));
        for(std::size_t index=0;index<states.size();++index)if(config.candidates[index].source_bit!=report.source_bit){
            other=std::max(other,report.candidates[index].normalized_score-report.candidates[index].null_threshold);
            score_scale=std::max(score_scale,std::abs(report.candidates[index].normalized_score));
        }
        const auto numerical_tie=64*std::numeric_limits<double>::epsilon()*score_scale;
        report.margin=std::isfinite(best)&&std::isfinite(other)?best-other:0;report.presence=selected.rank>0&&best>=0;
        const auto coverage=static_cast<double>(report.scored_samples)/static_cast<double>(config.observation_samples);
        report.accepted=report.complete_observation&&coverage>=config.minimum_observed_fraction&&report.analytic_null_supported&&report.presence&&std::isfinite(other)&&report.margin>std::max(config.minimum_score_margin,numerical_tie);
        if(report.complete_observation&&report.all_samples_observed&&report.analytic_null_supported&&std::isfinite(best)&&!report.presence)report.fully_scored_absence_seconds=shape.bit_seconds;
        report.wall_seconds=wall;report.cpu_seconds=cpu;return report;
    }
};
StreamingReceiver::StreamingReceiver(const ReceiverConfig& config,const Crypto& key):impl_(std::make_unique<Impl>(config,key)){}
StreamingReceiver::~StreamingReceiver()=default;
StreamingReceiver::StreamingReceiver(StreamingReceiver&&) noexcept=default;
StreamingReceiver& StreamingReceiver::operator=(StreamingReceiver&&) noexcept=default;
void StreamingReceiver::push(std::uint64_t first,std::span<const Complex> samples,std::span<const std::uint8_t> validity){const auto wall=std::chrono::steady_clock::now();const auto cpu=std::clock();impl_->consume(first,samples,validity);impl_->wall+=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();impl_->cpu+=static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC;}
ReceiverReport StreamingReceiver::finish(){const auto wall=std::chrono::steady_clock::now();const auto cpu=std::clock();auto result=impl_->finish();impl_->wall+=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();impl_->cpu+=static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC;impl_->report.wall_seconds=impl_->wall;impl_->report.cpu_seconds=impl_->cpu;result.wall_seconds=impl_->wall;result.cpu_seconds=impl_->cpu;return result;}
std::size_t StreamingReceiver::working_bytes()const{return impl_->workspace;}
} // namespace datapump::triad::v3

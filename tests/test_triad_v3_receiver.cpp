#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_statistics.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>

using namespace datapump;
namespace v=datapump::triad::v3;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Work>void rejects(Work work,const char* message){bool rejected=false;try{work();}catch(const std::exception&){rejected=true;}check(rejected,message);}
Crypto key(){std::array<std::uint8_t,32> bytes{};for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(11+7*i);return Crypto(bytes);}
v::Format format(){v::Format result;result.inner_rate_hz=360;result.added_symbols=10;result.sample_rate=4800;result.bit_seconds=8;return result;}
v::ReceiverConfig config(const v::Format& f){v::ReceiverConfig result;result.format=f;v::Candidate zero,one;one.source_bit=1;result.candidates={zero,one};result.log_family_hypotheses=std::log(2.);result.complex_noise_variance=.001;result.block_samples=397;result.maximum_chunk_samples=257;result.threads=2;return result;}
v::ReceiverReport clean(const Crypto& crypto,std::size_t chunk,bool missing=false){
    const auto f=format();auto p=config(f);v::PatternSource source(f,crypto,1);v::StreamingReceiver receiver(p,crypto);
    std::vector<v::Complex> data(chunk);std::vector<std::uint8_t> validity(chunk,1);const auto total=source.geometry().source_samples;
    for(std::uint64_t first=0;first<total;){const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(chunk,total-first));auto block=std::span(data).first(count);source.render(first,block);for(auto& value:block)value*=v::Complex(.7,.2);std::fill(validity.begin(),validity.end(),1);
        if(missing&&first+count>total/2&&first<=total/2)validity[static_cast<std::size_t>(total/2-first)]=0;
        receiver.push(first,block,missing?std::span<const std::uint8_t>(validity).first(count):std::span<const std::uint8_t>{});first+=count;}
    auto result=receiver.finish();check(result.accepted&&result.source_bit==1&&result.complete_observation,"streamed clean bit recovery");
    check(result.working_bytes<=p.memory_limit_bytes,"receiver workspace exceeded");check(!result.candidates[result.best_candidate].threshold_conservative,"exact Gamma gate silently replaced by concentration bound");std::cout<<"clean chunk="<<chunk<<" best="<<result.best_candidate<<" projection="<<result.candidates[result.best_candidate].projection_energy<<" input="<<result.candidates[result.best_candidate].input_energy<<" residual="<<result.candidates[result.best_candidate].residual_energy<<" rank="<<result.candidates[result.best_candidate].rank<<"\n";check(result.candidates[result.best_candidate].residual_energy<1e-8*result.candidates[result.best_candidate].input_energy,"raw shaped waveform was not fitted");
    check(receiver.finish().score==result.score,"finish repeated score accumulation");return result;
}
void streaming_checks(const Crypto& crypto){
    const auto a=clean(crypto,83),b=clean(crypto,257);
    check(std::abs(a.score-b.score)<1e-9*std::max(1.,a.score),"chunk partition changed score");check(a.candidates[a.best_candidate].rank==b.candidates[b.best_candidate].rank,"chunk partition changed rank");
    const auto erased=clean(crypto,131,true);check(!erased.all_samples_observed&&erased.missing_samples==1&&erased.fully_scored_absence_seconds==0,"missing sample became absence");
    const auto f=format();auto p=config(f);const auto g=v::geometry(f);v::StreamingReceiver null_receiver(p,crypto);std::vector<v::Complex> zeros(211);
    for(std::uint64_t first=0;first<g.source_samples;){const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(zeros.size(),g.source_samples-first));null_receiver.push(first,std::span(zeros).first(count));first+=count;}
    const auto absent=null_receiver.finish();check(!absent.accepted&&!absent.presence&&absent.fully_scored_absence_seconds>=6,"fully scored absent symbol contract");
    v::StreamingReceiver partial(p,crypto);partial.push(0,zeros);const auto short_result=partial.finish();check(!short_result.complete_observation&&!short_result.accepted&&short_result.fully_scored_absence_seconds==0,"EOF admitted or completed a partial symbol");
    p.memory_limit_bytes=1;rejects([&]{v::StreamingReceiver invalid(p,crypto);},"memory cap ignored");p=config(f);p.log_family_hypotheses=0;rejects([&]{v::StreamingReceiver invalid(p,crypto);},"null family count omitted bank candidates");
    p=config(f);p.candidates[0].channel.taps.assign(9,{});rejects([&]{v::StreamingReceiver invalid(p,crypto);},"oversized tap vector copied before admission");
    p=config(f);v::StreamingReceiver discontinuous(p,crypto);rejects([&]{discontinuous.push(1,zeros);},"missing input was manufactured");
}
void degenerate_block_check(const Crypto& crypto){
    auto f=format();f.inner_rate_hz=36;f.added_symbols=1;f.sample_rate=64;
    auto p=config(f);p.block_samples=1;p.threads=1;p.complex_noise_variance=1e-9;
    v::PatternSource source(f,crypto,0);v::StreamingReceiver receiver(p,crypto);
    std::vector<v::Complex> samples(197);
    for(std::uint64_t first=0;first<source.geometry().source_samples;){const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),source.geometry().source_samples-first));auto block=std::span(samples).first(count);source.render(first,block);receiver.push(first,block);first+=count;}
    const auto result=receiver.finish();
    std::cout<<"degenerate one-row presence="<<result.presence<<" margin="<<result.margin<<" score="<<result.score<<'\n';
    check(result.presence&&!result.accepted,"roundoff broke an unidentifiable full-row-space bit tie");
}
void joint_and_null_checks(const Crypto& crypto){
    const auto f=format();v::PatternSource source(f,crypto,0);v::Candidate candidate;candidate.channel.taps={{0,0},{2.5/f.sample_rate,.23}};
    constexpr std::size_t rows=257,columns=2;std::vector<v::Complex> basis(rows*columns);v::shaped_basis(source,candidate,f.sample_rate,1200,rows,basis);
    const v::Complex first(.8,.2),second(-.3,.4);std::vector<v::Complex> y(rows);for(std::size_t i=0;i<rows;++i)y[i]=basis[i]*first+basis[rows+i]*second;
    const auto fit=v::project_block(y,basis,columns);check(fit.rank==2&&fit.residual_energy<1e-10*fit.input_energy,"joint shaped two-ray QR fit");check(std::abs(fit.last_channel_coefficients[0]-first)<1e-9&&std::abs(fit.last_channel_coefficients[1]-second)<1e-9,"joint channel coefficients");
    std::copy(basis.begin(),basis.begin()+rows,basis.begin()+rows);for(std::size_t i=0;i<rows;++i)y[i]=basis[i]*first;
    const auto singular=v::project_block(y,basis,columns);check(singular.rank==1&&singular.rank_deficient_blocks==1,"dependent rays counted twice");
    candidate.channel.taps={{0,0},{2.5/f.sample_rate,.23}};v::shaped_basis(source,candidate,f.sample_rate,1200,rows,basis);
    std::mt19937_64 generator(1837301);std::normal_distribution<double> normal(0,std::sqrt(.5));double sum=0,squares=0;
    constexpr unsigned trials=512;for(unsigned trial=0;trial<trials;++trial){for(auto& value:y)value={normal(generator),normal(generator)};const auto projected=v::project_block(y,basis,columns);check(projected.rank==2,"fixed null model rank changed");sum+=projected.normalized_score;squares+=projected.normalized_score*projected.normalized_score;}
    const auto mean=sum/trials,variance=squares/trials-mean*mean;std::cout<<"fixed shaped rank2 null mean="<<mean<<" variance="<<variance<<'\n';check(mean>1.75&&mean<2.25&&variance>1.3&&variance<2.8,"fixed-subspace Gamma moments");
}
void reciprocal_basis_and_receiver_checks(const Crypto& crypto){
    auto f=format();f.inner_rate_hz=.0036;f.added_symbols=1;f.sample_rate=1;f.sample_rate_denominator=64;
    const auto fs=v::effective_sample_rate(f);v::PatternSource source(f,crypto,1);
    v::Candidate candidate;candidate.source_bit=1;candidate.arrival_seconds=73;candidate.clock_ppm=.01;
    candidate.frequency_hz=1e-6;candidate.channel.taps={{0,0},{.000005,.001}};
    constexpr std::size_t rows=257;const std::uint64_t start=1000;
    std::vector<v::Complex> basis(2*rows);v::shaped_basis(source,candidate,fs,start,rows,basis);
    auto high=f;high.sample_rate_denominator=1;v::PatternSource high_source(high,crypto,1);
    // Sources share keyed coefficients but have a public padding difference.
    // Express both candidates on the same low-rate receiver time lattice.
    auto high_candidate=candidate;
    const auto source_origin=source.geometry().calendar_origin_sample/fs;
    const auto high_origin=static_cast<double>(high_source.geometry().calendar_origin_sample);
    high_candidate.arrival_seconds+=(source_origin-high_origin)/(1+candidate.clock_ppm*1e-6);
    std::vector<v::Complex> reference(2*rows);v::shaped_basis(high_source,high_candidate,fs,start,rows,reference);
    long double error=0,energy=0;
    for(std::size_t i=0;i<basis.size();++i){error+=std::norm(basis[i]-reference[i]);energy+=std::norm(reference[i]);}
    check(energy>0&&error<1e-20*energy,"sub-Hz basis changed physical source time or tap phase");

    auto p=config(f);p.threads=1;p.block_samples=251;p.maximum_chunk_samples=197;
    v::StreamingReceiver receiver(p,crypto);std::vector<v::Complex> buffer(197);
    for(std::uint64_t first=0;first<source.geometry().source_samples;){
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),source.geometry().source_samples-first));
        auto samples=std::span(buffer).first(count);source.render(first,samples);receiver.push(first,samples);first+=count;
    }
    const auto result=receiver.finish();check(result.accepted&&result.source_bit==1&&result.complete_observation,
        "sub-Hz streaming receiver failed complete shaped bit");
    const auto& best=result.candidates[result.best_candidate];
    check(best.residual_energy<1e-10*best.input_energy,"sub-Hz receiver modeled numerator as Hz");
}
}
int main(){try{const auto crypto=key();streaming_checks(crypto);degenerate_block_check(crypto);joint_and_null_checks(crypto);reciprocal_basis_and_receiver_checks(crypto);std::cout<<"triad v3 receiver checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"triad v3 receiver failed: "<<error.what()<<'\n';return 1;}}

#include "datapump/triad_modem_v3.hpp"
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::triad::v3 {
namespace {
constexpr std::size_t maximum_columns=16;
constexpr long double tau=2*std::numbers::pi_v<long double>;
void require(bool condition,const char* message){if(!condition)throw Error(message);}
std::uint64_t checked_add(std::uint64_t a,std::uint64_t b){require(b<=UINT64_MAX-a,"triad sample coordinate overflow");return a+b;}
std::size_t checked_multiply(std::size_t a,std::size_t b){require(!a||b<=SIZE_MAX/a,"triad basis extent overflow");return a*b;}
void finite(Complex value){require(std::isfinite(value.real())&&std::isfinite(value.imag()),"triad nonfinite basis value");}
std::size_t column_count(const Candidate& candidate){return checked_multiply(candidate.channel.taps.size(),candidate.channel.linear_gain?2:1);}
void basis_row(PatternSource& source,const Candidate& candidate,double sample_rate,
               double source_sample_rate,double rate,long double index,
               std::size_t row,std::size_t rows,std::span<Complex> output,double center){
    const auto time=index/sample_rate;
    const auto carrier_phase=std::remainder(tau*(candidate.frequency_hz*time+.5L*candidate.frequency_drift_hz_per_second*time*time),tau);
    const auto common=std::polar(1.,static_cast<double>(carrier_phase));
    for(std::size_t tap=0;tap<candidate.channel.taps.size();++tap){const auto& path=candidate.channel.taps[tap];
        // Preserve the legacy sample-coordinate arithmetic and finite support.
        const auto shift=static_cast<long double>(candidate.arrival_seconds+path.delay_seconds);
        const auto receiver_coordinate=index-shift*sample_rate;
        const auto coordinate=receiver_coordinate*rate*(static_cast<long double>(source_sample_rate)/sample_rate);
        const auto phase=std::remainder(tau*path.doppler_hz*time,tau);
        auto value=source.sample_at(coordinate)*common*std::polar(1.,static_cast<double>(phase));
        if(center!=0){
            // Compute source_time-time without subtracting two large absolute
            // times. Reduce cycles before multiplying by2*pi; integral audio
            // phase origins then remain exact at large integer coordinates.
            const auto difference=((static_cast<long double>(rate)-1)*index-rate*shift*sample_rate)/sample_rate;
            const auto cycles=std::remainder(static_cast<long double>(center)*difference,1.L);
            value*=std::polar(1.,static_cast<double>(tau*cycles));
        }
        finite(value);const auto column=tap*(candidate.channel.linear_gain?2:1);output[column*rows+row]=value;
        if(candidate.channel.linear_gain){const auto coordinate_gain=(static_cast<double>(time)-candidate.channel.gain_reference_seconds)/candidate.channel.gain_scale_seconds;output[(column+1)*rows+row]=value*coordinate_gain;finite(output[(column+1)*rows+row]);}
    }
}
double validate_mapped(const Candidate& candidate,double sample_rate,std::uint64_t first_sample,
                       std::span<const double> offsets,double audio_center_hz){
    require(std::isfinite(sample_rate)&&sample_rate>0&&std::isfinite(audio_center_hz)&&
        audio_center_hz>=0&&audio_center_hz<=120e6,"triad v3 invalid mapped basis rate/center");
    require(std::isfinite(candidate.arrival_seconds)&&std::isfinite(candidate.frequency_hz)&&
        std::isfinite(candidate.frequency_drift_hz_per_second)&&std::isfinite(candidate.clock_ppm)&&
        candidate.clock_ppm>-1e6,"triad v3 invalid mapped basis candidate");
    require(!candidate.channel.linear_gain||(std::isfinite(candidate.channel.gain_reference_seconds)&&
        std::isfinite(candidate.channel.gain_scale_seconds)&&candidate.channel.gain_scale_seconds>0),
        "triad v3 invalid mapped gain basis");
    for(const auto& path:candidate.channel.taps)
        require(std::isfinite(path.delay_seconds)&&path.delay_seconds>=0&&std::isfinite(path.doppler_hz),
            "triad v3 invalid mapped channel tap");
    long double previous=-1;
    for(const auto offset:offsets){
        require(std::isfinite(offset)&&offset>=0&&offset<=1048576,"triad v3 mapped offset outside bounded request");
        require(static_cast<long double>(offset)<=static_cast<long double>(UINT64_MAX-first_sample),
            "triad v3 mapped sample coordinate overflow");
        const auto index=static_cast<long double>(first_sample)+offset;
        require(index>previous,"triad v3 mapped coordinates are not strictly increasing at native precision");
        previous=index;
    }
    const auto rate=1+candidate.clock_ppm*1e-6;
    require(std::isfinite(rate)&&rate>0,"triad v3 invalid mapped clock scale");
    return rate;
}
} // namespace

void shaped_basis(PatternSource& source,const Candidate& candidate,double sample_rate,
                  std::uint64_t first_sample,std::size_t rows,std::span<Complex> output){
    const auto columns=column_count(candidate);require(output.size()==checked_multiply(columns,rows),"triad v3 shaped basis extent mismatch");
    require(std::isfinite(sample_rate)&&sample_rate>0,"triad v3 invalid basis sample rate");
    const auto rate=1+candidate.clock_ppm*1e-6;
    const auto source_sample_rate=effective_sample_rate(source.format());
    for(std::size_t row=0;row<rows;++row){
        basis_row(source,candidate,sample_rate,source_sample_rate,rate,
            static_cast<long double>(checked_add(first_sample,row)),row,rows,output,0);
    }
}

void shaped_basis_mapped(PatternSource& source,const Candidate& candidate,double sample_rate,
                         std::uint64_t first_sample,std::span<const double> offsets,
                         std::span<Complex> output,double audio_center_hz){
    const auto columns=column_count(candidate);
    require(columns>0&&columns<=maximum_columns&&!offsets.empty()&&
        output.size()==checked_multiply(columns,offsets.size()),"triad v3 mapped basis extent mismatch");
    const auto rate=validate_mapped(candidate,sample_rate,first_sample,offsets,audio_center_hz);
    const auto source_rate=effective_sample_rate(source.format());
    for(std::size_t row=0;row<offsets.size();++row)
        basis_row(source,candidate,sample_rate,source_rate,rate,
            static_cast<long double>(first_sample)+offsets[row],row,offsets.size(),output,audio_center_hz);
}

void shaped_basis_mapped_pair(PatternSource& bit0,PatternSource& bit1,
                         const Candidate& candidate,double sample_rate,
                         std::uint64_t first_sample,std::span<const double> offsets,
                         std::span<Complex> output,double audio_center_hz,
                         std::stop_token stop){
    const auto columns=column_count(candidate);
    const auto rows=offsets.size();
    const auto per_bit=checked_multiply(columns,rows);
    require(columns>0&&columns<=maximum_columns&&!offsets.empty()&&
        output.size()==checked_multiply(per_bit,2),"triad v3 paired mapped basis extent mismatch");
    const auto rate=validate_mapped(candidate,sample_rate,first_sample,offsets,audio_center_hz);
    const auto source_rate=effective_sample_rate(bit0.format());
    const auto stride=candidate.channel.linear_gain?2u:1u;
    std::array<PatternSource*,2> source_pair{&bit0,&bit1};
    // 256 long-double coordinates and two 256-sample source rows: 12 KiB
    // bounded per call, independent of the full block length.
    constexpr std::size_t block_size=256;
    std::array<long double,block_size> coordinates{};
    std::array<Complex,2*block_size> rendered{};
    for(std::size_t begin=0;begin<rows;begin+=std::min(block_size,rows-begin)){
        if(stop.stop_requested())throw Error("triad v3 paired mapped basis cancelled");
        const auto count=std::min(block_size,rows-begin);
        for(std::size_t tap=0;tap<candidate.channel.taps.size();++tap){
            const auto& path=candidate.channel.taps[tap];
            const auto shift=static_cast<long double>(candidate.arrival_seconds+path.delay_seconds);
            for(std::size_t i=0;i<count;++i){
                const auto index=static_cast<long double>(first_sample)+offsets[begin+i];
                const auto receiver_coordinate=index-shift*sample_rate;
                coordinates[i]=receiver_coordinate*rate*(static_cast<long double>(source_rate)/sample_rate);
            }
            PatternSource::render_bank(source_pair,
                std::span<const long double>(coordinates.data(),count),
                std::span<Complex>(rendered.data(),2*count));
            for(std::size_t i=0;i<count;++i){
                const auto row=begin+i;
                const auto index=static_cast<long double>(first_sample)+offsets[row];
                const auto time=index/sample_rate;
                const auto carrier_phase=std::remainder(tau*(candidate.frequency_hz*time+
                    .5L*candidate.frequency_drift_hz_per_second*time*time),tau);
                const auto common=std::polar(1.,static_cast<double>(carrier_phase));
                const auto phase=std::remainder(tau*path.doppler_hz*time,tau);
                const auto path_rotation=std::polar(1.,static_cast<double>(phase));
                Complex center_rotation;
                if(audio_center_hz!=0){
                    const auto difference=((static_cast<long double>(rate)-1)*index-
                        rate*shift*sample_rate)/sample_rate;
                    const auto cycles=std::remainder(static_cast<long double>(audio_center_hz)*
                        difference,1.L);
                    center_rotation=std::polar(1.,static_cast<double>(tau*cycles));
                }
                for(std::size_t bit=0;bit<2;++bit){
                    auto value=rendered[bit*count+i]*common*path_rotation;
                    if(audio_center_hz!=0)value*=center_rotation;
                    finite(value);
                    const auto column=tap*stride;
                    output[bit*per_bit+column*rows+row]=value;
                    if(candidate.channel.linear_gain){
                        const auto coordinate_gain=(static_cast<double>(time)-
                            candidate.channel.gain_reference_seconds)/candidate.channel.gain_scale_seconds;
                        auto& gained=output[bit*per_bit+(column+1)*rows+row];
                        gained=value*coordinate_gain;finite(gained);
                    }
                }
            }
        }
    }
}

} // namespace datapump::triad::v3

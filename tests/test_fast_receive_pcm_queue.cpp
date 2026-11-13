#include "fast/receive_pcm_queue.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace datapump::fast;
using namespace datapump::fast::detail;

void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

void check_profile_caps() {
    auto ssb=capacity_profile(Channel::ssb);
    require(receive_pcm_capacity(ssb)==8*ssb.sample_rate,"Dense-pilot SSB capture lost its bounded backlog budget");
    ssb.ldpc_frame_bits=64800;
    require(receive_pcm_capacity(ssb)==8*ssb.sample_rate,"The full-frame SSB capture budget changed");
    ssb.pilot_spacing_symbols=128;
    require(receive_pcm_capacity(ssb)==ssb.sample_rate,"A different SSB geometry inherited an unqualified capture budget");
    auto dense=capacity_profile(Channel::ssb);
    dense.ldpc_frame_bits=64800;dense.constellation=4096;dense.code_rate=CodeRate::eight_ninths;
    require(receive_pcm_capacity(dense)==8*dense.sample_rate,"Qualified high-rate SSB lost its modeled capture budget");
    dense.sample_rate=44100;
    require(receive_pcm_capacity(dense)==dense.sample_rate,"Unsupported SSB input rate inherited a modeled capture budget");
    const auto ofdm=capacity_profile(Channel::acoustic);
    require(receive_pcm_capacity(ofdm)==4*ofdm.sample_rate,"OFDM capture queue changed its existing limit");
    auto wire=capacity_profile(Channel::wire);
    require(receive_pcm_capacity(wire)==3*wire.sample_rate,"Wire capture lost its four-frame burst reserve");
    wire.interleave_depth=16;
    require(receive_pcm_capacity(wire)==9*wire.sample_rate,"Wire capture lost its maximum-depth bound");
    wire.sample_rate=48001;
    wire.interleave_depth=3;
    require(receive_pcm_capacity(wire)==120003,"Wire capture rounded an odd frame reserve down");
    for(const unsigned depth:{0U,17U}) {
        wire.interleave_depth=depth;
        bool rejected=false;
        try { (void)receive_pcm_capacity(wire); }
        catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"Wire capture accepted an invalid local coding depth");
    }
    const auto classic=classic_profile(Channel::wire);
    require(receive_pcm_capacity(classic)==classic.sample_rate,"Classic capture queue changed its existing limit");
}

void check_wire_coding_burst() {
    const auto wire=capacity_profile(Channel::wire);
    const auto rate=static_cast<std::size_t>(wire.sample_rate);
    ReceivePcmQueue queue(receive_pcm_capacity(wire));
    std::array<float,1200> block{};
    std::size_t written=0,read=0;
    const auto capture=[&](std::size_t count) {
        while(count) {
            auto input=std::span(block).first(std::min(count,block.size()));
            for(auto& sample:input)sample=static_cast<float>(written++);
            require(queue.push(input),"Wire coding burst dropped capture samples");
            count-=input.size();
        }
    };
    const auto consume=[&](std::size_t count) {
        while(count) {
            auto output=std::span(block).first(std::min(count,block.size()));
            require(queue.pop(output)==output.size(),"Wire coding burst lost a PCM block");
            for(auto sample:output)
                require(sample==static_cast<float>(read++),"Wire coding burst changed capture time or order");
            count-=output.size();
        }
    };

    // Capture continues while the receiver spends two seconds on the fixed
    // LDPC cycle. The consumer then resumes while later samples keep arriving.
    capture(2*rate);
    require(queue.status().queued_samples==2*rate,"Wire capture did not retain the coding burst");
    consume(rate);
    capture(2*rate);
    require(queue.status().queued_samples==3*rate&&queue.status().peak_samples==3*rate,
        "Wire capture did not reach its exact bounded extent");
    queue.close();
    consume(3*rate);
    require(read==written&&!queue.status().overflow&&!queue.status().queued_samples,
        "Wire capture did not drain the continuous burst and wrapped tail");
}

void check_wrap_and_close() {
    ReceivePcmQueue queue(5);
    const std::array first{1.F,2.F,3.F,4.F};
    const std::array second{5.F,6.F,7.F,8.F};
    std::array<float,5> output{};
    require(queue.push(first) && queue.pop(std::span(output).first(3))==3 &&
        output[0]==1.F && output[1]==2.F && output[2]==3.F,"Capture queue changed the first PCM prefix");
    require(queue.push(second),"Capture queue did not accept a full wrapped extent");
    queue.close();
    require(queue.pop(output)==output.size() && output==std::array{4.F,5.F,6.F,7.F,8.F},
        "Capture queue changed the wrapped PCM order at normal EOF");
    const auto status=queue.status();
    require(status.peak_samples==5 && !status.queued_samples && status.closed && !status.overflow &&
        !queue.push(first) && !queue.pop(output),"Capture queue lost its high-water mark or reopened after EOF");
    require(queue.workspace_bytes()>=sizeof(queue)+queue.capacity()*sizeof(float),
        "Capture queue did not charge its allocated PCM ring");
}

void check_faults_and_stop() {
    const std::array samples{.25F,.5F};
    std::array<float,2> output{};
    ReceivePcmQueue overflow(3);
    require(overflow.push(samples) && !overflow.push(samples) && !overflow.pop(output) && !overflow.push(samples),
        "Capture queue retained post-gap data after overflow");
    const auto overflowed=overflow.status();
    require(overflowed.closed && overflowed.overflow && overflowed.peak_samples==2 && overflowed.queued_samples==2,
        "Capture queue lost its overflow receipt");
    ReceivePcmQueue failed(3);
    require(failed.push(samples),"Capture fault fixture did not enqueue");
    failed.close("capture discontinuity");
    failed.close();
    require(!failed.pop(output) && failed.status().error=="capture discontinuity" && !failed.push(samples),
        "Capture queue drained into a discontinuity or discarded its error");
    ReceivePcmQueue cancelled(3);
    require(cancelled.push(samples),"Capture cancellation fixture did not enqueue");
    std::stop_source stop;
    stop.request_stop();
    require(!cancelled.pop(output,stop.get_token()) && cancelled.status().queued_samples==2,
        "Stopped capture consumer discarded or reported samples");
    const std::runtime_error cancellation("audio operation cancelled");
    const std::runtime_error fault("capture discontinuity");
    require(expected_audio_cancellation(cancellation,stop.get_token()) &&
            !expected_audio_cancellation(cancellation,{}) && !expected_audio_cancellation(fault,stop.get_token()),
            "Capture teardown suppressed a genuine device fault or mislabeled cancellation");
}
}

int main() {
    try {
        check_profile_caps();
        check_wire_coding_burst();
        check_wrap_and_close();
        check_faults_and_stop();
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}

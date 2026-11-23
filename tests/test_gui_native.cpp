#include "../src/gui/state.hpp"
#include "../src/gui/record_presentations.hpp"
#include "../src/gui/application.hpp"
#include "../src/gui/controller.hpp"
#include "../src/gui/transmit_scope.hpp"
#include "datapump/compression.hpp"
#include "datapump/live.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>
#include <vector>

using namespace datapump;
void check(bool value, std::source_location location=std::source_location::current()) {
    if (!value) throw Error("Native GUI policy test failed at line "+std::to_string(location.line()));
}
StreamContent content(std::uint8_t id, std::size_t size) {
    StreamContent result;
    result.message.local_id[0]=id;
    result.message.data.resize(size,id);
    return result;
}
bool document_contains(const gui::ui::DocumentNode& node, std::string_view text) {
    if (node.text.find(text)!=std::string::npos) return true;
    for (const auto& child:node.children) if (document_contains(child,text)) return true;
    return false;
}
void planner_quick_references() {
    gui::Application app({.simulation=true});
    app.navigate(gui::ui::Page::planner);
    const auto collapsed=app.document(gui::ui::Page::planner,900);
    check(collapsed && document_contains(*collapsed,"Model limits and references"));
    check(!document_contains(*collapsed,"Quick references"));
    app.dispatch(gui::ui::Command::planner_toggle_details);
    const auto expanded=app.document(gui::ui::Page::planner,900);
    check(expanded && document_contains(*expanded,"Hide details"));
    for (const auto* reference:{"Sub-9 kHz", "Groundwave", "Skywave", "Meteor burst", "Earth–Moon–Earth"})
        check(document_contains(*expanded,reference));
    check(document_contains(*expanded,"not propagation predictions"));
    app.dispatch(gui::ui::Command::planner_toggle_details);
    const auto hidden=app.document(gui::ui::Page::planner,900);
    check(hidden && !document_contains(*hidden,"Quick references"));
}
void shared_robust_target() {
    gui::Application app({.simulation=true});
    app.select(gui::ui::Field::fast_mode,"robust");
    const auto& declarations=gui::ui::console_screen();
    for(const auto& control:declarations) {
        const std::string_view label=control.label;
        check(label!="Pattern / tone"&&label!="Helper Carrier Frequency"&&
              label!="Long / file target SNR (dB-Hz)"&&label!="RX targets (dB-Hz)");
    }
    const auto target=std::find_if(declarations.begin(),declarations.end(),[](const auto& control) {
        return control.field==gui::ui::Field::snr;
    });
    check(target!=declarations.end());
    check(app.field(gui::ui::Field::snr).text=="32"&&app.field(gui::ui::Field::planner_target).text=="32");
    check(std::string_view(target->label)=="Target SNR (dB-Hz)");
    check(app.control(*target).label=="Target SNR (dB-Hz) · Budget target 47");
    app.edit(gui::ui::Field::link_loss,"90 dB");
    check(app.control(*target).label=="Target SNR (dB-Hz) · Budget target 77");
    app.edit(gui::ui::Field::link_loss,"");
    check(app.control(*target).label==target->label);
    app.edit(gui::ui::Field::link_loss,"90.25 dB");
    check(app.control(*target).label=="Target SNR (dB-Hz) · Budget target 76");
    check(app.field(gui::ui::Field::snr).text=="32");
    app.edit(gui::ui::Field::snr,"40");
    const auto first_command=app.field(gui::ui::Field::planner_command).text;
    check(app.field(gui::ui::Field::planner_target).text=="40"&&first_command.find("--target-snr 40")!=std::string::npos);
    app.edit(gui::ui::Field::snr,"40x");
    check(app.field(gui::ui::Field::snr).text=="40x"&&app.field(gui::ui::Field::planner_target).text=="40"&&
          app.field(gui::ui::Field::planner_command).text==first_command);
    app.edit(gui::ui::Field::snr,"40.0000");
    check(app.field(gui::ui::Field::planner_target).text=="40.0000");
    check(app.submit(*target,false,false));
    check(app.field(gui::ui::Field::snr).text=="40"&&app.field(gui::ui::Field::planner_target).text=="40");
    app.navigate(gui::ui::Page::planner);
    app.edit(gui::ui::Field::planner_target,"55");
    check(app.field(gui::ui::Field::snr).text=="55"&&
          app.field(gui::ui::Field::planner_command).text.find("--target-snr 55")!=std::string::npos);

    std::vector<std::string> args{"--target-snr","-3"};
    gui::Launch loaded;loaded.settings=gui::launch_command::parse_arguments(args);
    gui::Application from_command(std::move(loaded));
    check(from_command.field(gui::ui::Field::snr).text=="-3"&&from_command.field(gui::ui::Field::planner_target).text=="-3");
    auto invalid=gui::Launch{};invalid.settings=gui::launch_command::Patch{};invalid.settings->target_db_hz=201;
    bool rejected=false;
    try {gui::Application bad(std::move(invalid));} catch(const Error&) {rejected=true;}
    check(rejected);
}
void shared_gpu_toggle() {
    using F=gui::ui::Field;using S=gui::ui::Slot;
    const auto& declarations=gui::ui::console_screen();
    const auto gpu=std::find_if(declarations.begin(),declarations.end(),[](const auto& control){return control.field==F::gpu;});
    const auto developer=std::find_if(declarations.begin(),declarations.end(),[](const auto& control){return control.field==F::developer_mode;});
    check(gpu!=declarations.end()&&developer!=declarations.end()&&gpu<developer);
    check(gpu->slot==S::gpu&&gpu->persistent&&gpu->scope==gui::ui::ScreenScope::shared);
    const bool compiled=live::gpu_receiver_available();
    const auto* explicit_command=compiled?"--gpu yes":"--gpu no";
    gui::Application app({.simulation=true});
    check(!app.field(F::developer_mode).checked&&app.control(*gpu).visible);
    check(!app.field(F::gpu).checked&&!app.control(*gpu).enabled);
    app.select(F::fast_mode,"fast");
    check(app.control(*gpu).visible&&!app.control(*gpu).enabled);
    app.select(F::fast_mode,"robust");
    check(!app.control(*gpu).enabled);
    app.toggle(F::gpu,true);
    check(!app.field(F::gpu).checked&&app.field(F::planner_command).text.find("--gpu ")==std::string::npos);

    gui::Launch simulation{.simulation_only=true};
    gui::Application lab(simulation);
    check(!lab.field(F::gpu).checked&&!lab.control(*gpu).enabled);
    lab.toggle(F::gpu,false);
    lab.edit(F::link_power,"1 W");
    check(!lab.field(F::gpu).checked&&lab.field(F::planner_command).text.find("--gpu ")==std::string::npos);
    gui::Controller automatic({.simulation_only=true});
    check(automatic.settings().gpu==compiled);

    simulation.settings=gui::launch_command::Patch{};
    simulation.settings->gpu=false;
    gui::Application saved_cpu(simulation);
    check(!saved_cpu.field(F::gpu).checked&&!saved_cpu.control(*gpu).enabled&&
          saved_cpu.field(F::planner_command).text.find("--gpu no")!=std::string::npos);

    gui::launch_command::Patch requested_gpu;
    requested_gpu.gpu=true;
    gui::Controller from_command({.simulation=true,.launch_settings=requested_gpu});
    check(!from_command.field(F::gpu).checked&&!from_command.field(F::gpu).enabled);
    check(from_command.settings().gpu==compiled&&
          from_command.field(F::planner_command).text.find(explicit_command)!=std::string::npos);

    gui::launch_command::Patch fp64;
    fp64.search_arithmetic="fp64-force";
    gui::Controller cpu_precision({.simulation=true,.launch_settings=fp64});
    check(!cpu_precision.settings().gpu&&cpu_precision.field(F::search_arithmetic).selected=="fp64-force");
}
void gpu_route_diagnostics() {
    live::Snapshot snapshot;
    snapshot.gpu_device="fixture GPU";
    check(gui::gpu_search_diagnostics(snapshot)=="Vulkan ready · CPU tracking · fixture GPU");
    // Selection may be reported before any completed CPU or GPU scoring.
    snapshot.gpu_native_coarse_lag_sessions=1;
    check(gui::gpu_search_diagnostics(snapshot)==
          "Vulkan ready · CPU tracking · native lag search selected · fixture GPU");
    snapshot.gpu_score_rows=42;
    check(gui::gpu_search_diagnostics(snapshot)==
          "Vulkan GPU search + CPU tracking · 42 rows · native lag search selected · fixture GPU");
    snapshot.gpu_native_coarse_lag_sessions=0;
    check(gui::gpu_search_diagnostics(snapshot)==
          "Vulkan GPU search + CPU tracking · 42 rows · fixture GPU");
    snapshot.gpu_fp32_score_rows=32;
    snapshot.gpu_fp16_storage_score_rows=10;
    snapshot.gpu_vulkan_timed_submissions=4;
    snapshot.gpu_vulkan_device_command_seconds=.000058624;
    check(gui::gpu_search_diagnostics(snapshot)==
          "Vulkan GPU search + CPU tracking · 42 rows · FP32 32 rows · FP16 storage 10 rows · GPU commands 58.6 µs across 4 submissions · fixture GPU");
    snapshot.gpu_native_coarse_precision_sessions=1;
    check(gui::gpu_search_diagnostics(snapshot).find("native FP64 tight-clock search selected")!=std::string::npos);
    snapshot.gpu_native_coarse_precision_sessions=0;
    snapshot.gpu_fp32_score_rows=0;
    snapshot.gpu_fp16_storage_score_rows=0;
    snapshot.gpu_vulkan_timed_submissions=0;
    snapshot.gpu_vulkan_device_command_seconds=0;

    snapshot.gpu_backend="cuda:0";
    snapshot.gpu_statistics_blocks=7;
    snapshot.gpu_cuda_fp16_calls=2;
    snapshot.gpu_cuda_int8_calls=3;
    snapshot.gpu_selection_reason="automatic CUDA";
    check(gui::gpu_search_diagnostics(snapshot)==
          "GPU search · 42 rows · GPU fine statistics 7 · FP16 matrix 2 calls · INT8 matrix 3 calls · automatic CUDA · fixture GPU");
    snapshot.gpu_cuda_fp16_calls=0;
    snapshot.gpu_cuda_int8_calls=0;
    snapshot.gpu_statistics_blocks=0;
    snapshot.gpu_cuda_nvfp4_fine_modes=101;
    snapshot.gpu_cuda_cufft_fine_modes=202;
    check(gui::gpu_search_diagnostics(snapshot)==
          "GPU search · 42 rows · NVFP4 fine projection 101 modes · cuFFT fine projection 202 modes · automatic CUDA · fixture GPU");
    snapshot.gpu_cuda_nvfp4_fine_modes=0;
    snapshot.gpu_cuda_cufft_fine_modes=0;
    snapshot.gpu_cuda_native_fine_sessions=3;
    snapshot.gpu_cuda_native_fine_fallback_reason="small-chronological-blocks";
    check(gui::gpu_search_diagnostics(snapshot)==
          "GPU search · 42 rows · native fine fallback 3 sessions (small-chronological-blocks) · automatic CUDA · fixture GPU");
    snapshot.gpu_cuda_native_fine_sessions=0;
    snapshot.gpu_cuda_native_fine_fallback_reason.clear();
    snapshot.gpu_cuda_half_fft_score_rows=987654;
    check(gui::gpu_search_diagnostics(snapshot)==
          "GPU search · 42 rows · FP16 FFT 987654 score rows · automatic CUDA · fixture GPU");
    snapshot.gpu_cuda_half_fft_score_rows=0;
    snapshot.gpu_cuda_half_fft_fallback_summary="lag-group-route=3, fp32-minimum=1";
    check(gui::gpu_search_diagnostics(snapshot)==
          "GPU search · 42 rows · FP16 FFT skipped lag-group-route=3, fp32-minimum=1 · automatic CUDA · fixture GPU");

    snapshot.gpu_backend="vulkan:0";
    snapshot.gpu_cuda_half_fft_fallback_summary.clear();
    snapshot.gpu_cuda_native_fine_sessions=0;
    snapshot.gpu_cuda_native_fine_fallback_reason.clear();
    snapshot.gpu_vulkan_shared_workspace_limit_bytes=1024U*1024U*1024U;
    snapshot.gpu_vulkan_leased_bytes=64U*1024U*1024U;
    snapshot.gpu_vulkan_peak_leased_bytes=512U*1024U*1024U;
    snapshot.gpu_vulkan_peak_owner_lease_bytes=384U*1024U*1024U;
    snapshot.gpu_vulkan_waiting_sessions=2;
    snapshot.gpu_native_coarse_workspace_sessions=3;
    snapshot.gpu_selection_reason="automatic Vulkan";
    check(gui::gpu_search_diagnostics(snapshot)==
          "Vulkan GPU search + CPU tracking · 42 rows · Vulkan workspace leases 64/1024 MiB (peak 512, owner 384, waiting 2) · "
          "native coarse workspace fallback 3 (native-vulkan-workspace) · automatic Vulkan · fixture GPU");
    snapshot.gpu_opencl_nvfp4_fine_modes=303;
    check(gui::gpu_search_diagnostics(snapshot)==
          "Vulkan GPU search · 42 rows · OpenCL NVFP4 fine projection 303 modes · Vulkan workspace leases 64/1024 MiB "
          "(peak 512, owner 384, waiting 2) · native coarse workspace fallback 3 (native-vulkan-workspace) · "
          "automatic Vulkan · fixture GPU");
}
void text_and_raw_draft_boundaries() {
    using F=gui::ui::Field;using C=gui::ui::Command;
    gui::Application app({.simulation=true});
    app.select(F::fast_mode,"robust");
    std::string prior_digits;
    for(const char digit:{'0','1'}) {
        app.edit(F::message,std::string(1,digit));
        const auto bits=app.field(F::short_bits).text;
        Bytes wire;
        for(const auto bit:bits)wire.push_back(static_cast<std::uint8_t>(bit-'0'));
        check(!bits.empty()&&bits!=prior_digits&&app.field(F::message).text==std::string(1,digit)&&
              compression::decode_short_bits(wire)==Bytes{static_cast<std::uint8_t>(digit)});
        prior_digits=bits;
    }
    // The developer prefix uses the long text path; unsupported attachments
    // cannot replace the draft or trigger a native service.
    app.edit(F::message,std::string(236,'x'));
    check(app.field(F::repeatable).enabled);
    app.toggle(F::repeatable,true);
    check(app.field(F::repeatable).checked&&app.field(F::message).text.size()==256);
    const auto draft=app.field(F::message).text;
    check(app.field(F::short_bits).text.empty()&&app.field(F::airtime).text.starts_with("Long text · 256 bytes · FEC Off"));
    check(!app.enabled(C::attach_file));
    app.activate(C::attach_file);
    check(app.take_services().empty()&&app.field(F::message).text==draft);
    check(app.field(F::repeatable).enabled&&app.field(F::repeatable).checked);
    app.edit(F::message,draft+"x");
    check(!app.field(F::repeatable).enabled&&!app.field(F::repeatable).checked);
    check(app.field(F::message).text==std::string(237,'x'));
    app.edit(F::message,std::string(236,'x'));
    check(app.field(F::repeatable).enabled&&!app.field(F::repeatable).checked);

    app.edit(F::message,std::string(16,'x'));
    check(!app.field(F::short_bits).text.empty()&&app.field(F::airtime).text.find(" DATA + 2 scored quiet slots")!=std::string::npos);
    app.edit(F::message,std::string(17,'x'));
    check(app.field(F::message).text.size()==17&&app.field(F::short_bits).text.empty());
    check(app.field(F::airtime).text.starts_with("Long text · 17 bytes · FEC Off"));
    check(app.field(F::short_bits_detail).text.starts_with("Long text uses periodic markers")&&!app.enabled(C::transmit_short_bits));

    // Long drafts retain the existing byte quota and bounded previews. They
    // have no invented short-prefix TX or pre-compression wire-length estimate.
    const auto long_text=std::string(1024*1024,'x');
    app.edit(F::message,long_text);
    check(app.field(F::message).text==long_text&&app.field(F::binary).text.size()<=16*9);
    check(app.field(F::short_bits).text.empty()&&app.field(F::airtime).text.find("length prepared on Transmit")!=std::string::npos);
    app.edit(F::message,long_text+"x");
    check(app.field(F::message).text==long_text);
    app.edit(F::message,std::string(32,'x'));
    std::string prefix;
    for(unsigned i=0;i<16;++i)prefix+="01111001";
    app.edit(F::binary,prefix);
    check(app.field(F::message).text==std::string(16,'y')+std::string(16,'x')&&app.field(F::short_bits).text.empty());

    app.select(F::fec,"rs60");
    check(app.field(F::airtime).text.starts_with("Long text · 32 bytes · FEC RS60"));
    const auto raw=std::string(207,'0')+"1";
    app.edit(F::short_bits,raw);
    check(app.field(F::short_bits).text==raw&&app.field(F::binary_label).text=="Raw bits / 208 bits");
    check(app.field(F::short_bits_detail).text.starts_with("208 payload bits.")&&
          app.field(F::airtime).text.starts_with("208 DATA + 2 scored quiet slots"));
    check(app.field(F::fec).selected=="rs60"&&
          app.field(F::fec).display_text=="Off (exact raw bits)");
    const auto committed_binary=app.field(F::binary).text;
    app.edit(F::short_bits,raw+"0");
    check(app.field(F::short_bits_detail).text=="Enter 1-208 exact bits."&&
          app.field(F::binary).text==committed_binary);
    app.edit(F::short_bits,"00x1");
    check(app.field(F::binary).text==committed_binary&&
          app.field(F::airtime).text.find(" DATA + ")==std::string::npos);
    app.edit(F::short_bits,"001");
    check(app.field(F::short_bits).text=="001"&&app.field(F::binary_label).text=="Raw bits / 3 bits"&&
          app.field(F::airtime).text.starts_with("3 DATA + 2 scored quiet slots"));
    app.edit(F::message,"text");
    check(app.field(F::fec).enabled&&app.field(F::fec).selected=="rs60"&&app.field(F::fec).display_text.empty());
}
void pending_transmit_trace() {
    modem::TransmitTrace trace;trace.active=true;trace.source_available=true;trace.source=Bytes(64,'x');
    check(gui::transmit_scope_caption(trace).find("preparing exact wire length")!=std::string::npos);
    check(gui::transmit_scope_caption(trace).find("0 / 0")==std::string::npos);
    const auto contains=[&](std::string_view text) {
        const auto rows=gui::transmit_scope_records(trace,false);
        for(const auto& row:rows)for(const auto& cell:row.cells)if(cell.text.find(text)!=std::string::npos)return true;
        return false;
    };
    check(contains("source-coding preview unavailable")&&!contains("raw bits bypass compression"));
    trace.total_wire_bits=1216;trace.generated_bits=17;
    check(gui::transmit_scope_caption(trace).find("17 / 1216 wire bits begun")!=std::string::npos);
    trace.raw=true;
    check(contains("raw bits bypass compression"));
    trace.active=false;trace.temporary_key=true;trace.total_wire_bits=0;
    check(gui::transmit_scope_caption(trace).find("17 temporary-key noise bits begun")!=std::string::npos);
}
void retained_raw_bits() {
    gui::Signals signals;
    gui::SignalLine decoded;
    decoded.text="t";decoded.raw_bits="010";decoded.received_bits=3;
    decoded.complete=true;decoded.pattern_score=25.;
    signals.update(decoded);
    check(signals.copy_raw_bits(0)=="010" && signals.copy_text(0)=="t" &&
          signals.copy_bytes(0)==Bytes{'t'} && !signals.copy_bits(0) && !signals.copy_id(0));
    check(signals.lines()[0].raw_bits=="010" && !signals.copy_raw_bits(1) &&
          gui::signal_data_label(signals.lines()[0])=="No checksum / FEC");

    const auto unavailable=[&](gui::SignalLine line) {
        signals.clear();signals.update(std::move(line));check(!signals.copy_raw_bits(0));
    };
    auto pending=decoded;pending.complete=false;unavailable(pending);
    auto partial=decoded;partial.raw_bits="01";unavailable(partial);
    check(signals.lines()[0].raw_bits.empty());
    partial=decoded;partial.expected_bits=4;unavailable(partial);
    for (const auto& invalid : {std::string{},std::string("01x"),std::string("0 1"),std::string("01\0",3)}) {
        auto malformed=decoded;malformed.raw_bits=invalid;unavailable(malformed);
        check(signals.lines()[0].raw_bits.empty());
    }
    for (const auto score : {std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid_score=decoded;invalid_score.pattern_score=score;unavailable(invalid_score);
    }
    auto missing_score=decoded;missing_score.pattern_score.reset();unavailable(missing_score);
    auto verified=decoded;verified.validated=true;unavailable(verified);
    auto file=decoded;file.text_message=false;unavailable(file);
    auto empty=decoded;empty.text.clear();unavailable(empty);

    auto bounded=decoded;bounded.raw_bits=std::string(4096,'0');bounded.received_bits=4096;
    signals.clear();signals.update(bounded);
    check(signals.copy_raw_bits(0)==bounded.raw_bits);
    bounded.raw_bits.push_back('0');++bounded.received_bits;unavailable(bounded);
    check(signals.lines()[0].raw_bits.empty());

    gui::SignalLine binary;
    binary.binary=true;binary.text="01110100";binary.received_bits=8;binary.complete=true;
    signals.clear();signals.update(binary);
    check(signals.copy_raw_bits(0)=="01110100" && signals.copy_text(0)=="t" && !signals.copy_bits(0));
    binary.text="010";binary.received_bits=3;signals.update(binary);
    check(signals.copy_raw_bits(0)=="010" && signals.copy_bits(0)=="010" && !signals.copy_text(0));
    live::Snapshot simulation;
    live::SignalUpdate mismatch;
    mismatch.id=42;mismatch.binary=true;mismatch.complete=true;mismatch.robust_reception=true;
    mismatch.text="1";mismatch.raw_bits="1";mismatch.received_bits=1;
    mismatch.simulation_input_result=live::SimulationInputResult::mismatch;
    simulation.signals.push_back(std::move(mismatch));
    gui::Inbox inbox;
    signals.clear();gui::apply_receptions(inbox,signals,simulation);
    check(gui::signal_status_label(signals.lines()[0])=="sim mismatch" &&
          gui::signal_display_text(signals.lines()[0])=="1" &&
          signals.copy_raw_bits(0)=="1" && signals.copy_bits(0)=="1");
    live::SignalUpdate incomplete;
    incomplete.id=43;incomplete.binary=true;incomplete.complete=true;incomplete.robust_reception=true;
    incomplete.simulation_input_result=live::SimulationInputResult::incomplete;
    simulation.signals={std::move(incomplete)};
    gui::apply_receptions(inbox,signals,simulation);
    check(gui::signal_status_label(signals.lines()[1])=="sim incomplete" && !signals.copy_raw_bits(1));
    binary.complete=false;unavailable(binary);
    binary.complete=true;binary.expected_bits=4;unavailable(binary);
    binary.expected_bits=0;binary.text="01";unavailable(binary);
    binary.text="01x";unavailable(binary);
    binary.text=std::string(4097,'0');binary.received_bits=4097;unavailable(binary);
    check(signals.lines()[0].text.size()==4096);
}
void received_text_boundary() {
    const std::string allowed="aZ09,.@ -_/=\n";
    const std::string punctuation="();\\&";
    const std::string forbidden="\t\r\x1b\x7f\xc3\xa9";
    const std::string original=allowed+punctuation+forbidden;
    const std::string restricted=allowed+std::string(11,'_');
    const std::string shellcode=allowed+punctuation+std::string(6,'_');
    gui::SignalLine line;
    line.text=original;line.complete=true;line.pattern_score=25.;
    gui::Signals signals;
    signals.update(line);
    check(gui::signal_display_text(signals.lines()[0])==restricted);
    check(signals.copy_text(0)==restricted);
    check(gui::signal_records(signals)[0].cells.back().text==restricted);
    check(gui::signal_display_text(signals.lines()[0],true)==shellcode);
    check(signals.copy_text(0,true)==shellcode);
    check(gui::signal_records(signals,true)[0].cells.back().text==shellcode);
    // Changing the presentation never transforms the saveable original bytes.
    check(signals.copy_bytes(0)==Bytes(original.begin(),original.end()));
    check(gui::signal_display_text(signals.lines()[0],false)==restricted);

    // Byte-aligned raw input follows exactly the same presentation boundary;
    // the diagnostic/raw-bit operation remains lossless.
    std::string bits;
    for(const unsigned char byte:original)
        for(unsigned bit=8;bit;--bit)bits+=(byte>>(bit-1))&1?'1':'0';
    line.binary=true;line.text=bits;line.received_bits=bits.size();
    signals.clear();signals.update(line);
    check(gui::signal_display_text(signals.lines()[0])==restricted);
    check(signals.copy_text(0)==restricted && signals.copy_text(0,true)==shellcode);
    check(signals.copy_raw_bits(0)==bits);
    check(signals.copy_bytes(0)==Bytes(original.begin(),original.end()));

    // Validated interval text has the same boundary as dictionary/raw text.
    line.binary=false;line.validated=true;line.text=original;line.reception_id="verified-id";
    signals.clear();signals.update(line);
    check(gui::signal_display_text(signals.lines()[0])==restricted);
    check(gui::signal_display_text(signals.lines()[0],true)==shellcode);
    check(gui::signal_records(signals)[0].cells.back().text==restricted);

    // A transport diagnostic may already contain a restricted preview. The
    // shared receiver cache must preserve the original printable ASCII for
    // an explicitly enabled Shellcode view without recovering escaped text.
    auto decoded=content(2,original.size());
    decoded.message.data.assign(original.begin(),original.end());
    live::Snapshot snapshot;
    live::SignalUpdate update;
    update.id=42;update.text=restricted;update.validated=true;update.complete=true;
    update.reception_id=gui::id_label(decoded.message);
    snapshot.signals.push_back(update);
    transfer::Received result;
    result.content=decoded;result.stream_complete=true;result.content_validated=true;
    snapshot.received.push_back(std::move(result));
    gui::Inbox text_inbox;
    signals.clear();gui::apply_receptions(text_inbox,signals,snapshot);
    check(gui::signal_display_text(signals.lines()[0])==restricted);
    check(gui::signal_display_text(signals.lines()[0],true)==shellcode);
    check(text_inbox.items()[0].message.data==decoded.message.data);

    gui::Inbox inbox;
    auto file=content(1,3);
    file.message.kind=MessageKind::file;file.message.filename="name();&\xc3\xa9.bin";
    inbox.put(file);
    check(gui::file_records(inbox)[0].cells[0].text=="name______.bin (3 B)");
    check(inbox.items()[0].message.filename==file.message.filename);
    check(inbox.items()[0].message.data==file.message.data);
}
void reversible_admission_history() {
    using P=gui::WrongKeyAdmission;
    triad::v3::WrongKeyAdmissionEvidence evidence;
    evidence.available=true;evidence.data_bits=3;evidence.passing_bits={3,2,1,0,0,0};
    gui::SignalLine line;
    line.id=41;line.frequency_hz=1500;line.text="101";line.raw_bits="101";
    line.binary=true;line.complete=true;line.received_bits=3;
    line.robust_reception=true;line.wrong_key_evidence=evidence;
    gui::Signals signals;signals.update(line);
    check(gui::signal_records(signals,false,P::guarded).size()==1);
    check(gui::signal_records(signals,false,P::strict).empty());
    check(gui::signal_records(signals).size()==1&&signals.copy_bits(0)=="101");
    check(gui::admission_label(evidence,P::guarded)=="2/3 reduction ~1dB: 2/3 DATA bits pass");
    check(signals.lines()[0].text=="101"&&signals.lines()[0].raw_bits=="101");
    auto tie=evidence;tie.data_bits=4;tie.passing_bits={4,3,2,1,0,0};
    check(gui::admission_visible(tie,P::strict)&&!gui::admission_visible(tie,P::very_strict));
    auto single=evidence;single.data_bits=1;single.passing_bits={1,0,0,0,0,0};
    check(!gui::admission_visible(single,P::guarded));
    line.complete=false;check(gui::signal_visible(line,P::maximum));
    line.complete=true;line.robust_reception=false;check(gui::signal_visible(line,P::maximum));
    line.robust_reception=true;line.wrong_key_evidence.available=false;
    check(gui::signal_visible(line,P::maximum)&&
          gui::admission_label(line.wrong_key_evidence,P::maximum).find("unavailable")!=std::string::npos);
    auto invalid=evidence;invalid.passing_bits[2]=4;
    check(gui::admission_visible(invalid,P::strict)&&gui::admission_label(invalid,P::strict).find("unavailable")!=std::string::npos);

    auto file=content(6,5);file.message.kind=MessageKind::file;file.message.filename="original.bin";
    gui::Inbox inbox;
    inbox.put(file,gui::Inbox::ReceptionIdentity{41,1,evidence,true});
    signals.clear(); // File identity/evidence has the lifetime of its cached source.
    check(gui::file_records(inbox,P::guarded).size()==1&&gui::file_records(inbox,P::strict).empty());
    check(inbox.items()[0].message.data==file.message.data&&gui::file_records(inbox).size()==1);
    check(inbox.identity(file.message)&&inbox.identity(file.message)->wrong_key_evidence.data_bits==3);

    for(std::uint64_t id=0;id<gui::Signals::maximum_records+3;++id) {
        gui::SignalLine row;row.id=id;row.text="1";signals.update(std::move(row));
    }
    check(signals.lines().size()==gui::Signals::maximum_records&&signals.lines().front().id==3);
    const auto* retained_text=signals.lines().front().text.data();
    live::Snapshot pending;
    live::SignalUpdate changed;changed.id=gui::Signals::maximum_records+2;changed.text="pending update";
    pending.signals.push_back(changed);
    gui::apply_receptions(inbox,signals,pending);
    check(signals.lines().front().text.data()==retained_text&&signals.lines().back().text=="pending update");
    gui::Inbox small(8192);small.put(content(7,6000));
    gui::Signals recent;live::Snapshot update;
    live::SignalUpdate incoming;incoming.id=50;incoming.text=std::string(4096,'a');
    update.signals.push_back(incoming);
    gui::apply_receptions(small,recent,update);
    check(recent.lines().size()==1&&small.items().empty());
    check(recent.retained_bytes()+small.retained_bytes()<=small.capacity());
}
void required_gpu_policy() {
    using F=gui::ui::Field;
    // A saved setting cannot override an embedding's requirement. When the
    // compiled backend is unavailable, construction must fail rather than
    // allow a CPU receiver to start.
    gui::launch_command::Patch patch;patch.gpu=false;
    if(!live::gpu_receiver_available()) {
        bool rejected=false;
        try {gui::Controller required({.simulation=true,.launch_settings=patch,.require_gpu=true});}
        catch(const Error&) {rejected=true;}
        check(rejected);
        gui::Controller ordinary({.simulation=true,.launch_settings=patch});
        check(!ordinary.settings().gpu);
        return;
    }
    gui::Controller required({.simulation=true,.launch_settings=patch,.require_gpu=true});
    check(required.settings().gpu&&required.field(F::gpu).checked&&!required.field(F::gpu).enabled);
    required.toggle(F::gpu,false);
    required.edit(F::link_power,"1 W");
    check(required.settings().gpu&&required.field(F::gpu).checked&&!required.field(F::gpu).enabled);
    auto command=required.field(F::planner_command).text;
    const auto at=command.find("--gpu yes");check(at!=std::string::npos);
    command.replace(at,9,"--gpu no");
    required.edit(F::planner_command,command);
    required.activate(gui::ui::Command::planner_load_command);
    check(required.settings().gpu&&required.field(F::gpu).checked&&!required.field(F::gpu).enabled);
    gui::Launch launch;launch.simulation=true;launch.require_gpu=true;
    gui::Application application(launch);
    application.select(F::fast_mode,"fast");
    check(application.field(F::fast_mode).selected=="robust"&&!application.field(F::fast_mode).enabled);
}
void finite_duplex_policy() {
    using F=gui::ui::Field;using C=gui::ui::Command;
    gui::Controller ordinary({.simulation=true});
    check(!ordinary.settings().finite_duplex);
    gui::Controller finite({.finite_duplex=true});
    check(finite.settings().finite_duplex&&!finite.settings().simulation&&finite.settings().full_duplex);
    check(!finite.field(F::simulation).enabled&&!finite.field(F::live_duplex).enabled);
    finite.select(F::simulation,"yes");finite.toggle(F::live_duplex,false);
    check(finite.settings().finite_duplex&&!finite.settings().simulation&&finite.settings().full_duplex);
    finite.edit(F::calendar_epoch,"1793850000");finite.edit(F::live_noise_dbfs_hz,"-92.25");
    check(finite.settings().calendar&&finite.settings().live_noise_dbfs_hz);
    auto command=finite.field(F::planner_command).text;
    const auto at=command.find("--live-duplex yes");check(at!=std::string::npos);
    command.replace(at,17,"--live-duplex no");
    finite.edit(F::planner_command,command);finite.activate(C::planner_load_command);
    check(finite.settings().finite_duplex&&!finite.settings().simulation&&finite.settings().full_duplex);
    gui::Launch launch;launch.finite_duplex=true;
    gui::Application app(launch);
    app.select(F::fast_mode,"fast");
    check(app.field(F::fast_mode).selected=="robust"&&!app.field(F::fast_mode).enabled);
    app.select(F::fast_mode,"legacy");
    check(app.field(F::fast_mode).selected=="robust");
}
void data_cadence_policy() {
    using F = gui::ui::Field;
    using C = gui::ui::Command;
    gui::Controller controller({.simulation=true});
    const auto& cadence=controller.field(F::data_cadence);
    check(controller.settings().data_cadence == live::DataCadence::scheduled_bridge &&
          cadence.selected == "scheduled-bridge" &&
          std::none_of(cadence.options.begin(),cadence.options.end(),[](const auto& option){return option.id=="default";}) &&
          std::any_of(cadence.options.begin(),cadence.options.end(),[](const auto& option){
              return option.id=="scheduled-bridge"&&option.label=="Scheduled + short transitions";
          }));
    check(gui::launch_command::parse(controller.field(F::planner_command).text).data_cadence=="scheduled-bridge");
    gui::Controller from_legacy({.simulation=true, .launch_settings=gui::launch_command::Patch{.data_cadence="default"}});
    check(from_legacy.settings().data_cadence == live::DataCadence::scheduled_bridge &&
          from_legacy.field(F::data_cadence).selected == "scheduled-bridge");
    from_legacy.edit(F::planner_command, "--rate 360 --fhss fake-fcc");
    from_legacy.activate(C::planner_load_command);
    check(from_legacy.settings().fhss_profile == "fake-fcc" &&
          from_legacy.field(F::data_cadence).selected == "scheduled-bridge" &&
          from_legacy.settings().data_cadence == live::DataCadence::scheduled_bridge);
    from_legacy.select(F::fhss, "off");
    check(from_legacy.settings().fhss_profile == "off" &&
          from_legacy.settings().data_cadence == live::DataCadence::scheduled_bridge);
    controller.select(F::data_cadence, "continuous");
    check(controller.settings().data_cadence == live::DataCadence::continuous);
    const auto saved = gui::launch_command::parse(controller.field(F::planner_command).text);
    check(saved.data_cadence == "continuous");
    gui::Controller fresh({.simulation=true, .launch_settings=saved});
    check(fresh.settings().data_cadence == live::DataCadence::continuous &&
          fresh.field(F::data_cadence).selected == "continuous");
    fresh.select(F::data_cadence, "scheduled-fill");
    check(fresh.settings().data_cadence == live::DataCadence::scheduled_fill);
    const auto fill = gui::launch_command::parse(fresh.field(F::planner_command).text);
    gui::Controller filled({.simulation=true, .launch_settings=fill});
    check(fill.data_cadence == "scheduled-fill" && filled.settings().data_cadence == live::DataCadence::scheduled_fill &&
          filled.field(F::data_cadence).selected == "scheduled-fill");
    filled.select(F::data_cadence, "scheduled-bridge");
    const auto bridge = gui::launch_command::parse(filled.field(F::planner_command).text);
    gui::Controller bridged({.simulation=true, .launch_settings=bridge});
    check(bridge.data_cadence == "scheduled-bridge" &&
          bridged.settings().data_cadence == live::DataCadence::scheduled_bridge &&
          bridged.field(F::data_cadence).selected == "scheduled-bridge");
    fresh.select(F::data_cadence, "scheduled");
    check(fresh.settings().data_cadence == live::DataCadence::scheduled);
    fresh.edit(F::planner_command, "--target-snr 33");
    fresh.activate(C::planner_load_command);
    check(fresh.settings().target_cn0_db_hz == 33 &&
          fresh.settings().data_cadence == live::DataCadence::scheduled);
    fresh.edit(F::planner_command, "--data-cadence default");
    fresh.activate(C::planner_load_command);
    check(fresh.settings().data_cadence == live::DataCadence::scheduled_bridge &&
          fresh.field(F::data_cadence).selected == "scheduled-bridge");
}
void fhss_rate_compatibility() {
    using F = gui::ui::Field;
    gui::Controller defaults({.simulation=true});
    check(defaults.field(F::bandwidth).text == "3.6 kHz" &&
          defaults.field(F::fhss).selected == "off");
    defaults.select(F::fhss, "fake-fcc");
    check(defaults.settings().fhss_profile == "fake-fcc" &&
          defaults.field(F::fhss).selected == "fake-fcc");

    gui::Controller low_rate({.simulation=true});
    low_rate.edit(F::bandwidth, "36 Hz");
    check(low_rate.settings().transfer.modem.bandwidth_hz == 36 &&
          low_rate.field(F::fhss).selected == "off");
    low_rate.select(F::fhss, "fake-fcc");
    check(low_rate.settings().fhss_profile == "off" &&
          low_rate.field(F::fhss).selected == "fake-fcc" &&
          low_rate.field(F::status).text.find(
              "FHSS cannot fit a protected DATA pulse in each 400 ms dwell at this Rate and Spreading. "
              "Increase Rate or Spreading, or turn FHSS off.") != std::string::npos);
    low_rate.edit(F::bandwidth, "372 Hz");
    check(low_rate.settings().transfer.modem.bandwidth_hz == 372 &&
          low_rate.settings().fhss_profile == "fake-fcc");
}
void simulation_launch_defaults() {
    using F=gui::ui::Field;
    using C=gui::ui::Command;
    gui::Controller simulation({.simulation_only=true});
    check(simulation.settings().simulation && !simulation.field(F::simulation).enabled &&
          simulation.field(F::audio_error).text=="Default");
    simulation.edit(F::clock_accuracy,"1ms");
    check(simulation.field(F::audio_error).text=="Default");
    simulation.select(F::simulation,"no");
    simulation.edit(F::planner_command,
        "datapump-gui --tx-dbm 3 --helper-power 0 --helper-ratio-limit 90 --helper-callsign= "
        "--path-loss-db 90 --noise-dbm-hz -164 --oscillator gpsdo-xo --rf-oscillator gpsdo-xo "
        "--spread-preset hf-worst --selective-perturbation each-65 --shift 3e+07 --search-margin 3 "
        "--reference independent --target-snr 32 --rate 3.6000000000000001 --carrier 3.0001725e+07 "
        "--doppler 0c --calendar-epoch 1793850000 --clock-sync default --audio-error 70ms "
        "--dsss-mode off --dsss-factor 10 --dsss-version interleave --live-duplex yes --keyset amateur");
    simulation.activate(C::planner_load_command);
    check(simulation.settings().simulation && !simulation.settings().full_duplex &&
          simulation.settings().calendar && !simulation.settings().transfer.clock_sync);
    check(simulation.field(F::bandwidth).text=="3.6 Hz" && simulation.settings().transfer.modem.bandwidth_hz==3.6);
    check(simulation.field(F::audio_error).text=="70ms" &&
          simulation.field(F::planner_command).text.find("--audio-error 70ms")!=std::string::npos);
    simulation.edit(F::audio_error,"Default");
    check(simulation.field(F::planner_command).text.find("--audio-error default")!=std::string::npos);
    check(simulation.settings().minimum_carrier_spacing_hz==265);
    simulation.edit(F::helper_power,"4 W");
    check(simulation.field(F::status).text.find("Minimum carrier spacing")!=std::string::npos);
    simulation.select(F::minimum_carrier_spacing,"0");
    simulation.edit(F::helper_power,"4 W");
    check(std::abs(simulation.settings().helper.power_w-4)<1e-12 &&
          std::any_of(simulation.field(F::carrier).options.begin(),simulation.field(F::carrier).options.end(),
                      [](const auto& option){return option.enabled;}));
    simulation.edit(F::helper_power,"15 kW");
    check(!simulation.field(F::carrier).options.empty() &&
          !simulation.field(F::carrier).options.front().enabled &&
          simulation.field(F::status).text.find("6 kHz sampled band")!=std::string::npos);
    simulation.edit(F::helper_power,"1 W");
    check(simulation.settings().helper.power_w==1 &&
          simulation.settings().data_cadence==live::DataCadence::scheduled_bridge &&
          simulation.field(F::data_cadence).selected=="scheduled-bridge" &&
          std::any_of(simulation.field(F::carrier).options.begin(),simulation.field(F::carrier).options.end(),
                      [](const auto& option){return option.enabled;}));
}
void admission_browser_controller() {
    using F=gui::ui::Field;using C=gui::ui::Command;
    gui::Controller controller({.simulation=true});
    // Populate the real controller's owned history without starting audio.
    // The accessors expose const views; the underlying controller is mutable.
    auto& signals=const_cast<gui::Signals&>(controller.signals());
    auto& inbox=const_cast<gui::Inbox&>(controller.inbox());
    auto& snapshot=const_cast<live::Snapshot&>(controller.snapshot());
    triad::v3::WrongKeyAdmissionEvidence evidence;
    evidence.available=true;evidence.data_bits=3;evidence.passing_bits={3,0,0,0,0,0};
    gui::SignalLine raw;raw.id=42;raw.frequency_hz=1500;raw.text="110";raw.raw_bits="110";
    raw.binary=true;raw.complete=true;raw.received_bits=3;raw.robust_reception=true;raw.wrong_key_evidence=evidence;
    raw.simulation_input_result=live::SimulationInputResult::mismatch;
    signals.update(raw);
    auto file=content(9,4);file.message.kind=MessageKind::file;file.message.filename="keep.bin";
    inbox.put(file,gui::Inbox::ReceptionIdentity{43,1,evidence,true});
    snapshot.bit_seconds=123;snapshot.slot_stride_seconds=125;snapshot.transmitting=true;
    live::PatternStep step;
    step.decision=live::PatternDecision::data;step.bit=1;step.evidence_complete=true;
    step.browser_admission=std::array{true,false,false,false,false,false};
    snapshot.pattern_steps.push_back(step);
    controller.select(F::wrong_key_admission,"strict");
    check(controller.snapshot().bit_seconds==123&&controller.snapshot().slot_stride_seconds==125&&controller.snapshot().transmitting);
    check(controller.field(F::wrong_key_admission).enabled&&controller.settings().wrong_key_admission=="strict");
    check(controller.field(F::signals).records.empty()&&controller.field(F::files).records.empty());
    check(controller.field(F::profile_reference).records.front().cells.front().text.find("DATA rejected")!=std::string::npos);
    check(controller.snapshot().pattern_steps.front().bit==1);
    check(controller.field(F::status).text.find("1 signals / 1 files hidden")!=std::string::npos);
    check(controller.field(F::planner_command).text.find("--wrong-key-admission strict")!=std::string::npos);
    // Even a stale adapter selection cannot act on a now-hidden record.
    const_cast<gui::ui::FieldState&>(controller.field(F::signals)).selected="42";
    const_cast<gui::ui::FieldState&>(controller.field(F::files)).selected=gui::id_label(file.message);
    check(!controller.enabled(C::copy_signal)&&!controller.enabled(C::paste_signal)&&
          !controller.enabled(C::copy_raw_signal)&&!controller.enabled(C::save_file));
    controller.select(F::wrong_key_admission,"current");
    check(controller.field(F::signals).records.size()==1&&controller.field(F::files).records.size()==1);
    check(controller.field(F::status).text=="Unfiltered: showing all retained receptions."&&
          controller.settings().wrong_key_admission=="current"&&
          controller.field(F::planner_command).text.find("--wrong-key-admission current")!=std::string::npos);
    controller.select(F::signals,"42");
    check(controller.enabled(C::copy_signal)&&controller.enabled(C::copy_raw_signal)&&controller.enabled(C::save_file));
    check(signals.copy_bits(0)=="110"&&signals.copy_raw_bits(0)=="110"&&inbox.items()[0].message.data==file.message.data);
    check(controller.field(F::profile_reference).records.front().cells.front().text.find("DATA rejected")==std::string::npos);
    check(controller.snapshot().bit_seconds==123);
    for(std::uint64_t id=100;id<240;++id) {
        auto row=raw;row.id=id;row.wrong_key_evidence.passing_bits.fill(3);signals.update(row);
        auto item=content(static_cast<std::uint8_t>(id),1);item.message.kind=MessageKind::file;
        inbox.put(std::move(item));
    }
    // Lose the selected weak record under the filter, then browse older pages
    // without deleting history, treating navigation IDs as payloads or DSP work.
    controller.select(F::wrong_key_admission,"strict");
    check(controller.field(F::signals).records.size()<=gui::reception_page_size+2);
    check(controller.field(F::signals).records.front().id==gui::signals_older_id&&
          !controller.field(F::signals).records.front().activatable);
    controller.select(F::signals,std::string(gui::signals_older_id));
    check(!controller.enabled(C::copy_signal)&&controller.field(F::signals).records.back().id==gui::signals_newer_id);
    controller.select(F::signals,std::string(gui::signals_older_id));
    check(controller.field(F::signals).records.front().id=="100");
    controller.select(F::signals,"100");
    controller.select(F::wrong_key_admission,"current");
    check(controller.field(F::signals).selected=="100"&&controller.enabled(C::copy_signal));
    controller.select(F::files,std::string(gui::files_older_id));
    check(controller.field(F::files).records.size()<=gui::reception_page_size+2&&
          controller.field(F::files).records.back().id==gui::files_newer_id);
    check(signals.lines().size()==141&&inbox.items().size()==141&&controller.snapshot().bit_seconds==123);
}
int main() {
    try {
        planner_quick_references();
        shared_robust_target();
        shared_gpu_toggle();
        gpu_route_diagnostics();
        text_and_raw_draft_boundaries();
        pending_transmit_trace();
        retained_raw_bits();
        received_text_boundary();
        reversible_admission_history();
        admission_browser_controller();
        required_gpu_policy();
        finite_duplex_policy();
        data_cadence_policy();
        fhss_rate_compatibility();
        simulation_launch_defaults();
        check(gui::parse_binary_bits("0")==Bytes({0}));
        check(gui::parse_binary_bits("000101")==Bytes({0,0,0,1,0,1}));
        check(gui::parse_binary_bits("\t00 01\r\n0\f1\v")==Bytes({0,0,0,1,0,1}));
        check(gui::parse_binary_bits(std::string(1025,'0'))==Bytes(1025,0));
        for (const std::string& invalid : {std::string{},std::string(" \t\r\n"),std::string("0102"),
                                         std::string("0b101"),std::string("0,1"),std::string("01x"),
                                         std::string("0\0" "1",3),std::string("0\xc2\xa0" "1")}) {
            bool rejected_bits=false;
            try { (void)gui::parse_binary_bits(invalid); } catch (const Error&) { rejected_bits=true; }
            check(rejected_bits);
        }
        gui::Inbox inbox(5);
        inbox.put(content(1,3));
        inbox.put(content(1,2));
        check(inbox.items().size()==1 && inbox.size_bytes()==2);
        inbox.put(content(2,4));
        check(inbox.items().size()==1 && inbox.items()[0].message.local_id[0]==2);
        bool rejected=false;
        try { inbox.put(content(3,6)); } catch (const Error&) { rejected=true; }
        check(rejected && inbox.size_bytes()==4);
        inbox.clear();
        check(inbox.items().empty() && inbox.size_bytes()==0);
        gui::Inbox empty_payloads(1);
        for (std::size_t n=0;n<4097;++n) {
            auto item=content(static_cast<std::uint8_t>(n),0);
            item.message.local_id[1]=static_cast<std::uint8_t>(n>>8);
            empty_payloads.put(std::move(item));
        }
        check(empty_payloads.items().size()==4096 && empty_payloads.size_bytes()==0);
        check(gui::valid_clipboard_text(Bytes{'h','i','\n',0xc3,0xa9,0xf0,0x9f,0x8c,0x8d}));
        for (const auto& bad: {Bytes{0},Bytes{0xc0,0x80},Bytes{0xed,0xa0,0x80},
                              Bytes{0xf4,0x90,0x80,0x80},Bytes{0xe2,0x82},Bytes{0x80}})
            check(!gui::valid_clipboard_text(bad));
        check(gui::valid_clipboard_text({}));
        check(gui::display_label("name\n\tlabel")=="name  label");
        check(gui::id_label(content(0xab,0).message).substr(0,2)=="ab");
        gui::Signals signals;
        signals.update({7,1500,"uncertain tezt",false,{}});
        check(!signals.copy_id(0));
        check(gui::signal_preamble_label(signals.lines()[0])=="Preamble --");
        check(gui::signal_data_label(signals.lines()[0])=="Data pre-FEC pending");
        signals.update({7,1500,"corrected text",false,{}});
        check(signals.lines().size()==1 && signals.lines()[0].text=="corrected text");
        signals.update({7,1500,"corrected text",true,"verified-id"});
        check(signals.copy_id(0)=="verified-id");
        gui::SignalLine quality{7,1500,"corrected text",true,"verified-id",true,75.0,
                                StreamBitAccuracy{800,3}};
        check(gui::signal_preamble_label(quality)=="Preamble 75.0%");
        quality.preamble_received_percent=99.96;
        check(gui::signal_preamble_label(quality)=="Preamble >99.9%");
        quality.preamble_received_percent=100.;
        check(gui::signal_preamble_label(quality)=="Preamble 100.0%");
        quality.preamble_received_percent=75.;
        check(gui::signal_data_label(quality)=="Data 99.62% pre-FEC");
        signals.update(quality);
        signals.update({7,1500,"late unvalidated text",false,{}});
        check(signals.lines()[0].text=="corrected text" && signals.copy_id(0)=="verified-id");
        check(signals.lines()[0].preamble_received_percent==75.0 &&
              signals.lines()[0].pre_fec_accuracy->corrected_data_bits==3);
        quality.pre_fec_accuracy=StreamBitAccuracy{800,0};
        check(gui::signal_data_label(quality)=="Data 100% pre-FEC");
        quality.fec_stats.parity.repaired_bytes=2;
        check(gui::signal_repair_label(quality)=="RS repaired 2 B (data 0, parity 2)");
        quality.pre_fec_accuracy=StreamBitAccuracy{799,0,1};
        quality.fec_stats.data.erased_bytes=quality.fec_stats.data.repaired_bytes=1;
        quality.fec_stats.data.missing_bits=1;
        check(gui::signal_data_label(quality)=="Data 100% known" &&
              gui::signal_repair_label(quality)=="RS repaired 3 B (data 1, parity 2; erasures 1; missing data bits 1)");
        quality.pre_fec_accuracy=StreamBitAccuracy{800,3,1};
        check(gui::signal_data_label(quality)=="Data 99.62% known");
        quality.fec_stats={};
        quality.pre_fec_accuracy=StreamBitAccuracy{8000000,1};
        check(gui::signal_data_label(quality)=="Data >99.99% pre-FEC");
        quality.pre_fec_accuracy=StreamBitAccuracy{800,800};
        check(gui::signal_data_label(quality)=="Data 0.00% pre-FEC");
        for (const auto invalid : {StreamBitAccuracy{0,0},StreamBitAccuracy{10,11}}) {
            quality.pre_fec_accuracy=invalid;
            check(gui::signal_data_label(quality)=="Data pre-FEC --");
        }
        quality.pre_fec_accuracy.reset();
        check(gui::signal_data_label(quality)=="Data pre-FEC --");
        quality.validated=false; quality.pre_fec_accuracy=StreamBitAccuracy{800,0};
        check(gui::signal_data_label(quality)=="Data pre-FEC pending");
        for (const auto invalid : {-1.,101.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            quality.preamble_received_percent=invalid;
            check(gui::signal_preamble_label(quality)=="Preamble --");
        }
        for (std::uint64_t id=8;id<80;++id) signals.update({id,1500,std::string(5000,'a'),false,{}});
        check(signals.lines().size()==73 && signals.lines().back().text.size()==4096);
        signals.clear(); check(signals.lines().empty());
        signals.update({90,1500,"readme.txt",true,"file-id",false});
        check(!signals.copy_id(0)); // Even UTF-8 files require explicit Save.
        gui::SignalLine binary_line{91,1500,"Receiving binary...",false,{},false};
        binary_line.binary=true;
        binary_line.expected_bits=3;
        check(gui::signal_status_label(binary_line)=="binary pending");
        check(gui::signal_preamble_label(binary_line)=="Preamble none");
        check(gui::signal_data_label(binary_line)=="Raw observations");
        signals.clear(); signals.update(binary_line);
        check(!signals.copy_id(0) && !signals.copy_bits(0));
        binary_line.text="00"; binary_line.received_bits=2;
        signals.update(binary_line);
        check(signals.lines()[0].text=="00" && !signals.copy_bits(0));
        binary_line.text="001"; binary_line.complete=true; binary_line.received_bits=3;
        signals.update(binary_line);
        check(gui::signal_status_label(signals.lines()[0])=="binary received");
        check(!gui::signal_byte_aligned(signals.lines()[0]) && gui::signal_display_text(signals.lines()[0])=="001");
        check(!signals.copy_id(0) && !signals.lines()[0].validated && signals.lines()[0].complete);
        check(signals.copy_bits(0)=="001" && !signals.copy_text(0) && !signals.copy_bytes(0));
        binary_line.text="0"; binary_line.complete=false;
        signals.update(binary_line);
        check(signals.lines()[0].text=="001" && signals.lines()[0].complete);
        check(signals.copy_bits(0)=="001");
        // A raw result never gains stream authentication or accuracy labels,
        // even if a caller accidentally supplies unrelated content fields.
        binary_line.id=92; binary_line.text="001"; binary_line.complete=true;
        binary_line.validated=true; binary_line.reception_id="not-a-raw-reception-id";
        binary_line.preamble_received_percent=100.; binary_line.pre_fec_accuracy=StreamBitAccuracy{8,0};
        signals.update(binary_line);
        check(!signals.lines()[1].validated && signals.lines()[1].reception_id.empty() &&
              !signals.lines()[1].preamble_received_percent && !signals.lines()[1].pre_fec_accuracy);
        check(!signals.copy_id(1) && signals.copy_bits(1)=="001");
        for (const auto& invalid_text : {std::string{},std::string("00x"),std::string("0 1"),std::string("00\0",3)}) {
            binary_line.id=93; binary_line.text=invalid_text;
            signals.update(binary_line);
            check(!signals.copy_bits(2));
        }
        binary_line.text="001"; binary_line.expected_bits=4;
        signals.update(binary_line); check(!signals.copy_bits(2));
        binary_line.expected_bits=0; binary_line.received_bits=0;
        signals.update(binary_line); check(!signals.copy_bits(2));
        binary_line.received_bits=3;binary_line.pattern_score=25.;
        signals.update(binary_line);check(signals.copy_bits(2)=="001");
        binary_line.pattern_score.reset();
        binary_line.text=std::string(5000,'0'); binary_line.expected_bits=binary_line.received_bits=5000;
        signals.update(binary_line);
        check(signals.lines()[2].complete && signals.lines()[2].text.size()==4096 && !signals.copy_bits(2));
        check(gui::signal_data_label(signals.lines()[2])=="Raw observations / prefix");
        check(gui::signal_byte_aligned(signals.lines()[2]) && gui::signal_status_label(signals.lines()[2])=="text received");
        check(gui::signal_display_text(signals.lines()[2])==std::string(512,'_'));
        check(!signals.copy_text(2) && !signals.copy_bytes(2));
        check(!signals.copy_bits(3) && !signals.copy_text(3) && !signals.copy_bytes(3));

        gui::Signals byte_signals;
        gui::SignalLine byte_line;
        byte_line.id=100; byte_line.binary=true; byte_line.text_message=false;
        byte_line.text="01001000"; byte_line.received_bits=8; byte_line.expected_bits=32;
        byte_signals.update(byte_line);
        check(!gui::signal_byte_aligned(byte_signals.lines()[0]));
        check(gui::signal_status_label(byte_signals.lines()[0])=="binary pending" &&
              gui::signal_display_text(byte_signals.lines()[0])=="01001000");
        check(!byte_signals.copy_bits(0) && !byte_signals.copy_text(0) && !byte_signals.copy_bytes(0));
        byte_line.complete=true; byte_line.text="01001000011001010110110001110000"; byte_line.received_bits=32;
        byte_signals.update(byte_line);
        check(byte_signals.lines().size()==1 && byte_signals.lines()[0].binary && byte_signals.lines()[0].text==byte_line.text);
        check(gui::signal_byte_aligned(byte_signals.lines()[0]) && gui::signal_status_label(byte_signals.lines()[0])=="text received");
        check(gui::signal_display_text(byte_signals.lines()[0])=="Help" && byte_signals.copy_text(0)=="Help");
        check(byte_signals.copy_bytes(0)==Bytes({'H','e','l','p'}) && !byte_signals.copy_bits(0) && !byte_signals.copy_id(0));

        byte_line.id=101; byte_line.text="1100001110101001"; byte_line.received_bits=16; byte_line.expected_bits=0;
        byte_line.pattern_score=25.;
        byte_signals.update(byte_line);
        check(gui::signal_display_text(byte_signals.lines()[1])=="__" && byte_signals.copy_text(1)=="__");
        check(gui::signal_display_text(byte_signals.lines()[1],true)=="__" && byte_signals.copy_text(1,true)=="__");
        check(byte_signals.copy_bytes(1)==Bytes({0xc3,0xa9}) && !byte_signals.copy_bits(1));

        byte_line.id=102; byte_line.text="000000001111111101011100"; byte_line.received_bits=byte_line.expected_bits=24;
        byte_signals.update(byte_line);
        check(gui::signal_display_text(byte_signals.lines()[2])=="___" && byte_signals.copy_text(2)=="___");
        check(gui::signal_display_text(byte_signals.lines()[2],true)=="__\\" && byte_signals.copy_text(2,true)=="__\\");
        check(byte_signals.copy_bytes(2)==Bytes({0,0xff,'\\'}) && !byte_signals.copy_bits(2));

        byte_line.id=103; byte_line.text="01001000"; byte_line.received_bits=8; byte_line.expected_bits=16;
        byte_signals.update(byte_line);
        check(!byte_signals.copy_bits(3) && !byte_signals.copy_text(3) && !byte_signals.copy_bytes(3));
        byte_line.expected_bits=8; byte_line.text="0100100";
        byte_signals.update(byte_line);
        check(!byte_signals.copy_bits(3) && !byte_signals.copy_text(3) && !byte_signals.copy_bytes(3));
        byte_line.text="0100100x";
        byte_signals.update(byte_line);
        check(!byte_signals.copy_bits(3) && !byte_signals.copy_text(3) && !byte_signals.copy_bytes(3));
        byte_line.text.clear(); byte_line.received_bits=byte_line.expected_bits=0;
        byte_signals.update(byte_line);
        check(!gui::signal_byte_aligned(byte_signals.lines()[3]) && !byte_signals.copy_text(3) && !byte_signals.copy_bytes(3));
        gui::Inbox mixed;
        mixed.put(content(1,3)); // Ordinary bytes remain messages, never attachments.
        auto file=content(2,4); file.message.kind=MessageKind::file; file.message.filename="payload.bin";
        mixed.put(file);
        mixed.put(content(3,2));
        auto screenshot=content(4,5); screenshot.message.kind=MessageKind::screenshot; screenshot.message.filename="capture.png";
        mixed.put(screenshot);
        const auto files=mixed.file_items();
        check(files.size()==2 && files[0]->message.local_id[0]==2 && files[1]->message.local_id[0]==4);
        check(mixed.items().size()==4); // Originals remain in bounded memory independently of safe text presentation.
        gui::TransmissionPolicy policy;
        const auto time=gui::TransmissionPolicy::Clock::time_point{};
        policy.started(false,false,time);
        bool concurrent_rejected=false;
        try { policy.started(true,false,time); } catch (const Error&) { concurrent_rejected=true; }
        check(concurrent_rejected); // All modes allow only one active transmission.
        policy.finished(time+std::chrono::seconds(1));
        check(policy.remaining(false,false,time+std::chrono::seconds(1))==std::chrono::seconds(6));
        policy.started(true,true,time+std::chrono::seconds(1));
        policy.finished(time+std::chrono::seconds(2));
        check(policy.remaining(true,true,time+std::chrono::seconds(2)).count()==0);
        check(policy.remaining(false,true,time+std::chrono::seconds(2))==std::chrono::seconds(5));
        policy.started(false,true,time+std::chrono::seconds(7));
        policy.finished(time+std::chrono::seconds(8));
        check(policy.remaining(false,true,time+std::chrono::seconds(9))==std::chrono::seconds(5));
        check(policy.remaining(true,true,time+std::chrono::seconds(9)).count()==0);
        check(policy.remaining(false,false,time+std::chrono::seconds(9))==std::chrono::seconds(5));
        policy.started(true,false,time+std::chrono::seconds(9));
        policy.finished(time+std::chrono::seconds(10));
        check(policy.remaining(false,true,time+std::chrono::seconds(10))==std::chrono::seconds(4));
        policy.started(false,true,time+std::chrono::seconds(14));
        policy.abort_start();
        check(policy.remaining(false,true,time+std::chrono::seconds(14)).count()==0);
        policy.started(false,true,time+std::chrono::seconds(14));
        policy.finished(time+std::chrono::seconds(15));
        bool separation_rejected=false;
        try { policy.started(false,true,time+std::chrono::seconds(16)); }
        catch(const Error&) { separation_rejected=true; }
        check(separation_rejected);
        policy.started(false,true,time+std::chrono::seconds(16),true);
        bool forced_concurrent_rejected=false;
        try { policy.started(false,true,time+std::chrono::seconds(16),true); }
        catch(const Error&) { forced_concurrent_rejected=true; }
        check(forced_concurrent_rejected);
        policy.finished(time+std::chrono::seconds(17));
        check(policy.remaining(false,true,time+std::chrono::seconds(17))==std::chrono::seconds(6));
        policy.started(false,true,time+std::chrono::seconds(18),true);
        policy.abort_start();
        check(policy.remaining(false,true,time+std::chrono::seconds(18))==std::chrono::seconds(5));
        check(gui::format_bit_rate(1200)=="1.2 kbit/s");
        check(gui::format_bit_rate(.025)=="0.025 bit/s");
        check(gui::format_bit_rate(6.34e-9)=="6.34e-09 bit/s");
        check(gui::format_bit_rate(std::numeric_limits<double>::denorm_min()).find("e-")!=std::string::npos);
        check(gui::format_bit_rate(0)=="0 bit/s");
        check(gui::format_bit_rate(std::numeric_limits<double>::quiet_NaN())=="Unavailable");
        check(gui::key_entry_names(" Home, Portable , Caf\xc3\xa9 ")==std::vector<std::string>({"Home","Portable","Caf\xc3\xa9"}));
        const std::vector<std::string> choice_names{"A|B","None","_Home","Home","A&B","AB","Path/Back\\slash"};
        check(gui::key_choice_labels(choice_names)==std::vector<std::string>({
            "None","1. A|B","2. None","3. _Home","4. Home","5. A&B","6. AB","7. Path/Back\\slash"}));
        check(gui::key_choice_labels({})==std::vector<std::string>({"None"}));
        for (const auto* invalid : {"", " ", ",Home", "Home,", "Home,,Portable"}) {
            bool invalid_names=false;
            try { (void)gui::key_entry_names(invalid); } catch (const Error&) { invalid_names=true; }
            check(invalid_names);
        }
        bool empty_folder=false;
        try { (void)gui::folder_uri({}); } catch (const Error&) { empty_folder=true; }
        check(empty_folder);
#ifdef _WIN32
        check(gui::folder_uri(std::filesystem::path(u8"C:/Key folders/Caf\u00e9 #?%$`'\""))==
              "file:///C:/Key%20folders/Caf%C3%A9%20%23%3F%25%24%60%27%22");
        check(gui::folder_uri(std::filesystem::path(u8"//server/share/Key folders"))=="file://server/share/Key%20folders");
#else
        check(gui::folder_uri(std::filesystem::path(u8"/tmp/Key folders/Caf\u00e9 #?%$`'\""))==
              "file:///tmp/Key%20folders/Caf%C3%A9%20%23%3F%25%24%60%27%22");
#endif
        check(gui::folder_uri(std::filesystem::current_path()/"folder"/".."/"keys")==
              gui::folder_uri(std::filesystem::current_path()/"keys"));
        gui::PlotReplayPolicy plots;
        auto change=plots.observe(1,0,false);
        check(change.update_plots && change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(1,0,false);
        check(!change.update_plots && !change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(2,1,true,0);
        check(change.update_plots && change.append_waterfall && change.clear_waterfall);
        change=plots.observe(3,1,true,0); // Repeated delivery is not another frame.
        check(!change.update_plots && !change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(4,1,true,1);
        check(change.update_plots && change.append_waterfall && !change.clear_waterfall);
        // A scheduling gap may skip frames; the newest delivery is still
        // shown once, even if its unrelated source sequence is unchanged.
        change=plots.observe(4,1,true,7);
        check(change.update_plots && change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(4,1,true,7);
        check(!change.update_plots && !change.append_waterfall && !change.clear_waterfall);
        // Cancellation/completion returns every plot to live even when the
        // source sequence has not changed since the final replay frame.
        change=plots.observe(4,1,false);
        check(change.update_plots && change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(5,1,false);
        check(change.update_plots && change.append_waterfall && !change.clear_waterfall);
        change=plots.observe(5,2,true,0);
        check(change.update_plots && change.append_waterfall && change.clear_waterfall);
        // A new transmission can replace a replay without a live poll between.
        change=plots.observe(6,3,true,0);
        check(change.update_plots && change.append_waterfall && change.clear_waterfall);
        plots.reset();
        change=plots.observe(6,3,true,2);
        check(change.update_plots && change.append_waterfall && change.clear_waterfall);
        std::cout<<"Native GUI policy tests passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

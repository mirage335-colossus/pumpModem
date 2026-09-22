#include "../src/gui/gui_smoke.hpp"
#include "../src/gui/bitmap_sources.hpp"
#include "../src/gui/binary_editor.hpp"
#include "../src/gui/transmit_scope.hpp"
#include "datapump/compression.hpp"
#include "datapump/runtime.hpp"
#include "datapump/received_text.hpp"
#include "datapump/simulation_estimate.hpp"
#include <filesystem>
#include <iostream>
#include <set>
#include <thread>

namespace {
using namespace datapump;
using namespace datapump::gui;
void check(bool value, const char* message) { if (!value) throw Error(message); }
std::string repeatable_marker(std::string_view text) {
    constexpr std::string_view alphabet="bcdfghjklmnpqrstvwxzBCDFGHJKLMNPQRSTVWXZ0123456789";
    check(text.size()>=20&&text.starts_with("REPEATABLE-")&&text[19]==' ',
          "Repeatable marker is missing its eight-character identifier or trailing space");
    check(text.substr(11,8).find_first_not_of(alphabet)==std::string_view::npos,
          "Repeatable identifier contains a vowel or non-alphanumeric character");
    return std::string(text.substr(0,20));
}
void prepare(Controller& controller) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!controller.estimate()&&std::chrono::steady_clock::now()<deadline) {
        controller.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(controller.estimate().has_value(),"Payload estimate was not prepared");
}
void apply_exact_target(Controller& controller,const std::string& target) {
    // Deliberately unsupported geometry remains available through explicit
    // planner Apply; dropdown edits now select a nearby clock/RAM fit.
    controller.activate(ui::Command::planner_target);
    const auto requests=controller.take_services();
    check(requests.size()==1,"Exact target fixture needs the planner prompt");
    controller.complete_service({requests.front().id,false,target,{}});
    controller.activate(ui::Command::planner_apply_short);
    check(controller.field(ui::Field::snr).text==target,"Explicit target fixture changed its numerical anchor");
}
void simulation_estimate_controls() {
    using F=ui::Field;
    Controller controller({true,true});
    const auto text=[&](F field) {return controller.field(field).text;};
    for(const auto field:{F::simulation_confidence,F::simulation_cpu_time,F::simulation_gpu_time}) {
        const auto& screen=ui::console_screen();
        const auto declaration=std::find_if(screen.begin(),screen.end(),[&](const auto& control) {return control.field==field;});
        check(declaration!=screen.end()&&declaration->kind==ui::Kind::label&&declaration->persistent&&
              std::string_view(declaration->help).size()>40,
              "Simulation estimates need persistent shared labels and explanatory help");
    }
    controller.edit(F::message,"e");prepare(controller);
    check(text(F::simulation_confidence).find('%')!=std::string::npos&&
          text(F::simulation_cpu_time).find("i9-13900H\n~")!=std::string::npos&&
          text(F::simulation_gpu_time).find("RTX 4090 Laptop (projected)\n~")!=std::string::npos,
          "Simulation estimates omitted modeled probability or named reference hardware");
    const auto dictionary_cpu=text(F::simulation_cpu_time);
    const auto dictionary_confidence=text(F::simulation_confidence);
    controller.edit(F::short_bits,"001");
    check(text(F::simulation_confidence).ends_with("Calculating..."),
          "Draft change left a stale simulation probability on screen");
    prepare(controller);
    check(text(F::simulation_cpu_time)==dictionary_cpu&&text(F::simulation_confidence)==dictionary_confidence,
          "Identical three-bit dictionary and raw transmissions have different simulation estimates");
    controller.edit(F::message,"A longer message that requires fixed intervals.");prepare(controller);
    check(text(F::simulation_cpu_time)!=dictionary_cpu,
          "Simulation compute estimate ignored a longer framed draft");
    const auto strong_probability=text(F::simulation_confidence);
    controller.select(F::simulation,std::string(tuning::simulation_presets().back().name));prepare(controller);
    check(text(F::simulation_confidence)!=strong_probability,
          "Simulation probability did not respond to the weak channel preset");
    check(controller.field(F::simulation_confidence).text_tone==ui::TextTone::negative,
          "A weak numeric RX estimate below 80% must be red");
    controller.select(F::simulation,std::string(tuning::simulation_presets().front().name));
    check(!controller.settings().simulation,"Simulation No did not stop sampled simulation");
    // RX probability remains a link model when sampled simulation is off.
    prepare(controller);
    check(text(F::simulation_confidence).find('%')!=std::string::npos&&
          !text(F::simulation_confidence).ends_with("Simulation off"),
          "Simulation No must retain RX confidence for the configured link");
    controller.select(F::simulation,std::string(tuning::simulation_presets()[2].name));
    controller.edit(F::bandwidth,"invalid");
    check(controller.field(F::simulation_confidence).text_tone==ui::TextTone::normal,
          "Invalid settings retained the previous RX estimate warning color");
    check(text(F::simulation_confidence).ends_with("Invalid settings")&&
          text(F::simulation_cpu_time).ends_with("Invalid settings"),
          "Invalid settings left a previous numeric simulation estimate visible");
    controller.edit(F::bandwidth,"3.6 kHz");prepare(controller);
    controller.edit(F::short_bits,"001x");
    check(text(F::simulation_confidence).ends_with("Unavailable"),
          "Invalid bit draft left a previous numeric reception estimate visible");
    controller.edit(F::message,"a");
    controller.edit(F::bandwidth,"1 Hz");
    controller.select(F::simulation,"3dBm -120dB");prepare(controller);
    check(text(F::simulation_confidence).find('%')!=std::string::npos&&
          text(F::simulation_cpu_time).find("\n~")!=std::string::npos&&
          text(F::simulation_gpu_time).find("\n~")!=std::string::npos,
          "Expanded 1 Hz clock search must restore modeled coverage and retain compute estimates");
    controller.select(F::simulation,"3dBm -60dB");prepare(controller);
    check(text(F::simulation_confidence).find('%')!=std::string::npos,
          "Strong signal within the expanded carrier search must retain modeled coverage");
    controller.edit(F::bandwidth,"3.6 kHz");prepare(controller);
    check(text(F::simulation_confidence).find('%')!=std::string::npos,
          "Returning to supported carrier coverage did not restore the modeled percentage");
    controller.edit(F::bandwidth,"100 Hz");
    apply_exact_target(controller,"-61");
    controller.select(F::simulation,"3dBm -170dB");prepare(controller);
    check(text(F::simulation_confidence).ends_with("Carrier outside RX search"),
          "100 Hz with a -61 target must not show the old low numeric probability");
    check(controller.field(F::simulation_confidence).text_tone==ui::TextTone::normal,
          "Unavailable numeric RX estimate retained the warning color");
    const auto preset_snr=controller.settings().simulation_snr_db;
    controller.edit(F::snr,"140");prepare(controller);
    check(controller.settings().simulation_snr_db==preset_snr,
          "Changing the TX design target must not change simulated channel SNR");
    controller.select(F::simulation,"3dBm -120dB");prepare(controller);
    check(text(F::simulation_confidence).ends_with(">99%"),
          "Covered strong 100 Hz channel must not treat target 140 as an admission threshold");
    check(controller.field(F::simulation_confidence).text_tone==ui::TextTone::normal,
          "An RX estimate above 80% must retain the normal foreground");
}
void empty_composer_preview() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    controller.edit(F::snr,"-8");controller.edit(F::long_snr,"55");
    const auto expected=transfer::estimate_binary(Bytes{0},controller.settings().transfer);
    const auto check_empty=[&] {
        prepare(controller);
        check(controller.message_bytes().empty()&&controller.field(F::message).text.empty()&&
              controller.field(F::binary).text.empty()&&controller.field(F::short_bits).text.empty(),
              "The empty-composer preview must not insert its hypothetical zero into any editor");
        check(controller.inspection()->preview_only&&controller.inspection()->binary&&
              !controller.inspection()->stream_layout&&controller.estimate()->wire_bits==1&&
              controller.estimate()->waveform_samples==expected.waveform_samples&&
              controller.inspection()->title=="1-bit preview",
              "An empty GUI draft must preview exactly one raw zero at the short target");
        check(!controller.enabled(C::transmit)&&!controller.enabled(C::transmit_short_bits)&&
              controller.enabled(C::transmit_noise),
              "An empty preview must disable message sends while leaving tuning noise available");
        controller.activate(C::transmit);controller.activate(C::transmit_short_bits);
        check(controller.message_bytes().empty()&&!controller.snapshot().transmitting,
              "A one-bit planning placeholder became an actual transmission");
    };
    check_empty();
    for(const auto field:{F::message,F::binary,F::short_bits}) {
        controller.edit(F::short_bits,"001");prepare(controller);
        check(!controller.inspection()->preview_only&&controller.estimate()->wire_bits==3,
              "A real partial-byte draft was incorrectly marked as a hypothetical preview");
        controller.edit(field,"");check_empty();
    }
    controller.activate(C::planner_toggle_draft);
    check(controller.link_plan()->available&&controller.link_plan()->inputs.wire_bits==1,
          "The planner's current-draft view must use the empty editor's one-bit preview");
    // An empty file still has attachment metadata and fixed coding intervals.
    const auto path=std::filesystem::temp_directory_path()/
        ("dp-empty-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Remove {std::filesystem::path path;~Remove(){std::error_code ignored;std::filesystem::remove(path,ignored);}} remove{path};
    write_new_file(path.string(),{});
    controller.activate(C::attach_file);const auto requests=controller.take_services();
    check(requests.size()==1,"Empty attachment fixture did not request a file");
    controller.complete_service({requests.front().id,false,path.string(),{}});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while((!controller.inspection()||!controller.inspection()->stream_layout)&&std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(controller.inspection()&&!controller.inspection()->preview_only&&controller.inspection()->stream_layout&&
          controller.estimate()->wire_bits>=1216&&controller.estimate()->wire_bits%1216==0&&
          controller.enabled(C::transmit),
          "An empty attachment must retain its fixed-interval format and normal send eligibility");
    controller.close();
}
void oscillator_controls() {
    using F=ui::Field;
    Controller controller({true,true});
    check(controller.field(F::simulation_oscillator).selected=="crystal"&&
          controller.field(F::simulation_oscillator).options.size()==tuning::oscillator_presets().size()&&
          controller.settings().simulation_clock_error_ppm==100&&
          controller.settings().simulation_phase_noise_degrees_per_sqrt_second==.5,
          "The default oscillator must preserve the existing independent-clock simulation");
    controller.edit(F::message,"a");prepare(controller);
    const auto original_bits=controller.estimate()->wire_bits;
    const auto original_samples=controller.estimate()->waveform_samples;
    check(original_bits==3,"Oscillator fixture changed the exact a dictionary endpoint");
    for(const auto& preset:tuning::oscillator_presets()) {
        const auto revision=controller.revision();
        const bool changed=controller.field(F::simulation_oscillator).selected!=preset.id;
        controller.select(F::simulation_oscillator,std::string(preset.id));
        check(controller.settings().simulation_clock_error_ppm==preset.clock_error_ppm&&
              controller.settings().simulation_phase_noise_degrees_per_sqrt_second==preset.phase_noise_degrees_per_sqrt_second,
              "Oscillator selection did not reach the sampled simulation settings");
        if(changed)check(controller.revision()>revision&&!controller.estimate()&&
              controller.field(F::simulation_confidence).text.ends_with("Calculating..."),
              "Oscillator selection kept an estimate prepared with the old clock or phase model");
        prepare(controller);
        check(controller.estimate()->wire_bits==original_bits&&controller.estimate()->waveform_samples==original_samples,
              "Oscillator selection changed transmitted source bits or waveform geometry");
    }
    check(controller.field(F::simulation_oscillator_detail).text.find("Clock mismatch ")!=std::string::npos&&
          controller.field(F::simulation_oscillator_detail).text.find("Phase diffusion ")!=std::string::npos&&
          controller.field(F::simulation_oscillator_detail).text.find("GPS lock")==std::string::npos&&
          controller.field(F::simulation_oscillator_detail).text.find('\n')==std::string::npos,
          "Selected oscillator values must stay concise while detailed limitations remain in help");
    const auto accepted=controller.field(F::simulation_oscillator).selected;
    const auto revision=controller.revision();
    controller.select(F::simulation_oscillator,"unknown");
    check(controller.revision()==revision&&controller.field(F::simulation_oscillator).selected==accepted,
          "Unknown oscillator IDs changed accepted simulation settings");
    controller.select(F::simulation_oscillator,"crystal");
    controller.edit(F::bandwidth,"100 Hz");apply_exact_target(controller,"-30");prepare(controller);
    check(controller.field(F::simulation_confidence).text.ends_with("Carrier outside RX search"),
          "Long-symbol oscillator fixture must expose the default clock's uncovered carrier");
    controller.select(F::simulation_oscillator,"gpsdo-ocxo");prepare(controller);
    check(!controller.field(F::simulation_confidence).text.ends_with("Carrier outside RX search")&&
          !controller.field(F::simulation_confidence).text.ends_with("Calculating..."),
          "The model estimate did not use the selected oscillator's narrower carrier mismatch");
    const auto ppm=controller.settings().simulation_clock_error_ppm;
    const auto phase=controller.settings().simulation_phase_noise_degrees_per_sqrt_second;
    controller.edit(F::snr,"32");controller.select(F::simulation,std::string(tuning::simulation_presets().front().name));
    check(controller.settings().simulation_clock_error_ppm==ppm&&
          controller.settings().simulation_phase_noise_degrees_per_sqrt_second==phase,
          "Other simulation or modem edits discarded the saved oscillator choice");
    controller.close();controller.select(F::simulation_oscillator,"crystal");
    check(!controller.field(F::simulation_oscillator).enabled&&
          controller.field(F::simulation_oscillator).selected=="gpsdo-ocxo",
          "A stale oscillator callback reconfigured a closing application");
}
void lpi_estimate_controls() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    const auto text=[&] {return controller.field(F::lpi_estimate).text;};
    const auto same_advisory=[&](const Inspection& expected) {
        const auto actual=controller.inspection();
        check(actual&&actual->lpi_summary==expected.lpi_summary&&actual->lpi_description==expected.lpi_description&&
              text()==expected.lpi_summary,"Link or oscillator settings changed the relative LPI advisory");
        for(const auto& item:expected.fields)if(item.name.starts_with("LPI ")) {
            const auto found=std::find_if(actual->fields.begin(),actual->fields.end(),[&](const auto& field) {
                return field.name==item.name;
            });
            check(found!=actual->fields.end()&&found->value==item.value,
                  "Link or oscillator settings changed relative LPI inspection details");
        }
    };
    controller.edit(F::message,"e");controller.edit(F::snr,"0");
    controller.select(F::simulation,"3dBm -170dB");prepare(controller);
    check(controller.inspection()->lpi_estimate.status==lpi::Status::available&&
          controller.inspection()->lpi_estimate.hypothetical_encryption&&
          text().find("hypothetical")!=std::string::npos&&
          text().find("Observer / receiver")!=std::string::npos&&
          text().find("18 dB")==std::string::npos&&text().find('\n')==std::string::npos&&
          controller.inspection()->lpi_description.find("pattern design reference")!=std::string::npos&&
          controller.estimate()->wire_bits==3&&!controller.settings().transfer.key&&
          !controller.settings().transfer.modem.scramble&&!controller.settings().transfer.modem.dsss,
          "Unkeyed GUI must show a warned relative estimate while preserving its public waveform and exact short endpoint");
    check(controller.field(F::lpi_estimate).text_tone==ui::TextTone::normal,
          "An available observer ratio above 8x must retain the normal foreground");
    controller.edit(F::snr,"23");prepare(controller);
    check(controller.inspection()->lpi_estimate.status==lpi::Status::available&&
          controller.inspection()->lpi_estimate.equivalent_symbols<8&&
          controller.field(F::lpi_estimate).text_tone==ui::TextTone::negative,
          "An available observer ratio below 8x must be red even when hypothetical");
    controller.edit(F::short_bits,"001x");
    check(controller.field(F::lpi_estimate).text_tone==ui::TextTone::negative&&text().ends_with("Unavailable"),
          "Unavailable observer estimate must be red");
    controller.edit(F::message,"e");prepare(controller);
    check(controller.field(F::lpi_estimate).text_tone==ui::TextTone::negative,
          "A restored low observer estimate lost its warning color");
    controller.edit(F::snr,"0");
    check(controller.field(F::lpi_estimate).text_tone==ui::TextTone::normal&&text().ends_with("Calculating..."),
          "Pending observer estimate retained the previous warning color");
    prepare(controller);
    struct TemporaryKeyring {
        std::filesystem::path path=std::filesystem::temp_directory_path()/
            ("datapump-lpi-keys-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryKeyring(){std::error_code ignored;std::filesystem::remove(path,ignored);}
    } fixture;
    create_keyring(fixture.path,{"LPI estimate"});
    controller.activate(C::open_keyfile);
    const auto requests=controller.take_services();
    check(requests.size()==1,"LPI fixture did not open its keyfile chooser");
    controller.complete_service({requests.front().id,false,fixture.path.string(),{}});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while((!controller.settings().transfer.key||!controller.estimate())&&std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(controller.settings().transfer.key.has_value()&&controller.estimate().has_value(),"LPI fixture keyfile failed to load");
    controller.select(F::simulation,"3dBm -170dB");prepare(controller);
    const auto reference=controller.inspection();
    check(reference->lpi_estimate.status==lpi::Status::available&&
          !reference->lpi_estimate.hypothetical_encryption&&text().find("hypothetical")==std::string::npos&&
          std::abs(reference->lpi_estimate.reference_cn0_db_hz-
              (18-10*std::log10(reference->lpi_estimate.symbol_seconds)))<1e-10&&
          text()==reference->lpi_summary&&text().find("guarantee")==std::string::npos&&
          reference->lpi_description.find("no guaranteed hidden traffic")!=std::string::npos,
          "Private GUI estimate must use the one-bit receiver reference and shared inspection model");
    for(const auto& preset:tuning::simulation_presets()) {
        controller.select(F::simulation,std::string(preset.name));prepare(controller);
        check(controller.settings().simulation==preset.enabled&&
              controller.estimate()->waveform_samples==reference->estimate.waveform_samples,
              "Simulation selection changed the reference waveform geometry");
        same_advisory(*reference);
    }
    for(const auto& preset:tuning::oscillator_presets()) {
        controller.select(F::simulation_oscillator,std::string(preset.id));prepare(controller);
        same_advisory(*reference);
    }
    controller.edit(F::snr,"-3");
    check(text().ends_with("Calculating..."),"TX plan change left stale LPI numbers visible");
    prepare(controller);
    check(controller.inspection()->lpi_estimate.symbol_seconds>reference->lpi_estimate.symbol_seconds&&
          controller.inspection()->lpi_estimate.equivalent_symbols>reference->lpi_estimate.equivalent_symbols,
          "A longer automatic symbol failed to improve the relative observer-to-receiver ratio");
    controller.edit(F::snr,"0");prepare(controller);same_advisory(*reference);
    controller.edit(F::short_bits,"001");prepare(controller);
    check(controller.estimate()->wire_bits==3,"Exact three-bit draft changed the wire endpoint");
    same_advisory(*reference);
    controller.edit(F::receive_snr,"-100, 55");
    const auto rx_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while((controller.settings().transfer.receive_targets_db_hz!=std::vector<double>{-100,55}||!controller.estimate())&&
          std::chrono::steady_clock::now()<rx_deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(controller.estimate().has_value(),"RX target edits did not prepare an estimate");
    same_advisory(*reference);
    controller.edit(F::short_bits,"001x");
    check(text().ends_with("Unavailable")&&controller.field(F::lpi_estimate).text_tone==ui::TextTone::negative,
          "Invalid raw draft must replace the previous LPI number with a red unavailable status");
    controller.edit(F::message,"e");controller.edit(F::bandwidth,"invalid");
    check(text().ends_with("Invalid settings")&&controller.field(F::lpi_estimate).text_tone==ui::TextTone::negative,
          "Invalid modem settings must replace the previous LPI number with a red status");
    controller.edit(F::bandwidth,"3.6 kHz");
    controller.select(F::simulation,std::string(tuning::simulation_presets().front().name));prepare(controller);
    check(!controller.settings().simulation,"Simulation-off selection did not reach settings");
    same_advisory(*reference);
    controller.edit(F::long_snr,"0");controller.edit(F::message,std::string(17,'e'));prepare(controller);
    check(controller.estimate()->wire_bits==1216&&text()==reference->lpi_summary&&
          controller.inspection()->lpi_estimate.burst_exposure_ratio>reference->lpi_estimate.burst_exposure_ratio,
          "Fixed-interval draft must change exposure but not the same-geometry receiver-one-bit ratio");
    controller.edit(F::long_snr,"10");prepare(controller);
    check(controller.estimate()->wire_bits==1216&&
          controller.inspection()->lpi_estimate.equivalent_symbols<reference->lpi_estimate.equivalent_symbols,
          "Long draft must retain fixed interval bits and use its selected symbol geometry for the relative estimate");
    const auto long_reference=controller.inspection();
    controller.select(F::simulation,"3dBm -120dB");prepare(controller);
    same_advisory(*long_reference);
    controller.edit(F::long_snr,"55");prepare(controller);
    check(controller.inspection()->lpi_estimate.status==lpi::Status::outside_weak_signal_model&&
          text().find("×")==std::string::npos&&controller.field(F::lpi_estimate).text_tone==ui::TextTone::negative,
          "Short symbol geometry outside the normalized weak-signal model must show a red status without unsupported numbers");
    controller.select(F::key,"none");prepare(controller);
    check(controller.inspection()->lpi_estimate.status==lpi::Status::outside_weak_signal_model&&
          controller.inspection()->lpi_estimate.hypothetical_encryption&&
          text().find("hypothetical")!=std::string::npos&&
          !controller.settings().transfer.key&&controller.field(F::lpi_estimate).text_tone==ui::TextTone::negative,
          "Turning encryption off must restore the hypothetical warning even outside the relative weak-signal model");
    controller.edit(F::long_snr,"0");prepare(controller);
    check(controller.inspection()->lpi_estimate.status==lpi::Status::available&&
          text().find("hypothetical")!=std::string::npos&&controller.field(F::lpi_estimate).text_tone==ui::TextTone::normal,
          "Public experiments must retain numerical relative estimates after encryption is turned off");
    controller.select(F::key,"key:LPI estimate");prepare(controller);
    check(!controller.inspection()->lpi_estimate.hypothetical_encryption&&text().find("hypothetical")==std::string::npos,
          "Restoring encryption must clear the hypothetical warning");
    controller.edit(F::short_bits,"001");controller.edit(F::carrier,"2.7 kHz");
    controller.select(F::pattern,"auto-tone");prepare(controller);
    check(controller.inspection()->lpi_estimate.status==lpi::Status::available&&
          controller.inspection()->lpi_estimate.hypothetical_encryption&&
          text().find("hypothetical")!=std::string::npos&&
          controller.estimate()->wire_bits==3&&!controller.settings().transfer.key&&
          controller.settings().transfer.modem.spreading_mode==modem::SpreadingMode::tone&&
          !controller.settings().transfer.modem.scramble&&!controller.settings().transfer.modem.dsss,
          "Tone selection must restore the hypothetical warning without enabling encryption or changing exact bits");
    controller.select(F::pattern,"pattern-16");prepare(controller);
    const auto fixed_geometry=controller.inspection();
    controller.edit(F::snr,"55");prepare(controller);
    check(controller.estimate()->waveform_samples==fixed_geometry->estimate.waveform_samples,
          "Manual pattern fixture did not retain identical symbol geometry");
    same_advisory(*fixed_geometry);
}
void revised_reception_ingestion() {
    const auto complete=[](std::uint64_t row,std::uint64_t revision,MessageKind kind,std::uint8_t byte) {
        live::Snapshot snapshot;
        transfer::Received received;received.stream_complete=true;received.content_validated=true;
        received.content.message.local_id[0]=static_cast<std::uint8_t>(row);
        received.content.message.kind=kind;received.content.message.data=Bytes{byte,byte};
        received.content.message.filename=kind==MessageKind::text?"":"profile.bin";
        live::SignalUpdate signal;signal.id=row;signal.revision=revision;signal.validated=true;signal.complete=true;
        signal.reception_id=id_label(received.content.message);signal.text=kind==MessageKind::text?"text":"profile.bin";
        snapshot.received.push_back(std::move(received));snapshot.signals.push_back(std::move(signal));
        return snapshot;
    };
    const auto pending=[](std::uint64_t row,std::uint64_t revision) {
        live::SignalUpdate signal;signal.id=row;signal.revision=revision;signal.binary=true;
        signal.text="001";signal.received_bits=3;signal.pattern_score=32;
        return signal;
    };
    for(const auto kind:{MessageKind::text,MessageKind::file}) {
        Inbox inbox;Signals signals;
        auto original=complete(101,1,kind,'a');const auto stale=original;
        apply_receptions(inbox,signals,original);
        check(inbox.items().size()==1&&inbox.size_bytes()==2&&signals.lines().size()==1&&
              signals.lines().front().text_message==(kind==MessageKind::text),
              "Controller reception ingestion did not retain completed source content and type");

        auto stronger=stale;stronger.signals.push_back(pending(101,2));
        // Both event orders can occur when one UI poll drains several profiles.
        if(kind==MessageKind::file)std::reverse(stronger.signals.begin(),stronger.signals.end());
        apply_receptions(inbox,signals,stronger);
        check(inbox.items().empty()&&inbox.size_bytes()==0&&signals.lines().size()==1&&
              signals.lines().front().revision==2&&!signals.lines().front().complete&&
              !signals.copy_id(0)&&!signals.copy_raw_bits(0)&&!signals.copy_text(0),
              "A later stronger pending profile did not retract the completed inbox source before replayed content was inserted");

        auto replacement=complete(101,3,kind,'b');
        apply_receptions(inbox,signals,replacement);
        check(inbox.items().size()==1&&inbox.items().front().message.data==Bytes({'b','b'})&&
              inbox.size_bytes()==2&&signals.lines().front().revision==3&&signals.lines().front().complete,
              "Invalidation removed newly completed replacement content sharing the stable reception identity");
        auto unchanged=complete(102,0,MessageKind::file,'c');apply_receptions(inbox,signals,unchanged);
        auto late=stale;late.signals.front().superseded_ids={102};apply_receptions(inbox,signals,late);
        check(signals.lines().size()==2&&signals.lines().front().revision==3&&inbox.items().size()==2&&
              inbox.items().front().message.data==Bytes({'b','b'}),
              "A stale revision reinserted old content or retracted an unrelated current reception");

        live::Snapshot merged;auto event=pending(101,4);event.superseded_ids={102};merged.signals.push_back(event);
        merged.received=complete(102,0,MessageKind::file,'c').received;
        apply_receptions(inbox,signals,merged);
        check(signals.lines().size()==1&&signals.lines().front().id==101&&signals.lines().front().revision==4&&
              !signals.lines().front().complete&&inbox.items().empty()&&inbox.size_bytes()==0,
              "Merging profile rows left an obsolete file, source payload or duplicate history row");
        auto retired=complete(102,0,MessageKind::file,'c');apply_receptions(inbox,signals,retired);
        check(signals.lines().size()==1&&inbox.items().empty(),
              "A delayed event restored merged-away source content or its retired row");
        auto final=complete(101,4,kind,'d');apply_receptions(inbox,signals,final);
        check(signals.lines().size()==1&&signals.lines().front().complete&&inbox.items().size()==1&&
              inbox.items().front().message.data==Bytes({'d','d'}),
              "The merged pending reception could not complete with its stable row and source identity");
        auto replaced_complete=complete(101,5,kind,'e');apply_receptions(inbox,signals,replaced_complete);
        check(signals.lines().size()==1&&signals.lines().front().revision==5&&inbox.items().size()==1&&
              inbox.size_bytes()==2&&inbox.items().front().message.data==Bytes({'e','e'}),
              "Replacing a completed profile removed the new source sharing its canonical local identity");
    }
    for(const bool dictionary:{false,true}) {
        Inbox inbox;Signals signals;live::Snapshot initial;
        auto observed=pending(103,0);observed.complete=true;
        if(dictionary) {observed.binary=false;observed.text="e";observed.raw_bits="001";}
        initial.signals.push_back(observed);apply_receptions(inbox,signals,initial);
        check(signals.copy_raw_bits(0)=="001","Raw/dictionary fixture lost its completed source bits");
        live::Snapshot revised;revised.signals.push_back(pending(103,1));apply_receptions(inbox,signals,revised);
        check(signals.lines().size()==1&&!signals.lines().front().complete&&!signals.copy_raw_bits(0)&&
              !signals.copy_text(0),"Controller ingestion left completed raw/dictionary copy actions on a superseded profile");
    }
    for(const bool merge:{false,true}) {
        Inbox inbox;Signals signals;
        auto old=complete(101,5,MessageKind::file,'a');const auto stale=old;
        apply_receptions(inbox,signals,old);
        auto unrelated=complete(102,0,MessageKind::file,'b');apply_receptions(inbox,signals,unrelated);
        for(std::uint64_t id=200;id<264;++id) {
            SignalLine row;row.id=id;row.binary=true;row.text="0";row.received_bits=1;signals.update(row);
        }
        check(signals.lines().size()==64&&signals.lines().front().id==200&&inbox.items().size()==2,
              "The cache regression did not retain sources beyond the visible row history");
        auto late=complete(101,4,MessageKind::file,'z');late.signals.front().superseded_ids={102};
        apply_receptions(inbox,signals,late);
        live::Snapshot same_revision;same_revision.signals.push_back(pending(101,5));
        apply_receptions(inbox,signals,same_revision);
        check(signals.lines().front().id==200&&inbox.items().size()==2&&
              inbox.items().front().message.data==Bytes({'a','a'}),
              "Evicting a display row lost cached completion/revision protection or allowed stale alias retraction");

        live::Snapshot revised;auto event=pending(merge?104:101,merge?0:6);
        if(merge)event.superseded_ids={101};
        revised.signals.push_back(event);revised.received=stale.received;
        apply_receptions(inbox,signals,revised);
        check(inbox.items().size()==1&&inbox.items().front().message.local_id[0]==102&&inbox.size_bytes()==2&&
              !inbox.revision(101)&&inbox.revision(102)==0&&signals.lines().back().id==event.id&&
              !signals.lines().back().complete,
              "A revised or merged profile left its cached attachment after the old display row was evicted");
        auto delayed=stale;apply_receptions(inbox,signals,delayed);
        check(inbox.items().size()==1&&inbox.size_bytes()==2,
              "A delayed completion restored an evicted row's superseded attachment");
        auto final=complete(event.id,event.revision,MessageKind::file,'c');apply_receptions(inbox,signals,final);
        check(inbox.items().size()==2&&inbox.revision(event.id)==event.revision&&
              inbox.items().back().message.data==Bytes({'c','c'}),
              "A replacement source lost ownership after its earlier display row had expired");
    }
    {
        Inbox inbox(2);
        const auto first=complete(101,1,MessageKind::file,'a');
        const auto second=complete(102,2,MessageKind::file,'b');
        inbox.put(first.received.front().content,Inbox::ReceptionIdentity{101,1});
        inbox.put(second.received.front().content,Inbox::ReceptionIdentity{102,2});
        check(!inbox.revision(101)&&inbox.revision(102)==2&&inbox.items().size()==1,
              "Content-cache eviction retained obsolete signal ownership");
        inbox.put(second.received.front().content,Inbox::ReceptionIdentity{103,3});
        check(!inbox.revision(102)&&inbox.revision(103)==3,
              "Replacing a cached source retained its previous signal ownership");
        inbox.erase(second.signals.front().reception_id);
        check(!inbox.revision(103)&&inbox.items().empty()&&inbox.size_bytes()==0,
              "Erasing cached content left its signal ownership behind");
        inbox.put(first.received.front().content,Inbox::ReceptionIdentity{101,1});inbox.clear();
        check(!inbox.revision(101)&&inbox.items().empty(),"Clearing received content retained stale signal ownership");
    }
}
void noise_start_stop(Controller& controller) {
    using F=ui::Field;using C=ui::Command;
    const auto draft=controller.field(F::message).text,binary=controller.field(F::binary).text;
    const auto short_bits=controller.field(F::short_bits).text,key=controller.field(F::key).selected;
    const auto key_path=controller.field(F::key_path).text,message_label=controller.field(F::message_label).text;
    const auto bytes=controller.message_bytes();const auto inspection=controller.inspection();
    const auto revision=controller.revision();const auto cursor=controller.field(F::message).text_cursor_end_revision;
    const auto signals=controller.field(F::signals).records,files=controller.field(F::files).records;
    const auto inbox_size=controller.inbox().items().size();
    const auto previous=controller.enabled(C::paste_previous),attachment=controller.enabled(C::use_text);
    const auto settings=controller.settings();
    check(controller.enabled(C::transmit_noise),"Valid noise transmission was coupled to a draft or key selection");
    controller.activate(C::transmit_noise);
    check(!controller.enabled(C::transmit_noise)&&!controller.enabled(C::transmit)&&
          controller.enabled(C::cancel)&&controller.command_label(C::cancel)=="Stop noise",
          "Queued noise did not immediately disable competing sends and expose Stop noise");
    const auto oscillator=controller.field(F::simulation_oscillator).selected;
    controller.select(F::simulation_oscillator,oscillator=="crystal"?"gpsdo-ocxo":"crystal");
    check(!controller.field(F::simulation_oscillator).enabled&&
          controller.field(F::simulation_oscillator).selected==oscillator&&
          controller.settings().simulation_clock_error_ppm==settings.simulation_clock_error_ppm&&
          controller.settings().simulation_phase_noise_degrees_per_sqrt_second==settings.simulation_phase_noise_degrees_per_sqrt_second,
          "An oscillator selection changed a queued or running transmission's channel");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do { controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    while((!controller.snapshot().transmitting_noise||controller.snapshot().transmission_seconds<=0)&&
          std::chrono::steady_clock::now()<deadline);
    check(controller.snapshot().transmitting&&controller.snapshot().transmitting_noise&&
          controller.snapshot().transmission_seconds>0&&controller.snapshot().transmission_fraction==0&&
          controller.field(F::mode).text.find("noise / ")!=std::string::npos&&
          controller.field(F::mode).text.find("elapsed")!=std::string::npos&&
          controller.field(F::mode).text.find('%')==std::string::npos,
          "Continuous noise did not show active elapsed progress without a finite percentage");
    check(controller.field(F::transmit_scope_format).selected=="hex-auto-hide"&&
          !controller.field(F::transmit_scope).visible&&!controller.field(F::transmit_scope_caption).visible,
          "Automatic noise preview exposed an empty message capture instead of leaving room for live plots");
    for(const auto* format:{"hex","bits"}) {
        controller.select(F::transmit_scope_format,format);
        check(controller.field(F::transmit_scope).visible&&controller.field(F::transmit_scope_caption).visible&&
              controller.field(F::transmit_scope_caption).text.find("Tuning noise / ordinary encrypted modulation")!=std::string::npos&&
              !controller.snapshot().transmit_trace.active&&
              controller.field(F::transmit_scope).records==transmit_scope_records({},std::string_view(format)=="bits"),
              "Manual noise preview claimed to wait for transmission or showed a retained message capture");
    }
    controller.select(F::transmit_scope_format,"hex-auto-hide");
    check(!controller.field(F::transmit_scope).visible,
          "Returning to automatic preview kept the unused noise capture open");
    const auto transmission=controller.snapshot().transmission_id;
    controller.activate(C::transmit_noise);controller.poll();
    check(controller.snapshot().transmission_id==transmission,
          "Repeated noise activation restarted the keystreams or replaced the transmission");
    controller.activate(C::cancel);
    const auto stop_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do { controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    while((controller.snapshot().transmitting||!controller.snapshot().transmission_finished)&&
          std::chrono::steady_clock::now()<stop_deadline);
    check(controller.snapshot().transmission_finished&&!controller.snapshot().transmitting&&
          !controller.snapshot().transmitting_noise&&!controller.snapshot().simulation_replay&&
          controller.enabled(C::transmit_noise)&&!controller.enabled(C::cancel)&&
          controller.command_label(C::cancel)=="Cancel TX",
          "Stopping noise did not return to reception without a simulation replay");
    check(controller.field(F::message).text==draft&&controller.field(F::binary).text==binary&&
          controller.field(F::short_bits).text==short_bits&&controller.message_bytes()==bytes&&
          controller.field(F::message_label).text==message_label&&controller.revision()==revision&&
          controller.field(F::message).text_cursor_end_revision==cursor&&
          (!inspection||controller.inspection()==inspection)&&
          controller.enabled(C::paste_previous)==previous&&controller.enabled(C::use_text)==attachment,
          "Noise changed the draft, attachment, previous-message state or prepared inspection");
    check(controller.field(F::key).selected==key&&controller.field(F::key_path).text==key_path&&
          controller.settings().transfer.key.has_value()==settings.transfer.key.has_value()&&
          controller.settings().receive_keys.size()==settings.receive_keys.size()&&
          controller.settings().transfer.modem.carrier_hz==settings.transfer.modem.carrier_hz&&
          controller.settings().transfer.modem.spreading_mode==settings.transfer.modem.spreading_mode&&
          controller.settings().mono==settings.mono,
          "Temporary noise keys or waveform selection replaced saved modem settings");
    for(const auto purpose:{StreamPurpose::Data,StreamPurpose::Dsss,StreamPurpose::Scrambler,StreamPurpose::Fhss}) {
        if(settings.transfer.key)
            check(controller.settings().transfer.key->stream(purpose,0,0,32)==settings.transfer.key->stream(purpose,0,0,32),
                  "Noise replaced a selected message keystream");
        for(std::size_t index=0;index<settings.receive_keys.size();++index)
            check(controller.settings().receive_keys[index].stream(purpose,0,0,32)==settings.receive_keys[index].stream(purpose,0,0,32),
                  "Noise replaced a configured receive key");
    }
    check(controller.field(F::signals).records==signals&&controller.field(F::files).records==files&&
          controller.inbox().items().size()==inbox_size,
          "Noise replaced received history or created an apparent local message");
}
void noise_transmission_controls() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    check(controller.field(F::message).text.empty()&&!controller.estimate()&&controller.enabled(C::transmit_noise),
          "Empty unestimated composer disabled tuning noise");
    controller.activate(C::transmit_noise);
    check(controller.enabled(C::transmit_noise)&&!controller.enabled(C::cancel)&&
          controller.command_label(C::cancel)=="Cancel TX",
          "Rejected noise start left the controller busy");
    controller.edit(F::bandwidth,"invalid");
    check(!controller.enabled(C::transmit_noise),"Invalid modem settings allowed noise output");
    controller.edit(F::bandwidth,"3.6 kHz");
    controller.edit(F::binary,"invalid");
    check(!controller.estimate()&&!controller.enabled(C::transmit)&&controller.enabled(C::transmit_noise),
          "An invalid data draft disabled independent noise output");
    controller.start();noise_start_stop(controller);
    controller.edit(F::message,"Keep this draft");prepare(controller);noise_start_stop(controller);
    struct TemporaryAttachment {
        std::filesystem::path path=std::filesystem::temp_directory_path()/
            ("datapump-noise-attachment-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryAttachment(){std::error_code ignored;std::filesystem::remove(path,ignored);}
    } fixture;
    write_new_file(fixture.path.string(),Bytes{'n','o','i','s','e'});
    controller.activate(C::attach_file);auto requests=controller.take_services();
    check(requests.size()==1,"Noise fixture did not request its attachment chooser");
    controller.complete_service({requests.front().id,false,fixture.path.string(),{}});
    prepare(controller);
    check(controller.enabled(C::use_text)&&!controller.field(F::message).enabled,
          "Noise fixture attachment did not load");
    noise_start_stop(controller);controller.activate(C::use_text);
    check(controller.field(F::message).text=="Keep this draft","Noise discarded the attachment's text draft");
    controller.activate(C::open_keyfile);requests=controller.take_services();
    check(requests.size()==1,"Noise fixture did not request its key chooser");
    controller.complete_service({requests.front().id,false,fixture.path.string()+".missing-key",{}});
    check(!controller.enabled(C::transmit_noise),"Noise began while a key load could reconfigure the session");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!controller.enabled(C::acknowledge_key_failure)&&std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(controller.enabled(C::acknowledge_key_failure)&&!controller.enabled(C::transmit)&&
          controller.enabled(C::transmit_noise),"Failed message keys disabled independent temporary noise keys");
    noise_start_stop(controller);
    check(controller.enabled(C::acknowledge_key_failure),"Noise silently acknowledged a keyfile failure");
    controller.close();check(!controller.enabled(C::transmit_noise),"Closing left tuning noise available");
}
std::size_t check_pending_snapshot(Controller& controller) {
    using F=ui::Field;using C=ui::Command;
    // A poll can consume several updates for the same acquisition. Its newest
    // accepted prefix must already be visible when that poll returns.
    std::set<std::uint64_t> checked;
    std::size_t pending_count=0;
    const auto& updates=controller.snapshot().signals;
    for(auto update=updates.rbegin();update!=updates.rend();++update) {
        if(!checked.insert(update->id).second||update->complete||update->validated||update->text.empty())continue;
        ++pending_count;
        const auto& lines=controller.signals().lines();
        const auto line=std::find_if(lines.begin(),lines.end(),[&](const auto& value){return value.id==update->id;});
        const auto& rows=controller.field(F::signals).records;
        const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& value){return value.id==std::to_string(update->id);});
        check(line!=lines.end()&&row!=rows.end(),"An accepted incoming prefix was withheld from the GUI until completion");
        check(update->binary&&line->binary&&!line->complete&&!line->validated&&
              line->received_bits==update->received_bits&&line->expected_bits==update->expected_bits&&
              line->text==update->text&&row->cells[4].text==update->text&&
              row->cells[1].text=="binary pending"&&row->cells[4].tone==ui::TextTone::muted&&!row->activatable,
              "The same controller poll must show the latest pending bits without byte batching or premature source/dictionary decoding");
        controller.select(F::signals,row->id);
        check(!controller.enabled(C::copy_signal)&&!controller.enabled(C::copy_raw_signal)&&
              !controller.enabled(C::paste_signal)&&!controller.enabled(C::paste_raw_signal),
              "Selecting visible pending bits must not enable completed-message actions");
    }
    return pending_count;
}
void rate_carrier_controls() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    check(controller.field(F::bandwidth).text=="3.6 kHz" && controller.field(F::carrier).text=="1.5 kHz" &&
          controller.settings().transfer.modem.bandwidth_hz==3600 && controller.settings().transfer.modem.carrier_hz==1500 &&
          controller.field(F::snr).text=="32" && controller.field(F::long_snr).text=="55" &&
          controller.settings().transfer.fec==FecMode::rs60,
          "GUI defaults must use the 3.6 kHz rate, 1.5 kHz carrier, 32/55 dB-Hz targets and 60% FEC");
    struct CarrierPreset { const char* rate; const char* recommended; const char* center; double center_hz; };
    constexpr CarrierPreset presets[]={
        {"0.01 Hz","1.5 kHz","0.005 Hz",.005},{"0.1 Hz","1.5 kHz","0.05 Hz",.05},
        {"1 Hz","1.5 kHz","0.5 Hz",.5},{"100 Hz","1.5 kHz","50 Hz",50},
        {"1.2 kHz","1.5 kHz","600 Hz",600},{"2.4 kHz","1.8 kHz","1.2 kHz",1200},
        {"3.6 kHz","1.5 kHz","1.8 kHz",1800},{"12 kHz","9 kHz","6 kHz",6000},
        {"18 kHz","13.5 kHz","9 kHz",9000},{"24 kHz","18 kHz","12 kHz",12000},
        {"1 MHz","750 kHz","500 kHz",500000},{"30 MHz","22.5 MHz","15 MHz",15000000}};
    for(const auto& preset:presets) {
        controller.edit(F::bandwidth,preset.rate);
        const auto& carrier=controller.field(F::carrier);
        check(carrier.text==preset.recommended && carrier.options.size()==2 &&
              carrier.options[0].id==preset.recommended && carrier.options[1].id==preset.center,
              "Rate presets must keep the recommended carrier first and offer the center frequency");
        controller.edit(F::carrier,preset.center);
        check(controller.settings().transfer.modem.carrier_hz==preset.center_hz,
              "A center carrier suggestion did not reach the modem configuration");
    }
    controller.edit(F::bandwidth,"3 kHz");
    check(controller.field(F::carrier).options.size()==2 && controller.field(F::carrier).options.front().id=="2.25 kHz" &&
          controller.field(F::carrier).options.back().id=="1.5 kHz",
          "A manually entered Rate must offer its recommended and center carriers");
    controller.edit(F::bandwidth,"3.6 kHz");
    controller.edit(F::carrier,"1650 Hz");
    check(controller.settings().transfer.modem.carrier_hz==1650,
          "A custom audio carrier did not reach the modem configuration");
    controller.edit(F::snr,"60");controller.select(F::fec,"off");
    controller.select(F::simulation,"3dBm -90dB");controller.edit(F::device,"test-device");
    check(controller.field(F::carrier).text=="1650 Hz" && controller.settings().transfer.modem.carrier_hz==1650,
          "An unrelated setting replaced the manual carrier");
    const auto profiles=tuning::receive_profiles(controller.settings().transfer.modem,
        std::vector<double>{60,40},tuning::PatternMode::auto_pattern,false);
    check(!profiles.empty() && std::all_of(profiles.begin(),profiles.end(),[](const auto& profile){return profile.carrier_hz==1650;}),
          "Automatic receive hypotheses discarded the selected carrier");
    controller.edit(F::bandwidth,"2.4 kHz");
    check(controller.field(F::carrier).text=="1.8 kHz" && controller.settings().transfer.modem.carrier_hz==1800,
          "Changing Rate did not restore that rate's recommended carrier");
    controller.edit(F::bandwidth,"30 MHz");
    check(controller.field(F::carrier).text=="22.5 MHz" && controller.settings().transfer.modem.carrier_hz==22500000 &&
          controller.field(F::carrier).options.front().id=="22.5 MHz",
          "The widest rate has no selectable recommended carrier");
    controller.edit(F::bandwidth,"3.6 kHz");
    check(controller.field(F::carrier).text=="1.5 kHz" && controller.settings().transfer.modem.carrier_hz==1500,
          "Returning to the HF rate did not restore the 1.5 kHz carrier");
    const auto valid=controller.settings().transfer.modem;
    controller.edit(F::bandwidth,"unfinished");
    check(controller.field(F::carrier).text=="1.5 kHz" && controller.settings().transfer.modem.bandwidth_hz==valid.bandwidth_hz &&
          !controller.enabled(C::transmit),"Invalid Rate text changed the carrier or permitted transmission");
    controller.edit(F::bandwidth,"3.6 kHz");controller.edit(F::carrier,"900 Hz");
    check(controller.field(F::carrier).text=="900 Hz" && controller.settings().transfer.modem.carrier_hz==1500 &&
          !controller.enabled(C::transmit) && controller.field(F::status).text.find("carrier")!=std::string::npos,
          "An incompatible carrier was silently replaced or applied");
    controller.edit(F::carrier,"1.5 kHz");controller.select(F::pattern,"auto-tone");
    check(controller.field(F::carrier).text=="1.5 kHz" && !controller.enabled(C::transmit) &&
          controller.field(F::status).text.find("raise the carrier")!=std::string::npos,
          "An incompatible tone profile silently changed the carrier or hid the corrective action");
    controller.edit(F::carrier,"2.7 kHz");
    check(controller.settings().transfer.modem.carrier_hz==2700 &&
          controller.settings().transfer.modem.spreading_mode==modem::SpreadingMode::tone,
          "Raising the carrier did not recover the selected tone profile");
}
void sub_hertz_controls() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    const auto& rates=controller.field(F::bandwidth).options;
    for(const auto* value:{"0.01 Hz","0.1 Hz"})
        check(std::any_of(rates.begin(),rates.end(),[&](const auto& option){return option.id==value;}),
              "sub-hertz rates must be discoverable in the Rate dropdown");
    controller.edit(F::message,"a");controller.edit(F::bandwidth,"0.01 Hz");prepare(controller);
    check(controller.settings().transfer.modem.bandwidth_hz==.01 &&
          controller.settings().transfer.modem.carrier_hz==1500 && controller.estimate()->wire_bits==3 &&
          controller.estimate()->coded_seconds==38400 && controller.enabled(C::transmit),
          "sub-hertz GUI preparation must keep the exact short message and permit bounded transmission");
    controller.edit(F::bandwidth,"0.009 Hz");
    check(!controller.enabled(C::transmit) && controller.settings().transfer.modem.bandwidth_hz==.01 &&
          controller.field(F::status).text.find("0.01")!=std::string::npos,
          "an out-of-range rate must preserve the previous configuration and explain the lower bound");
}
void shannon_capacity_display() {
    using F=ui::Field;
    Controller controller({true,true});
    controller.edit(F::message,"e");
    controller.start();
    const auto expect_capacity=[&](std::string_view expected) {
        controller.poll();
        const auto& diagnostics=controller.field(F::diagnostics).text;
        check(diagnostics.starts_with(format_bit_rate(modem::bit_rate(controller.settings().transfer.modem))+" | Shannon-Hartley limit "),
              "The theoretical capacity must appear alongside the existing gross modem bitrate");
        check(diagnostics.find(std::string("Shannon-Hartley limit ")+std::string(expected)+" |")!=std::string::npos,
              "The Shannon-Hartley display does not match the accepted TX target and nominal bandwidth");
    };
    expect_capacity("1.89 kbit/s"); // Default: 3,600 Hz, 32 dB-Hz.
    controller.edit(F::bandwidth,"1 kHz");controller.edit(F::snr,"30");
    expect_capacity("1 kbit/s"); // C/N0 = 1,000 Hz, hence S/N = 1.
    controller.edit(F::bandwidth,"2 kHz");
    expect_capacity("1.17 kbit/s");
    controller.edit(F::snr,"40");
    expect_capacity("5.17 kbit/s");
    controller.edit(F::receive_snr,"60,80");
    controller.select(F::simulation,"3dBm -90dB");
    expect_capacity("5.17 kbit/s");
    controller.edit(F::snr,"unfinished");
    expect_capacity("5.17 kbit/s"); // Invalid drafts preserve the active settings.
    controller.edit(F::snr,"40"); // Planner Apply requires valid modem settings.
    apply_exact_target(controller,"-60");
    expect_capacity("1.44e-06 bit/s");
    controller.close();
}
void profile_reference_display() {
    using F=ui::Field;
    Controller controller({true,true});
    controller.edit(F::message,"e");
    const auto active=[&]() -> std::string {
        std::string label;unsigned count=0;
        for(const auto& row:controller.field(F::profile_reference).records)
            for(const auto& cell:row.cells)if(cell.bold) {label=cell.text;++count;}
        check(count==1,"Profile reference must highlight exactly one configured duration");
        return label;
    };
    controller.edit(F::snr,"30");
    check(active().find("128complex")!=std::string::npos&&active().find("14.06bit/s")!=std::string::npos,
          "Reference did not show the actual 128-chip profile near the 30 dB-Hz transition");
    const auto before=controller.field(F::profile_reference).records;
    controller.edit(F::snr,"26");
    check(active().find("512complex")!=std::string::npos&&
          controller.field(F::profile_reference).records!=before,
          "Reference did not follow the active integration step");
    controller.edit(F::bandwidth,"1.2 kHz");
    check(active().find("128complex")!=std::string::npos,
          "Reference retained stale boundaries after the rate changed");
    controller.select(F::pattern,"pattern-8");
    check(controller.field(F::profile_reference).records.size()==1&&active().find("8complex")!=std::string::npos,
          "Forced profiles must show their actual length instead of automatic boundaries");
    controller.edit(F::snr,"unfinished");
    check(controller.field(F::profile_reference).records.empty(),"Invalid settings retained an apparently current profile reference");
}
void mono_controls() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    check(controller.field(F::mono).checked&&controller.field(F::mono).enabled&&controller.settings().mono&&
          controller.field(F::mono).selected=="left"&&controller.field(F::mono).options.size()==3&&
          controller.settings().channel_mode==audio::ChannelMode::left_mono,
          "Mono audio routing must be enabled by default in the shared GUI and live settings");
    controller.edit(F::binary,"001");prepare(controller);
    const auto prepared_revision=controller.revision();
    const auto prepared_inspection=controller.inspection();
    controller.toggle(F::mono,false);
    check(!controller.field(F::mono).checked&&!controller.settings().mono&&
          controller.revision()==prepared_revision&&controller.inspection()==prepared_inspection&&
          controller.estimate()&&controller.estimate()->wire_bits==3&&controller.enabled(C::transmit),
          "Turning Mono off must configure stereo transmission without invalidating the prepared message");
    const auto revision=controller.revision();
    controller.toggle(F::mono,false);
    check(controller.revision()==revision,"An unchanged Mono callback reconfigured the modem");
    controller.edit(F::device,"test-device");controller.edit(F::snr,"40");
    check(!controller.field(F::mono).checked&&!controller.settings().mono,
          "Changing the device or modem settings discarded the audio routing choice");
    controller.toggle(F::mono,true);
    check(controller.field(F::mono).checked&&controller.settings().mono,
          "Turning Mono back on did not restore single-channel transmission");
    prepare(controller);controller.start();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(controller.snapshot().samples_received<controller.settings().transfer.modem.sample_rate/5&&
          std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(controller.snapshot().samples_received>=controller.settings().transfer.modem.sample_rate/5,
          "Continuous simulated reception did not advance its sample clock");
    const auto live_revision=controller.revision();
    const auto live_inspection=controller.inspection();
    const auto estimated_airtime=controller.estimate()->total_seconds;
    controller.plot_update();
    for(const auto mode:{"stereo","right","left"}) {
        const bool mono=std::string_view(mode)!="stereo";
        const auto channels=std::string_view(mode)=="right"?audio::ChannelMode::right_mono:
            mono?audio::ChannelMode::left_mono:audio::ChannelMode::stereo;
        const auto samples=controller.snapshot().samples_received;
        const auto seconds=controller.snapshot().virtual_seconds;
        controller.select(F::mono,mode);controller.poll();
        check(controller.field(F::mono).checked==mono&&controller.settings().mono==mono&&
              controller.field(F::mono).selected==mode&&controller.settings().channel_mode==channels&&
              controller.snapshot().running&&controller.snapshot().samples_received>=samples&&
              controller.snapshot().virtual_seconds>=seconds,
              "Changing audio output routing reset or interrupted the running receiver clock");
        check(controller.revision()==live_revision&&controller.inspection()==live_inspection&&
              controller.estimate()&&controller.estimate()->wire_bits==3&&
              controller.estimate()->total_seconds==estimated_airtime&&controller.enabled(C::transmit)&&
              !controller.plot_update().clear_waterfall,
              "Changing audio output routing invalidated the prepared message or cleared receiver plots");
    }
    controller.activate(C::transmit);
    check(!controller.field(F::mono).enabled&&!controller.field(F::device).enabled,
          "Audio routing remained editable while transmitting");
    controller.toggle(F::mono,false);
    check(controller.field(F::mono).checked&&controller.settings().mono,
          "A disabled Mono callback changed the active transmission routing");
    controller.select(F::mono,"right");
    check(controller.field(F::mono).selected=="left","Disabled channel choice changed active transmission routing");
    controller.close();controller.toggle(F::mono,false);controller.select(F::mono,"right");
    check(!controller.field(F::mono).enabled&&controller.field(F::mono).checked&&controller.settings().mono,
          "A stale Mono callback changed a closing session");
}
void tone_mode_controls() {
    using F=ui::Field;
    Controller controller({true,true});
    controller.edit(F::carrier,"2.7 kHz");
    for(const auto mode:tuning::pattern_modes()) {
        const std::string name(tuning::pattern_mode_name(mode));
        if(name!="auto-tone" && !name.starts_with("tone-"))continue;
        controller.select(F::pattern,name);
        check(controller.field(F::key).selected=="none" && !controller.field(F::key).enabled &&
              !controller.settings().transfer.key && controller.settings().receive_keys.empty() &&
              !controller.settings().transfer.modem.scramble && !controller.settings().transfer.modem.dsss,
              "Tone selection retained private modulation or permitted key selection");
        check(controller.field(F::status).text.find("unencrypted")!=std::string::npos,
              "Tone selection did not explain that encryption is off");
    }
    controller.select(F::pattern,"auto-pattern");
    check(controller.field(F::key).enabled && controller.field(F::key).selected=="none",
          "Leaving tone mode failed to restore the key selector with encryption off");
}
void tone_key_controls() {
    using F=ui::Field;using C=ui::Command;
    struct TemporaryKeyring {
        std::filesystem::path path=std::filesystem::temp_directory_path()/
            ("datapump-tone-keys-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryKeyring(){std::error_code ignored;std::filesystem::remove(path,ignored);}
    } fixture;
    create_keyring(fixture.path,{"Tone policy"});
    Controller controller({true,true});
    controller.edit(F::carrier,"2.7 kHz");
    const auto load=[&] {
        controller.activate(C::open_keyfile);
        const auto requests=controller.take_services();
        check(requests.size()==1 && requests.front().kind==ui::ServiceKind::open_file,
              "Tone policy test did not receive its keyfile chooser");
        controller.complete_service({requests.front().id,false,fixture.path.string(),{}});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while((!controller.enabled(C::open_keyfile)||!controller.estimate()) && std::chrono::steady_clock::now()<deadline) {
            controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(controller.enabled(C::open_keyfile)&&controller.estimate().has_value()&&
              !controller.enabled(C::acknowledge_key_failure),"Tone policy keyfile load did not complete");
    };
    load();
    check(controller.settings().transfer.key && controller.settings().receive_keys.size()==1,
          "Production keyfile did not activate private transmission and reception");
    controller.start();noise_start_stop(controller);
    for(const auto mode:tuning::pattern_modes()) {
        const std::string name(tuning::pattern_mode_name(mode));
        if(!tuning::tone_mode(mode))continue;
        controller.select(F::pattern,name);
        check(controller.field(F::key).selected=="none"&&!controller.field(F::key).enabled&&
              !controller.settings().transfer.key&&controller.settings().receive_keys.empty()&&
              !controller.settings().transfer.modem.scramble&&!controller.settings().transfer.modem.dsss,
              "Switching from encryption to tone retained private key or modulation state");
        controller.select(F::key,"key:Tone policy");
        check(!controller.settings().transfer.key,"Tone permitted a disabled key selection to restore encryption");
        controller.select(F::pattern,"auto-pattern");
        check(controller.field(F::key).enabled&&!controller.settings().transfer.key,
              "Leaving tone silently restored encryption");
        controller.select(F::key,"key:Tone policy");
        check(controller.settings().transfer.key.has_value(),"Leaving tone lost the loaded key entry");
    }
    controller.select(F::pattern,"auto-tone");load();
    check(controller.field(F::key).selected=="none"&&!controller.field(F::key).enabled&&
          !controller.settings().transfer.key&&controller.settings().receive_keys.empty(),
          "Loading a keyfile while tone re-enabled private transmission or reception");
}
void composer_conveniences() {
    using F=ui::Field; using C=ui::Command;
    Controller controller({true,true});
    check(controller.message_bytes().empty()&&controller.field(F::message).text.empty()&&
          !controller.enabled(C::paste_previous),"Empty convenience fields added text or enabled previous-message paste");
    controller.edit(F::callsign,"N0CALL");
    check(controller.field(F::message).text=="CQ CQ CQ DE N0CALL. Please reply. ",
          "Callsign did not seed an editable greeting with a trailing space");
    check(controller.field(F::message).text_cursor_end_revision!=0,
          "Seeding a greeting did not place subsequent typing after it");
    controller.edit(F::grid,"AB12cd");
    const std::string greeting="CQ CQ CQ DE N0CALL GRID AB12cd. Please reply. ";
    check(controller.field(F::message).text==greeting,
          "Grid did not update an untouched greeting or was normalized");
    controller.edit(F::message,greeting+"Anyone listening?");
    const auto draft_cursor_revision=controller.field(F::message).text_cursor_end_revision;
    controller.edit(F::callsign,"Portable station / N0CALL");
    controller.edit(F::grid,"Somewhere nearby");
    check(controller.field(F::message).text==greeting+"Anyone listening?",
          "Convenience field edits modified an existing message body");
    check(controller.field(F::message).text_cursor_end_revision==draft_cursor_revision,
          "Changing convenience fields moved the cursor in a user-written draft");
    controller.edit(F::message,"");
    check(controller.field(F::message).text.empty()&&controller.message_bytes().empty(),
          "Clearing a draft must leave it empty even when convenience fields are populated");
    controller.edit(F::callsign,"");
    check(controller.field(F::message).text=="CQ CQ CQ GRID Somewhere nearby. Please reply. ",
          "A grid-only greeting retained an empty callsign separator");
    controller.edit(F::grid,"");
    check(controller.field(F::message).text.empty(),"Removing all convenience fields left whitespace");
    controller.toggle(F::repeatable,true);
    check(controller.field(F::message).text==repeatable_marker(controller.field(F::message).text),
          "Repeatable alone did not seed only its in-band marker");
    controller.edit(F::callsign,"N0CALL");
    check(controller.field(F::message).text==repeatable_marker(controller.field(F::message).text)+"CQ CQ CQ DE N0CALL. Please reply. ",
          "Repeatable marker did not precede the station greeting");
    controller.edit(F::callsign,"");
    controller.toggle(F::repeatable,false);
    check(controller.message_bytes().empty(),"Turning off the only convenience field left whitespace");
    controller.edit(F::message,"A message body");
    controller.toggle(F::repeatable,true);
    const auto initial_repeatable=controller.field(F::message).text;
    controller.toggle(F::repeatable,true);
    check(initial_repeatable==repeatable_marker(initial_repeatable)+"A message body"&&
          controller.field(F::message).text==initial_repeatable,
          "Repeatable did not prepend exactly one editable marker");
    controller.toggle(F::repeatable,false);
    check(controller.field(F::message).text=="A message body","Turning repeatable off damaged the message body");

    controller.edit(F::message,std::string(236,'x'));
    controller.toggle(F::repeatable,true);
    check(controller.field(F::repeatable).checked&&controller.message_bytes().size()==256,
          "Repeatable rejected the inclusive 256-byte payload limit");
    controller.edit(F::message,controller.field(F::message).text+"x");
    check(!controller.field(F::repeatable).checked&&!controller.field(F::repeatable).enabled&&
          controller.field(F::message).text==std::string(237,'x'),
          "Exceeding 256 bytes did not force repeatable off and remove its intact marker");
    controller.edit(F::message,std::string(235,'x')+"\xC3\xA9");
    check(!controller.field(F::repeatable).enabled,"Repeatable counted Unicode characters instead of payload bytes");
    controller.edit(F::message,std::string(234,'x')+"\xC3\xA9");
    controller.toggle(F::repeatable,true);
    check(controller.field(F::repeatable).checked&&controller.message_bytes().size()==256,
          "A UTF-8 payload at the byte limit could not be repeatable");

    controller.toggle(F::repeatable,false);
    controller.edit(F::message,"");
    controller.edit(F::binary,"00000000 11111111");
    controller.edit(F::message,controller.field(F::message).text);
    controller.toggle(F::repeatable,true);
    const auto binary_repeatable=controller.message_bytes();
    repeatable_marker(controller.field(F::message).text);
    check(controller.field(F::repeatable).checked&&binary_repeatable.size()==22&&
          binary_repeatable[20]==0&&binary_repeatable[21]==255,
          "A short escaped payload lost bytes or incorrectly disabled repeatable");
    controller.toggle(F::repeatable,false);
    check(controller.message_bytes()==Bytes({0,255}),"Removing the repeatable marker changed arbitrary payload bytes");
    controller.edit(F::message,"short");
    controller.toggle(F::repeatable,true);
    controller.activate(C::attach_file);
    const auto services=controller.take_services();
    check(services.size()==1&&services.front().kind==ui::ServiceKind::open_file,
          "Attachment did not request a file");
    controller.complete_service({services.front().id,false,std::filesystem::absolute(__FILE__).string(),{}});
    check(!controller.field(F::repeatable).checked&&!controller.field(F::repeatable).enabled,
          "Loading an attachment did not force repeatable off");
    prepare(controller);
    check(!controller.field(F::message).enabled&&!controller.field(F::repeatable).enabled,
          "An attached file retained the repeatable convenience control");
    check(controller.field(F::message).text=="#ATTACHMENT### test_gui_controller.cpp ###ATTACHMENT# ",
          "Attachment did not show its source marker and filename");
    controller.activate(C::use_text);
    check(controller.field(F::repeatable).enabled&&!controller.field(F::repeatable).checked&&
          controller.field(F::message).text=="short", "Returning to text restored repeatable or damaged the draft");

    controller.edit(F::bandwidth,"1 Hz");
    controller.toggle(F::repeatable,true);
    prepare(controller);
    check(!controller.estimate()->repeatable_allowed&&controller.field(F::repeatable).checked&&
          controller.enabled(C::transmit),"Legacy repeatable airtime metadata still gated an in-band marker");
    controller.edit(F::bandwidth,"1.2 kHz");
    controller.edit(F::callsign,std::string(128,'C'));
    controller.edit(F::grid,std::string(128,'G'));
    controller.edit(F::message,"");
    controller.edit(F::grid,std::string(127,'G'));
    controller.edit(F::grid,std::string(128,'G'));
    check(controller.field(F::message).text=="CQ CQ CQ DE "+std::string(128,'C')+" GRID "+std::string(128,'G')+". Please reply. "&&
          !controller.field(F::repeatable).checked&&!controller.field(F::repeatable).enabled,
          "Large convenience fields lost text or retained an over-limit repeatable greeting");
    prepare(controller);
    check(controller.enabled(C::transmit),"Legacy callsign/grid metadata length limits still rejected visible message text");
}
void repeatable_message_identity() {
    using F=ui::Field;
    Controller controller({true,true});
    controller.toggle(F::repeatable,true);
    auto marker=repeatable_marker(controller.field(F::message).text);
    controller.edit(F::message,"Roger ");
    const auto marker_only_replacement=repeatable_marker(controller.field(F::message).text);
    check(marker_only_replacement!=marker&&controller.field(F::message).text==marker_only_replacement+"Roger "&&
          controller.field(F::repeatable).checked,
          "Replacing a marker-only draft with a shared initial and trailing space lost message text");
    marker=marker_only_replacement;
    const auto change_body=[&](std::string body) {
        controller.edit(F::message,marker+body);
        const auto next=repeatable_marker(controller.field(F::message).text);
        check(next!=marker,"Editing the message reused its repeatable identifier");
        check(controller.field(F::message).text==next+body&&controller.field(F::repeatable).checked,
              "Renewing a repeatable identifier changed the body or disabled its convenience control");
        marker=next;
    };
    change_body("A");
    change_body("AB");
    change_body("A");
    change_body("A pasted message\nwith another line");
    change_body("A pasted message\nwith another line \xC3\xA9");
    change_body("A pasted message\nwith another line ");
    change_body("Alpha omega");
    const auto mid_body_cursor_revision=controller.field(F::message).text_cursor_end_revision;
    change_body("Alpha middle omega");
    check(controller.field(F::message).text_cursor_end_revision==mid_body_cursor_revision,
          "Renewing the identifier during a middle insertion requested a jump to the end of the message");
    auto edited_identifier=controller.field(F::message).text;
    edited_identifier[13]=edited_identifier[13]=='b'?'c':'b';
    controller.edit(F::message,edited_identifier);
    auto edited_marker=repeatable_marker(controller.field(F::message).text);
    check(edited_marker!=marker&&controller.field(F::message).text==edited_marker+"Alpha middle omega"&&
          controller.field(F::repeatable).checked,
          "Editing an identifier character duplicated the marker or changed the message body");
    marker=edited_marker;
    change_body("discard keep");
    auto crossing_edit=controller.field(F::message).text;
    crossing_edit.replace(15,13,"replacement ");
    controller.edit(F::message,crossing_edit);
    edited_marker=repeatable_marker(controller.field(F::message).text);
    check(edited_marker!=marker&&controller.field(F::message).text==edited_marker+crossing_edit&&
          controller.field(F::repeatable).checked,
          "Replacing a selection across the marker and body discarded part of the edited text");
    marker=edited_marker;
    crossing_edit=controller.field(F::message).text;
    crossing_edit.erase(16,16);
    controller.edit(F::message,crossing_edit);
    edited_marker=repeatable_marker(controller.field(F::message).text);
    check(edited_marker!=marker&&controller.field(F::message).text==edited_marker+crossing_edit&&
          controller.field(F::repeatable).checked,
          "Deleting a selection across the marker and body discarded surviving text from the edited draft");
    marker=edited_marker;
    std::set<std::string> sampled_markers{marker};
    for(unsigned edit=0;edit<128;++edit) {
        change_body("Fast edit "+std::to_string(edit));
        check(sampled_markers.insert(marker).second,
              "Rapid message edits repeated a previously generated repeatable identifier");
    }
    for(const auto body:{"Roger","REPLACED"}) {
        controller.edit(F::message,body);
        const auto replacement=repeatable_marker(controller.field(F::message).text);
        check(replacement!=marker&&controller.field(F::message).text==replacement+body&&
              controller.field(F::repeatable).checked,
              "A whole-message replacement sharing the marker's first letters lost part of its body");
        marker=replacement;
    }
    const auto stale_native_marker=marker;
    for(const auto body:{"a","ab"}) {
        controller.edit(F::message,stale_native_marker+body);
        const auto replacement=repeatable_marker(controller.field(F::message).text);
        check(replacement!=marker&&replacement!=stale_native_marker&&
              controller.field(F::message).text==replacement+body&&controller.field(F::repeatable).checked,
              "Rapid native edits carrying an earlier marker duplicated its identifier or changed the message body");
        marker=replacement;
    }
    controller.edit(F::message,"A completely replaced message");
    const auto replacement_marker=repeatable_marker(controller.field(F::message).text);
    check(replacement_marker!=marker&&controller.field(F::message).text==replacement_marker+"A completely replaced message"&&
          controller.field(F::repeatable).checked,
          "Replacing the entire draft did not retain a fresh repeatable marker");
    marker=replacement_marker;
    const auto stable=controller.field(F::message).text;
    prepare(controller);
    controller.poll();
    check(controller.field(F::message).text==stable&&
          controller.message_bytes()==Bytes(stable.begin(),stable.end()),
          "Preparing or polling the unchanged message renewed its repeatable identifier");
    const auto stable_binary=controller.field(F::binary).text;
    const auto stable_revision=controller.revision();
    const auto stable_inspection=controller.inspection();
    const auto stable_coded_bytes=controller.estimate()->coded_bytes;
    for(const auto size:{BinaryEditor::payload_limit,BinaryEditor::payload_limit+1}) {
        controller.edit(F::message,std::string(size,'z'));
        check(controller.field(F::message).text==stable&&controller.field(F::binary).text==stable_binary&&
              controller.message_bytes()==Bytes(stable.begin(),stable.end())&&controller.revision()==stable_revision&&
              controller.inspection()==stable_inspection&&controller.estimate()&&
              controller.estimate()->coded_bytes==stable_coded_bytes&&controller.enabled(ui::Command::transmit)&&
              controller.field(F::repeatable).checked,
              "Rejecting an oversized repeatable paste changed its committed text, binary, identifier or prepared estimate");
    }
    controller.edit(F::message,"");
    check(controller.field(F::message).text.empty()&&controller.message_bytes().empty()&&
          !controller.enabled(ui::Command::transmit),
          "Clearing a repeatable message must leave an empty, unsendable preview");
}
void transmit_key_lock_controls() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    controller.edit(F::message,"e");prepare(controller);
    const auto airtime=controller.field(F::airtime).text;
    const auto revision=controller.revision();
    // Feed timing reports into an unstarted shared controller. The live-session
    // regressions independently exercise the clock and actual waveform exposure.
    auto& timing=const_cast<live::Snapshot&>(controller.snapshot());
    timing.transmit_key_lock_seconds=600.01;
    timing.long_transmit_key_lock_seconds=0;
    controller.poll();
    check(controller.command_label(C::transmit)=="TX lock 10m01s"&&
          controller.command_label(C::transmit_short_bits)=="TX lock 10m01s"&&
          !controller.enabled(C::transmit)&&!controller.enabled(C::transmit_short_bits)&&
          controller.enabled(C::force_transmit)&&controller.field(F::force_transmit).visible,
          "Key lock must disable both ordinary submits and expose one validated force action");
    auto warning=controller.field(F::airtime).text;std::replace(warning.begin(),warning.end(),'\n',' ');
    check(warning=="Earlier output used this key for a future symbol.",
          "Key lock did not replace the estimate with its compact explanation");
    const auto draft=controller.message_bytes();
    controller.activate(C::transmit);controller.activate(C::transmit_short_bits);
    check(controller.message_bytes()==draft&&!controller.enabled(C::paste_previous),
          "Locked ordinary submissions changed the draft");
    for(const auto& [seconds,label]:std::vector<std::pair<double,std::string>>{
            {.01,"TX lock 1s"},{61,"TX lock 1m01s"},{7380,"TX lock 2h03m"},{180000,"TX lock 2d02h"}}) {
        timing.transmit_key_lock_seconds=seconds;controller.poll();
        check(controller.command_label(C::transmit)==label,"Long key-lock countdown is not compact");
    }
    timing.transmit_key_lock_seconds=0;controller.poll();
    check(controller.enabled(C::transmit)&&!controller.enabled(C::force_transmit)&&
          !controller.field(F::force_transmit).visible&&controller.field(F::airtime).text==airtime&&
          controller.revision()==revision,
          "Unlocking must restore the existing estimate without recomputing the draft");
    timing.transmit_separation_seconds=5.1;controller.poll();
    check(!controller.enabled(C::transmit)&&!controller.enabled(C::transmit_short_bits)&&
          controller.command_label(C::transmit)=="TX wait 6s"&&controller.field(F::force_transmit).visible&&
          controller.enabled(C::force_transmit)&&controller.field(F::airtime).text=="Waiting for receiver\nsilence check.",
          "Ordinary receiver separation must explain its distinct cause and expose the one-shot override");
    timing.transmit_separation_seconds=601;controller.poll();
    check(!controller.settings().transfer.key&&controller.command_label(C::transmit)=="TX wait 10m01s"&&
          controller.enabled(C::force_transmit)&&controller.field(F::force_transmit).visible,
          "Unencrypted long-symbol separation must offer the same validated force action");
    controller.edit(F::message,"");prepare(controller);
    check(controller.field(F::force_transmit).visible&&!controller.enabled(C::force_transmit),
          "Separation override must remain visible but cannot transmit an empty draft");
    controller.edit(F::short_bits,"001x");controller.poll();
    check(!controller.enabled(C::force_transmit),"Separation override bypassed invalid raw input");
    controller.edit(F::message,"e");prepare(controller);
    timing.transmit_separation_seconds=0;
    controller.poll();
    check(!controller.field(F::force_transmit).visible&&!controller.enabled(C::force_transmit)&&
          controller.field(F::airtime).text==airtime,
          "Ending receiver separation must hide the override and restore the ordinary airtime");
    timing.transmit_key_lock_seconds=601;
    controller.edit(F::message,std::string(16,'e'));prepare(controller);
    check(!controller.enabled(C::transmit)&&controller.enabled(C::force_transmit),
          "Sixteen-byte text selected the long-profile lock");
    controller.edit(F::message,std::string(17,'e'));prepare(controller);
    check(controller.enabled(C::transmit)&&!controller.field(F::force_transmit).visible,
          "Seventeen-byte text retained the short-profile lock");
    timing.long_transmit_key_lock_seconds=7200;controller.poll();
    check(controller.command_label(C::transmit)=="TX lock 2h00m"&&controller.enabled(C::force_transmit),
          "Long text failed to use its own waveform-prefix lock");
    controller.edit(F::short_bits,std::string(150,'0'));prepare(controller);
    check(controller.command_label(C::transmit)=="TX lock 10m01s"&&controller.enabled(C::force_transmit),
          "Explicit raw bits incorrectly selected the long-source lock");
    controller.edit(F::message,"");prepare(controller);
    check(controller.command_label(C::transmit)=="TX lock 10m01s"&&!controller.enabled(C::force_transmit),
          "Empty one-bit preview selected the wrong lock or became transmittable");
    timing.transmit_key_lock_seconds=0;controller.poll();
    check(controller.field(F::airtime).text.starts_with("1-bit preview"),
          "Unlocking did not restore the prepared one-bit estimate");
    timing.transmit_key_lock_seconds=601;
    controller.edit(F::short_bits,"001x");controller.poll();
    check(!controller.enabled(C::force_transmit),"Force action bypassed invalid raw input");
    controller.edit(F::message,"e");prepare(controller);
    struct TemporaryAttachment {
        std::filesystem::path path=std::filesystem::temp_directory_path()/
            ("datapump-lock-attachment-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryAttachment(){std::error_code ignored;std::filesystem::remove(path,ignored);}
    } fixture;
    write_new_file(fixture.path.string(),Bytes{'e'});
    controller.activate(C::attach_file);const auto requests=controller.take_services();
    check(requests.size()==1,"Lock fixture did not request its attachment chooser");
    controller.complete_service({requests.front().id,false,fixture.path.string(),{}});
    check(!controller.enabled(C::force_transmit),"Force action bypassed pending attachment loading");
    prepare(controller);
    check(controller.command_label(C::transmit)=="TX lock 2h00m"&&controller.enabled(C::force_transmit),
          "Even a one-byte attachment must use the long-profile lock");
    controller.activate(C::use_text);controller.edit(F::short_bits,"001");prepare(controller);
    controller.activate(C::force_transmit);
    check(!controller.enabled(C::paste_previous)&&controller.message_bytes()==Bytes{'e'}&&
          controller.enabled(C::force_transmit),
          "A rejected forced start changed the draft or left transmission armed");
    const auto forced_draft=controller.field(F::short_bits).text;
    const bool forced_binary=controller.inspection()->binary;
    controller.start();controller.activate(C::force_transmit);
    check(controller.enabled(C::paste_previous)&&!controller.enabled(C::force_transmit)&&
          !controller.field(F::force_transmit).visible,
          "Forced start did not dispatch once through normal draft capture and busy validation");
    // Computation completion starts the ordinary three-second replay at its
    // first (pre-transmission) trace. Wait for its final frame before checking
    // the complete wire bits, just as the other shared reception fixtures do.
    // Always poll once: the retained unstarted snapshot initially says finished.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    do {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while((!controller.snapshot().transmission_finished||controller.snapshot().simulation_replay)&&
            std::chrono::steady_clock::now()<deadline);
    const auto& completed=controller.snapshot();
    if(!completed.transmission_finished||completed.simulation_replay)
        throw Error("Forced raw transmission did not finish its simulation and replay; status="+completed.status+
            ", error="+completed.error+", generated="+std::to_string(completed.transmit_trace.wire_bits.size()));
    check(completed.running&&completed.transmission_id>0,
          "Forced transmission never reached the live session");
    if(completed.transmit_trace.wire_bits!=Bytes({0,0,1})) {
        std::string bits;for(const auto bit:completed.transmit_trace.wire_bits)bits+=std::to_string(bit);
        throw Error("Completed forced raw transmission changed its exact leading-zero bits; bits="+bits+
            ", trace_active="+std::to_string(completed.transmit_trace.active)+
            ", raw="+std::to_string(completed.transmit_trace.raw)+
            ", generated="+std::to_string(completed.transmit_trace.generated_bits)+
            ", draft="+forced_draft+", binary="+std::to_string(forced_binary)+
            ", status="+completed.status+", error="+completed.error);
    }
    controller.edit(F::short_bits,"001");prepare(controller);
    timing.transmit_key_lock_seconds=601;
    check(!controller.enabled(C::transmit)&&controller.enabled(C::force_transmit),
          "A forced transmission left a sticky bypass for the next draft");
    controller.close();
    check(!controller.enabled(C::force_transmit)&&!controller.field(F::force_transmit).visible,
          "Closing left the override actionable");
}
void previous_message_controls() {
    using F=ui::Field; using C=ui::Command;
    Controller controller({true,true});
    controller.edit(F::callsign,"N0CALL");
    controller.edit(F::grid,"AB12cd");
    controller.toggle(F::repeatable,true);
    const auto seeded=controller.field(F::message).text;
    controller.edit(F::message,seeded+"Anyone listening?");
    const auto sent=controller.message_bytes();
    const auto sent_marker=repeatable_marker(controller.field(F::message).text);
    const auto sent_cursor_revision=controller.field(F::message).text_cursor_end_revision;
    prepare(controller);
    controller.activate(C::transmit);
    check(controller.message_bytes()==sent&&!controller.enabled(C::paste_previous)&&
          controller.field(F::message).text_cursor_end_revision==sent_cursor_revision,
          "Rejected transmission cleared the draft or replaced previous-message state");
    controller.start();
    controller.activate(C::transmit);
    const auto next_seeded=controller.field(F::message).text;
    const auto next_marker=repeatable_marker(next_seeded);
    check(next_seeded==next_marker+seeded.substr(20)&&next_marker!=sent_marker&&controller.enabled(C::paste_previous),
          "Accepted transmission did not immediately reseed the composer and enable previous-message paste");
    check(controller.field(F::message).text_cursor_end_revision>sent_cursor_revision,
          "Starting transmission left subsequent typing inside the new greeting");
    controller.edit(F::message,"The next message is already being typed");
    const auto next_draft=controller.field(F::message).text;
    check(next_draft==repeatable_marker(next_draft)+"The next message is already being typed",
          "Typing the next message lost its repeatable marker or message body");
    // This checks exact delivery and composer state, not processing speed.
    // Sampled interval reception can exceed 15 seconds across compiler builds.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while((controller.inbox().items().empty()||!controller.snapshot().transmission_finished)&&
          std::chrono::steady_clock::now()<deadline) {
        controller.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(!controller.inbox().items().empty(),"Convenience message did not arrive in simulation");
    const auto& received=controller.inbox().items().front().message;
    check(received.data==sent&&received.callsign.empty()&&received.grid.empty()&&!received.repeatable,
          "Convenience values changed the transmitted bytes or leaked into packet metadata");
    check(controller.snapshot().transmission_finished&&
          controller.field(F::message).text==next_draft,
          "Late transmission completion cleared a newly typed draft");
    controller.edit(F::callsign,"ANOTHER");
    controller.edit(F::grid,"ZZ99zz");
    const auto paste_cursor_revision=controller.field(F::message).text_cursor_end_revision;
    controller.activate(C::paste_previous);
    check(controller.message_bytes()==sent&&!controller.field(F::repeatable).checked,
          "Previous-message paste changed exact bytes or interpreted the restored repeatable marker");
    check(controller.field(F::message).text_cursor_end_revision>paste_cursor_revision,
          "Restoring the previous message did not place the cursor after it");
    controller.toggle(F::repeatable,false);
    check(controller.message_bytes()==sent,"A pasted in-band marker was removed as checkbox state");
    controller.edit(F::message,controller.field(F::message).text+"!");
    const auto edited_marker=repeatable_marker(controller.field(F::message).text);
    check(edited_marker!=sent_marker&&!controller.field(F::repeatable).checked&&
          controller.field(F::message).text==edited_marker+std::string(sent.begin()+20,sent.end())+"!",
          "Editing a pasted previous message reused its identifier or changed its content");
    controller.activate(C::paste_previous);
    check(controller.message_bytes()==sent,
          "Editing a pasted previous message changed the saved retransmission bytes");
    controller.close();
    check(!controller.enabled(C::paste_previous),"Previous-message paste remained enabled while closing");
}
void repeatable_pending_drafts() {
    using F=ui::Field; using C=ui::Command;
    Controller controller({true,true});
    const auto force_off_with_attachment=[&] {
        controller.activate(C::attach_file);
        const auto services=controller.take_services();
        check(services.size()==1&&services.front().kind==ui::ServiceKind::open_file,
              "Pending draft attachment did not request a file");
        controller.complete_service({services.front().id,false,std::filesystem::absolute(__FILE__).string(),{}});
        check(!controller.field(F::repeatable).checked&&!controller.field(F::repeatable).enabled,
              "Pending draft prevented an attachment from forcing repeatable off");
        controller.activate(C::use_text);
    };
    const std::string body="abcdefghijklmnopqrst";
    controller.edit(F::message,body);
    controller.toggle(F::repeatable,true);
    const auto binary_bytes=controller.message_bytes();
    const auto original_binary=controller.field(F::binary).text;
    auto binary_draft=original_binary;
    binary_draft.pop_back();
    controller.edit(F::binary,binary_draft);
    force_off_with_attachment();
    check(controller.field(F::binary).text==binary_draft&&controller.message_bytes()==binary_bytes,
          "Forcing repeatable off changed an incomplete binary draft or its committed prefix offsets");
    controller.edit(F::binary,original_binary);
    check(controller.field(F::message).text==body&&
          controller.message_bytes()==Bytes(body.begin(),body.end()),
          "Completing a binary draft after repeatable was forced off lost or duplicated its suffix");

    controller.edit(F::message,"");
    controller.edit(F::binary,"00000000 11111111");
    controller.edit(F::message,controller.field(F::message).text);
    controller.toggle(F::repeatable,true);
    auto escaped_marker=repeatable_marker(controller.field(F::message).text);
    for(const auto suffix:{"\\x0\\xFF","\\x\\xFF"}) {
        controller.edit(F::message,escaped_marker+suffix);
        const auto changed_marker=repeatable_marker(controller.field(F::message).text);
        auto expected_bytes=Bytes(changed_marker.begin(),changed_marker.end());
        expected_bytes.insert(expected_bytes.end(),{0,255});
        check(changed_marker!=escaped_marker&&controller.field(F::message).text==changed_marker+suffix&&
              controller.message_bytes()==expected_bytes&&!controller.estimate()&&!controller.enabled(C::transmit),
              "An incomplete escaped edit failed to renew its identifier or changed its draft and committed body");
        escaped_marker=changed_marker;
    }
    const auto escaped_draft=controller.field(F::message).text;
    const auto escaped_bytes=controller.message_bytes();
    force_off_with_attachment();
    check(controller.field(F::message).text==escaped_draft&&controller.message_bytes()==escaped_bytes,
          "Forcing repeatable off changed an incomplete escaped draft or its committed bytes");
    controller.edit(F::message,escaped_marker+"\\x01\\xFF");
    check(controller.message_bytes()==Bytes({1,255})&&controller.field(F::message).text=="\\x01\\xFF",
          "Completing an escaped draft after repeatable was forced off changed its payload bytes");
}
void binary_editor_controls() {
    using F=ui::Field; using C=ui::Command;
    Controller controller({true,true});
    check(controller.field(F::message).enabled&&controller.field(F::binary).enabled,
          "Both payload editors must be editable without a source choice");
    controller.edit(F::message,"abcdefghijklmnopTAIL");
    check(parse_binary_bits(controller.field(F::binary).text).size()==128,
          "Binary view must stop at sixteen bytes");
    BinaryEditor prefix(Bytes(16,'A'));
    controller.edit(F::binary,prefix.binary());
    check(controller.field(F::message).text==std::string(16,'A')+"TAIL",
          "Binary edit changed the suffix beyond sixteen bytes");
    controller.edit(F::binary,"01000010");
    check(controller.field(F::message).text=="BTAIL"&&
          parse_binary_bits(controller.field(F::binary).text).size()==40,
          "Shortened prefix did not preserve and display the shifted suffix");
    controller.edit(F::message,"A");
    prepare(controller);
    controller.edit(F::binary,"001");
    check(controller.field(F::binary).text=="001"&&controller.field(F::message).text=="A"&&
          !controller.estimate()&&!controller.enabled(C::transmit),
          "New raw draft changed its text view or retained a stale estimate");
    prepare(controller);
    check(controller.inspection()->binary&&controller.enabled(C::transmit)&&
          !controller.field(F::repeatable).enabled&&!controller.field(F::fec).enabled,
          "Three raw bits were not prepared or retained framing controls");
    controller.edit(F::message,"A");
    check(controller.field(F::binary).text=="01000001",
          "Reapplying the displayed message did not leave raw-bit mode");
    controller.edit(F::binary,"00000000 11111111");
    check(controller.message_bytes()==Bytes({0,255})&&controller.field(F::message).text=="\\x00\\xFF"&&
          controller.field(F::message_label).text.find("escaped")!=std::string::npos,
          "Arbitrary binary bytes were lost or displayed as ordinary text");
    prepare(controller);
    check(controller.inspection()->binary&&!controller.inspection()->stream_layout,
          "Short binary editing still selected packet framing");
    const auto expected_seconds=controller.estimate()->total_seconds;
    controller.start();controller.activate(C::transmit);controller.poll();
    const auto receive_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(!controller.snapshot().transmission_finished&&std::chrono::steady_clock::now()<receive_deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(!controller.snapshot().transmission_finished ||
       std::abs(controller.snapshot().transmission_seconds-expected_seconds)>1./controller.settings().transfer.modem.sample_rate)
        throw Error("Raw binary airtime mismatch: expected "+std::to_string(expected_seconds)+", got "+
            std::to_string(controller.snapshot().transmission_seconds)+"; "+controller.snapshot().error);
    check(controller.message_bytes().empty()&&controller.enabled(C::paste_previous),
          "Transmitting arbitrary bytes did not clear the composer and retain previous-message paste");
    controller.activate(C::paste_previous);
    check(controller.message_bytes()==Bytes({0,255})&&controller.field(F::message).text=="\\x00\\xFF",
          "Previous-message paste did not restore exact arbitrary bytes and their escaped editor mode");
    prepare(controller);
    check(controller.inspection()->binary,"Previous-message paste lost raw-bit dispatch mode");
    controller.edit(F::message,"\\x0\\xFF");
    check(controller.field(F::message).text=="\\x0\\xFF"&&!controller.estimate()&&
          controller.message_bytes()==Bytes({0,255}),"Incomplete escape lost its draft or changed committed bytes");
    controller.edit(F::message,"\\x01\\xFF");
    check(controller.message_bytes()==Bytes({1,255})&&controller.field(F::binary).text=="00000001 11111111",
          "Completing an escape did not update the binary view");
    controller.edit(F::binary,std::string(136,'0'));
    check(controller.message_bytes()==Bytes({1,255})&&!controller.enabled(C::transmit),
          "Oversized binary paste changed the payload");
    controller.edit(F::binary,"");
    check(controller.message_bytes().empty()&&controller.field(F::message).text.empty()&&
          controller.field(F::message_label).text=="Message","Clearing binary did not clear the short payload");
}
void three_bit_dispatch() {
    using F=ui::Field; using C=ui::Command;
    Controller controller({true,true});
    controller.edit(F::carrier,"1.65 kHz");
    controller.edit(F::binary,"001");
    prepare(controller);
    const auto& inspection=*controller.inspection();
    check(inspection.binary&&!inspection.stream_layout&&controller.field(F::binary).text=="001"&&
          controller.message_bytes().empty(),"Empty composer padded its three-bit raw draft");
    const auto expected=transfer::estimate_binary(Bytes{0,0,1},controller.settings().transfer);
    check(controller.estimate()->total_seconds==expected.total_seconds,
          "Three-bit GUI estimate encoded text or rounded the bit count");
    controller.start();controller.activate(C::transmit);
    check(controller.enabled(C::paste_previous)&&controller.field(F::binary).text.empty(),
          "Accepted raw draft did not clear and retain its exact previous value");
    controller.poll();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(!controller.snapshot().transmission_finished&&std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(controller.snapshot().transmission_finished&&
          std::abs(controller.snapshot().transmission_seconds-expected.total_seconds)<=1./controller.settings().transfer.modem.sample_rate,
          "Three-bit draft did not dispatch as the exact unframed signal");
    check(controller.settings().transfer.modem.carrier_hz==1650 && controller.field(F::carrier).text=="1.65 kHz",
          "Live transmission replaced the manually selected carrier");
    controller.activate(C::paste_previous);
    check(controller.field(F::binary).text=="001"&&controller.message_bytes().empty(),
          "Restoring three-bit draft changed its leading zeros or synthesized a byte");
    prepare(controller);
    check(controller.inspection()->binary&&controller.estimate()->total_seconds==expected.total_seconds,
          "Restored three-bit draft changed transport or airtime");
}
BitmapImage render(const plots::PlotSnapshot& source) {
    BitmapImage image(120, 120);
    source.paint(full_bitmap_request(120, 120, false, true), [&](unsigned x, unsigned y, PixelBlock block) { image.blit(x, y, block); });
    return image;
}
void fixed_text_reception() {
    using F=ui::Field;using C=ui::Command;
    const std::string expected="A fixed interval!";
    Controller controller({true,true});controller.edit(F::message,expected);prepare(controller);
    check(controller.inspection()->stream_layout && controller.estimate()->coded_bytes==128,
          "17-byte message uses the single fixed coded interval");
    controller.start();controller.activate(C::transmit);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);
    std::size_t pending_count=0;
    while(controller.inbox().items().empty() && std::chrono::steady_clock::now()<deadline) {
        controller.poll();pending_count+=check_pending_snapshot(controller);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(pending_count>0,"Fixed interval reception must publish pending bits before source validation and completion");
    check(controller.inbox().items().size()==1 && controller.inbox().items().front().message.data==Bytes(expected.begin(),expected.end()),
          "fixed source reception preserves text");
    check(controller.inbox().file_items().empty(),"Ordinary source text became a received file");
    std::optional<std::size_t> index;
    for(std::size_t i=0;i<controller.signals().lines().size();++i)
        if(controller.signals().lines()[i].validated)index=i;
    check(index.has_value(),"completed source has a validated display row");
    controller.select(F::signals,std::to_string(controller.signals().lines()[*index].id));
    controller.activate(C::copy_signal);const auto requests=controller.take_services();
    check(requests.size()==1 && requests.front().value=="A fixed interval_","copy must restrict decoded source bytes");
    controller.complete_service({requests.front().id,false,{},{}});
    controller.activate(C::paste_signal);
    check(controller.field(F::message).text=="A fixed interval_","paste must restrict decoded source text");
    controller.set_shellcode_mode(true);
    controller.activate(C::paste_signal);
    check(controller.message_bytes()==Bytes(expected.begin(),expected.end()),"Shellcode permits printable ASCII source text");
    controller.activate(C::copy_signal);
    controller.set_shellcode_mode(false);
    const auto withdrawn=controller.take_services();
    check(withdrawn.size()==1&&withdrawn.front().value=="A fixed interval_"&&
          controller.field(F::message).text=="A fixed interval_",
          "Revoking Shellcode must scrub the received draft and queued clipboard");
    prepare(controller);noise_start_stop(controller);
    controller.activate(C::paste_previous);
    check(controller.message_bytes()==Bytes(expected.begin(),expected.end()),"Noise replaced the previous transmitted message");
    controller.close();
}

void receive_pattern_text(Controller& controller,const std::string& expected) {
    check(!controller.snapshot().transmit_trace.active,
          "A prepared draft became an actual generation trace before transmission");
    const auto expected_wire=controller.inspection()->binary?
        parse_binary_bits(controller.field(ui::Field::binary).text):compression::encode_short_bits(controller.message_bytes());
    controller.start();controller.activate(ui::Command::transmit);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    bool received=false;
    bool generated_seen=false;
    std::size_t pending_count=0;
    while(std::chrono::steady_clock::now()<deadline) {
        controller.poll();
        const auto& trace=controller.snapshot().transmit_trace;
        check(controller.field(ui::Field::transmit_scope).records==transmit_scope_records(trace),
              "A progress poll deferred generation scope updates or substituted estimated bytes");
        const bool active=controller.snapshot().transmitting||controller.snapshot().simulation_replay;
        check(controller.field(ui::Field::transmit_scope).visible==active&&
              controller.field(ui::Field::transmit_scope_caption).visible==active,
              "Auto-hide scope did not follow transmission and replay on the same progress poll");
        if(trace.active&&!trace.wire_bits.empty()) {
            if(!generated_seen&&active) {
                const auto transmission_id=controller.snapshot().transmission_id;
                const auto captured=controller.field(ui::Field::transmit_scope).records;
                controller.select(ui::Field::transmit_scope_format,"none");
                check(!controller.field(ui::Field::transmit_scope).visible&&
                      !controller.field(ui::Field::transmit_scope_caption).visible&&
                      controller.field(ui::Field::transmit_scope_format).enabled,
                      "None must hide an active preview while leaving the display choice available");
                controller.select(ui::Field::transmit_scope_format,"hex-auto-hide");
                check(controller.field(ui::Field::transmit_scope).visible&&
                      controller.field(ui::Field::transmit_scope).records==captured&&
                      controller.snapshot().transmission_id==transmission_id,
                      "Changing preview visibility altered the active transmission or its captured data");
            }
            generated_seen=true;
            check(trace.wire_bits.size()<=expected_wire.size()&&
                  std::equal(trace.wire_bits.begin(),trace.wire_bits.end(),expected_wire.begin()),
                  "Unencrypted short scope lost an exact leading-zero or partial-byte wire prefix");
        }
        pending_count+=check_pending_snapshot(controller);
        for(std::size_t i=0;i<controller.signals().lines().size();++i)
            received=received||controller.signals().copy_text(i)==expected;
        if(received && controller.snapshot().transmission_finished && !controller.snapshot().simulation_replay)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(!received || !controller.snapshot().transmission_finished || controller.snapshot().simulation_replay) {
        std::string detail="Pattern reception did not complete as expected copyable text; "+controller.snapshot().error;
        for(const auto& line:controller.signals().lines())detail+=" ["+signal_status_label(line)+": "+line.text+"]";
        throw Error(detail);
    }
    check(pending_count>0,"Pattern text reception must expose pending bits before it becomes received text");
    check(generated_seen,"Actual simulated generation never reached the Console scope");
    check(!controller.field(ui::Field::transmit_scope).visible&&
          !controller.field(ui::Field::transmit_scope_caption).visible,
          "Auto-hide left the completed transmission preview visible");
}
void short_text_reception() {
    using F=ui::Field;
    Controller controller({true,true});controller.edit(F::message,"quick brown fox");prepare(controller);
    check(!controller.inspection()->stream_layout && !controller.inspection()->binary &&
          controller.estimate()->wire_bits==compression::encode_short_bits(Bytes{'q','u','i','c','k',' ','b','r','o','w','n',' ','f','o','x'}).size() && !controller.field(F::fec).enabled &&
          controller.field(F::fec).selected=="rs60" && controller.field(F::fec).display_text=="Off (short dictionary)",
          "15-byte text must use exact dictionary bits and retain the selected FEC for later longer messages");
    receive_pattern_text(controller,"quick brown fox");
    check(controller.inbox().items().empty() && controller.signals().lines().size()==1 &&
          !controller.signals().lines().front().validated,
          "short raw text must be copyable without acquiring coded validation or an attachment");
    const auto& received=controller.signals().lines().front();
    check(!received.raw_bits.empty() && signal_data_label(received)=="No checksum / FEC" &&
          !controller.signals().copy_id(0),"Dictionary decoding must retain exact bits without claiming validation");
    controller.edit(F::message,"quick brown fox ");prepare(controller);
    check(!controller.inspection()->stream_layout && controller.estimate()->wire_bits==98 &&
          parse_binary_bits(controller.field(F::short_bits).text).size()==98 && !controller.field(F::fec).enabled,
          "A 16-byte message must remain exactly 98 dictionary bits through both message and code previews");
    controller.edit(F::message,"quick brown fox!!");prepare(controller);
    check(controller.inspection()->stream_layout && controller.estimate()->wire_bits==1216 &&
          controller.field(F::fec).enabled && controller.field(F::fec).selected=="rs60" &&
          controller.field(F::fec).display_text.empty(),
          "17-byte text must restore selected fixed interval coding");
    controller.close();
}

void three_bit_text_reception() {
    using F=ui::Field;
    Controller controller({true,true});controller.edit(F::message,"e");prepare(controller);
    check(controller.estimate()->wire_bits==3 && !controller.inspection()->binary &&
          controller.field(F::short_bits).text=="001", "Text e must transmit exactly three dictionary bits");
    receive_pattern_text(controller,"e");
    check(controller.signals().lines().size()==1 && controller.signals().copy_raw_bits(0)=="001" &&
          !controller.signals().lines().front().validated && controller.inbox().items().empty(),
          "Three-bit dictionary text must retain raw transport independently of its decoded character");
    check(controller.snapshot().transmit_trace.wire_bits==Bytes({0,0,1})&&
          controller.field(F::transmit_scope).records==transmit_scope_records(controller.snapshot().transmit_trace),
          "Completed three-bit generation did not retain the exact diagnostic capture");
    const auto retained=controller.field(F::transmit_scope).records;
    controller.edit(F::message,"A different draft");prepare(controller);
    check(controller.field(F::transmit_scope).records==retained,
          "Editing the next draft replaced the previous actual generation capture");
    const auto transmission_id=controller.snapshot().transmission_id;
    controller.select(F::transmit_scope_format,"bits");
    check(controller.field(F::transmit_scope).records==transmit_scope_records(controller.snapshot().transmit_trace,true)&&
          controller.field(F::transmit_scope).visible&&controller.field(F::transmit_scope_caption).visible&&
          controller.snapshot().transmission_id==transmission_id,
          "Bit-detail selection regenerated the transmission or lost its actual retained prefix");
    controller.close();
}

void short_raw_editor() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    check(!controller.enabled(C::transmit_short_bits)&&!controller.enabled(C::copy_raw_signal)&&
          !controller.enabled(C::paste_raw_signal),"Empty raw tab enabled a transmission or receive action");
    controller.edit(F::callsign,"N0CALL");controller.toggle(F::repeatable,true);
    controller.edit(F::short_bits,"0 1 0");prepare(controller);
    check(controller.field(F::binary).text=="010"&&controller.field(F::message).text=="t"&&
          !controller.field(F::repeatable).checked&&controller.inspection()->binary&&
          controller.enabled(C::transmit_short_bits),"Short raw entry did not replace a long greeting with exact bits");
    check(controller.estimate()->total_seconds==transfer::estimate_binary(Bytes{0,1,0},controller.settings().transfer).total_seconds&&
          controller.field(F::short_bits_detail).text.find("exactly as entered")!=std::string::npos,
          "Raw tab did not distinguish three transmitted bits from the decoded t byte");
    const auto& reference=controller.field(F::compression_codes).text;
    check(reference.find("13 total")!=std::string::npos && reference.find("e 001")!=std::string::npos &&
          reference.find("t 010")!=std::string::npos && reference.find("space 000")!=std::string::npos,
          "Compression reference must show the fixed dictionary and long source transport");
    for(const auto& invalid:{std::string(transfer::short_message_bits+1,'0'),std::string("01x"),std::string{}}) {
        controller.edit(F::short_bits,invalid);controller.poll();
        check(controller.field(F::short_bits).text==invalid&&!controller.estimate()&&
              !controller.enabled(C::transmit_short_bits)&&!controller.enabled(C::transmit),
              "Invalid short raw input silently transmitted the previous draft");
    }
    for(const auto raw:{"0","00","0010","1111","1011","01010","110000","1111101000001"}) {
        controller.edit(F::short_bits,raw);prepare(controller);
        check(parse_binary_bits(controller.field(F::binary).text)==parse_binary_bits(raw)&&controller.enabled(C::transmit_short_bits)&&
              controller.inspection()->binary&&controller.estimate()->total_seconds==
                  transfer::estimate_binary(parse_binary_bits(raw),controller.settings().transfer).total_seconds,
              "Exact raw input was padded, compressed, or rejected as an incomplete dictionary token");
    }
    for(const auto& source:{std::string("s"),std::string("A"),std::string("quick brown"),std::string(transfer::short_message_bytes,'A')}) {
        const Bytes bytes(source.begin(),source.end());const auto expected_bits=compression::encode_short_bits(bytes);
        controller.edit(F::message,source);prepare(controller);
        check(parse_binary_bits(controller.field(F::short_bits).text)==expected_bits && !controller.inspection()->binary &&
              !controller.inspection()->stream_layout && controller.estimate()->wire_bits==expected_bits.size() &&
              controller.field(F::short_bits_detail).text.find("Expected text: '"+source+"'")!=std::string::npos,
              "Compression tab must expose the full short text code without changing Message transmission mode");
        controller.edit(F::short_bits,controller.field(F::short_bits).text);prepare(controller);
        check(controller.inspection()->binary && controller.message_bytes()==bytes &&
              parse_binary_bits(controller.field(F::binary).text)==expected_bits && controller.estimate()->wire_bits==expected_bits.size(),
              "Selecting displayed dictionary bits must preserve its text preview and exact raw transmission");
    }
    controller.edit(F::short_bits,std::string(3*(transfer::short_message_bytes+1),'0'));prepare(controller);
    check(controller.inspection()->binary && controller.estimate()->wire_bits==3*(transfer::short_message_bytes+1) &&
          controller.field(F::short_bits_detail).text.find("exceed")!=std::string::npos,
          "Complete codes beyond the short text byte limit must remain usable raw bits with an honest preview");
    controller.edit(F::message,"t");prepare(controller);
    check(controller.field(F::short_bits).text=="010"&&!controller.inspection()->binary&&
          controller.field(F::binary).text=="01110100"&&controller.enabled(C::transmit_short_bits),
          "Message text did not expose its lowercase code separately from byte bits");
    controller.edit(F::short_bits,"010");prepare(controller);
    check(controller.inspection()->binary&&controller.field(F::binary).text=="010",
          "Reapplying the displayed code did not select exact raw transmission");
    controller.edit(F::binary,"01x");
    check(!controller.enabled(C::transmit_short_bits),"Invalid Console Binary edit left raw transmission enabled");
    controller.edit(F::short_bits,"010");prepare(controller);
    check(controller.enabled(C::transmit_short_bits)&&controller.field(F::binary).text=="010",
          "Reapplying raw bits did not discard an invalid Console Binary draft");
    controller.edit(F::message,"A longer console message");prepare(controller);
    check(controller.field(F::short_bits).text.empty()&&!controller.enabled(C::transmit_short_bits)&&
          controller.enabled(C::transmit),"Raw tab could transmit an unrelated long Console draft");
    controller.edit(F::short_bits,"010");controller.activate(C::attach_file);
    const auto services=controller.take_services();
    controller.complete_service({services.front().id,false,std::filesystem::absolute(__FILE__).string(),{}});
    prepare(controller);
    check(!controller.field(F::short_bits).enabled&&!controller.enabled(C::transmit_short_bits),
          "Raw tab could edit or transmit an attachment");
    controller.activate(C::use_text);prepare(controller);
    check(controller.field(F::short_bits).text=="010"&&controller.enabled(C::transmit_short_bits),
          "Returning from an attachment lost the short raw draft");
    controller.close();
}
void short_raw_reception() {
    using F=ui::Field;using C=ui::Command;
    for(const auto raw:{"010","01","0010","1111101110001110101101011011011111011010110001111001100101001110011011"}) {
        Controller controller({true,true});controller.edit(F::short_bits,raw);prepare(controller);
        controller.start();controller.activate(C::transmit_short_bits);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        std::optional<std::size_t> received;
        std::size_t pending_count=0;
        while(std::chrono::steady_clock::now()<deadline) {
            controller.poll();
            pending_count+=check_pending_snapshot(controller);
            for(std::size_t i=0;i<controller.signals().lines().size();++i)
                if(controller.signals().copy_raw_bits(i)==raw)received=i;
            if(controller.snapshot().simulation_replay)
                check(!received,"Exact received bits became copyable before simulation replay completed");
            if(received&&controller.snapshot().transmission_finished&&!controller.snapshot().simulation_replay)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(pending_count>0,"Short raw reception must display incoming bits before physical completion");
        check(received.has_value(),"Simulated short raw pattern did not retain its exact received bits");
        controller.select(F::signals,std::to_string(controller.signals().lines()[*received].id));
        check(controller.enabled(C::copy_raw_signal)&&controller.enabled(C::paste_raw_signal)&&
              controller.field(F::received_raw_bits).text.find(std::string(raw).substr(0,64))!=std::string::npos,
              "Completed raw reception did not enable inspection, copy and reuse");
        controller.activate(C::copy_raw_signal);const auto requests=controller.take_services();
        check(requests.size()==1&&requests.front().value==raw,"Copy raw bits copied decoded text or padded byte bits");
        controller.complete_service({requests.front().id,false,{},{}});
        if(std::string_view(raw)=="010" || std::string_view(raw).size()==70) {
            check(controller.signals().copy_text(*received)==(std::string_view(raw)=="010"?"t":"quick brown") && !controller.signals().copy_id(*received) &&
                  controller.signals().lines().size()==1 && controller.inbox().items().empty(),
                  "Complete dictionary token must replace its pending row with unvalidated text and retain exact bits");
        } else check(!controller.signals().copy_text(*received),"Incomplete dictionary token must remain raw bits");
        controller.activate(C::paste_raw_signal);prepare(controller);
        check(controller.field(F::short_bits).text==raw&&parse_binary_bits(controller.field(F::binary).text)==parse_binary_bits(raw)&&
              controller.inspection()->binary&&controller.enabled(C::transmit_short_bits)&&
              controller.estimate()->wire_bits==std::string_view(raw).size(),
              "Reusing received raw bits changed their exact length or value");
        controller.activate(C::clear_received);
        check(!controller.enabled(C::copy_raw_signal)&&!controller.enabled(C::paste_raw_signal),
              "Clearing receptions left stale raw bits available");
        controller.close();
    }
}
void byte_aligned_pattern_reception() {
    using F=ui::Field;using C=ui::Command;
    const std::string binary="01001000 01100101\n01101100 01110000";
    const auto expected_bits=parse_binary_bits(binary);
    Controller controller({true,true});controller.edit(F::binary,binary);prepare(controller);
    check(controller.inspection()->binary && expected_bits.size()==32,
          "Byte-aligned reception fixture must transmit exact raw bytes");
    receive_pattern_text(controller,"Help");
    check(controller.signals().lines().size()==1 && controller.field(F::signals).records.size()==1,
          "Byte-aligned raw reception left a duplicate raw-bit or dictionary row in the signal browser");
    const auto& line=controller.signals().lines().front();
    check(line.complete && signal_status_label(line)=="text received" && !line.validated && line.pattern_score.has_value() &&
          controller.inbox().items().empty() && !controller.signals().copy_bits(0),
          "Byte-aligned raw text retained binary copying or acquired packet validation");
    controller.select(F::signals,std::to_string(line.id));
    check(controller.enabled(C::copy_signal),"Byte-aligned received text was blocked from copying");
    controller.activate(C::copy_signal);const auto requests=controller.take_services();
    check(requests.size()==1 && requests.front().kind==ui::ServiceKind::clipboard && requests.front().value=="Help",
          "Byte-aligned signal copied a bit string instead of its message text");
    controller.complete_service({requests.front().id,false,{},{}});
    controller.edit(F::message,requests.front().value);
    check(controller.message_bytes()==Bytes({'H','e','l','p'}) &&
          parse_binary_bits(controller.field(F::binary).text)==expected_bits,
          "Pasting received text into Message did not restore its exact bytes in the binary editor");
    controller.close();
}
void escaped_signal_message_paste() {
    using F=ui::Field;using C=ui::Command;
    const Bytes expected{0,255,'\\','x','4','1'};
    const BinaryEditor original(expected);
    check(original.escaped(),"Received arbitrary-byte fixture must require escaped text");
    Controller controller({true,true});
    check(!controller.enabled(C::paste_signal),"Paste as message was enabled without a received selection");
    controller.edit(F::binary,original.binary());prepare(controller);
    receive_pattern_text(controller,"___x41");
    check(controller.signals().lines().size()==1 && controller.field(F::signals).records.size()==1 &&
          signal_status_label(controller.signals().lines().front())=="text received",
          "Byte-aligned arbitrary bytes did not produce exactly one escaped text row");
    controller.select(F::signals,std::to_string(controller.signals().lines().front().id));
    check(controller.enabled(C::paste_signal),"Received arbitrary bytes could not be pasted as a message");
    controller.activate(C::copy_signal);const auto requests=controller.take_services();
    check(requests.size()==1 && requests.front().kind==ui::ServiceKind::clipboard && requests.front().value=="___x41",
          "Copying arbitrary received bytes exposed an escape or raw byte");
    controller.complete_service({requests.front().id,false,{},{}});
    controller.edit(F::message,"A plain message with an unfinished binary prefix");
    controller.toggle(F::repeatable,true);
    controller.edit(F::binary,"001");
    check(controller.field(F::repeatable).checked && !controller.estimate() &&
          controller.field(F::binary_label).text.find("incomplete")!=std::string::npos,
          "Paste fixture did not retain a repeatable plain-text draft with an incomplete binary prefix");
    controller.activate(C::paste_signal);
    const BinaryEditor safe(Bytes{'_','_','_','x','4','1'});
    check(controller.message_bytes()==safe.bytes() && controller.field(F::message).text==safe.text() &&
          controller.field(F::binary).text==safe.binary() && !controller.field(F::repeatable).checked &&
          controller.field(F::message_label).text.find("escaped")==std::string::npos,
          "Paste as message must restrict arbitrary bytes before composer or QR processing");
    controller.set_shellcode_mode(true);controller.activate(C::paste_signal);
    check(controller.field(F::message).text=="__\\x41","Shellcode must allow backslash while excluding binary bytes");
    controller.activate(C::attach_file);
    const auto attachment_request=controller.take_services();
    check(attachment_request.size()==1,"Receive draft fixture could not choose an attachment");
    controller.complete_service({attachment_request.front().id,false,std::filesystem::absolute(__FILE__).string(),{}});
    prepare(controller);
    const auto attachment_marker=controller.field(F::message).text;
    controller.set_shellcode_mode(false);
    check(controller.field(F::message).text==attachment_marker&&controller.field(F::message_label).text.starts_with("Attached:"),
          "Revoking Shellcode replaced the local attachment preview with its retained receive draft");
    controller.activate(C::use_text);
    check(controller.field(F::message).text=="___x41","Leaving Shellcode must scrub the retained received-derived draft");
    controller.edit(F::message,"__\\x41");
    check(controller.field(F::message).text=="___x41","Stale native edits must not restore received shell punctuation");
    const auto text_history=controller.field(F::message).text_history_revision;
    const auto binary_history=controller.field(F::binary).text_history_revision;
    const auto short_history=controller.field(F::short_bits).text_history_revision;
    controller.edit(F::message,"");
    check(controller.field(F::message).text_history_revision>text_history&&
          controller.field(F::binary).text_history_revision>binary_history&&
          controller.field(F::short_bits).text_history_revision>short_history,
          "Clearing received provenance must invalidate undo in every linked editor");
    controller.edit(F::message,"echo hi; (test) &");
    check(controller.field(F::message).text=="echo hi; (test) &","Fresh locally typed TX text must stay unrestricted");
    prepare(controller);
    check(controller.enabled(C::transmit),"Paste as message retained the discarded draft's incomplete binary error");
    controller.close();
}
void received_raw_text_boundary() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});controller.edit(F::message,";()\\&");prepare(controller);
    receive_pattern_text(controller,"_____");
    controller.select(F::signals,std::to_string(controller.signals().lines().front().id));
    const auto bits=*controller.signals().copy_raw_bits(0);
    controller.activate(C::paste_raw_signal);prepare(controller);
    check(controller.field(F::message).text=="_____"&&controller.field(F::short_bits).text==bits&&
          controller.field(F::short_bits_detail).text.find("Expected text: '_____'")!=std::string::npos,
          "Received raw bits leaked their decoded text into the composer or expected-text preview");
    controller.set_shellcode_mode(true);controller.activate(C::paste_raw_signal);
    check(controller.field(F::message).text==";()\\&","Shellcode must allow printable ASCII raw-bit interpretations");
    controller.set_shellcode_mode(false);prepare(controller);
    check(controller.field(F::message).text=="_____"&&controller.field(F::short_bits).text==bits&&
          controller.inspection()->binary&&controller.estimate()->wire_bits==bits.size(),
          "Revoking Shellcode must scrub text without changing original raw-bit transmission");
    controller.close();
}
void binary_source_representation() {
    using F=ui::Field;
    for(const auto& expected:{Bytes{'i','n'},Bytes{0,'e',0}}) {
        Controller controller({true,true});const BinaryEditor editor(expected);
        controller.edit(F::binary,editor.binary()); // Establish the byte editor's explicit escaped mode.
        controller.edit(F::message,editor.text());prepare(controller);
        check(controller.message_bytes()==expected && !controller.inspection()->stream_layout &&
              !controller.inspection()->binary && controller.estimate()->wire_bits==compression::encode_short_bits(expected).size(),
              "short dictionary must preserve exact source bytes and trailing zeros");
        if(expected.front()==0) {
            receive_pattern_text(controller,received_text(expected));
            check(controller.inbox().items().empty(),"Short raw binary source became an attachment or validated source");
            const auto& lines=controller.signals().lines();
            check(lines.size()==1 && lines.front().complete && !lines.front().validated,
                  "Short binary source had no completed raw message row");
            controller.select(F::signals,std::to_string(lines.front().id));controller.activate(ui::Command::copy_signal);
            const auto copied=controller.take_services();
            check(copied.size()==1 && copied.front().value==received_text(expected),"Binary source copy escaped the restricted presentation");
            controller.complete_service({copied.front().id,false,{},{}});
        }
        controller.close();
    }
}

void receive_target_controls() {
    using F=ui::Field;
    Controller controller({true,true});
    check(controller.field(F::receive_snr).text=="32, 55" && controller.settings().transfer.receive_targets_db_hz==std::vector<double>({32,55}) &&
          controller.settings().transfer.automatic_receive_profiles,"automatic reception must cover both default TX targets");
    const auto tx=controller.settings().transfer.modem;
    controller.edit(F::receive_snr,"40,");
    controller.poll();
    check(controller.field(F::receive_snr).text=="40,","a partial comma-list edit must remain editable before normalization");
    controller.edit(F::receive_snr," 40, +6, -6, 40 ");
    std::this_thread::sleep_for(std::chrono::milliseconds(775));controller.poll();
    const auto accepted_targets=controller.settings().transfer.receive_targets_db_hz;
    const bool adjusted_list=accepted_targets.size()==3&&accepted_targets.front()==40&&
          accepted_targets[1]!=6&&
          controller.field(F::receive_snr).text==" 40, +6, -6, 40 "&&
          !controller.field(F::receive_snr).display_text.empty();
    if(!adjusted_list)throw Error("The short-profile clock gap near 6 dB must snap without overwriting the typed RX list: "+
        controller.field(F::receive_snr).text+" / "+controller.field(F::receive_snr).display_text+" / "+
        controller.field(F::status).text);
    auto options=controller.settings().transfer;
    auto input=controller.link_plan()->inputs;input.options=options;input.target_db_hz=-6;
    const std::array companions{accepted_targets[0],accepted_targets[1]};
    const auto expected_last=planner::nearest_fit_target(input,companions,planner::ReceiveBanks{true,0});
    check(expected_last&&*expected_last==accepted_targets.back(),
          "The RX list must use the nearest checked target under its actual RAM allowance and companion bank");
    const auto profiles=tuning::receive_profiles(options.modem,accepted_targets,options.receive_pattern_mode,false);
    modem::ChannelConfig channel;channel.clock_error_ppm=controller.settings().simulation_clock_error_ppm;
    for(const auto& profile:profiles) {
        options.modem=profile;
        const auto receiver=simulation::estimate(transfer::estimate_binary(Bytes{0},options),options,true,channel,profiles,1,false);
        check(receiver.carrier_in_search&&receiver.receiver_workspace_supported,
              "Every advertised RX target must independently fit clock search and the shared receiver workspace");
    }
    controller.commit_target(F::receive_snr);
    check(tuning::parse_receive_targets(controller.field(F::receive_snr).text).values==accepted_targets&&
          controller.field(F::receive_snr).display_text.empty(),
          "Enter must reveal the complete exact accepted RX list");
    check(controller.field(F::snr).text=="32" && controller.settings().transfer.modem.spreading_factor==tx.spreading_factor &&
          controller.settings().transfer.modem.integration_seconds==tx.integration_seconds,"receive search targets must not change the transmitted profile");
    controller.edit(F::bandwidth,"18 kHz");
    check(controller.settings().transfer.modem.bandwidth_hz==18000 &&
          controller.settings().transfer.receive_targets_db_hz==accepted_targets,
          "bandwidth changes must preserve custom receive targets");
    controller.edit(F::receive_snr,"20,");
    controller.edit(F::snr,"-23");
    const auto fitted_targets=controller.settings().transfer.receive_targets_db_hz;
    const auto fitted_text=controller.field(F::receive_snr).text;
    check(fitted_targets.size()==2&&fitted_targets[1]==55&&
          fitted_targets[0]!=55&&controller.field(F::snr).text=="-23"&&
          tuning::parse_receive_targets(fitted_text).values==fitted_targets,
          "changing short TX SNR must immediately replace pending receive edits with both matching targets");
    std::this_thread::sleep_for(std::chrono::milliseconds(775));controller.poll();
    check(controller.settings().transfer.receive_targets_db_hz==fitted_targets,
          "a pending receive edit must not overwrite the target selected by TX SNR");
    controller.edit(F::snr,"-");
    check(controller.field(F::receive_snr).text==fitted_text &&
          controller.settings().transfer.receive_targets_db_hz==fitted_targets,
          "an incomplete TX SNR edit must preserve the last valid receive target");
    controller.edit(F::snr,"80");
    check(controller.field(F::receive_snr).text=="80, 55" &&
          controller.settings().transfer.receive_targets_db_hz==std::vector<double>({80,55}),
          "correcting TX SNR must restore matching receive targets");
    controller.edit(F::receive_snr,"20,");controller.edit(F::long_snr,"40");
    check(controller.field(F::receive_snr).text=="80, 40" &&
          controller.settings().transfer.receive_targets_db_hz==std::vector<double>({80,40}) &&
          controller.field(F::snr).text=="80",
          "changing long TX SNR must replace pending receive edits and preserve the independent short target");
    controller.edit(F::long_snr,"-");
    check(controller.field(F::receive_snr).text=="80, 40" &&
          controller.settings().transfer.receive_targets_db_hz==std::vector<double>({80,40}),
          "an incomplete long TX target must preserve the last valid receive list");
    controller.edit(F::long_snr,"80");
    check(controller.field(F::receive_snr).text=="80" &&
          controller.settings().transfer.receive_targets_db_hz==std::vector<double>{80},
          "equal short and long targets must share one receive target");
    controller.edit(F::receive_snr,"40, wrong");
    std::this_thread::sleep_for(std::chrono::milliseconds(775));controller.poll();
    check(controller.field(F::receive_snr).text=="32" && controller.settings().transfer.receive_targets_db_hz==std::vector<double>{32},
          "invalid receive target text must reset the complete field to its default");
    controller.edit(F::receive_snr,std::string(513,'1'));
    check(controller.field(F::receive_snr).text=="32","overlong receive target input must reset to the default too");
    const auto& controls=ui::console_screen();
    const auto found=std::find_if(controls.begin(),controls.end(),[](const auto& control){return control.field==F::receive_snr;});
    check(found!=controls.end() && found->persistent && found->kind==ui::Kind::text,
          "receive targets must be an editable persistent text field");
}
void message_target_selection() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    controller.start();
    const auto inspect_value=[&](std::string_view name) {
        const auto& fields=controller.inspection()->fields;
        const auto found=std::find_if(fields.begin(),fields.end(),[&](const auto& field){return field.name==name;});
        check(found!=fields.end(),"Transmission inspection omitted its target or symbol duration");
        return found->value;
    };
    const auto expect=[&](double target,bool stream,bool binary) {
        prepare(controller);
        const auto& inspection=*controller.inspection();
        check(std::stod(inspect_value("TX target C/N0"))==target &&
              inspection.stream_layout.has_value()==stream && inspection.binary==binary,
              "Draft source selected the wrong TX target or changed its existing wire path");
        const auto target_label=std::string(target>=0?"+":"")+std::to_string(static_cast<int>(target))+" dB target";
        check(controller.field(F::simulation_confidence).text.find(target_label)!=std::string::npos,
              "RX confidence must identify the actual draft target, independently of the planner preview");
        auto options=controller.settings().transfer;
        options.modem=tuning::resolve(3600,target,tuning::PatternMode::auto_pattern,false,1500).config;
        const auto expected=transfer::estimate_binary(Bytes(inspection.estimate.wire_bits,0),options);
        check(inspection.estimate.total_seconds==expected.total_seconds &&
              controller.estimate()->coded_seconds==expected.coded_seconds &&
              controller.estimate()->total_seconds==expected.total_seconds,
              "Prepared inspection and airtime did not use the selected target's symbol timing");
        controller.poll();
        check(controller.field(F::diagnostics).text.starts_with(format_bit_rate(modem::bit_rate(options.modem))+" | Shannon-Hartley limit "+
              format_bit_rate(tuning::shannon_capacity_bps(3600,target))+" |"),
              "Draft diagnostics did not follow the selected target and modem geometry");
    };
    expect(32,false,true); // An empty GUI draft previews one raw bit at the short target.
    check(controller.inspection()->preview_only&&controller.estimate()->wire_bits==1&&
          controller.message_bytes().empty()&&!controller.enabled(C::transmit),
          "Empty GUI target selection must keep the editor empty and its one-bit preview unsendable");
    controller.edit(F::message,"e");expect(32,false,false);
    check(controller.estimate()->wire_bits==3,"Short-target selection changed the exact e dictionary code");
    controller.edit(F::message,"quick brown fox ");expect(32,false,false);
    check(controller.message_bytes().size()==16 && controller.estimate()->wire_bits==98,
          "The inclusive 16-byte boundary lost its exact short dictionary endpoint");
    controller.edit(F::message,"quick brown fox!!");expect(55,true,false);
    check(controller.message_bytes().size()==17 && controller.estimate()->wire_bits==1216,
          "The 17-byte boundary changed fixed marker and coded-interval geometry");
    const std::string utf8="\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9";
    controller.edit(F::message,utf8);expect(32,false,false);
    check(controller.message_bytes().size()==16,"UTF-8 target fixture did not contain sixteen source bytes");
    controller.edit(F::message,utf8+"e");expect(55,true,false);
    controller.edit(F::message,"e");controller.edit(F::binary,std::string(128,'0'));
    std::string escaped16;for(unsigned i=0;i<16;++i)escaped16+="\\x00";
    controller.edit(F::message,escaped16);expect(32,false,false);
    check(controller.message_bytes()==Bytes(16,0) && controller.field(F::message).text.size()==64,
          "Escaped source fixture did not distinguish source bytes from visible editor characters");
    controller.edit(F::message,escaped16+"\\x00");expect(55,true,false);
    controller.edit(F::message,"\\x0");controller.poll();
    check(!controller.estimate() && controller.message_bytes()==Bytes(17,0) && controller.settings().long_message_modem &&
          controller.field(F::diagnostics).text.starts_with(format_bit_rate(modem::bit_rate(*controller.settings().long_message_modem))+" |"),
          "An incomplete escaped draft changed the target selected by its last committed source bytes");
    controller.edit(F::message,"e");controller.edit(F::binary,"001");expect(32,false,true);
    check(controller.estimate()->wire_bits==3,"Raw target selection padded leading zeros or partial bytes");
    controller.edit(F::short_bits,std::string(3*(transfer::short_message_bytes+1),'0'));expect(32,false,true);
    check(controller.estimate()->wire_bits==3*(transfer::short_message_bytes+1),
          "Raw target selection classified decoded text size instead of explicit bit input");
    controller.edit(F::snr,"26");controller.edit(F::long_snr,"40");
    controller.edit(F::message,"e");expect(26,false,false);
    const auto short_airtime=controller.estimate()->total_seconds;
    controller.edit(F::long_snr,"55");expect(26,false,false);
    check(controller.estimate()->total_seconds==short_airtime,"Changing the long target altered short-message airtime");
    controller.edit(F::long_snr,"40");controller.edit(F::message,std::string(17,'e'));expect(40,true,false);
    const auto long_airtime=controller.estimate()->total_seconds;
    controller.edit(F::snr,"32");expect(40,true,false);
    check(controller.estimate()->total_seconds==long_airtime,"Changing the short target altered long-message airtime");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(controller.snapshot().samples_received<controller.settings().transfer.modem.sample_rate/5 &&
          std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(controller.snapshot().samples_received>0,"Target-switch fixture did not start continuous reception");
    controller.plot_update();
    auto samples=controller.snapshot().samples_received;
    auto seconds=controller.snapshot().virtual_seconds;
    const auto receive_targets=controller.settings().transfer.receive_targets_db_hz;
    const auto uninterrupted=[&] {
        controller.poll();
        check(controller.snapshot().running && controller.snapshot().samples_received>=samples &&
              controller.snapshot().virtual_seconds>=seconds && !controller.plot_update().clear_waterfall &&
              controller.settings().transfer.receive_targets_db_hz==receive_targets,
              "A draft target transition restarted reception, cleared plots or replaced receive targets");
        samples=controller.snapshot().samples_received;seconds=controller.snapshot().virtual_seconds;
    };
    controller.edit(F::message,"e");expect(32,false,false);uninterrupted();
    controller.edit(F::message,std::string(17,'e'));expect(40,true,false);uninterrupted();
    controller.edit(F::message,"e");controller.edit(F::binary,"001");expect(32,false,true);uninterrupted();
    controller.edit(F::message,"e");
    struct TemporaryAttachment {
        std::filesystem::path path=std::filesystem::temp_directory_path()/
            ("datapump-target-attachment-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryAttachment(){std::error_code ignored;std::filesystem::remove(path,ignored);}
    } fixture;
    write_new_file(fixture.path.string(),Bytes{'e'});
    controller.activate(C::attach_file);const auto requests=controller.take_services();
    check(requests.size()==1,"Target fixture did not request its attachment chooser");
    controller.complete_service({requests.front().id,false,fixture.path.string(),{}});
    expect(40,true,false);uninterrupted();
    check(controller.enabled(C::use_text),"One-byte attachment did not keep its attachment source type");
    controller.activate(C::use_text);expect(32,false,false);uninterrupted();
    check(controller.message_bytes()==Bytes{'e'},"Use text did not restore the preserved short source");
    controller.close();
}
void workspace_controls() {
    Controller controller({true, true});
    const auto workspace=ui::Field::dsp_workspace;
    const auto declaration=std::find_if(ui::console_screen().begin(),ui::console_screen().end(),
        [&](const auto& control) { return control.field==workspace; });
    check(declaration!=ui::console_screen().end()&&declaration->kind==ui::Kind::choice&&declaration->persistent,
          "DSP workspace must be a shared dropdown available on every page");
    check(controller.field(workspace).selected=="ram-50"&&
          controller.field(workspace).display_text.starts_with("50% RAM ("),
          "DSP workspace did not default to half of available RAM with its resolved byte budget");
    const auto initial_revision=controller.revision();
    controller.select(workspace,"ram-25");
    check(controller.revision()>initial_revision&&controller.field(workspace).selected=="ram-25"&&
          controller.field(workspace).display_text.starts_with("25% RAM ("),
          "Selecting the workspace budget did not reconfigure the modem and its airtime estimate");
    check(controller.settings().dsp_workspace_bytes>0&&
          controller.settings().dsp_workspace_bytes==controller.settings().transfer.dsp_workspace_bytes&&
          controller.settings().content_limit==default_memory_limit,
          "Live and estimated DSP budgets differ or changed the independent received-content limit");
    const auto accepted_revision=controller.revision();
    const auto accepted_budget=controller.settings().dsp_workspace_bytes;
    controller.select(workspace,"unknown");
    check(controller.revision()==accepted_revision&&controller.settings().dsp_workspace_bytes==accepted_budget&&
          controller.field(workspace).selected=="ram-25", "Invalid workspace choice changed modem settings");
    controller.select(workspace,"ram-75");
    const auto chosen_budget=controller.settings().dsp_workspace_bytes;
    controller.edit(ui::Field::snr,"6");
    check(controller.field(workspace).selected=="ram-75"&&controller.field(workspace).display_text.starts_with("75% RAM (")&&
          controller.settings().dsp_workspace_bytes==chosen_budget&&chosen_budget==controller.settings().transfer.dsp_workspace_bytes,
          "Changing other modem settings recalculated or discarded the chosen workspace budget");
    controller.close();
    const auto closed_budget=controller.settings().dsp_workspace_bytes;
    controller.select(workspace,"ram-50");
    check(!controller.field(workspace).enabled&&controller.field(workspace).selected=="ram-75"&&
          controller.settings().dsp_workspace_bytes==closed_budget,
          "A stale workspace dropdown callback reconfigured a closing session");
}
void bitmap_source_checks() {
    Controller controller({true, true});
    BitmapSources sources;
    const auto contains = [](const auto& ids, ui::Bitmap id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    const auto& controls = ui::console_screen();
    const auto constellation = std::find_if(controls.begin(), controls.end(),
        [](const auto& control) { return control.bitmap == ui::Bitmap::constellation; });
    const auto patterns = std::find_if(controls.begin(), controls.end(),
        [](const auto& control) { return control.bitmap == ui::Bitmap::pattern_scores; });
    check(constellation != controls.end() && patterns != controls.end() &&
          constellation->page == ui::Page::console && patterns->page == ui::Page::console &&
          constellation->row == patterns->row && constellation->slot == ui::Slot::constellation &&
          patterns->slot == ui::Slot::pattern_scores && patterns->kind == ui::Kind::bitmap,
          "Console did not declare separate live I/Q and pattern evidence plots on the same row");
    const auto first = sources.update(controller);
    check(contains(first, ui::Bitmap::qr), "Bitmap source initialization omitted the default dark QR preview");
    check(contains(first, ui::Bitmap::pattern_scores) &&
          sources.caption(ui::Bitmap::pattern_scores).find("complete pattern windows") != std::string::npos &&
          sources.caption(ui::Bitmap::pattern_scores, 240).find("waiting") != std::string::npos &&
          std::string_view(patterns->help).find("Hardware audio input is paused during transmission") != std::string_view::npos,
          "Console pattern plot did not explain complete-window evidence and paused reception during hardware TX");
    check(patterns->click == ui::Command::clear_pattern_scores &&
          std::string_view(patterns->help).find("Click to clear") != std::string_view::npos &&
          std::string_view(patterns->help).find("older than six seconds") != std::string_view::npos,
          "Pattern evidence must declare its click-to-clear action and six-second retention in the shared GUI");
    check(std::string_view(patterns->help).find("noise remains visible") != std::string_view::npos &&
          std::string_view(patterns->help).find("single-symbol threshold T") != std::string_view::npos &&
          std::string_view(patterns->help).find("shared logarithmic range") != std::string_view::npos &&
          std::string_view(patterns->help).find("twice the log score, not twice the probability") != std::string_view::npos &&
          std::string_view(patterns->help).find("crossing T alone does not guarantee") != std::string_view::npos,
          "Pattern evidence must explain its threshold references without promising bit admission");
    const auto original = sources.get(ui::Bitmap::qr);
    const auto empty = render(original);
    check(empty.pixels()[0] == 32 && empty.pixels()[1] == 0 && empty.pixels()[2] == 0,
          "Named QR source did not start with a dark red background");
    check(sources.update(controller).empty(), "Repeated state update invalidated unchanged bitmap sources");
    controller.edit(ui::Field::message, "Shared named bitmap source");
    check(contains(sources.update(controller), ui::Bitmap::qr), "Message edit did not invalidate the named QR source");
    check(render(sources.get(ui::Bitmap::qr)).pixels() != empty.pixels(), "Message edit did not reach the shared QR producer");
    check(render(original).pixels() == empty.pixels(), "Replacing a named bitmap mutated its retained previous snapshot");
    controller.select(ui::Field::qr_brightness, "normal");
    const auto brightness = sources.update(controller);
    check(brightness == std::vector{ui::Bitmap::qr}, "QR brightness changed an unrelated bitmap source");
    check(render(sources.get(ui::Bitmap::qr)).pixels()[0] == 255, "QR brightness did not update its quiet zone");
    controller.activate(ui::Command::zoom_in);
    const auto zoom = sources.update(controller);
    check(contains(zoom, ui::Bitmap::waveform) && !contains(zoom, ui::Bitmap::waterfall),
          "Waveform zoom appended/replaced waterfall history");
    controller.activate(ui::Command::clear_waterfall);
    check(sources.update(controller) == std::vector{ui::Bitmap::waterfall}, "Clear waterfall did not target only its named source");
    check(sources.update(controller).empty(), "Bitmap mapping consumed one-shot invalidation more than once");
    const auto before_clear_version = sources.version(ui::Bitmap::pattern_scores);
    const auto before_clear_revision = controller.revision();
    const auto before_clear_samples = controller.snapshot().samples_received;
    const auto before_clear_message = controller.message_bytes();
    controller.activate(patterns->click);
    check(sources.update(controller) == std::vector{ui::Bitmap::pattern_scores} &&
          sources.version(ui::Bitmap::pattern_scores) == before_clear_version + 1 &&
          sources.caption(ui::Bitmap::pattern_scores).find("waiting") != std::string::npos,
          "Clicking pattern evidence must clear and invalidate only its named plot, including an empty plot");
    check(sources.update(controller).empty(), "Pattern evidence clear invalidation must be consumed exactly once");
    check(controller.revision() == before_clear_revision &&
          controller.snapshot().samples_received == before_clear_samples && controller.message_bytes() == before_clear_message,
          "Clearing pattern evidence must not reconfigure reception or change the draft");
    controller.edit(ui::Field::message, std::string(501, 'a'));
    sources.update(controller);
    check(sources.caption(ui::Bitmap::qr).find("500") != std::string::npos,
          "Invalid QR message lost its ordinary-label error feedback");
    check(sources.error(ui::Bitmap::qr).find("500") != std::string::npos,
          "Invalid QR message lost its overlay error feedback");
    controller.edit(ui::Field::message, "Valid QR again");
    sources.update(controller);
    check(sources.error(ui::Bitmap::qr).empty(), "Valid QR retained an error over its quiet zone");
    check(std::string(sources.title(ui::Bitmap::waveform)) == "Live waveform" &&
          std::string(sources.title(ui::Bitmap::constellation)) == "Receiver input I/Q" &&
          std::string(sources.title(ui::Bitmap::pattern_scores)) == "Pattern evidence",
          "Shared bitmap titles did not identify the actual measurement source");
    controller.edit(ui::Field::carrier,"2.7 kHz");
    controller.select(ui::Field::pattern, "auto-tone");
    sources.update(controller);
    check(controller.settings().transfer.modem.pattern_symbols &&
          controller.settings().transfer.modem.spreading_mode == modem::SpreadingMode::tone &&
          sources.caption(ui::Bitmap::pattern_scores).find("waiting") != std::string::npos,
          "Tone spreading incorrectly disabled the live pattern evidence plot");
    controller.select(ui::Field::pattern, "auto-pattern");
    sources.update(controller);
    check(controller.settings().transfer.modem.pattern_symbols &&
          controller.settings().transfer.modem.spreading_mode == modem::SpreadingMode::pattern &&
          sources.caption(ui::Bitmap::pattern_scores).find("waiting") != std::string::npos,
          "Pattern spreading did not preserve the live evidence plot");
}
}

int main(int argc,char** argv) {
    try {
        datapump::gui::controller_self_check();
        simulation_estimate_controls();
        empty_composer_preview();
        oscillator_controls();
        lpi_estimate_controls();
        revised_reception_ingestion();
        rate_carrier_controls();
        sub_hertz_controls();
        shannon_capacity_display();
        profile_reference_display();
        mono_controls();
        noise_transmission_controls();
        tone_mode_controls();
        tone_key_controls();
        composer_conveniences();
        repeatable_message_identity();
        previous_message_controls();
        transmit_key_lock_controls();
        repeatable_pending_drafts();
        binary_editor_controls();
        three_bit_dispatch();
        short_raw_editor();short_raw_reception();
        receive_target_controls();message_target_selection();short_text_reception();three_bit_text_reception();fixed_text_reception();byte_aligned_pattern_reception();
        escaped_signal_message_paste();received_raw_text_boundary();binary_source_representation();workspace_controls();
        bitmap_source_checks();
        if(argc>1&&std::string_view(argv[1])=="--smoke") {
            datapump::gui::Controller controller({true,true});
            datapump::gui::Smoke smoke({},300); // Match the native workflow budget.
            datapump::gui::BitmapSources bitmaps;
            controller.start();
            while(!smoke.done()) {
                controller.poll(); bitmaps.update(controller); smoke.step(controller, &bitmaps);
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            controller.close();
            while(!controller.ready_to_close()) {
                controller.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        std::cout<<"Shared GUI controller checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

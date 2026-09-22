#include "gui_smoke.hpp"
#include "bitmap_sources.hpp"
#include "datapump/runtime.hpp"
#include "datapump/received_text.hpp"
#include <cmath>
#include <fstream>
#include <set>
#include <tuple>
#include <thread>

namespace datapump::gui {
namespace {
using F=ui::Field;
using C=ui::Command;
using B=ui::Bitmap;
using Clock=std::chrono::steady_clock;
const std::string message=
    "CQ CQ - continuous reception\nClipboard caf\xc3\xa9 \xf0\x9f\x8c\x8d verified.\n"
    "This message passes through the noisy symbol receiver while the waterfall keeps scrolling. "
    "Text first appears as pending, then becomes available to copy after the complete stream "
    "has passed error correction and integrity checks.";
const std::string interrupted_message="Replace this pending replay. "+message;
const std::string cancelled_message="Cancel this pending replay. "+message;
const Bytes file_bytes{0,1,2,3,0xff,0xc0,0x80,'D','a','t','a','P','u','m','p','\n'};
void require(bool condition,const char* message_) { if(!condition) throw Error(message_); }
std::string path_text(const std::filesystem::path& path) { const auto s=path.u8string(); return {s.begin(),s.end()}; }
std::string inspection_field(const Inspection& model,std::string_view name) {
    for(const auto& field:model.fields) if(field.name==name) return field.value;
    return {};
}
std::vector<unsigned char> preview_pixels(const plots::PlotSnapshot& source) {
    BitmapImage pixels(96,64);
    source.paint(full_bitmap_request(96,64,false,true),[&](unsigned x,unsigned y,PixelBlock block){pixels.blit(x,y,block);});
    return pixels.pixels();
}
}
struct Smoke::Impl {
    enum class Phase {
        initialize,fec20,fec60,generate,generated,reloaded,key_failed,noise_ready,noise_active,noise_stopped,text_ready,text_received,
        file_ready,file_received,interrupt_ready,interrupt_replay,replacement_ready,
        replacement_replay,cancelled,binary_attachment,binary_ready,binary_received,tiny,long_text
    };
    struct Replay {
        bool active=false,binary=false,pending_required=false,saw_symbols=false,resumed=false;
        std::uint64_t id=0,pending_poll=0,waterfall_version=0,waveform_version=0,constellation_version=0;
        std::size_t frame=0,frame_count=0,frames=0,waveform_changes=0;
        double fraction=0;
        Clock::time_point started;
        std::vector<float> waveform;
        std::vector<std::complex<double>> constellation;
    } replay;
    std::filesystem::path directory,input_path,save_path,key_path;
    double timeout;
    Clock::time_point started=Clock::now(),cancelled_at;
    Phase phase=Phase::initialize;
    bool done=false,launched_binary=false,saw_idle_change=false,key_reception=false,owns_directory=false;
    std::uint64_t polls=0,completed_replay=0,key_samples=0,cancel_samples=0;
    std::uint64_t noise_revision=0;
    std::shared_ptr<const Inspection> noise_inspection;
    std::vector<float> idle_waveform;
    std::set<std::uint64_t> interrupted;
    std::set<std::string> verified_ids;
    Bytes first_key_mac;
    std::string first_key_id;
    std::uintmax_t key_size=0;
    explicit Impl(std::filesystem::path path,double seconds):directory(std::move(path)),timeout(seconds) {
        if(directory.empty()) {
            directory=std::filesystem::temp_directory_path()/("datapump-shared-smoke-"+std::to_string(started.time_since_epoch().count()));
            owns_directory=std::filesystem::create_directory(directory);
            if(!owns_directory)throw Error("GUI smoke temporary directory already exists");
        } else std::filesystem::create_directories(directory);
        const auto suffix=std::to_string(started.time_since_epoch().count());
        input_path=directory/("attachment-"+suffix+".bin"); save_path=directory/("received-"+suffix+".bin");
        const auto key_name="generated keys caf\xc3\xa9-"+suffix+".key";
        key_path=directory/std::filesystem::path(std::u8string(key_name.begin(),key_name.end()));
    }
    ~Impl() {
        // A successful regression run creates a large key fixture. Retain
        // explicitly requested output and failed-run evidence, but do not fill
        // the host's temporary filesystem on repeated backend conformance runs.
        if(owns_directory&&done) {std::error_code ignored;std::filesystem::remove_all(directory,ignored);}
    }
    ui::ServiceRequest take(Controller& controller,ui::ServiceKind kind) {
        auto requests=controller.take_services();
        require(requests.size()==1&&requests.front().kind==kind,"Smoke received an unexpected platform service request");
        return std::move(requests.front());
    }
    void respond(Controller& controller,ui::ServiceKind kind,const std::string& value) {
        const auto request=take(controller,kind);controller.complete_service({request.id,false,value,{}});
    }
    void generate(Controller& controller,const std::string& names) {
        controller.activate(C::generate_keyfile);respond(controller,ui::ServiceKind::prompt,names);
        respond(controller,ui::ServiceKind::save_file,path_text(key_path));
    }
    void attach(Controller& controller) {
        controller.activate(C::attach_file);respond(controller,ui::ServiceKind::open_file,path_text(input_path));
    }
    void transmit(Controller& controller) {
        require(controller.enabled(C::transmit),"Smoke attempted a transmission before its preparation finished");
        launched_binary=controller.inspection()&&controller.inspection()->binary;
        controller.activate(C::transmit);
        require(!controller.enabled(C::transmit)&&controller.enabled(C::cancel),"Transmission did not immediately claim its single active slot");
    }
    ui::ServiceRequest copy(Controller& controller,std::string_view expected) {
        require(controller.enabled(C::copy_signal),"Completed signal did not enable its clipboard action");
        controller.activate(C::copy_signal);const auto request=take(controller,ui::ServiceKind::clipboard);
        require(request.value==received_text(expected),"Shared GUI clipboard did not restrict received text");
        return request;
    }
    void check_fec(Controller& controller,FecMode mode) {
        const auto& model=controller.inspection();
        require(model&&model->stream_layout&&!model->binary,"Stream inspection was unavailable after preparation");
        const auto& layout=*model->stream_layout;
        require(layout.fec==mode,"Stream inspection did not follow the selected effective FEC");
        require((layout.parity_bytes_per_interval>0)==(mode!=FecMode::off),"FEC inspection disagrees with fixed interval parity");
        require(model->lanes.size()==2&&!model->sections.empty()&&
                (controller.settings().transfer.modem.pattern_symbols?model->pattern_space.has_value():model->constellations.size()==2),
                "Stream inspection lost its flow, structure or pattern alphabet");
        bool unavailable=false;
        for(const auto& lane:model->lanes)for(const auto& step:lane.steps)
            unavailable=unavailable||step.state==InspectionState::unavailable;
        if(!controller.settings().transfer.modem.pattern_symbols)require(unavailable,"Inspection falsely presented unimplemented receiver stages as available");
    }
    void inspect_replay(Controller& controller,const BitmapSources* bitmaps) {
        const auto& snapshot=controller.snapshot();
        if(snapshot.simulation_replay) {
            require(snapshot.transmission_id&&snapshot.replay_frame_count>=2&&
                    snapshot.replay_frame_index<snapshot.replay_frame_count&&
                    std::isfinite(snapshot.simulation_sample_fraction)&&snapshot.simulation_sample_fraction>=0&&
                    snapshot.simulation_sample_fraction<=1,"Simulation replay did not identify a chronological frame");
            require(controller.field(F::mode).text.starts_with("Simulation replay "),"Replay mode label did not identify displayed frames");
            if(bitmaps)require(std::string_view(bitmaps->title(B::waveform))=="Simulation replay / waveform"&&
                    std::string_view(bitmaps->title(B::waterfall))=="Replay spectrum","Shared bitmap titles lost their replay source");
            const bool beginning=!replay.active||replay.id!=snapshot.transmission_id;
            if(beginning) {
                require(!replay.active||interrupted.contains(replay.id),"An active replay was replaced without an explicit new transmission");
                replay={};replay.active=true;replay.id=snapshot.transmission_id;replay.binary=launched_binary;replay.pending_required=!launched_binary;replay.started=Clock::now();
                replay.frame_count=snapshot.replay_frame_count;
            } else require(snapshot.replay_frame_index>=replay.frame&&snapshot.simulation_sample_fraction>=replay.fraction,
                           "Simulation replay moved backwards in transmission time");
            require(snapshot.replay_frame_count==replay.frame_count,"Simulation replay changed its retained frame count");
            for(const auto& signal:snapshot.signals) {
                require(!signal.validated&&!signal.complete&&signal.binary,"Replay delivered completed source content before its physical end");
                const auto& lines=controller.signals().lines();
                const auto found=std::find_if(lines.begin(),lines.end(),[&](const auto& line){return line.id==signal.id;});
                require(found!=lines.end()&&!found->validated&&!found->complete,"Pending replay signal was not presented");
                const auto index=static_cast<std::size_t>(found-lines.begin());
                require(!controller.signals().copy_id(index)&&!controller.signals().copy_bits(index),"Pending reception became copyable before completion");
                require(!found->pre_fec_accuracy&&signal_status_label(*found)=="binary pending",
                        "Pending physical observations claimed completed content or measured FEC accuracy");
                require(found->expected_bits==0&&!found->preamble_received_percent&&found->pattern_score,
                        "Pending physical observations acquired a transmitted length or lost their pattern evidence");
                if(!replay.pending_poll)replay.pending_poll=polls;
            }
            const auto source=snapshot.constellation_source;
            require(source!=live::ConstellationSource::transmitted,"Simulation showed synthetic transmitted symbols as receiver measurements");
            if(bitmaps)require(std::string_view(bitmaps->title(B::constellation))==(source==live::ConstellationSource::received?
                    "Received constellation":"Receiver input I/Q"),"Constellation title did not identify the actual receiver source");
            replay.saw_symbols=replay.saw_symbols||(source==live::ConstellationSource::received&&!snapshot.constellation.empty());
            if(beginning||snapshot.replay_frame_index!=replay.frame) {
                if(bitmaps)require(preview_pixels(bitmaps->get(B::constellation))==preview_pixels(plots::PlotSnapshot::constellation(
                        snapshot.constellation,source!=live::ConstellationSource::input))&&
                        preview_pixels(bitmaps->get(B::waveform))==preview_pixels(plots::PlotSnapshot::waveform(
                        snapshot.waveform,controller.settings().transfer.modem,controller.waveform_zoom())),
                        "Shared replay bitmaps retained measurements from a different frame");
                if(!beginning) {
                    if(bitmaps)require(bitmaps->version(B::waterfall)>replay.waterfall_version&&
                            bitmaps->version(B::waveform)>replay.waveform_version&&bitmaps->version(B::constellation)>replay.constellation_version,
                            "A new replay frame did not refresh the shared plots and waterfall");
                    if(snapshot.waveform!=replay.waveform)++replay.waveform_changes;
                }
                replay.frame=snapshot.replay_frame_index;replay.fraction=snapshot.simulation_sample_fraction;
                replay.waveform=snapshot.waveform;replay.constellation=snapshot.constellation;++replay.frames;
                if(bitmaps){replay.waterfall_version=bitmaps->version(B::waterfall);replay.waveform_version=bitmaps->version(B::waveform);replay.constellation_version=bitmaps->version(B::constellation);}
            } else {
                require(snapshot.waveform==replay.waveform&&snapshot.constellation==replay.constellation,"Repeated replay polls changed an unchanged measurement frame");
                if(bitmaps)require(bitmaps->version(B::waterfall)==replay.waterfall_version&&bitmaps->version(B::waveform)==replay.waveform_version&&
                        bitmaps->version(B::constellation)==replay.constellation_version,"Repeated replay polls duplicated plot updates or waterfall rows");
            }
            return;
        }
        if(replay.active) {
            replay.active=false;
            if(!interrupted.contains(replay.id)) {
                const auto elapsed=std::chrono::duration<double>(Clock::now()-replay.started).count();
                if(replay.binary && !controller.settings().transfer.modem.pattern_symbols)require(!replay.saw_symbols&&!replay.pending_poll&&snapshot.signals.empty()&&snapshot.received.empty(),
                        "Unsynchronized raw replay fabricated symbol lock or received bits");
                const auto replay_diagnostics=std::string("Replay did not show changing measured frames and pending reception over about three seconds")+
                        ": elapsed="+std::to_string(elapsed)+" frames="+std::to_string(replay.frames)+
                        " changes="+std::to_string(replay.waveform_changes)+" fraction="+std::to_string(replay.fraction)+
                        " symbols="+std::to_string(replay.saw_symbols)+" dropped="+std::to_string(snapshot.constellation_dropped)+
                        " pending="+std::to_string(replay.pending_poll);
                require(elapsed>=2.4&&elapsed<=8&&replay.frames>=(replay.binary?2U:10U)&&
                        replay.waveform_changes>=(replay.binary?1U:5U)&&replay.fraction>=.9&&
                        (controller.settings().transfer.modem.pattern_symbols||replay.binary||(replay.saw_symbols&&(!replay.pending_required||replay.pending_poll))),
                        replay_diagnostics.c_str());
                completed_replay=replay.id;
            }
        }
        if(replay.id&&snapshot.transmission_id==replay.id&&!snapshot.transmitting&&snapshot.constellation_source==live::ConstellationSource::input&&
           snapshot.waveform!=replay.waveform&&(!bitmaps||bitmaps->version(B::waterfall)>replay.waterfall_version)) {
            if(bitmaps)require(std::string_view(bitmaps->title(B::waveform))=="Live waveform"&&
                    std::string_view(bitmaps->title(B::constellation))=="Receiver input I/Q","Replay completion did not restore live bitmap titles");
            replay.resumed=true;
        }
    }
    void inspect_streams(Controller& controller) {
        for(const auto& stream:controller.inbox().items()) {
            const auto id=id_label(stream.message);
            if(verified_ids.contains(id))continue;
            const auto text=std::string(stream.message.data.begin(),stream.message.data.end());
            require(text!=interrupted_message&&text!=cancelled_message,"Replaced or cancelled replay delivered a late verified stream");
            require(!controller.snapshot().transmitting&&!controller.snapshot().simulation_replay&&
                    completed_replay==controller.snapshot().transmission_id&&
                    (!replay.pending_required||(replay.pending_poll&&replay.pending_poll<polls)),
                    "Verified stream bypassed an earlier pending replay poll");
            require(text==message||text=="Help"||stream.message.data==file_bytes,"Smoke received unexpected stream content");
            verified_ids.insert(id);
        }
    }
    void inspect_records(Controller& controller) {
        const auto& records=controller.field(F::signals).records;
        require(records.size()==controller.signals().lines().size(),"Signal collection did not present every retained reception");
        for(const auto& signal:controller.signals().lines()) {
            const auto row=std::find_if(records.begin(),records.end(),[&](const auto& value){return value.id==std::to_string(signal.id);});
            require(row!=records.end(),"Signal collection lost its stable reception identity");
            const auto has=[&](const std::string& text){return std::any_of(row->cells.begin(),row->cells.end(),[&](const auto& cell){return cell.text==text;});};
            // Received rows preserve permitted LF bytes; compare the complete
            // text without the filename/label helper's whitespace flattening.
            require(has(std::to_string(static_cast<long long>(std::llround(signal.frequency_hz)))+" Hz")&&
                    has(signal_status_label(signal))&&has(signal_preamble_label(signal))&&has(signal_data_label(signal))&&has(signal_display_text(signal)),
                    "Signal record omitted frequency, status, preamble, data accuracy or complete received text");
            require(row->activatable==(signal.binary?signal.complete:(signal.validated||signal.complete&&signal.pattern_score)&&signal.text_message),
                    "Signal record activation disagreed with verified-text or completed-raw clipboard eligibility");
        }
    }
    void step(Controller& controller,const BitmapSources* bitmaps) {
        if(done)return;
        if(std::chrono::duration<double>(Clock::now()-started).count()>timeout)
            throw Error("Shared GUI smoke timed out in phase "+std::to_string(static_cast<int>(phase))+": "+controller.field(F::status).text);
        ++polls;
        require(controller.settings().simulation,"Shared GUI smoke attempted hardware audio");
        const auto& snapshot=controller.snapshot();
        inspect_replay(controller,bitmaps);inspect_streams(controller);inspect_records(controller);
        if(!snapshot.waveform.empty()) {
            if(!idle_waveform.empty()&&idle_waveform!=snapshot.waveform)saw_idle_change=true;
            idle_waveform=snapshot.waveform;
        }
        if(phase==Phase::generated&&!controller.field(F::key).enabled&&snapshot.samples_received>key_samples)key_reception=true;
        switch(phase) {
        case Phase::initialize: {
            controller.edit(F::message,"Discarded draft");controller.edit(F::message,message);
            const auto revision=controller.revision();
            controller.select(F::qr_brightness,"dim");controller.activate(C::zoom_in);controller.activate(C::reset_zoom);
            controller.select(F::send_key,"ctrl-enter");controller.select(F::send_key,"enter");
            require(controller.revision()==revision,"Display/input-preference actions changed modem settings");
            phase=Phase::fec60;break;
        }
        case Phase::fec20:
            if(!controller.estimate()||!saw_idle_change||!snapshot.samples_received)break;
            check_fec(controller,FecMode::rs20);
            require(controller.field(F::fec).enabled&&controller.field(F::fec).display_text.empty(),"Long text did not expose its retained FEC selection");
            phase=Phase::generate;break;
        case Phase::fec60:
            if(!controller.estimate())break;
            check_fec(controller,FecMode::rs60);controller.select(F::fec,"rs20");phase=Phase::fec20;break;
        case Phase::generate:
            if(!controller.estimate())break;
            key_samples=snapshot.samples_received;generate(controller,"A|B, None");
            require(!controller.enabled(C::transmit)&&!controller.field(F::key).enabled,"Pending key generation did not gate transmission/key selection");
            phase=Phase::generated;break;
        case Phase::generated: {
            if(!controller.field(F::key).enabled||!controller.estimate())break;
            const auto& keys=controller.field(F::key);
            require(keys.options.size()==3&&keys.options[1].label=="1. A|B"&&keys.options[2].label=="2. None"&&
                    keys.selected=="key:A|B"&&controller.settings().transfer.key&&controller.settings().receive_keys.size()==2,
                    "Production key generation did not preserve literal names and select its first key");
            require(key_reception,"Production keyfile generation stopped continuous reception");
            key_size=std::filesystem::file_size(key_path);
            require(key_size>keyfile_header_bytes&&controller.enabled(C::show_key_folder),"Generated keyfile was not the production format or lost its folder action");
            first_key_id=keys.selected;first_key_mac=controller.settings().transfer.key->mac(file_bytes);
            controller.select(F::key,"key:None");
            require(controller.settings().transfer.key&&controller.settings().transfer.key->mac(file_bytes)!=first_key_mac,"Named None selected plaintext or the wrong key identity");
            controller.select(F::key,first_key_id);
            require(controller.settings().transfer.key->mac(file_bytes)==first_key_mac,"Key selection did not restore its original key identity");
            generate(controller,"Replacement");
            require(controller.field(F::key).enabled&&std::filesystem::file_size(key_path)==key_size&&
                    controller.settings().transfer.key->mac(file_bytes)==first_key_mac&&
                    controller.field(F::status).text.find("exist")!=std::string::npos,"Generating over an existing keyfile was not safely refused");
            controller.activate(C::show_key_folder);const auto folder=take(controller,ui::ServiceKind::open_folder);
            require(folder.value==folder_uri(std::filesystem::absolute(key_path).parent_path()),"Key folder service did not preserve its native path");
            controller.complete_service({folder.id,false,{},{}});
            controller.select(F::key,"none");controller.activate(C::open_keyfile);
            respond(controller,ui::ServiceKind::open_file,path_text(key_path));phase=Phase::reloaded;break;
        }
        case Phase::reloaded:
            if(!controller.field(F::key).enabled||!controller.estimate())break;
            require(controller.field(F::key).selected==first_key_id&&controller.settings().transfer.key&&
                    controller.settings().transfer.key->mac(file_bytes)==first_key_mac,"Loading a production keyfile did not restore and auto-select its first key");
            controller.activate(C::open_keyfile);respond(controller,ui::ServiceKind::open_file,path_text(directory/"missing.key"));
            phase=Phase::key_failed;break;
        case Phase::key_failed:
            if(!controller.enabled(C::acknowledge_key_failure))break;
            require(!controller.enabled(C::transmit)&&controller.field(F::key).selected==first_key_id&&
                    controller.settings().receive_keys.size()==2&&controller.settings().transfer.key->mac(file_bytes)==first_key_mac,
                    "Failed key load discarded the working keyring or permitted unacknowledged transmission");
            controller.select(F::key,first_key_id);
            require(controller.enabled(C::acknowledge_key_failure),"Unchanged key selection silently acknowledged a failed load");
            controller.activate(C::acknowledge_key_failure);
            require(!controller.enabled(C::acknowledge_key_failure)&&controller.field(F::key).selected==first_key_id,
                    "Key failure acknowledgement changed the retained selected key");
            phase=Phase::noise_ready;break;
        case Phase::noise_ready:
            if(!controller.estimate())break;
            require(controller.enabled(C::transmit_noise)&&controller.field(F::message).text==message,
                    "Tuning noise was unavailable or changed the prepared message before starting");
            noise_revision=controller.revision();noise_inspection=controller.inspection();
            controller.activate(C::transmit_noise);
            require(controller.enabled(C::cancel)&&!controller.enabled(C::transmit_noise)&&
                    !controller.enabled(C::transmit)&&controller.command_label(C::cancel)=="Stop noise",
                    "Noise did not immediately expose Stop noise and claim the single transmitter");
            phase=Phase::noise_active;break;
        case Phase::noise_active:
            if(!snapshot.transmitting_noise||snapshot.transmission_seconds<.1)break;
            require(snapshot.transmitting&&!snapshot.simulation_replay&&snapshot.transmission_fraction==0&&
                    controller.field(F::mode).text.starts_with("Simulating noise / ")&&
                    controller.field(F::mode).text.find("elapsed")!=std::string::npos&&
                    controller.command_label(C::cancel)=="Stop noise",
                    "Noise did not present continuous elapsed generation and its stop action");
            controller.activate(C::cancel);phase=Phase::noise_stopped;break;
        case Phase::noise_stopped:
            if(snapshot.transmitting||!snapshot.transmission_finished)break;
            require(!snapshot.transmitting_noise&&!snapshot.simulation_replay&&
                    controller.enabled(C::transmit_noise)&&!controller.enabled(C::cancel)&&
                    controller.command_label(C::cancel)=="Cancel TX",
                    "Stop noise failed to return to continuous reception without replay");
            require(controller.field(F::message).text==message&&controller.revision()==noise_revision&&
                    controller.inspection()==noise_inspection&&controller.field(F::key).selected==first_key_id&&
                    controller.settings().transfer.key&&controller.settings().transfer.key->mac(file_bytes)==first_key_mac&&
                    controller.settings().receive_keys.size()==2&&controller.inbox().items().empty()&&
                    controller.signals().lines().empty()&&!controller.enabled(C::paste_previous),
                    "Noise changed the draft, prepared inspection, saved keys or message history");
            noise_inspection.reset();controller.select(F::key,"none");phase=Phase::text_ready;break;
        case Phase::text_ready:
            if(!controller.enabled(C::transmit))break;
            require(!controller.settings().transfer.key,"Plaintext smoke retained an encryption key");
            transmit(controller);phase=Phase::text_received;break;
        case Phase::text_received: {
            if(verified_ids.size()!=1)break;
            require(controller.inbox().file_items().empty(),"Ordinary text appeared as a received attachment");
            bool copied=false;
            for(const auto& line:controller.signals().lines())if(line.validated) {
                require((line.pattern_score||line.preamble_received_percent)&&line.pre_fec_accuracy&&line.pre_fec_accuracy->received_data_bits,
                        "Verified text lost its measured preamble and pre-FEC diagnostics");
                controller.select(F::signals,std::to_string(line.id));const auto request=copy(controller,message);
                controller.complete_service({request.id,false,{},{}});copied=true;break;
            }
            require(copied,"Verified text was not selectable through its signal record");
            write_new_file(path_text(input_path),file_bytes);attach(controller);phase=Phase::file_ready;break;
        }
        case Phase::file_ready:
            if(!controller.enabled(C::transmit)||!controller.field(F::message_label).text.starts_with("Attached:"))break;
            require(!controller.field(F::message).enabled,"Attached file did not make the inactive text draft read-only");
            transmit(controller);phase=Phase::file_received;break;
        case Phase::file_received: {
            if(verified_ids.size()!=2) {
                if(!snapshot.transmitting&&!snapshot.simulation_replay&&completed_replay==snapshot.transmission_id&&replay.resumed) {
                    std::string detail="Completed file replay did not validate; fixture="+path_text(directory)+
                        "; expected bits="+(controller.inspection()?inspection_field(*controller.inspection(),"Meaningful bits"):"none");
                    for(const auto& line:controller.signals().lines())detail+=" [id="+std::to_string(line.id)+
                        " status="+signal_status_label(line)+" bits="+std::to_string(line.received_bits)+
                        " score="+(line.pattern_score?std::to_string(*line.pattern_score):"none")+
                        " text="+line.text.substr(0,64)+(line.text.size()>64?"..."+line.text.substr(line.text.size()-32):"")+"]";
                    throw Error(detail);
                }
                break;
            }
            const auto files=controller.inbox().file_items();
            require(files.size()==1&&files.back()->message.data==file_bytes&&files.back()->message.filename==path_text(input_path.filename()),"Received file list did not retain the exact binary attachment");
            const auto id=id_label(files.back()->message);
            bool measured_file=false;
            for(const auto& line:controller.signals().lines())if(line.reception_id==id) {
                require((line.pattern_score||line.preamble_received_percent)&&line.pre_fec_accuracy&&files.back()->pre_fec_accuracy&&
                        line.pre_fec_accuracy->received_data_bits==files.back()->pre_fec_accuracy->received_data_bits&&
                        line.pre_fec_accuracy->corrected_data_bits==files.back()->pre_fec_accuracy->corrected_data_bits&&
                        line.pre_fec_accuracy->received_data_bits,"Received file signal lost its validated stream's measured accuracy");
                controller.select(F::signals,std::to_string(line.id));require(!controller.enabled(C::copy_signal),"Binary file was coerced to clipboard text");
                measured_file=true;
            }
            require(measured_file,"Received file had no measured signal record");
            controller.select(F::files,id);controller.activate(C::save_file);const auto save=take(controller,ui::ServiceKind::save_file);
            controller.activate(C::save_file);const auto second_save=take(controller,ui::ServiceKind::save_file);
            ui::ServiceRequest retained_copy;
            for(const auto& line:controller.signals().lines())if(line.validated&&line.text_message) {
                controller.select(F::signals,std::to_string(line.id));retained_copy=copy(controller,message);break;
            }
            require(retained_copy.id!=0,"Text copy request could not be retained alongside a pending file save");
            controller.activate(C::clear_received);
            require(controller.inbox().items().empty()&&controller.signals().lines().empty()&&controller.field(F::files).records.empty()&&
                    controller.field(F::signals).records.empty()&&!controller.enabled(C::save_file)&&!controller.enabled(C::copy_signal),
                    "Clearing received content left stale collection records or actions");
            require(retained_copy.value==received_text(message),"Clearing reception invalidated a safe queued clipboard payload");
            controller.complete_service({retained_copy.id,false,{},{}});
            controller.complete_service({save.id,false,path_text(save_path),{}});
            {std::ifstream input(save_path,std::ios::binary);require(input&&read_bounded(input,1024)==file_bytes,"Clearing the inbox invalidated a pending save payload");}
            controller.complete_service({second_save.id,false,path_text(save_path),{}});
            require(controller.field(F::status).text.find("may already exist")!=std::string::npos,"Repeated save silently overwrote an existing file");
            {std::ifstream input(save_path,std::ios::binary);require(input&&read_bounded(input,1024)==file_bytes,"Refused save changed the existing output file");}
            controller.activate(C::use_text);controller.edit(F::message,interrupted_message);phase=Phase::interrupt_ready;break;
        }
        case Phase::interrupt_ready:
            if(!controller.enabled(C::transmit)||!replay.resumed)break;
            transmit(controller);phase=Phase::interrupt_replay;break;
        case Phase::interrupt_replay:
            if(!snapshot.simulation_replay)break;
            controller.edit(F::message,cancelled_message);phase=Phase::replacement_ready;break;
        case Phase::replacement_ready:
            require(snapshot.simulation_replay,"Replacement preparation missed the active replay");
            if(!controller.enabled(C::transmit)||(replay.pending_required&&(!replay.pending_poll||replay.pending_poll>=polls))||replay.frames<3)break;
            interrupted.insert(replay.id);transmit(controller);phase=Phase::replacement_replay;break;
        case Phase::replacement_replay:
            if(!snapshot.simulation_replay||interrupted.contains(replay.id)||(replay.pending_required&&(!replay.pending_poll||replay.pending_poll>=polls))||replay.frames<3)break;
            require(controller.command_label(C::cancel)=="Stop replay","Cancel action did not describe stopping the replay");
            interrupted.insert(replay.id);controller.activate(C::cancel);cancelled_at=Clock::now();cancel_samples=snapshot.samples_received;
            phase=Phase::cancelled;break;
        case Phase::cancelled:
            require(controller.inbox().items().empty()&&verified_ids.size()==2,"Interrupted replay added a received stream");
            if(Clock::now()-cancelled_at>std::chrono::seconds(2))require(replay.resumed&&snapshot.samples_received>cancel_samples,
                    "Stopping replay did not promptly resume live receiver samples and plots");
            if(Clock::now()-cancelled_at<std::chrono::milliseconds(3250)||!replay.resumed)break;
            controller.edit(F::message,"help");controller.toggle(F::repeatable,true);
            require(controller.field(F::repeatable).checked,"Smoke could not establish a retained repeatable stream draft");
            attach(controller);phase=Phase::binary_attachment;break;
        case Phase::binary_attachment:
            if(!controller.field(F::message_label).text.starts_with("Attached:")||!controller.estimate())break;
            require(!controller.field(F::binary).enabled,"Attachment left its inactive binary draft editable");
            controller.activate(C::use_text);controller.toggle(F::repeatable,false);
            controller.edit(F::binary,"001x");
            require(!controller.enabled(C::transmit)&&!controller.estimate()&&controller.field(F::message).text=="help",
                    "Invalid binary draft changed committed bytes or retained a usable estimate");
            controller.edit(F::binary,"01001000 01100101\n01101100 01110000");controller.select(F::key,first_key_id);
            require(controller.field(F::message).text=="Help","Binary edit did not update the message");
            phase=Phase::binary_ready;break;
        case Phase::binary_ready: {
            if(!controller.enabled(C::transmit))break;
            const auto& model=controller.inspection();
            require(controller.settings().transfer.key&&controller.settings().transfer.key->mac(file_bytes)==first_key_mac,
                    "Binary-edited message did not retain the selected production key");
            require(model&&model->binary&&!model->stream_layout&&inspection_field(*model,"Meaningful bits")=="32",
                    "Binary edit did not prepare its exact raw bit count without a stream");
            require(controller.field(F::message).enabled&&controller.field(F::binary).enabled&&
                    controller.field(F::callsign).enabled&&controller.field(F::grid).enabled&&
                    controller.field(F::fec).selected=="rs20"&&controller.field(F::fec).display_text=="Off (raw bits)",
                    "Synchronized editors changed stream settings or disabled the other editor");
            transmit(controller);phase=Phase::binary_received;break;
        }
        case Phase::binary_received:
            if(snapshot.transmitting||snapshot.simulation_replay||completed_replay!=snapshot.transmission_id)break;
            require(controller.inbox().items().empty()&&verified_ids.size()==2,"Raw binary reception acquired a stream identity");
            { bool recovered=false;for(std::size_t i=0;i<controller.signals().lines().size();++i)
                recovered=recovered||(controller.signals().copy_text(i)=="Help"&&controller.signals().copy_bytes(i)==Bytes({'H','e','l','p'})&&!controller.signals().copy_bits(i));
              if(!recovered) {
                  std::string detail="Binary-edited bytes did not arrive as exact copyable text; error="+snapshot.error+
                      "; transmission="+std::to_string(snapshot.transmission_id)+"; fixture="+path_text(directory);
                  for(std::size_t i=0;i<controller.signals().lines().size();++i) {
                      const auto& line=controller.signals().lines()[i];
                      detail+=" [id="+std::to_string(line.id)+" status="+signal_status_label(line)+" bits="+
                          std::to_string(line.received_bits)+" expected="+std::to_string(line.expected_bits)+
                          " score="+(line.pattern_score?std::to_string(*line.pattern_score):"none")+
                          " copy="+controller.signals().copy_text(i).value_or("unavailable")+" text="+signal_display_text(line)+"]";
                  }
                  throw Error(detail);
              } }
            // Observe live plot resumption before reconfiguration clears the
            // transmission ID that identifies this completed replay.
            if(!replay.resumed)break;
            controller.select(F::key,"none");controller.edit(F::message,"e");phase=Phase::tiny;break;
        case Phase::tiny:
            if(!controller.estimate())break;
            require(!controller.inspection()->stream_layout&&controller.estimate()->wire_bits==3&&
                    controller.field(F::fec).selected=="rs20"&&!controller.field(F::fec).enabled&&
                    controller.field(F::fec).display_text=="Off (short dictionary)",
                    "Short text did not bypass fixed-interval coding while retaining the FEC selection");
            controller.edit(F::message,message);phase=Phase::long_text;break;
        case Phase::long_text:
            if(!controller.estimate()||!replay.resumed)break;
            check_fec(controller,FecMode::rs20);
            require(inspection_field(*controller.inspection(),"Compression").starts_with("Raw LZMA2")&&
                    controller.field(F::fec).enabled&&controller.field(F::fec).display_text.empty(),
                    "Returning to long text failed to restore compression and retained FEC presentation");
            require(verified_ids.size()==2&&interrupted.size()==2,"Smoke did not complete stream, raw-bit, replacement and cancellation workflows");
            done=true;break;
        }
    }
};
Smoke::Smoke(std::filesystem::path directory,double timeout):impl_(std::make_unique<Impl>(std::move(directory),timeout)) {}
Smoke::~Smoke()=default;
void Smoke::step(Controller& controller,const BitmapSources* bitmaps) {
    try { impl_->step(controller,bitmaps); }
    catch(const std::exception& error) {
        throw Error("Shared GUI smoke phase "+std::to_string(static_cast<int>(impl_->phase))+": "+error.what());
    }
}
bool Smoke::done() const { return impl_->done; }
void controller_self_check() {
    Controller controller({true,true});
    require(controller.field(F::qr_brightness).selected=="dark","QR brightness must start Dark");
    require(controller.field(F::binary).enabled&&controller.field(F::message).enabled,"Both synchronized editors must be available");
    controller.edit(F::message,"Preserve this message");
    const auto revision=controller.revision();
    controller.edit(F::message,"Preserve this message");
    controller.select(F::qr_brightness,"normal");
    controller.activate(C::zoom_in); controller.activate(C::reset_zoom);
    require(controller.revision()==revision,"Unchanged input or display actions changed application revision");
    const auto settings=controller.settings().transfer.modem;
    controller.edit(F::bandwidth,"invalid draft");
    require(controller.field(F::bandwidth).text=="invalid draft","Invalid draft was silently replaced");
    require(controller.settings().transfer.modem.bandwidth_hz==settings.bandwidth_hz,"Invalid draft replaced the last valid modem settings");
    require(!controller.enabled(C::transmit),"Invalid settings enabled transmission");
    controller.edit(F::bandwidth,"1.2 kHz");
    controller.edit(F::binary,"001");
    require(controller.field(F::message).text=="Preserve this message"&&!controller.estimate(),"Partial byte changed committed message bytes");
    controller.edit(F::message,"Preserve this message!");
    require(controller.field(F::binary).text.starts_with("01010000"),"Message edit did not restore the synchronized binary view");
    const auto before=controller.field(F::message).text;
    controller.edit(F::message,std::string(1024*1024+1,'a'));
    require(controller.field(F::message).text==before,"Over-limit edit was truncated or accepted");
    controller.edit(F::message,std::string("bad\xc3",4));
    require(controller.field(F::message).text==before,"Invalid UTF-8 was accepted by shared text state");
    controller.edit(F::callsign,"two\nlines");
    require(controller.field(F::callsign).text.empty(),"Single-line field accepted a newline");
    std::set<std::tuple<ui::Page,int,int,unsigned>> identities;
    for(const auto& control:ui::console_screen()) {
        const auto kind=control.kind==ui::Kind::action?1:control.kind==ui::Kind::bitmap?2:0;
        // Literal labels have no model binding; several can share a page.
        if(kind==0 && control.field==F::count)continue;
        const auto binding=kind==1?static_cast<int>(control.command):kind==2?static_cast<int>(control.bitmap):static_cast<int>(control.field);
        require(identities.emplace(control.page,kind,binding,control.instance).second,"Screen declares a duplicate binding identity");
    }
    require(controller.field(F::key).selected=="none","Empty key list selected an imaginary key");
    require(!controller.enabled(C::acknowledge_key_failure),"Key failure acknowledgement is available without a failure");
    controller.activate(C::open_keyfile);
    const auto requests=controller.take_services();
    require(requests.size()==1&&requests.front().kind==ui::ServiceKind::open_file,"Key command did not request the platform file service");
    const auto missing=std::filesystem::temp_directory_path()/("missing-datapump-key-"+std::to_string(Clock::now().time_since_epoch().count()));
    controller.complete_service({requests.front().id,false,path_text(missing),{}});
    const auto deadline=Clock::now()+std::chrono::seconds(2);
    while(!controller.enabled(C::acknowledge_key_failure)&&Clock::now()<deadline) {
        controller.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(controller.enabled(C::acknowledge_key_failure),"Failed key load did not expose explicit acknowledgement");
    controller.select(F::key,"none");
    require(controller.enabled(C::acknowledge_key_failure),"Repeating an unchanged Choice silently acknowledged a key failure");
    controller.activate(C::acknowledge_key_failure);
    require(!controller.enabled(C::acknowledge_key_failure)&&controller.field(F::key).selected=="none","Explicit key failure acknowledgement changed the retained selection");

    // Completion cannot be delivered until poll, so this deterministically
    // tests an already-dispatched attachment finishing after Use text.
    Controller attachment_controller({true,true});
    attachment_controller.edit(F::message,"Preserve this message");
    const auto attachment_path=std::filesystem::temp_directory_path()/("datapump-controller-attachment-"+std::to_string(Clock::now().time_since_epoch().count()));
    write_new_file(path_text(attachment_path),file_bytes);
    attachment_controller.activate(C::attach_file);
    const auto attachment_requests=attachment_controller.take_services();
    require(attachment_requests.size()==1,"Attachment chooser was not requested");
    attachment_controller.complete_service({attachment_requests.front().id,false,path_text(attachment_path),{}});
    attachment_controller.poll();
    attachment_controller.activate(C::use_text);
    const auto text_revision=attachment_controller.revision();
    const auto attachment_deadline=Clock::now()+std::chrono::seconds(3);
    while(!attachment_controller.estimate()&&Clock::now()<attachment_deadline) {
        attachment_controller.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::filesystem::remove(attachment_path);
    require(attachment_controller.estimate().has_value(),"Estimate did not recover after cancelling a pending attachment");
    require(attachment_controller.revision()==text_revision&&attachment_controller.field(F::message_label).text=="Message"&&attachment_controller.field(F::message).enabled,"Late attachment completion undid Use text");
}
}

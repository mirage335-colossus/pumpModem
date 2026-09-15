#include "../src/gui/gui_smoke.hpp"
#include "../src/gui/bitmap_sources.hpp"
#include "../src/gui/binary_editor.hpp"
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
void rate_carrier_controls() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    check(controller.field(F::bandwidth).text=="3.6 kHz" && controller.field(F::carrier).text=="1.5 kHz" &&
          controller.settings().transfer.modem.bandwidth_hz==3600 && controller.settings().transfer.modem.carrier_hz==1500,
          "GUI defaults must use the 3.6 kHz rate and 1.5 kHz audio carrier");
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
    check(controller.field(F::message).text_cursor_end_revision>draft_cursor_revision,
          "Clearing a draft left the cursor before its new greeting");
    check(controller.field(F::message).text==
          "CQ CQ CQ DE Portable station / N0CALL GRID Somewhere nearby. Please reply. ",
          "Clearing the message did not seed the latest verbatim convenience fields");
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
    const auto cleared=repeatable_marker(controller.field(F::message).text);
    check(cleared!=marker&&controller.field(F::message).text==cleared,
          "Clearing the message did not seed a fresh repeatable identifier");
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
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
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
    Controller controller({true,true});controller.edit(F::message,"e");prepare(controller);
    check(controller.inspection()->stream_layout && controller.estimate()->coded_bytes==128,
          "one-byte message uses the single fixed coded interval");
    controller.start();controller.activate(C::transmit);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);
    while(controller.inbox().items().empty() && std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(controller.inbox().items().size()==1 && controller.inbox().items().front().message.data==Bytes{'e'},
          "fixed source reception preserves one-byte text");
    std::optional<std::size_t> index;
    for(std::size_t i=0;i<controller.signals().lines().size();++i)
        if(controller.signals().lines()[i].validated)index=i;
    check(index.has_value(),"completed source has a validated display row");
    controller.select(F::signals,std::to_string(controller.signals().lines()[*index].id));
    controller.activate(C::copy_signal);const auto requests=controller.take_services();
    check(requests.size()==1 && requests.front().value=="e","copy uses exact decoded source bytes");
    controller.complete_service({requests.front().id,false,{},{}});
    controller.activate(C::paste_signal);
    check(controller.message_bytes()==Bytes{'e'},"paste uses source bytes rather than coded data");
    controller.close();
}

void receive_pattern_text(Controller& controller,const std::string& expected) {
    controller.start();controller.activate(ui::Command::transmit);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    bool received=false;
    while(std::chrono::steady_clock::now()<deadline) {
        controller.poll();
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
}
void fixed_sentence_reception() {
    using F=ui::Field;
    Controller controller({true,true});controller.edit(F::message,"quick brown fox");prepare(controller);
    check(controller.inspection()->stream_layout && !controller.inspection()->binary &&
          controller.estimate()->wire_bits==1216,"short sentence has the same fixed interval as other sources");
    controller.close();
}

void short_raw_editor() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    check(!controller.enabled(C::transmit_short_bits)&&!controller.enabled(C::copy_raw_signal)&&
          !controller.enabled(C::paste_raw_signal),"Empty raw tab enabled a transmission or receive action");
    controller.edit(F::callsign,"N0CALL");controller.toggle(F::repeatable,true);
    controller.edit(F::short_bits,"0 1 0");prepare(controller);
    check(controller.field(F::binary).text=="010"&&controller.field(F::message).text.empty()&&
          !controller.field(F::repeatable).checked&&controller.inspection()->binary&&
          controller.enabled(C::transmit_short_bits),"Short raw entry did not replace a long greeting with exact bits");
    check(controller.estimate()->total_seconds==transfer::estimate_binary(Bytes{0,1,0},controller.settings().transfer).total_seconds&&
          controller.field(F::short_bits_detail).text.find("exactly as entered")!=std::string::npos,
          "Raw tab did not distinguish three transmitted bits from the decoded t byte");
    const auto& reference=controller.field(F::compression_codes).text;
    check(reference.find("128-byte")!=std::string::npos,"Compression reference shows fixed interval source transport");
    for(const auto invalid:{"01010","01x",""}) {
        controller.edit(F::short_bits,invalid);controller.poll();
        check(controller.field(F::short_bits).text==invalid&&!controller.estimate()&&
              !controller.enabled(C::transmit_short_bits)&&!controller.enabled(C::transmit),
              "Invalid short raw input silently transmitted the previous draft");
    }
    for(const auto raw:{"0","00","0010","1111","1011"}) {
        controller.edit(F::short_bits,raw);prepare(controller);
        check(controller.field(F::binary).text==raw&&controller.enabled(C::transmit_short_bits)&&
              controller.inspection()->binary&&controller.estimate()->total_seconds==
                  transfer::estimate_binary(parse_binary_bits(raw),controller.settings().transfer).total_seconds,
              "One- to four-bit raw input was padded, compressed, or rejected as an incomplete dictionary token");
    }
    controller.edit(F::message,"t");prepare(controller);
    check(controller.field(F::short_bits).text.empty()&&!controller.inspection()->binary&&
          controller.field(F::binary).text=="01110100"&&!controller.enabled(C::transmit_short_bits),
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
    for(const auto raw:{"010","1","0010"}) {
        Controller controller({true,true});controller.edit(F::short_bits,raw);prepare(controller);
        controller.start();controller.activate(C::transmit_short_bits);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        std::optional<std::size_t> received;
        while(std::chrono::steady_clock::now()<deadline) {
            controller.poll();
            for(std::size_t i=0;i<controller.signals().lines().size();++i)
                if(controller.signals().copy_raw_bits(i)==raw)received=i;
            if(controller.snapshot().simulation_replay)
                check(!received,"Exact received bits became copyable before simulation replay completed");
            if(received&&controller.snapshot().transmission_finished&&!controller.snapshot().simulation_replay)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(received.has_value(),"Simulated short raw pattern did not retain its exact received bits");
        controller.select(F::signals,std::to_string(controller.signals().lines()[*received].id));
        check(controller.enabled(C::copy_raw_signal)&&controller.enabled(C::paste_raw_signal)&&
              controller.field(F::received_raw_bits).text.find(raw)!=std::string::npos,
              "Completed raw reception did not enable inspection, copy and reuse");
        controller.activate(C::copy_raw_signal);const auto requests=controller.take_services();
        check(requests.size()==1&&requests.front().value==raw,"Copy raw bits copied decoded text or padded byte bits");
        controller.complete_service({requests.front().id,false,{},{}});
        check(!controller.signals().copy_text(*received),"few raw bits have no legacy dictionary interpretation");
        controller.activate(C::paste_raw_signal);prepare(controller);
        check(controller.field(F::short_bits).text==raw&&controller.field(F::binary).text==raw&&
              controller.inspection()->binary&&controller.enabled(C::transmit_short_bits),
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
    receive_pattern_text(controller,original.text());
    check(controller.signals().lines().size()==1 && controller.field(F::signals).records.size()==1 &&
          signal_status_label(controller.signals().lines().front())=="text received",
          "Byte-aligned arbitrary bytes did not produce exactly one escaped text row");
    controller.select(F::signals,std::to_string(controller.signals().lines().front().id));
    check(controller.enabled(C::paste_signal),"Received arbitrary bytes could not be pasted as a message");
    controller.activate(C::copy_signal);const auto requests=controller.take_services();
    check(requests.size()==1 && requests.front().kind==ui::ServiceKind::clipboard && requests.front().value==original.text(),
          "Copying arbitrary received bytes did not use their escaped text representation");
    controller.complete_service({requests.front().id,false,{},{}});
    controller.edit(F::message,"A plain message with an unfinished binary prefix");
    controller.toggle(F::repeatable,true);
    controller.edit(F::binary,"001");
    check(controller.field(F::repeatable).checked && !controller.estimate() &&
          controller.field(F::binary_label).text.find("incomplete")!=std::string::npos,
          "Paste fixture did not retain a repeatable plain-text draft with an incomplete binary prefix");
    controller.activate(C::paste_signal);
    check(controller.message_bytes()==expected && controller.field(F::message).text==original.text() &&
          controller.field(F::binary).text==original.binary() && !controller.field(F::repeatable).checked &&
          controller.field(F::message_label).text.find("escaped")!=std::string::npos,
          "Paste as message reinterpreted arbitrary bytes, retained repeatable text or lost the binary view");
    prepare(controller);
    check(controller.enabled(C::transmit),"Paste as message retained the discarded draft's incomplete binary error");
    controller.close();
}
void binary_source_representation() {
    using F=ui::Field;
    for(const auto& expected:{Bytes{'i','n'},Bytes{0,'e',0}}) {
        Controller controller({true,true});const BinaryEditor editor(expected);
        controller.edit(F::binary,editor.binary()); // Establish the byte editor's explicit escaped mode.
        controller.edit(F::message,editor.text());prepare(controller);
        check(controller.message_bytes()==expected && controller.inspection()->stream_layout &&
              !controller.inspection()->binary,"all exact source bytes use fixed coding with selected codec");
        controller.close();
    }
}

void receive_target_controls() {
    using F=ui::Field;
    Controller controller({true,true});
    check(controller.field(F::receive_snr).text=="80" && controller.settings().transfer.receive_targets_db_hz==std::vector<double>{80} &&
          controller.settings().transfer.automatic_receive_profiles,"automatic receive targets must default to the TX target of 80");
    const auto tx=controller.settings().transfer.modem;
    controller.edit(F::receive_snr,"40,");
    controller.poll();
    check(controller.field(F::receive_snr).text=="40,","a partial comma-list edit must remain editable before normalization");
    controller.edit(F::receive_snr," 40, +6, -6, 40 ");
    std::this_thread::sleep_for(std::chrono::milliseconds(775));controller.poll();
    check(controller.field(F::receive_snr).text=="40, 6, -6" &&
          controller.settings().transfer.receive_targets_db_hz==std::vector<double>({40,6,-6}),"receive target field must canonicalize a valid list");
    check(controller.field(F::snr).text=="80" && controller.settings().transfer.modem.spreading_factor==tx.spreading_factor &&
          controller.settings().transfer.modem.integration_seconds==tx.integration_seconds,"receive search targets must not change the transmitted profile");
    controller.edit(F::bandwidth,"18 kHz");
    check(controller.settings().transfer.modem.bandwidth_hz==18000 &&
          controller.settings().transfer.receive_targets_db_hz==std::vector<double>({40,6,-6}),
          "bandwidth changes must preserve custom receive targets");
    controller.edit(F::receive_snr,"20,");
    controller.edit(F::snr,"-23");
    check(controller.field(F::receive_snr).text=="-23" &&
          controller.settings().transfer.receive_targets_db_hz==std::vector<double>{-23},
          "changing TX SNR must immediately replace pending receive edits with the matching target");
    std::this_thread::sleep_for(std::chrono::milliseconds(775));controller.poll();
    check(controller.settings().transfer.receive_targets_db_hz==std::vector<double>{-23},
          "a pending receive edit must not overwrite the target selected by TX SNR");
    controller.edit(F::snr,"-");
    check(controller.field(F::receive_snr).text=="-23" &&
          controller.settings().transfer.receive_targets_db_hz==std::vector<double>{-23},
          "an incomplete TX SNR edit must preserve the last valid receive target");
    controller.edit(F::snr,"80");
    check(controller.field(F::receive_snr).text=="80" &&
          controller.settings().transfer.receive_targets_db_hz==std::vector<double>{80},
          "correcting TX SNR must restore matching receive targets");
    controller.edit(F::receive_snr,"40, wrong");
    std::this_thread::sleep_for(std::chrono::milliseconds(775));controller.poll();
    check(controller.field(F::receive_snr).text=="40" && controller.settings().transfer.receive_targets_db_hz==std::vector<double>{40},
          "invalid receive target text must reset the complete field to its default");
    controller.edit(F::receive_snr,std::string(513,'1'));
    check(controller.field(F::receive_snr).text=="40","overlong receive target input must reset to the default too");
    const auto& controls=ui::console_screen();
    const auto found=std::find_if(controls.begin(),controls.end(),[](const auto& control){return control.field==F::receive_snr;});
    check(found!=controls.end() && found->persistent && found->kind==ui::Kind::text,
          "receive targets must be an editable persistent text field");
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
        rate_carrier_controls();
        tone_mode_controls();
        tone_key_controls();
        composer_conveniences();
        repeatable_message_identity();
        previous_message_controls();
        repeatable_pending_drafts();
        binary_editor_controls();
        three_bit_dispatch();
        short_raw_editor();short_raw_reception();
        receive_target_controls();fixed_sentence_reception();fixed_text_reception();byte_aligned_pattern_reception();
        escaped_signal_message_paste();binary_source_representation();workspace_controls();
        bitmap_source_checks();
        if(argc>1&&std::string_view(argv[1])=="--smoke") {
            datapump::gui::Controller controller({true,true});
            datapump::gui::Smoke smoke({},100);
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

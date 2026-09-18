#include "application.hpp"
#include "controller.hpp"
#include "document_layout.hpp"
#include "link_planner_model.hpp"
#include "link_planner_page.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string_view>
#include <thread>

using namespace datapump;
using namespace datapump::gui;
namespace {
void check(bool value,const char* message) {if(!value)throw Error(message);}
void near(double actual,double expected,const char* message) {
    check(std::isfinite(actual)&&std::abs(actual-expected)<=1e-10*std::max(1.,std::abs(expected)),message);
}
planner::Inputs example() {
    planner::Inputs inputs;
    inputs.options.modem=tuning::resolve(3600,-8,tuning::PatternMode::auto_pattern,false,1500).config;
    return inputs;
}
void flatten(const ui::DocumentNode& node,std::vector<const ui::DocumentNode*>& nodes) {
    nodes.push_back(&node);for(const auto& child:node.children)flatten(child,nodes);
}
std::vector<const ui::DocumentNode*> nodes(const ui::DocumentNode& root) {
    std::vector<const ui::DocumentNode*> result;flatten(root,result);return result;
}
bool contains_text(const ui::DocumentNode& root,std::string_view text) {
    const auto flat=nodes(root);
    return std::any_of(flat.begin(),flat.end(),[&](const auto* node){return node->text.find(text)!=std::string::npos;});
}
void independent_reference_values() {
    const auto inputs=example();
    const auto short_bit=planner::build(inputs);
    check(short_bit.available&&short_bit.automatic_mode&&short_bit.observer_available,
          "The 3.6 kHz / 1.5 kHz short-bit example must be available");
    // Independent sampled anchors: ceil(10^((18-C/N0)/10)*14400) samples.
    // No training symbol fits here; the waveform adds 128 tail samples and
    // three seconds of suppression, without adding any payload bits.
    near(short_bit.bit_seconds,398.1072222222222,"-8 dB-Hz bit duration changed");
    near(short_bit.send_seconds,401.1161111111111,"One-bit example acquired framing or lost waveform tails");
    near(short_bit.finish_seconds,799.2233333333334,"One full absent symbol must follow the short-bit burst");
    near(short_bit.observer_ratio,2928.958076042251,"Short-bit observer/receiver time anchor changed");
    near(short_bit.occupied_bandwidth_hz,2250,"Shaped bandwidth must use the sampled chip rate");
    near(short_bit.low_audio_hz,375,"SSB lower edge changed");
    near(short_bit.high_audio_hz,2625,"SSB upper edge changed");
    near(short_bit.received_dbm,-167,"3 dBm less 170 dB must give -167 dBm");
    near(short_bit.actual_cn0_db_hz,-3,"Link power must be compared against the per-Hz noise floor");
    near(short_bit.margin_db,5,"Actual-link margin must compare against the selected target");
    auto weak_inputs=inputs;weak_inputs.target_db_hz=-23;
    const auto weak=planner::build(weak_inputs);
    check(weak.available&&weak.observer_available,"The weak example must retain its analytical estimate");
    near(weak.bit_seconds,12589.254166666668,"-23 dB-Hz must take approximately 3.5 hours per bit");
    near(weak.send_seconds,12592.263055555557,"Weak example did not preserve its exact one-bit endpoint");
    near(weak.finish_seconds,25181.517222222225,"Weak example completed without a whole absent symbol");
    near(weak.observer_ratio,92617.28706819766,"Weak observer/receiver time anchor changed");
    check(short_bit.receiver_status!=weak.receiver_status,"Free-running crystal limit did not distinguish the two examples");
    auto lpi_inputs=inputs;lpi_inputs.target_db_hz=23;
    const auto lpi_example=planner::build(lpi_inputs);
    check(lpi_example.available&&lpi_example.observer_available,"Positive 23 dB LPI example must be available");
    // At +23 dB the automatic profile is 1024 chips; settling uses four
    // additional symbols and absence uses eleven complete failed symbols.
    near(lpi_example.bit_seconds,.5688888888888889,"Positive LPI example must retain its subsecond bit");
    near(lpi_example.send_seconds,5.8533333333333335,"Positive LPI example lost its complete waveform overhead");
    near(lpi_example.finish_seconds,12.11111111111111,"Positive LPI example lost its complete-symbol silence check");
    near(lpi_example.observer_ratio,4.333069879228238,"Positive LPI observer/receiver time anchor changed");
}
void exact_geometry_and_physical_finish() {
    auto inputs=example();const auto one=planner::build(inputs);
    inputs.wire_bits=3;const auto three=planner::build(inputs);
    check(three.available,"Exact three-bit draft must be plannable");
    near(three.send_seconds-one.send_seconds,2*one.bit_seconds,"Three bits were padded to a byte or given per-bit overhead");
    near(three.observer_ratio,one.observer_ratio,"Draft size must not change normalized observer time");
    inputs.wire_bits=1216;const auto interval=planner::build(inputs);
    check(interval.available,"Existing 192+1024-bit fixed interval geometry must remain plannable");
    near(interval.send_seconds-one.send_seconds,1215*one.bit_seconds,"Current-draft interval bits were changed by planning");
    // A subsecond bit needs several consecutive fully scored absent symbols;
    // an hours-long bit still needs a whole additional symbol, never six seconds.
    inputs.wire_bits=1;
    for(const auto target:{21.,-8.,-23.,-33.}) {
        inputs.target_db_hz=target;const auto model=planner::build(inputs);
        check(model.available,"Physical-finish reference scenario unavailable");
        const auto absent=model.finish_seconds-model.send_seconds;
        const auto symbols=std::ceil(6/model.bit_seconds);
        near(absent,symbols*model.bit_seconds,"Physical finish must cover six seconds in whole failed symbols");
        check(absent+1e-8>=6&&absent-model.bit_seconds<6,
              "Physical finish did not use the first complete absence interval covering six seconds");
    }
}
void timing_milestones() {
    auto inputs=example();const auto model=planner::build(inputs);
    check(model.fast_target&&model.day_target,"Automatic planning must offer the one-second and one-day milestones");
    // The last automatic code below one second has 1024 chips at 1800 chips/s.
    // The one-day boundary instead uses continuous integration rounded to PCM.
    near(*model.fast_target,20.449725484634943,"Fast milestone ignored the automatic code-length step");
    near(*model.day_target,-31.365137424788934,"One-day milestone changed its 18 dB energy reference");
    for(const auto& milestone:{std::pair{*model.fast_target,1.},std::pair{*model.day_target,86400.}}) {
        inputs.target_db_hz=milestone.first;const auto selected=planner::build(inputs);
        check(selected.available&&selected.bit_seconds<=milestone.second,
              "A milestone must select an available sampled duration at or below its limit");
        inputs.target_db_hz=milestone.first-.00001;const auto weaker=planner::build(inputs);
        check(weaker.available&&weaker.bit_seconds>milestone.second,
              "The weak side of a milestone must cross the advertised duration");
    }
    inputs.target_db_hz=*model.fast_target;
    near(planner::build(inputs).bit_seconds,1024./1800,"Fast milestone must preserve its discrete 1024-chip duration");
    inputs.target_db_hz=*model.day_target;
    near(planner::build(inputs).bit_seconds,86400,"Day milestone must select one day per bit");
}
void clock_and_ram_milestones() {
    // Fixed analytical allowances avoid dependence on the host's free memory.
    // Both budgets cover the crystal offset at -8; only 1 GiB covers its FFT
    // workspace. No allocation scales to these planning limits.
    for(const std::size_t mib:{512,1024}) {
        auto inputs=example();inputs.options.dsp_workspace_bytes=mib*1024*1024;
        inputs.dsp_workspace_percent=75;
        const auto selected=planner::build(inputs);
        check(selected.available&&selected.clock_search_supported&&
              selected.receiver_workspace_supported==(mib==1024),
              "Clock coverage alone must not qualify a plan whose selected DSP allowance cannot hold its search");
        if(mib==512)check(selected.receiver_status.find("Wide RX search exceeds RAM (75%)")!=std::string::npos,
                          "RAM failure must identify the selected workspace percentage");
        check(selected.clock_target&&selected.clock_limit_reason=="RAM",
              "Memory-constrained clock milestone must identify RAM as its limiting condition");
        inputs.target_db_hz=*selected.clock_target;
        const auto milestone=planner::build(inputs);
        check(milestone.available&&milestone.clock_search_supported&&milestone.receiver_workspace_supported,
              "Returned clock/RAM milestone must itself fit the selected search and byte allowance");
    }
}
void separate_link_budget_and_observer_model() {
    auto inputs=example();const auto reference=planner::build(inputs);
    inputs.tx_dbm=50;inputs.path_loss_db=200;inputs.noise_density_dbm_hz=-170;
    const auto different_link=planner::build(inputs);
    check(different_link.available,"A changed illustrative link budget should remain available");
    near(different_link.actual_cn0_db_hz,20,"Power/path/noise arithmetic is not independent of the timing target");
    near(different_link.observer_ratio,reference.observer_ratio,"Actual power must not change the normalized time comparison");
    near(different_link.bit_seconds,reference.bit_seconds,"Power/path/noise edits must not silently retune the preview");
    check(reference.observer_hypothetical,"Public patterns need a hypothetical private-pattern observer estimate");
    inputs.options.key.emplace(Bytes(32,0x37));
    const auto keyed=planner::build(inputs);
    check(keyed.available&&!keyed.observer_hypothetical,"A private-pattern preview must recognize its configured key");
    near(keyed.observer_ratio,reference.observer_ratio,"Key material must not change the normalized observer time");
    check(!reference.inputs.options.key,"Planner build must not mutate the caller's encryption settings");
}
void quantization_fixed_modes_and_limits() {
    auto inputs=example();
    inputs.options.modem.sample_rate=15001;
    const auto quantized=planner::build(inputs);
    check(quantized.available,"Valid nonintegral sampled geometry must remain available");
    near(quantized.bit_seconds,5972006./15001,"Planner duration must be rounded to whole samples");
    near(quantized.occupied_bandwidth_hz,1.25*15001/9,"Planner bandwidth must use ceil-quantized chip samples");
    inputs=example();inputs.mode=tuning::PatternMode::pattern_16;
    const auto fixed=planner::build(inputs);
    check(fixed.available&&!fixed.automatic_mode,"A fixed code length must remain fixed in the planner");
    check(!fixed.fast_target&&!fixed.day_target&&!fixed.clock_target,
          "A fixed-length mode must not offer target-dependent timing milestones");
    near(fixed.bit_seconds,128./14400,"Fixed pattern length was replaced by automatic integration");
    inputs=example();
    const auto bounded=planner::build(inputs);
    check(!bounded.points.empty()&&bounded.points.size()<=1024,"Planner sweep must use bounded storage");
    for(std::size_t index=0;index<bounded.points.size();++index) {
        const auto& point=bounded.points[index];
        check(std::isfinite(point.target_db_hz)&&std::isfinite(point.bit_seconds)&&point.bit_seconds>0,
              "Planner curve exposed a nonfinite or nonpositive duration");
        check(!point.observer_available||(std::isfinite(point.observer_ratio)&&point.observer_ratio>0),
              "Available observer curve exposed a nonfinite ratio");
        if(index)check(point.target_db_hz>bounded.points[index-1].target_db_hz,
                       "Planner targets must remain ordered without duplicate plot positions");
    }
    for(const auto bad:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        for(auto member:{&planner::Inputs::target_db_hz,&planner::Inputs::tx_dbm,
                         &planner::Inputs::path_loss_db,&planner::Inputs::noise_density_dbm_hz}) {
            auto invalid=inputs;invalid.*member=bad;const auto rejected=planner::build(invalid);
            check(!rejected.available&&!rejected.error.empty(),"Nonfinite planner input needs an explicit unavailable result");
        }
    }
    inputs.target_db_hz=-200;
    check(!planner::build(inputs).available,"Unrepresentable sampled duration must not appear usable");
    inputs=example();inputs.wire_bits=0;
    check(!planner::build(inputs).available,"An empty bit selection must not invent a one-bit transmission");
    inputs=example();inputs.wire_bits=std::numeric_limits<std::size_t>::max();
    check(!planner::build(inputs).available,"An overflowing draft duration must not wrap the sample counter");
}
void prepare(Controller& controller) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!controller.estimate()&&std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(controller.estimate().has_value(),"Current-draft estimate was not prepared");
}
void current_draft_and_modem_isolation() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    controller.edit(F::bandwidth,"3600");controller.edit(F::carrier,"1500");
    controller.edit(F::message,"e");prepare(controller);
    const auto short_target=controller.field(F::snr).text;
    const auto long_target=controller.field(F::long_snr).text;
    const auto receive_targets=controller.settings().transfer.receive_targets_db_hz;
    const auto modem=controller.settings().transfer.modem;
    const auto estimate=controller.estimate();
    controller.activate(C::planner_example_lpi);
    check(controller.link_plan()->inputs.target_db_hz==23&&controller.link_plan()->inputs.wire_bits==1,
          "LPI example must begin with exactly one raw bit");
    controller.activate(C::planner_toggle_draft);
    check(controller.planner_uses_draft()&&controller.link_plan()->available&&controller.link_plan()->inputs.wire_bits==3,
          "Current draft must use the fixed dictionary's three-bit e endpoint");
    controller.activate(C::planner_power_100w);controller.activate(C::planner_toggle_details);
    check(controller.planner_details()&&controller.link_plan()->inputs.tx_dbm==50,
          "Planner detail and power actions did not update their own preview state");
    check(controller.field(F::snr).text==short_target&&controller.field(F::long_snr).text==long_target&&
          controller.settings().transfer.receive_targets_db_hz==receive_targets&&
          controller.settings().transfer.modem.integration_seconds==modem.integration_seconds&&
          controller.settings().transfer.modem.spreading_factor==modem.spreading_factor&&
          controller.settings().transfer.modem.memory_limit==modem.memory_limit&&
          controller.estimate()->wire_bits==estimate->wire_bits&&
          controller.estimate()->waveform_samples==estimate->waveform_samples&&
          controller.message_bytes()==Bytes{'e'},
          "Planner preview changed the draft, receive profiles, modem settings or transmission geometry");
    controller.edit(F::short_bits,"00101");
    check(!controller.link_plan()->available,"A draft edit must immediately withdraw its stale planner estimate");
    prepare(controller);
    check(controller.link_plan()->available&&controller.link_plan()->inputs.wire_bits==5,
          "Accepted raw draft must retain leading zeros and its partial-byte endpoint");
    controller.edit(F::message,std::string(17,'e'));prepare(controller);
    check(controller.link_plan()->available&&controller.link_plan()->inputs.wire_bits==controller.estimate()->wire_bits&&
          controller.link_plan()->inputs.wire_bits%1216==0,
          "Long draft planning must use the accepted fixed-interval wire geometry");
    controller.activate(C::planner_toggle_draft);
    check(!controller.planner_uses_draft()&&controller.link_plan()->inputs.wire_bits==1,
          "One-bit selection must not retain the previous draft length");
    controller.close();
}
void failed_draft_estimate_and_recovery() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    controller.edit(F::bandwidth,"3600");controller.edit(F::carrier,"1500");
    controller.edit(F::message,"e");controller.edit(F::snr,"-132");
    // One symbol fits the modem sample counter, but the dictionary's three-bit
    // draft overflows it. Settings remain valid while asynchronous work fails.
    near(controller.settings().transfer.modem.integration_seconds,1e15,
         "Failure fixture must retain a valid, exceptionally long symbol");
    controller.activate(C::planner_toggle_draft);
    const auto pending=controller.link_plan();
    check(!pending->available&&pending->error.find("Calculating")!=std::string::npos,
          "Draft planning must show pending preparation before the worker completes");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(controller.link_plan()->error.find("Calculating")!=std::string::npos&&
          std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto failed=controller.link_plan();
    check(!controller.estimate()&&!failed->available&&failed!=pending&&
          failed->error.find("sample counter")!=std::string::npos,
          "Failed preparation must replace the cached pending planner with its actual failure");
    controller.activate(C::planner_toggle_draft);
    check(!controller.planner_uses_draft()&&controller.link_plan()->available&&controller.link_plan()->inputs.wire_bits==1,
          "Failed draft preparation must still allow a one-bit preview");
    controller.activate(C::planner_toggle_draft);controller.edit(F::snr,"32");
    check(!controller.link_plan()->available&&controller.link_plan()->error.find("Calculating")!=std::string::npos,
          "A corrected draft must clear the prior preparation failure while recalculating");
    prepare(controller);
    check(controller.link_plan()->available&&controller.link_plan()->inputs.wire_bits==3,
          "Successful corrected preparation must restore the exact current-draft plan");
    controller.close();
}
void selected_workspace_reaches_planner() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    controller.edit(F::bandwidth,"3600");controller.edit(F::carrier,"1500");
    const auto initial=controller.link_plan();
    check(initial->inputs.dsp_workspace_percent==50&&
          initial->inputs.options.dsp_workspace_bytes==controller.settings().transfer.dsp_workspace_bytes,
          "Planner must receive the initial selected workspace percentage and actual byte allowance");
    controller.select(F::dsp_workspace,"ram-75");
    const auto selected=controller.link_plan();
    const auto allowance=controller.settings().dsp_workspace_bytes;
    check(controller.field(F::dsp_workspace).selected=="ram-75"&&selected!=initial&&
          selected->inputs.dsp_workspace_percent==75&&
          selected->inputs.options.dsp_workspace_bytes==allowance&&
          allowance==controller.settings().transfer.dsp_workspace_bytes,
          "Selecting 75% DSP workspace must invalidate the plan and forward its actual configured allowance");
    near(selected->bit_seconds,initial->bit_seconds,"Changing DSP memory must not change the selected bit duration");
    if(selected->clock_target) {
        controller.activate(C::planner_clock);
        const auto milestone=controller.link_plan();
        check(milestone->inputs.target_db_hz==*selected->clock_target&&milestone->available&&
              milestone->clock_search_supported&&milestone->receiver_workspace_supported,
              "A 75% workspace milestone must fit both the clock search and the selected memory allowance");
        controller.activate(C::planner_target);const auto requests=controller.take_services();
        check(requests.size()==1,"Clock/RAM target must remain editable through the normal prompt");
        controller.complete_service({requests.front().id,false,requests.front().value,{}});
        check(controller.link_plan()->inputs.target_db_hz==milestone->inputs.target_db_hz,
              "A clock/RAM boundary must survive its displayed prompt value without changing a sampled endpoint");
        controller.activate(C::planner_apply_short);
        const auto& applied=controller.settings().transfer;
        const auto& config=applied.modem;
        check(std::find(applied.receive_targets_db_hz.begin(),applied.receive_targets_db_hz.end(),
                        milestone->inputs.target_db_hz)!=applied.receive_targets_db_hz.end(),
              "Applying a clock/RAM boundary must retain its exact target in matching receive profiles");
        near(static_cast<double>(modem::symbol_sample_count(config))/config.sample_rate,milestone->bit_seconds,
             "Applying a clock/RAM boundary changed its sampled symbol duration");
    }
    controller.close();
}
void one_warning_on_planner_page() {
    Launch launch;launch.simulation=true;Application app(launch);
    const auto& pages=ui::pages();
    check(pages.size()>1&&pages[0].id==ui::Page::console&&pages[1].id==ui::Page::planner,
          "Link planner must be the second tab immediately after Console");
    const auto& screen=ui::console_screen();
    const auto found=std::find_if(screen.begin(),screen.end(),[](const auto& control) {
        return control.field==ui::Field::lpi_estimate;
    });
    check(found!=screen.end()&&found->persistent,"Current-draft LPI advisory must remain a persistent shared control");
    const auto advisory=app.field(ui::Field::lpi_estimate).text;
    for(const auto& page:ui::pages()) {
        app.select_page(page.id);
        check(app.control(*found).visible==(page.id!=ui::Page::planner),
              "Current-draft advisory must hide only while the planner owns the LPI warning");
    }
    app.select_page(ui::Page::console);
    check(app.control(*found).visible&&app.field(ui::Field::lpi_estimate).text==advisory,
          "Returning to Console must restore the existing advisory without modifying its content");
    app.close();
}
void application_prompt_roundtrips() {
    using F=ui::Field;using C=ui::Command;
    Launch launch;launch.simulation=true;Application app(launch);
    app.edit(F::bandwidth,"3600");app.edit(F::carrier,"1500");app.edit(F::message,"e");
    const auto short_target=app.field(F::snr).text,long_target=app.field(F::long_snr).text;
    const auto receive_targets=app.field(F::receive_snr).text;
    const auto document=app.document(ui::Page::planner,900);
    check(document&&contains_text(*document,"Link planner")&&app.document(ui::Page::planner,900)==document,
          "Shared planner document must remain available and retain its unchanged presentation identity");
    const auto prompt=[&](C command) {
        app.activate(command);const auto requests=app.take_services();
        check(requests.size()==1&&requests.front().kind==ui::ServiceKind::prompt,
              "Planner editor must use one native prompt service");
        return requests.front();
    };
    const auto value=[&](C command) {
        const auto request=prompt(command);app.complete_service({request.id,true,{},{}});return request.value;
    };
    const auto edit=[&](C command,const std::string& entered) {
        const auto request=prompt(command);app.complete_service({request.id,false,entered,{}});
    };
    app.activate(C::planner_example_lpi);check(value(C::planner_target)=="23","Positive 23 dB LPI example did not reach the shared facade");
    const auto lpi_document=app.document(ui::Page::planner,900);
    check(lpi_document!=document&&contains_text(*lpi_document,"5.85 sec"),
          "Changing the target must replace the shared document with the positive LPI example's real duration");
    app.activate(C::planner_toggle_details);
    check(contains_text(*app.document(ui::Page::planner,900),"Power, path and noise"),
          "Model details action did not expose its native shared controls");
    app.activate(C::planner_example_short);check(value(C::planner_target)=="-8","Short example action did not reach the shared facade");
    edit(C::planner_target,"-12.5");check(value(C::planner_target)=="-12.5","Planner target prompt did not round-trip");
    edit(C::planner_power,"20");check(value(C::planner_power)=="20","Planner power prompt did not round-trip");
    edit(C::planner_loss,"150");check(value(C::planner_loss)=="150","Planner path-loss prompt did not round-trip");
    edit(C::planner_noise,"-170");check(value(C::planner_noise)=="-170","Planner noise-density prompt did not round-trip");
    for(const auto command:{C::planner_target,C::planner_power,C::planner_loss,C::planner_noise}) {
        const auto before=value(command);
        for(const auto* invalid:{"nan","inf","1e300","nonsense"}) {
            edit(command,invalid);check(value(command)==before,"Rejected planner input changed its prior accepted value");
        }
        const auto request=prompt(command);app.complete_service({request.id,true,"42",{}});
        check(value(command)==before,"Cancelling a planner prompt changed the accepted value");
    }
    check(app.field(F::snr).text==short_target&&app.field(F::long_snr).text==long_target&&
          app.field(F::receive_snr).text==receive_targets&&app.field(F::message).text=="e",
          "Preview prompt edits changed live targets or draft text");
    app.activate(C::planner_apply_short);
    check(app.field(F::snr).text=="-12.5"&&app.field(F::long_snr).text==long_target&&
          app.field(F::receive_snr).text.find("-12.5")!=std::string::npos,
          "Explicit short-target apply did not use the existing matching receive-target update");
    edit(C::planner_target,"-18");app.activate(C::planner_apply_long);
    check(app.field(F::snr).text=="-12.5"&&app.field(F::long_snr).text=="-18"&&
          app.field(F::receive_snr).text.find("-18")!=std::string::npos,
          "Explicit long-target apply changed the wrong transmit profile");
    app.close();
}
void document_semantics_layout_and_plots() {
    const auto model=planner::build(example());
    for(const float width:{220.f,460.f,900.f}) {
        const auto document=planner_page::build(model,width,false,false);
        const auto flat=nodes(document);
        check(contains_text(document,"Time per bit")&&contains_text(document,"Observer / receiver time")&&
              contains_text(document,"Clock / RAM limit")&&
              contains_text(document,"Stronger → weaker · dB in 1 Hz")&&contains_text(document,"1 sec")&&
              contains_text(document,"1 min")&&contains_text(document,"1 hr")&&contains_text(document,"1 day"),
              "Planner labels and logarithmic time axes must remain native text");
        check(std::count_if(flat.begin(),flat.end(),[](const auto* node) {
                  return node->text.find("LPI is not guaranteed")!=std::string::npos;
              })==1,"Planner must use one succinct general LPI warning");
        check(!contains_text(document,"safely hidden")&&!contains_text(document,"safe bits"),
              "Observer time must not be presented as a count of safely hidden bits");
        for(const auto command:{ui::Command::planner_target,ui::Command::planner_example_short,
                                ui::Command::planner_example_lpi,ui::Command::planner_fast,
                                ui::Command::planner_day,ui::Command::planner_clock,
                                ui::Command::planner_toggle_draft,ui::Command::planner_toggle_details})
            check(std::any_of(flat.begin(),flat.end(),[&](const auto* node) {
                      return node->kind==ui::DocumentKind::action&&node->command==command;
                  }),"Planner milestone and editing affordances must use native shared actions");
        const auto layout=ui::layout_document(document,static_cast<int>(width),[](const auto& node,int available) {
            const auto lines=std::max(1.,std::ceil(node.text.size()*node.font_size*.55/std::max(1,available)));
            return lines*node.font_size*1.2;
        });
        const auto within=[&](auto&& self,const ui::DocumentBox& box)->void {
            for(const auto& child:box.children) {
                check(child.bounds.width>0&&child.bounds.x>=0&&child.bounds.y>=0&&
                      child.bounds.x+child.bounds.width<=box.bounds.width&&
                      child.bounds.y+child.bounds.height<=box.bounds.height,
                      "Planner document overflowed or squeezed content to zero at a supported width");
                self(self,child);
            }
        };
        within(within,layout.root);
    }
    const auto detailed=planner_page::build(model,900,true,false);
    check(contains_text(detailed,"Model limits")&&contains_text(detailed,"Power, path and noise")&&
          contains_text(detailed,"90% detection")&&contains_text(detailed,"1% false alarm")&&
          contains_text(detailed,"FT8 reference"),"Expandable model details lost their assumptions and familiar reference");
    auto wide_band=example();
    wide_band.options.modem=tuning::resolve(10000,-8,tuning::PatternMode::auto_pattern,false).config;
    const auto wide_document=planner_page::build(planner::build(wide_band),900,false,false);
    const auto wide_nodes=nodes(wide_document);
    const auto band=std::find_if(wide_nodes.begin(),wide_nodes.end(),[](const auto* node) {
        return node->text.starts_with("Ideal signal:");
    });
    check(band!=wide_nodes.end()&&(*band)->text=="Ideal signal: 4.375–10.625 kHz",
          "A 10 kHz rate must show exact plain-decimal band endpoints, without scientific notation");
    const auto flat=nodes(detailed);
    std::size_t plot_count=0;
    for(const auto* node:flat)if(node->kind==ui::DocumentKind::bitmap) {
        ++plot_count;
        check(node->plot_name=="planner/bit-time"||node->plot_name=="planner/observer-time",
              "Planner graph lost its stable native bitmap identity");
        for(const auto format:{PixelFormat::gray8,PixelFormat::rgb24,PixelFormat::mono1}) {
            constexpr unsigned width=319,height=167;
            BitmapImage full(width,height,format),damaged(width,height,format);
            auto request=full_bitmap_request(width,height,format==PixelFormat::mono1,format==PixelFormat::rgb24);
            node->plot.paint(request,[&](unsigned x,unsigned y,PixelBlock pixels){full.blit(x,y,pixels);});
            const std::set<unsigned char> levels(full.pixels().begin(),full.pixels().end());
            check(levels.size()>1,"Planner graph did not paint visible data and reference marks");
            for(const auto damage:{PixelRect{0,0,113,57},PixelRect{113,0,width-113,57},
                                   PixelRect{0,57,113,height-57},PixelRect{113,57,width-113,height-57}}) {
                request.damage=damage;
                node->plot.paint(request,[&](unsigned x,unsigned y,PixelBlock pixels){damaged.blit(x,y,pixels);});
            }
            check(full.pixels()==damaged.pixels(),"Planner damage repaint changed its immutable graph geometry");
            request.damage={0,0,0,0};bool painted=false;
            node->plot.paint(request,[&](unsigned,unsigned,PixelBlock){painted=true;});
            check(!painted,"Zero-area planner damage must not paint");
        }
    }
    check(plot_count==2,"Planner must include its two time-comparison graphs");
    auto invalid=model;invalid.available=false;invalid.error="Current draft pending";
    const auto unavailable=planner_page::build(invalid,220,true,true);
    check(contains_text(unavailable,"Current draft pending")&&!contains_text(unavailable,"Send current draft"),
          "Unavailable planner must withdraw stale numerical results and explain its pending state");
    check(contains_text(unavailable,"Power, path and noise")&&!contains_text(unavailable,"Received:"),
          "Unavailable model must retain editable inputs while hiding stale derived link strength");
    const auto pending_nodes=nodes(unavailable);
    check(std::any_of(pending_nodes.begin(),pending_nodes.end(),[](const auto* node) {
              return node->kind==ui::DocumentKind::action&&node->command==ui::Command::planner_toggle_draft&&node->enabled;
          }),"Pending or invalid draft planning must leave an action to return to one bit");
}
}
int main() {
    try {
        independent_reference_values();exact_geometry_and_physical_finish();timing_milestones();
        clock_and_ram_milestones();
        separate_link_budget_and_observer_model();quantization_fixed_modes_and_limits();
        current_draft_and_modem_isolation();application_prompt_roundtrips();
        failed_draft_estimate_and_recovery();one_warning_on_planner_page();
        selected_workspace_reaches_planner();
        document_semantics_layout_and_plots();
        std::cout<<"Shared Link planner tests passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

#pragma once
#include "controller.hpp"
#include "plot_render.hpp"
#include <map>

namespace datapump::gui {
inline std::vector<plots::PatternEvidencePoint> pattern_evidence_points(std::span<const live::PatternStep> steps,
    WrongKeyAdmission policy=WrongKeyAdmission::current) {
    if(steps.size()>plots::pattern_evidence_capacity)steps=steps.last(plots::pattern_evidence_capacity);
    std::vector<plots::PatternEvidencePoint> points;points.reserve(steps.size());
    for(const auto& step:steps) {
        plots::PatternEvidencePoint point;point.slot=step.index;
        point.browser_admitted=pattern_browser_pass(step,policy);
        if(step.evidence_complete && std::isfinite(step.operational_bit_evidence[0]) &&
           std::isfinite(step.operational_bit_evidence[1]) &&
           std::isfinite(step.operational_log_e) && std::isfinite(step.operational_threshold)) {
            // Channel branches need not favor the same bit as the affine
            // diagnostic. Plot the scorer's actual prior-corrected alternatives.
            point.bit_log_e=step.operational_bit_evidence;point.operational_log_e=step.operational_log_e;
            point.operational_threshold=step.operational_threshold;
        }
        if(step.evidence_complete && step.quiet_modes && std::isfinite(step.quiet_energy) && step.quiet_energy>=0 &&
           std::isfinite(step.quiet_threshold) && step.quiet_threshold>0) {
            point.quiet_energy=step.quiet_energy;point.quiet_threshold=step.quiet_threshold;
        }
        points.push_back(std::move(point));
    }
    return points;
}

// Application mapping for named bitmap declarations. Toolkit adapters observe
// changed IDs and transfer their opaque snapshots; they do not interpret modem
// measurements, QR brightness, replay policy or inspection diagram contents.
class BitmapSources {
public:
    std::vector<ui::Bitmap> update(Controller& controller) {
        std::vector<ui::Bitmap> changed;
        const auto put = [&](ui::Bitmap id, plots::PlotSnapshot source) {
            frames_[id] = std::move(source); ++versions_[id]; changed.push_back(id);
        };
        const auto update = controller.plot_update();
        const auto& snapshot = controller.snapshot();
        const bool clear=update.clear_waterfall || (snapshot.simulation_replay && !replaying_);
        replaying_ = snapshot.simulation_replay;
        simulation_=snapshot.simulation;environment_monitor_=snapshot.environment_monitor;
        if (clear) {history_.clear();spectrum_revision_.reset();}
        const bool append=update.append_waterfall && snapshot.spectrum_revision &&
            spectrum_revision_!=snapshot.spectrum_revision;
        if (append) history_.push(snapshot.spectrum_db, snapshot.spectrum_bin_hz,
            snapshot.replay_waterfall ? std::optional(snapshot.replay_waterfall->gain_db) : snapshot.simulation_spectrum_gain_db,
            snapshot.spectrum_complex,
            snapshot.spectrum_start_sample,snapshot.spectrum_sample_count,
            snapshot.simulation_replay?0:snapshot.spectrum_segment);
        if(append)spectrum_revision_=snapshot.spectrum_revision;
        const auto zoom = controller.waveform_zoom();
        if (update.update_plots || zoom_ != zoom)
            put(ui::Bitmap::waveform, plots::PlotSnapshot::oscilloscope(snapshot.waveform,
                snapshot.waveform_sample_rate?snapshot.waveform_sample_rate:controller.settings().transfer.modem.sample_rate,zoom));
        zoom_ = zoom;
        if (update.update_plots) {
            put(ui::Bitmap::constellation, plots::PlotSnapshot::constellation(snapshot.constellation,
                snapshot.constellation_source != live::ConstellationSource::input));
            constellation_source_ = snapshot.constellation_source;
            constellation_dropped_ = snapshot.constellation_dropped;
        }
        const auto& evidence_steps=snapshot.simulation_pattern_replay_steps
            ? *snapshot.simulation_pattern_replay_steps : snapshot.pattern_steps;
        std::span<const live::PatternStep> pattern_steps(evidence_steps);
        pattern_replay_caption_.clear();
        if(snapshot.simulation_pattern_replay_active && !pattern_steps.empty()) {
            const auto total=pattern_steps.size();
            pattern_steps=pattern_steps.first(std::min(total,snapshot.simulation_pattern_replay_visible_steps));
            pattern_replay_caption_="Replay · "+std::to_string(pattern_steps.size())+"/"+std::to_string(total)+" slots";
            if(!pattern_steps.empty())
                pattern_replay_caption_+=" · +"+std::to_string(pattern_steps.back().epoch-pattern_steps.front().epoch)+" s modeled";
        }
        const auto policy=triad::v3::parse_wrong_key_admission(controller.field(ui::Field::wrong_key_admission).selected);
        auto evidence=pattern_evidence_points(pattern_steps,policy);
        if(!frames_.contains(ui::Bitmap::pattern_scores) || evidence!=pattern_evidence_) {
            pattern_evidence_=std::move(evidence);
            const auto frame=plots::PlotSnapshot::pattern_evidence(pattern_evidence_);
            put(ui::Bitmap::pattern_scores,frame);put(ui::Bitmap::pattern_evidence,frame);
        }
        const auto band=controller.spectrum_search_band();
        if (clear || append) {
            frozen_history_=std::make_shared<const plots::SpectrumHistory>(history_);
            put(ui::Bitmap::waterfall,plots::PlotSnapshot::waterfall_shared(frozen_history_));
        }
        if(clear || append || band!=searched_band_ || !frames_.contains(ui::Bitmap::waterfall_detail))
            put(ui::Bitmap::waterfall_detail,plots::PlotSnapshot::waterfall_detail(frozen_history_,band));
        searched_band_=band;
        const auto helper_band=controller.spectrum_helper_band();
        if((helper_band || helper_band_) && (clear || append || helper_band!=helper_band_ || !frames_.contains(ui::Bitmap::waterfall_helper_detail)))
            put(ui::Bitmap::waterfall_helper_detail,helper_band?
                plots::PlotSnapshot::waterfall_detail(frozen_history_,helper_band,"Helper Carrier"):
                plots::PlotSnapshot{});
        helper_band_=helper_band;
        const auto& bytes = controller.message_bytes();
        const std::string text(bytes.begin(), bytes.end());
        const auto& brightness = controller.field(ui::Field::qr_brightness).selected;
        if (qr_text_ != text || brightness_ != brightness) {
            qr_text_ = text; brightness_ = brightness; qr_error_.clear();
            std::optional<QrCode> code;
            try { if (!text.empty()) code = encode_qr(text); }
            catch (const Error& error) { qr_error_ = error.what(); }
            const auto mode = brightness == "normal" ? plots::QrBrightness::normal : brightness == "dim" ? plots::QrBrightness::dim :
                brightness == "off" ? plots::QrBrightness::off : plots::QrBrightness::dark;
            put(ui::Bitmap::qr, plots::PlotSnapshot::qr(std::move(code), mode));
        }
        const auto model = controller.inspection();
        if (inspected_ != model) {
            inspected_ = model;
            put(ui::Bitmap::pattern, {}); put(ui::Bitmap::pattern_distances, {});
            for (auto id : {ui::Bitmap::payload_alphabet, ui::Bitmap::reference_alphabet}) {
                const std::size_t index = id == ui::Bitmap::payload_alphabet ? 0 : 1;
                put(id, model && index < model->constellations.size() ?
                    plots::PlotSnapshot::constellation(model->constellations[index].points, true) : plots::PlotSnapshot{});
            }
        }
        return changed;
    }
    const plots::PlotSnapshot& get(ui::Bitmap id) const {
        const auto found = frames_.find(id);
        return found == frames_.end() ? empty_ : found->second;
    }
    std::uint64_t version(ui::Bitmap id) const { const auto it=versions_.find(id);return it==versions_.end()?0:it->second; }
    std::string caption(ui::Bitmap id, unsigned width = 640) const {
        if (id == ui::Bitmap::qr && !qr_error_.empty()) return qr_error_;
        auto result = get(id).caption(width);
        if(id==ui::Bitmap::waveform || id==ui::Bitmap::waterfall || id==ui::Bitmap::waterfall_detail || id==ui::Bitmap::waterfall_helper_detail)
            result+=environment_monitor_?" / input monitor":simulation_?" / simulated stream":" / modem stream";
        if((id==ui::Bitmap::pattern_scores || id==ui::Bitmap::pattern_evidence) && !pattern_replay_caption_.empty())
            result=pattern_replay_caption_+" · "+result;
        if (id == ui::Bitmap::constellation && constellation_dropped_)
            result += " / " + std::to_string(constellation_dropped_) + " omitted";
        return result;
    }
    std::string error(ui::Bitmap id) const { return id==ui::Bitmap::qr?qr_error_:std::string{}; }
    const char* title(ui::Bitmap id) const {
        switch (id) {
        case ui::Bitmap::waveform: return replaying_ ? "Simulation replay / waveform" : "Live waveform";
        case ui::Bitmap::waterfall: return replaying_ ? "Replay spectrum" : "Spectrum";
        case ui::Bitmap::waterfall_detail: return replaying_ ? "Replay / configured searched spectrum" : "Configured searched spectrum";
        case ui::Bitmap::waterfall_helper_detail: return "Helper Carrier / auxiliary and pilot";
        case ui::Bitmap::constellation:
            return constellation_source_ == live::ConstellationSource::received ? "Received constellation" :
                constellation_source_ == live::ConstellationSource::transmitted ? "Transmitted constellation" : "Receiver input I/Q";
        case ui::Bitmap::pattern_scores: case ui::Bitmap::pattern_evidence: return "Pattern evidence";
        default: return ""; // Other sources retain their ordinary declaration label.
        }
    }
private:
    std::map<ui::Bitmap, plots::PlotSnapshot> frames_;
    std::map<ui::Bitmap, std::uint64_t> versions_;
    plots::PlotSnapshot empty_;
    plots::SpectrumHistory history_;
    std::shared_ptr<const plots::SpectrumHistory> frozen_history_=std::make_shared<const plots::SpectrumHistory>();
    std::optional<std::pair<double,double>> searched_band_;
    std::optional<std::pair<double,double>> helper_band_;
    std::optional<std::uint64_t> spectrum_revision_;
    std::vector<plots::PatternEvidencePoint> pattern_evidence_;
    std::string pattern_replay_caption_, qr_text_, brightness_, qr_error_;
    std::shared_ptr<const Inspection> inspected_;
    std::uint64_t constellation_dropped_ = 0;
    double zoom_ = 0;
    bool replaying_ = false;
    bool simulation_ = true, environment_monitor_ = false;
    live::ConstellationSource constellation_source_ = live::ConstellationSource::input;
};
}

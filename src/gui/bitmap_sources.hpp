#pragma once
#include "controller.hpp"
#include "plot_render.hpp"
#include <iomanip>
#include <map>
#include <sstream>

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
        const auto modeled = snapshot.simulation_waterfall;
        const bool modeled_changed = modeled != modeled_source_;
        const auto detail = snapshot.simulation_detail;
        const bool detail_changed = detail != simulation_detail_source_;
        const bool new_simulation_segment=snapshot.simulation && update.append_waterfall &&
            spectrum_segment_ && (*spectrum_segment_!=snapshot.spectrum_segment ||
                (!snapshot.spectrum_revision && snapshot.transmitting));
        const bool clear=update.clear_waterfall || new_simulation_segment ||
            (snapshot.simulation_replay && !replaying_);
        const bool simulation_changed=simulation_!=snapshot.simulation;
        replaying_ = snapshot.simulation_replay;
        previewing_=snapshot.simulation_progress && snapshot.transmitting && !replaying_ &&
            !snapshot.simulation_progress->complete;
        simulation_=snapshot.simulation;environment_monitor_=snapshot.environment_monitor;
        if (clear) {
            history_.clear();spectrum_revision_.reset();spectrum_segment_.reset();
        }
        if (modeled && modeled->segment != retained_modeled_segment_ &&
            (replaying_ || (snapshot.simulation_progress && snapshot.simulation_progress->complete))) {
            retained_modeled_segment_ = modeled->segment;
            retained_modeled_clear_row_ = modeled->segment == cleared_modeled_segment_ ? cleared_modeled_row_ : 0;
        }
        if (update.clear_waterfall) {
            if (modeled) {
                // Keep the replay cut separately while another run is in progress.
                cleared_modeled_segment_ = modeled->segment;
                cleared_modeled_row_ = modeled->first_row + modeled->rows.size();
                if (modeled->segment == retained_modeled_segment_)
                    retained_modeled_clear_row_ = cleared_modeled_row_;
            }
            cleared_detail_segment_ = std::max(snapshot.spectrum_segment, detail ? detail->segment : 0);
            cleared_detail_sample_ = snapshot.spectrum_segment == cleared_detail_segment_ ?
                snapshot.spectrum_start_sample + snapshot.spectrum_sample_count : 0;
            if (detail && detail->segment == cleared_detail_segment_)
                cleared_detail_sample_ = std::max(cleared_detail_sample_, detail->end_sample);
        }
        modeled_source_=modeled;
        const bool append=update.append_waterfall && snapshot.spectrum_revision &&
            spectrum_revision_!=snapshot.spectrum_revision;
        if (append) history_.push(snapshot.spectrum_db, snapshot.spectrum_bin_hz,
            snapshot.replay_waterfall ? std::optional(snapshot.replay_waterfall->gain_db) : snapshot.simulation_spectrum_gain_db,
            snapshot.spectrum_complex,
            snapshot.spectrum_start_sample,snapshot.spectrum_sample_count,snapshot.spectrum_segment);
        if(append) {
            spectrum_revision_=snapshot.spectrum_revision;
            spectrum_segment_=snapshot.spectrum_segment;
        }
        const auto zoom = controller.waveform_zoom();
        if (update.update_plots || zoom_ != zoom || simulation_changed)
            put(ui::Bitmap::waveform, plots::PlotSnapshot::oscilloscope(snapshot.waveform,
                snapshot.waveform_sample_rate?snapshot.waveform_sample_rate:controller.settings().transfer.modem.sample_rate,
                zoom,simulation_));
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
        }
        if (detail_changed || update.clear_waterfall || simulation_changed) {
            static_assert(plots::SpectrumHistory::capacity == live::simulation_detail_rows);
            auto history = std::make_shared<plots::SpectrumHistory>();
            if (detail) {
                const auto gain = snapshot.replay_waterfall ? std::optional(snapshot.replay_waterfall->gain_db)
                    : snapshot.simulation_spectrum_gain_db;
                for (const auto& row : detail->rows) {
                    if (row.db.empty() || (detail->segment == cleared_detail_segment_ &&
                                          row.start_sample < cleared_detail_sample_)) {
                        history->push_empty_detail();
                    } else {
                        history->push(row.db, static_cast<double>(detail->sample_rate) / detail->fft_samples,
                            gain, row.spectrum, row.start_sample, detail->fft_samples, detail->segment);
                    }
                }
            }
            frozen_simulation_detail_ = std::move(history);
            simulation_detail_source_ = detail;
        }
        if (modeled_changed || (modeled ? update.clear_waterfall : clear || append)) {
            if (modeled) {
                const auto clear_row = modeled->segment == cleared_modeled_segment_ ? cleared_modeled_row_ :
                    modeled->segment == retained_modeled_segment_ ? retained_modeled_clear_row_ : 0;
                const auto first_available = std::clamp(clear_row, modeled->first_row,
                    modeled->first_row + modeled->rows.size());
                const auto skipped = static_cast<std::size_t>(first_available - modeled->first_row);
                auto history = std::make_shared<plots::SpectrumHistory>();
                for (std::size_t i = skipped; i < modeled->rows.size(); ++i)
                    history->push(*modeled->rows[i], modeled->bin_hz, modeled->gain_db);
                put(ui::Bitmap::waterfall,plots::PlotSnapshot::waterfall_timeline_shared(
                    std::move(history),modeled->total_rows,skipped));
            } else put(ui::Bitmap::waterfall,plots::PlotSnapshot::waterfall_shared(frozen_history_));
        }
        const auto detail_history = simulation_ ? frozen_simulation_detail_ : frozen_history_;
        const auto detail_seconds = simulation_ ? std::optional(live::simulation_detail_seconds) : std::nullopt;
        const bool detail_repaint = simulation_changed || (simulation_ ? detail_changed || update.clear_waterfall : clear || append);
        if(detail_repaint || band!=searched_band_ || !frames_.contains(ui::Bitmap::waterfall_detail))
            put(ui::Bitmap::waterfall_detail,plots::PlotSnapshot::waterfall_detail(detail_history,band,"Search",detail_seconds));
        searched_band_=band;
        const auto helper_band=controller.spectrum_helper_band();
        if((helper_band || helper_band_) && (detail_repaint || helper_band!=helper_band_ || !frames_.contains(ui::Bitmap::waterfall_helper_detail)))
            put(ui::Bitmap::waterfall_helper_detail,helper_band?
                plots::PlotSnapshot::waterfall_detail(detail_history,helper_band,"Helper Carrier",detail_seconds):
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
        if(id==ui::Bitmap::waveform || id==ui::Bitmap::waterfall || id==ui::Bitmap::waterfall_detail || id==ui::Bitmap::waterfall_helper_detail) {
            const auto detail = id==ui::Bitmap::waterfall_detail || id==ui::Bitmap::waterfall_helper_detail;
            if(simulation_) {
                std::ostringstream prefix;
                prefix << (replaying_?"RX replay · ":previewing_&&!detail?"Sampled RX preview · ":"Simulated RX · ");
                if(detail && simulation_detail_source_ && simulation_detail_source_->sample_rate)
                    prefix << "end " << std::fixed << std::setprecision(3) <<
                        static_cast<double>(simulation_detail_source_->end_sample) / simulation_detail_source_->sample_rate <<
                        " modeled s · ";
                result=prefix.str()+result;
            }
            else result+=environment_monitor_?" / input monitor":" / modem stream";
        }
        if(id==ui::Bitmap::waterfall && modeled_source_) {
            const auto row_seconds=modeled_source_->seconds_per_row;
            std::ostringstream caption;
            caption << "RX overview · " << std::fixed << std::setprecision(1);
            if (modeled_source_->rows.size() < modeled_source_->total_rows) {
                caption << modeled_source_->rows.size()*row_seconds << '/' << modeled_source_->total_rows*row_seconds;
            } else {
                caption << modeled_source_->first_row*row_seconds << "–" <<
                    (modeled_source_->first_row+modeled_source_->rows.size())*row_seconds;
            }
            caption << " modeled s · avg "
                    << std::setprecision(row_seconds<2 ? 2 : 1) << row_seconds << " s/row";
            return caption.str();
        }
        if((id==ui::Bitmap::pattern_scores || id==ui::Bitmap::pattern_evidence) && !pattern_replay_caption_.empty())
            result=pattern_replay_caption_;
        if (id == ui::Bitmap::constellation && constellation_dropped_)
            result += " / " + std::to_string(constellation_dropped_) + " omitted";
        return result;
    }
    std::string error(ui::Bitmap id) const { return id==ui::Bitmap::qr?qr_error_:std::string{}; }
    const char* title(ui::Bitmap id) const {
        switch (id) {
        case ui::Bitmap::waveform: return replaying_ ? "Simulation replay / waveform" : "Live waveform";
        case ui::Bitmap::waterfall: return modeled_source_ ? "Modeled RX history" : replaying_ ? "Replay spectrum" : "Spectrum";
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
    std::shared_ptr<const plots::SpectrumHistory> frozen_simulation_detail_=std::make_shared<const plots::SpectrumHistory>();
    std::shared_ptr<const live::Snapshot::SimulationWaterfall> modeled_source_;
    std::shared_ptr<const live::Snapshot::SimulationDetail> simulation_detail_source_;
    std::uint64_t cleared_detail_segment_=0, cleared_detail_sample_=0;
    std::uint64_t cleared_modeled_segment_=0, cleared_modeled_row_=0;
    std::uint64_t retained_modeled_segment_=0, retained_modeled_clear_row_=0;
    std::optional<std::pair<double,double>> searched_band_;
    std::optional<std::pair<double,double>> helper_band_;
    std::optional<std::uint64_t> spectrum_revision_, spectrum_segment_;
    std::vector<plots::PatternEvidencePoint> pattern_evidence_;
    std::string pattern_replay_caption_, qr_text_, brightness_, qr_error_;
    std::shared_ptr<const Inspection> inspected_;
    std::uint64_t constellation_dropped_ = 0;
    double zoom_ = 0;
    bool replaying_ = false, previewing_ = false;
    bool simulation_ = true, environment_monitor_ = false;
    live::ConstellationSource constellation_source_ = live::ConstellationSource::input;
};
}

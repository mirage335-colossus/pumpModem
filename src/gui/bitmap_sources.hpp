#pragma once
#include "controller.hpp"
#include "plot_render.hpp"
#include <map>

namespace datapump::gui {
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
        replaying_ = snapshot.simulation_replay;
        constellation_source_ = snapshot.constellation_source;
        if (update.clear_waterfall) history_.clear();
        if (update.append_waterfall) history_.push(snapshot.spectrum_db, snapshot.spectrum_bin_hz);
        const auto zoom = controller.waveform_zoom();
        if (update.update_plots || zoom_ != zoom)
            put(ui::Bitmap::waveform, plots::PlotSnapshot::waveform(snapshot.waveform, controller.settings().transfer.modem, zoom));
        zoom_ = zoom;
        if (update.update_plots) {
            put(ui::Bitmap::constellation, plots::PlotSnapshot::constellation(snapshot.constellation,
                snapshot.constellation_source != live::ConstellationSource::input));
            constellation_dropped_ = snapshot.constellation_dropped;
        }
        if (update.clear_waterfall || update.append_waterfall)
            put(ui::Bitmap::waterfall, plots::PlotSnapshot::waterfall(history_));
        const auto& text = controller.field(ui::Field::message).text;
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
        const auto first = controller.pattern_first();
        const auto page_size = controller.pattern_page_size();
        if (inspected_ != model || pattern_first_ != first || pattern_page_size_ != page_size) {
            inspected_ = model; pattern_first_ = first; pattern_page_size_ = page_size;
            if (model && model->pattern_space) {
                put(ui::Bitmap::pattern, plots::PlotSnapshot::pattern_chips(*model->pattern_space, first, controller.pattern_page_size()));
                put(ui::Bitmap::pattern_distances, plots::PlotSnapshot::pattern_distances(*model->pattern_space));
                put(ui::Bitmap::pattern_evidence, plots::PlotSnapshot::pattern_evidence(*model->pattern_space));
            } else {
                put(ui::Bitmap::pattern, {}); put(ui::Bitmap::pattern_distances, {}); put(ui::Bitmap::pattern_evidence, {});
            }
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
        if (id == ui::Bitmap::constellation && constellation_dropped_)
            result += " / " + std::to_string(constellation_dropped_) + " omitted";
        return result;
    }
    std::string error(ui::Bitmap id) const { return id==ui::Bitmap::qr?qr_error_:std::string{}; }
    const char* title(ui::Bitmap id) const {
        switch (id) {
        case ui::Bitmap::waveform: return replaying_ ? "Simulation replay / waveform" : "Live waveform";
        case ui::Bitmap::waterfall: return replaying_ ? "Simulation replay / waterfall" : "Spectrum / amplitude waterfall";
        case ui::Bitmap::constellation:
            return constellation_source_ == live::ConstellationSource::received ? "Received constellation" :
                constellation_source_ == live::ConstellationSource::transmitted ? "Transmitted constellation" : "Receiver input I/Q";
        default: return ""; // Other sources retain their ordinary declaration label.
        }
    }
private:
    std::map<ui::Bitmap, plots::PlotSnapshot> frames_;
    std::map<ui::Bitmap, std::uint64_t> versions_;
    plots::PlotSnapshot empty_;
    plots::SpectrumHistory history_;
    std::string qr_text_, brightness_, qr_error_;
    std::shared_ptr<const Inspection> inspected_;
    std::size_t pattern_first_ = 0;
    std::size_t pattern_page_size_ = 0;
    std::uint64_t constellation_dropped_ = 0;
    double zoom_ = 0;
    bool replaying_ = false;
    live::ConstellationSource constellation_source_ = live::ConstellationSource::input;
};
}

#pragma once
#include "bitmap.hpp"
#include "pattern_space.hpp"
#include "plot_data.hpp"
#include "datapump/qr.hpp"
#include <memory>
#include <optional>
#include <string>

namespace datapump::gui::plots {
enum class QrBrightness { normal, dim, dark, off };

// Immutable, toolkit-independent source data. Repaint and disjoint damage use
// exactly the same snapshot and full sample grid. paint emits one bounded row
// at a time; the adapter only copies opaque rectangles. Labels, status and
// navigation are ordinary semantic controls, never rasterized here.
class PlotSnapshot {
public:
    PlotSnapshot();
    static PlotSnapshot waveform(std::vector<float> samples, modem::Config config, double zoom = 1);
    static PlotSnapshot constellation(std::vector<std::complex<double>> points, bool symbols = false);
    // Each pair contains nonnegative natural-log evidence against noise:
    // real is the score for binary pattern 0, imaginary for binary pattern 1.
    static PlotSnapshot pattern_scores(std::vector<std::complex<double>> scores, bool enabled = true);
    static PlotSnapshot waterfall(SpectrumHistory history, bool overview = false);
    static PlotSnapshot qr(std::optional<QrCode> code, QrBrightness brightness = QrBrightness::dark);
    static PlotSnapshot pattern_chips(inspection::PatternSpace model, std::size_t first = 0, std::size_t count = 32);
    static PlotSnapshot pattern_distances(inspection::PatternSpace model);
    static PlotSnapshot pattern_evidence(inspection::PatternSpace model);
    static PlotSnapshot codeword(std::size_t data, std::size_t parity);
    // color_enabled is user preference, independently gated by target support.
    void paint(const BitmapRequest& request, const BitmapSink& sink, bool color_enabled = true) const;
    // Erase application-specific factories and data at the backend boundary.
    operator BitmapSource() const;
    // Native labels may use this descriptive scale/capture text.
    std::string caption(unsigned width = 640) const;
private:
    struct Data;
    explicit PlotSnapshot(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};
}

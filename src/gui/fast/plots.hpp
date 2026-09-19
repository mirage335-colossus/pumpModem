#pragma once
#include "../bitmap.hpp"
#include "../ui_contract.hpp"
#include "datapump/fast/telemetry.hpp"
#include <chrono>
#include <memory>
#include <string>

namespace datapump::gui::fast_ui {
// Presentation only: consumes immutable Fast telemetry, never modem decisions.
// The waterfall has a fixed retention bound independent of the file size.
class FastPlots {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr std::size_t history_capacity=96;
    FastPlots();
    bool update(std::shared_ptr<const fast::Diagnostics>,bool active,Clock::time_point now=Clock::now());
    void reset();
    BitmapSource source(ui::Bitmap) const;
    std::uint64_t revision() const {return revision_;}
    std::string title(ui::Bitmap) const;
    std::string caption(ui::Bitmap,unsigned width=640) const;
    std::size_t history_size() const;
private:
    struct Frame;
    std::shared_ptr<const Frame> frame_;
    Clock::time_point last_update_{};
    std::uint64_t revision_=1,ignored_stream_=0;
};
bool owns(ui::Bitmap);
}

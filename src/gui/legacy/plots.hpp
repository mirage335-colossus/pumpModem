#pragma once
#include "../bitmap.hpp"
#include <chrono>
#include <memory>
#include <span>
#include <string>
namespace datapump::gui::legacy_ui {
class Waterfall {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr std::size_t history_capacity=96;
    Waterfall();
    bool update(std::span<const float>,std::uint64_t audio_revision,bool active,bool transmitting,Clock::time_point now=Clock::now());
    BitmapSource source() const;
    std::uint64_t revision() const {return revision_;}
    std::size_t history_size() const;
    std::string caption() const;
    std::string title() const;
private:
    struct Frame;
    std::shared_ptr<const Frame> frame_;
    Clock::time_point last_update_{};
    std::uint64_t revision_=1,audio_revision_=0;
};
}

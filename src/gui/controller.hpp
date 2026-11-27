#pragma once
#include "ui_contract.hpp"
#include "state.hpp"
#include "inspection_model.hpp"
#include "link_planner_model.hpp"
#include "launch_command.hpp"
#include "datapump/live.hpp"
#include <memory>

namespace datapump::gui {
// Display validated GPU scoring and native lag-route selection separately.
// Selection is not a claim that native search has completed.
std::string gpu_search_diagnostics(const live::Snapshot&);
// Owns application state and workers. Invoke on one UI thread. Poll every 40 ms
// independently of paint cadence; no method calls a toolkit or enters its loop.
class Controller {
public:
    struct Options {
        bool simulation = false;
        bool smoke = false;
        std::optional<launch_command::Patch> launch_settings = std::nullopt;
        // Optional embedding policy: all Robust configurations require GPU.
        bool require_gpu = false;
        // Optional embedding policy for a single finite physical-audio episode.
        bool finite_duplex = false;
        bool simulation_only = false;
    };
    Controller();
    explicit Controller(Options options);
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    void start(bool active=true);
    ui::SimulationStartup simulation_startup() const;
    void poll(bool expanded_spectrum = false, bool planner_visible = false);
    void close();
    // TX-idle audio handoff cancels partial DSP capture and retains incomplete
    // rows. Capture restarts on the configured calendar with quiet-slot rearming.
    bool try_suspend_capture();
    void resume_capture();
    bool closing() const;
    bool ready_to_close() const;
    void edit(ui::Field field, std::string text);
    // Commit exact text and request a workspace check for the shared target.
    void commit_target(ui::Field field);
    void select(ui::Field field, std::string option_id);
    void toggle(ui::Field field, bool value);
    void set_shellcode_mode(bool value);
    void set_developer_mode(bool value);
    void activate(ui::Command command);
    void complete_service(ui::ServiceResult result);
    std::vector<ui::ServiceRequest> take_services();
    std::vector<ui::ServiceCompletion> take_service_completions();
    const ui::FieldState& field(ui::Field field) const;
    bool enabled(ui::Command command) const;
    std::string command_label(ui::Command command) const; // Empty keeps the declaration label.
    const live::Snapshot& snapshot() const;
    // Pattern evidence ages in the same clock domain as its session timestamps.
    std::chrono::steady_clock::time_point presentation_time() const;
    const live::Settings& settings() const;
    // Configured receive-domain union in audio Hz; computed on settings changes.
    std::optional<std::pair<double,double>> spectrum_search_band() const;
    std::optional<std::pair<double,double>> spectrum_helper_band() const;
    const std::vector<audio::Device>& audio_devices() const;
    const Inbox& inbox() const;
    const Signals& signals() const;
    const std::shared_ptr<const Inspection>& inspection() const;
    const std::optional<transfer::Estimate>& estimate() const;
    const std::shared_ptr<const planner::Model>& link_plan() const;
    void request_link_plan();
    bool planner_details() const;
    bool planner_uses_draft() const;
    // Consumes accumulated invalidation flags once for all bitmap producers.
    PlotUpdate plot_update() const;
    std::uint64_t revision() const;
    const Bytes& message_bytes() const;
    double waveform_zoom() const;
    std::size_t pattern_first() const;
    void pattern_page_size(std::size_t size);
    std::size_t pattern_page_size() const;
    void report_error(std::string message);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

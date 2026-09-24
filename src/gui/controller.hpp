#pragma once
#include "ui_contract.hpp"
#include "state.hpp"
#include "inspection_model.hpp"
#include "link_planner_model.hpp"
#include "launch_command.hpp"
#include "datapump/live.hpp"
#include <memory>

namespace datapump::gui {
// Owns application state and workers. Invoke on one UI thread. Poll every 40 ms
// independently of paint cadence; no method calls a toolkit or enters its loop.
class Controller {
public:
    struct Options {
        bool simulation = false;
        bool smoke = false;
        std::optional<launch_command::Patch> launch_settings = std::nullopt;
    };
    Controller();
    explicit Controller(Options options);
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    void start();
    void poll();
    void close();
    // Idle-only audio handoff for the mode host; pending reception is retained.
    bool try_suspend_capture();
    void resume_capture();
    bool closing() const;
    bool ready_to_close() const;
    void edit(ui::Field field, std::string text);
    // Finish a target edit without changing its already accepted sample timing.
    void commit_target(ui::Field field);
    void select(ui::Field field, std::string option_id);
    void toggle(ui::Field field, bool value);
    void activate(ui::Command command);
    void complete_service(ui::ServiceResult result);
    std::vector<ui::ServiceRequest> take_services();
    const ui::FieldState& field(ui::Field field) const;
    bool enabled(ui::Command command) const;
    std::string command_label(ui::Command command) const; // Empty keeps the declaration label.
    const live::Snapshot& snapshot() const;
    const live::Settings& settings() const;
    const Inbox& inbox() const;
    const Signals& signals() const;
    const std::shared_ptr<const Inspection>& inspection() const;
    const std::optional<transfer::Estimate>& estimate() const;
    const std::shared_ptr<const planner::Model>& link_plan() const;
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

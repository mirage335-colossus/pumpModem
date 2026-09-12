#pragma once
#include "controller.hpp"
#include <chrono>
#include <filesystem>
#include <memory>

namespace datapump::gui {
// Backend-independent smoke choreography. Its service requests are completed
// by this harness, checking exact payloads without requiring desktop dialogs.
// Call after each controller poll; exceptions identify an actual failed check.
class Smoke {
public:
    explicit Smoke(std::filesystem::path directory = {}, double timeout_seconds = 100);
    ~Smoke();
    void step(Controller& controller);
    bool done() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Pure controller/declaration policy checks; does not open a window or device.
void controller_self_check();
}

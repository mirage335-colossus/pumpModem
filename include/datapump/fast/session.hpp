#pragma once
#include "datapump/audio.hpp"
#include "datapump/fast/profile.hpp"
#include "datapump/fast/simulation.hpp"
#include "datapump/fast/telemetry.hpp"
#include "datapump/crypto.hpp"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace datapump::fast {
class ReceivedFile;
inline constexpr std::size_t text_byte_limit=32768;
struct Settings {
    Profile profile;
    // Local choice: no key means public, checksum-protected transmission.
    // Peers must select the same mode; receivers never fall back from encryption.
    std::optional<Crypto> key;
    std::string device="default";
    // Output routing is a local device choice; mono defaults to the left.
    bool mono=true;
    audio::ChannelMode channel_mode=audio::ChannelMode::left_mono;
    std::uint64_t quota_bytes=256ULL*1024*1024;
    double transmit_gain=1.0;
    bool exclusive=false;
    std::optional<SimulationSettings> simulation;
    std::optional<ChannelAssumptions> receiver_assumptions;
};
struct Snapshot {
    bool active=false,transmitting=false,listening=false;
    bool physical_complete=false,complete=false,cancelled=false;
    bool encrypted=false,authenticated=false,simulated=false;
    bool loopback_test=false;
    std::string loopback_stage;
    std::uint64_t transmitted_samples=0,captured_samples=0;
    bool decoding_stopped=false;
    std::uint64_t revision=0,source_bytes=0,intervals=0,authenticated_groups=0;
    std::uint64_t checksum_groups=0;
    std::uint64_t corrected_bytes=0,erased_bytes=0;
    std::uint64_t ldpc_frames=0,ldpc_failed_frames=0,ldpc_iterations=0,ldpc_changed_bits=0;
    std::uint64_t coding_cycles=0,failed_cycles=0,verified_bytes=0;
    std::size_t capture_queue_capacity_samples=0,peak_capture_backlog_samples=0,capture_queue_workspace_bytes=0;
    double estimated_seconds=0,transmit_fraction=0;
    double elapsed_seconds=0,goodput_bps=0,evm=0,carrier_error_hz=0,clock_error_ppm=0;
    std::string_view receiver_backend;
    std::optional<std::uint64_t> physical_completion_input_tick;
    std::string status="Fast mode ready",error;
    std::shared_ptr<const ReceivedFile> file;
    std::shared_ptr<const Diagnostics> diagnostics;
    std::optional<SimulationReport> simulation;
};
// Fast owns its local audio and simulation sessions independently of other modems.
class Session {
public:
    Session();
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    void configure(const Settings& settings);
    void transmit(const std::filesystem::path& source);
    // Exact UTF-8 bytes use the same source stream as files, with no type header.
    void transmit_text(const std::string& text);
    void listen();
    // Tests the selected source on the configured device in both directions.
    // Selected waveform impairments are applied before playback; physical
    // loss/noise and selected audio gain replace the simulated link budget.
    // Only exact, physically
    // complete reception is reported and exposed in Snapshot::file.
    void test_loopback_text(const SimulationSettings& channel,const std::string& text);
    void test_loopback_file(const SimulationSettings& channel,const std::filesystem::path& source);
    void cancel();
    Snapshot poll() const;
    void save(const std::filesystem::path& destination) const;
    // Release the session's completed file handle; history may own other copies.
    void clear_received();
    bool active() const;
    void close();
    bool ready_to_close() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

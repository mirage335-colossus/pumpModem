#pragma once
#include "datapump/fast/profile.hpp"
#include "datapump/crypto.hpp"
#include <filesystem>
#include <memory>
#include <optional>

namespace datapump::fast {
class ReceivedFile;
struct Settings {
    Profile profile;
    std::optional<Crypto> key;
    std::string device="default";
    bool mono=true;
    std::uint64_t quota_bytes=256ULL*1024*1024;
};
struct Snapshot {
    bool active=false,transmitting=false,listening=false;
    bool physical_complete=false,complete=false,cancelled=false;
    std::uint64_t revision=0,source_bytes=0,intervals=0,authenticated_groups=0;
    std::uint64_t corrected_bytes=0,erased_bytes=0;
    double elapsed_seconds=0,goodput_bps=0,evm=0,carrier_error_hz=0,clock_error_ppm=0;
    std::string status="Fast mode ready",error;
    std::shared_ptr<const ReceivedFile> file;
};
// No GUI, regular receiver, simulation, or pattern dependencies.
class Session {
public:
    Session();
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    void configure(const Settings& settings);
    void transmit(const std::filesystem::path& source);
    void listen();
    void cancel();
    Snapshot poll() const;
    void save(const std::filesystem::path& destination) const;
    bool active() const;
    void close();
    bool ready_to_close() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

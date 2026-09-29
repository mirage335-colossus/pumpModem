#pragma once
#include "application.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace datapump::gui::web {
inline constexpr std::uint32_t protocol_version=1;
// Transport-independent input. Decoders must bound strings before allocation.
// Identifiers refer only to declarations in the current snapshot, never enums,
// pointers, filesystem paths, HTML, or executable callbacks.
enum class EventKind : std::uint32_t {
    edit=1, select=2, toggle=3, activate=4, preset=5, submit=6,
    record=7, click=8, double_click=9, wheel=10, navigate=11,
    key=12, service=13, close=14
};
struct Event {
    std::uint32_t version=protocol_version;
    std::uint64_t generation=0,sequence=0,target=0;
    EventKind kind=EventKind::activate;
    std::string value,error;
    bool checked=false,cancelled=false,ctrl=false,shift=false,alt=false;
    int amount=0;
    // Set only by the trusted host after it created a private output object.
    // It is not a protocol flag and cannot be requested by renderer input.
    bool track_completion=false;
};
struct Result {bool accepted=false;std::string error;};
// Application and Bridge are confined to the host's application event thread.
// Snapshots own all serialized bytes. Services remain revocable C++ objects;
// snapshots expose their IDs and allowed presentation, never lifetime tokens.
class Bridge {
public:
    explicit Bridge(Application& application);
    ~Bridge();
    Bridge(const Bridge&)=delete;
    Bridge& operator=(const Bridge&)=delete;
    std::string snapshot(int width=1000,int height=760);
    Result accept(const Event& event);
    void reconnect(); // Withdraws every previous event/service generation.
    std::uint64_t generation() const;
    std::uint64_t last_sequence() const;
    // Trusted host file adapter only: verifies a live service before creating
    // or exporting its private backing object. It does not expose file bytes.
    std::optional<ui::ServiceRequest> service(std::uint64_t opaque_id) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

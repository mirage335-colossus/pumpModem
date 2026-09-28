#pragma once
#include "application.hpp"
#include "ui_surface.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace datapump::gui::framebuffer {
// Byte order is explicit, independent of the host CPU's integer endianness.
enum class Format { rgba8888, bgra8888, rgb565le };
struct Surface {
    unsigned width=0,height=0;
    std::size_t stride_bytes=0;
    Format format=Format::rgba8888;
    std::span<std::uint8_t> pixels;
};
struct Frame {
    unsigned width=0,height=0;
    std::size_t stride_bytes=0;
    Format format=Format::rgba8888;
    std::uint64_t revision=0;
    // Damage relative to the immediately preceding returned revision. A host
    // which skipped a revision must copy the entire frame. New size: full damage.
    std::vector<PixelRect> damage;
    std::vector<std::uint8_t> pixels;
};
using FrameHandle=std::shared_ptr<const Frame>;
// The immutable handle owns every byte. It remains valid across future renders,
// resizes and destruction of its renderer; no host callback borrows this memory.
void copy_frame(const Frame&,Surface destination,PixelRect rectangle);
inline void copy_frame(const Frame& frame,Surface destination) {
    copy_frame(frame,destination,{0,0,frame.width,frame.height});
}
struct Config {
    unsigned width=1200,height=1048;
    unsigned font_scale=2;
    bool color=true;
};
class Renderer {
public:
    explicit Renderer(Config={});
    surface::Metrics metrics() const;
    FrameHandle render(const surface::Scene&);
    FrameHandle frame() const {return frame_;}
private:
    Config config_;
    FrameHandle frame_;
};
// Embedded hosts own scheduling, display upload and platform services. No SDL,
// OpenGL, window handle, thread, event loop or controller enters this interface.
// Call tick at least every 40 ms even if display uploads occur less often. Calls
// on one Runtime are serialized by the host; FrameHandles may outlive those calls.
class Runtime {
public:
    explicit Runtime(Launch={},Config={});
    ~Runtime();
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    void resize(unsigned width,unsigned height);
    // Minimal game-engine interface: one call returns one complete image.
    // Unsupported optional desktop services are declined with an in-frame
    // notice, so embedding needs no callbacks to keep the interface usable.
    FrameHandle update(unsigned width,unsigned height,std::span<const surface::Event> events={});
    void input(const surface::Event&);
    bool tick();
    FrameHandle frame() const;
    std::vector<ui::ServiceRequest> take_host_services();
    void complete_host_service(ui::ServiceResult);
    void close();
    bool finished() const;
    int result() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

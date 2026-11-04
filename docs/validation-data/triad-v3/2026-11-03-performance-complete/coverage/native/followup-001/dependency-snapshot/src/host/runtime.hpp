#pragma once
#include "datapump/host/protocol.hpp"
#include <filesystem>
#include <memory>

namespace datapump::host {
// The same local framed interface drives native inherited pipes and a Wasm
// Worker. file_authority is supplied by the embedding host, never a frame bit.
class Runtime {
public:
    explicit Runtime(std::filesystem::path workspace,bool simulation=false);
    ~Runtime();
    void accept(const protocol::Frame&,bool file_authority=false);
    void tick();
    void close();
    bool finished() const;
    std::span<const std::byte> output() const;
    void consume(std::size_t);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

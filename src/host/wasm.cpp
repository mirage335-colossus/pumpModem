#include "runtime.hpp"
#include "datapump/host/protocol.hpp"
#include <emscripten/emscripten.h>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace {
std::unique_ptr<datapump::host::Runtime> runtime;
datapump::host::protocol::Decoder decoder;
bool failed=false;
template<class Function>int checked(Function function) {
    if(failed)return -1;
    try{function();return 0;}catch(const std::exception& e){std::fprintf(stderr,"Data Pump Wasm: %s\n",e.what());failed=true;if(runtime)runtime->close();return -1;}
}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int datapump_web_create() {return checked([]{
    if(runtime)throw std::runtime_error("only one application per Wasm Worker is allowed");
    const std::filesystem::path workspace="/datapump-private";std::filesystem::create_directory(workspace);
    runtime=std::make_unique<datapump::host::Runtime>(workspace);
});}
EMSCRIPTEN_KEEPALIVE int datapump_web_feed(const unsigned char* bytes,unsigned size) {return checked([&]{
    if(!runtime||!bytes||!size)throw std::runtime_error("invalid Wasm input");
    for(auto& frame:decoder.feed({reinterpret_cast<const std::byte*>(bytes),size}))runtime->accept(frame,true);
});}
EMSCRIPTEN_KEEPALIVE int datapump_web_tick(){return checked([]{if(runtime)runtime->tick();});}
EMSCRIPTEN_KEEPALIVE const unsigned char* datapump_web_output(){if(!runtime)return nullptr;return reinterpret_cast<const unsigned char*>(runtime->output().data());}
EMSCRIPTEN_KEEPALIVE unsigned datapump_web_output_size(){return runtime?static_cast<unsigned>(runtime->output().size()):0;}
EMSCRIPTEN_KEEPALIVE int datapump_web_consume(unsigned size){return checked([&]{if(runtime)runtime->consume(size);});}
EMSCRIPTEN_KEEPALIVE void datapump_web_destroy(){if(runtime)runtime->close();runtime.reset();}
}

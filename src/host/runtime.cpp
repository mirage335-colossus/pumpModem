#include "runtime.hpp"
#include "datapump/host/audio.hpp"
#include "datapump/execution.hpp"
#include "web_bridge.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <deque>
#include <fstream>
#include <map>

namespace datapump::host {
namespace {
using protocol::Frame;using protocol::Type;using protocol::Reader;using protocol::Writer;
constexpr std::uint64_t max_file_bytes=256ULL*1024*1024,max_workspace_bytes=1024ULL*1024*1024;
double epoch_now(){return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();}
gui::web::Event read_event(Reader& in) {
    gui::web::Event e;e.version=in.u32();e.generation=in.u64();e.sequence=in.u64();e.target=in.u64();
    e.kind=static_cast<gui::web::EventKind>(in.u32());const auto flags=in.u32();
    if(flags&~31U)throw Error("unknown host event flags");
    e.checked=flags&1;e.cancelled=flags&2;e.ctrl=flags&4;e.shift=flags&8;e.alt=flags&16;
    e.amount=std::bit_cast<std::int32_t>(in.u32());e.value=in.text();e.error=in.text(4096);return e;
}
std::string filename(std::string value) {
    // A name is presentation metadata, never a host path. Preserve ordinary
    // UTF-8 names while forbidding separators, controls and reserved dot names.
    if(value.empty() || value.size()>240 || value=="." || value=="..")throw Error("invalid file name");
    for(unsigned char c:value)if(c<32||c==127||c=='/'||c=='\\'||c==':')throw Error("file name must not contain a path");
    return value;
}
std::string path_text(const std::filesystem::path& path) {const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
}
struct Runtime::Impl {
    struct Output {Type type;std::vector<std::byte> bytes;std::size_t offset=0;};
    struct Upload {gui::web::Event event;std::filesystem::path path;std::ofstream file;std::uint64_t size=0,written=0;};
    struct Save {std::uint64_t target;std::filesystem::path path;std::string name;};
    struct Download {Save save;std::ifstream file;std::uint64_t size=0,sent=0;};
    gui::Application application;
    gui::web::Bridge bridge;
    std::filesystem::path workspace;
    std::deque<Output> queue;
    std::optional<Upload> upload;
    std::map<std::uint64_t,Save> saves;
    std::deque<Download> downloads;
    std::uint64_t object=0,workspace_bytes=0;
    std::size_t queued_bytes=0;
    int width=1000,height=760;
    bool force_snapshot=true,closed_sent=false;
    explicit Impl(std::filesystem::path path,bool simulation):application([&]{gui::Launch launch;launch.simulation=simulation;return launch;}()),bridge(application),workspace(std::move(path)) {application.start();}
    void send(Type type,Writer body={}) {send_frame({type,std::move(body.bytes)});}
    void send_frame(Frame frame) {
        // Coalesce only complete unsent snapshots. Never remove an audio packet
        // or splice a snapshot that a pipe reader has already partly consumed.
        if(frame.type==Type::snapshot)for(auto i=queue.begin();i!=queue.end();) {
            if(i->type==Type::snapshot && !i->offset){queued_bytes-=i->bytes.size();i=queue.erase(i);}else ++i;
        }
        auto bytes=protocol::encode(frame);
        if(bytes.size()>2*protocol::max_frame-queued_bytes)throw Error("host stopped consuming bounded output");
        queued_bytes+=bytes.size();queue.push_back({frame.type,std::move(bytes),0});
    }
    void error(const std::string& value) {application.report_error(value);Writer out;out.text(value);send(Type::error,std::move(out));force_snapshot=true;}
    void file_end(std::uint64_t target,const std::string& error) {Writer out;out.u64(target);out.text(error);send(Type::file_end,std::move(out));}
    std::optional<gui::ui::ServiceRequest> service(const gui::web::Event& event,gui::ui::ServiceKind kind) {
        if(event.kind!=gui::web::EventKind::service || event.version!=1 || event.generation!=bridge.generation() || event.sequence<=bridge.last_sequence())throw Error("stale file service event");
        auto request=bridge.service(event.target);
        if(!request || request->kind!=kind || (request->valid && !*request->valid))throw Error("file service is no longer available");
        return request;
    }
    std::filesystem::path new_path(const std::string& name) {
        if(object>=4096)throw Error("worker file object quota exhausted");
        const auto directory=workspace/("object-"+std::to_string(++object));
        if(!std::filesystem::create_directory(directory))throw Error("private file object already exists");
        const auto safe=filename(name);
        return directory/std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(safe.data()),safe.size()));
    }
    void accept_event(gui::web::Event event,bool file_authority) {
        if(event.kind==gui::web::EventKind::service && !event.cancelled && event.error.empty()) {
            const auto request=bridge.service(event.target);
            if(request && (request->kind==gui::ui::ServiceKind::open_file || request->kind==gui::ui::ServiceKind::save_file || request->kind==gui::ui::ServiceKind::open_folder) && !file_authority)
                throw Error("file services require an explicit host file capability");
        }
        const auto result=bridge.accept(event);if(!result.accepted)throw Error(result.error);force_snapshot=true;
        if(event.kind==gui::web::EventKind::service && event.cancelled && upload && upload->event.target==event.target)upload.reset();
    }
    void completed_files() {
        for(auto& completion:application.take_service_completions()) {
            const auto found=saves.find(completion.id);if(found==saves.end())continue;
            Save save=std::move(found->second);saves.erase(found);
            if(!completion.error.empty()){file_end(save.target,completion.error);continue;}
            const auto target=save.target;
            try {
                const auto size=std::filesystem::file_size(save.path);
                if(size>max_file_bytes || size>max_workspace_bytes-workspace_bytes)throw Error("output file exceeds worker quota");
                workspace_bytes+=size;Download download{std::move(save),{},size,0};download.file.open(download.save.path,std::ios::binary);
                if(!download.file)throw Error("cannot open completed output file");
                Writer out;out.u64(download.save.target);out.text(download.save.name);out.u64(size);send(Type::file_begin,std::move(out));downloads.push_back(std::move(download));
            }catch(const std::exception& error){file_end(target,error.what());}
        }
        // A bounded chunk per tick leaves capture, playback and UI polling
        // available during downloads. Completion is after bytes, not file size.
        if(!downloads.empty() && queued_bytes<256*1024) {
            auto& item=downloads.front();const auto n=std::min<std::uint64_t>(65536,item.size-item.sent);
            if(n) {
                Writer out;out.u64(item.save.target);out.u64(item.sent);out.u32(static_cast<std::uint32_t>(n));const auto at=out.bytes.size();out.bytes.resize(at+static_cast<std::size_t>(n));
                item.file.read(reinterpret_cast<char*>(out.bytes.data()+at),static_cast<std::streamsize>(n));if(item.file.gcount()!=static_cast<std::streamsize>(n)){file_end(item.save.target,"completed output file changed during export");downloads.pop_front();return;}
                item.sent+=n;send(Type::file_chunk,std::move(out));
            } else {Writer out;out.u64(item.save.target);out.text("");send(Type::file_end,std::move(out));downloads.pop_front();}
        }
    }
};
Runtime::Runtime(std::filesystem::path workspace,bool simulation):impl_(std::make_unique<Impl>(std::move(workspace),simulation)){}
Runtime::~Runtime(){audio_endpoint().close();impl_->application.close();}
void Runtime::accept(const Frame& frame,bool file_authority) {
    auto& p=*impl_;Reader in(frame.payload);
    try {
        switch(frame.type) {
        case Type::viewport:{const auto width=in.u32(),height=in.u32();in.end();if(width<240||width>4096||height<240||height>4096)throw Error("invalid viewport");p.width=static_cast<int>(width);p.height=static_cast<int>(height);p.force_snapshot=true;break;}
        case Type::event:{auto event=read_event(in);in.end();p.accept_event(std::move(event),file_authority);break;}
        case Type::audio_configure:{const auto generation=in.u64();const auto rate=in.u32();in.end();audio_endpoint().configure(generation,rate);break;}
        case Type::audio_capture:{const auto generation=in.u64(),stream=in.u64(),position=in.u64();const auto count=in.u32();if(!count||count>AudioEndpoint::max_packet_frames)throw Error("invalid capture block");std::vector<float> samples(count);for(auto& x:samples)x=in.f32();in.end();audio_endpoint().capture_samples(generation,stream,position,samples);break;}
        case Type::audio_ready:{const auto generation=in.u64(),stream=in.u64();in.end();audio_endpoint().playback_ready(generation,stream);break;}
        case Type::audio_progress:{const auto generation=in.u64(),stream=in.u64(),position=in.u64();const auto drained=in.u32();in.end();if(drained>1)throw Error("invalid audio drain flag");audio_endpoint().playback_progress(generation,stream,position,drained!=0);break;}
        case Type::audio_error:{const auto generation=in.u64();const auto error=in.text(512);in.end();audio_endpoint().interrupted(generation,error);break;}
        case Type::audio_cancelled:{const auto generation=in.u64(),stream=in.u64();in.end();audio_endpoint().playback_cancelled(generation,stream);break;}
        case Type::reconnect:in.end();p.upload.reset();for(const auto& [id,save]:p.saves){(void)id;p.file_end(save.target,"File export cancelled by reconnection");}p.saves.clear();for(const auto& download:p.downloads)p.file_end(download.save.target,"File export cancelled by reconnection");p.downloads.clear();p.bridge.reconnect();p.force_snapshot=true;break;
        case Type::clock_ping:{const auto nonce=in.u64();const auto time=in.f64();in.end();if(!std::isfinite(time))throw Error("invalid clock probe");Writer out;out.u64(nonce);out.f64(time);out.f64(epoch_now());out.f64(epoch_now());p.send(Type::clock_reply,std::move(out));break;}
        case Type::upload_begin:{
            if(!file_authority)throw Error("file import requires the host file channel");
            if(p.upload)throw Error("a file import is already active");
            auto event=read_event(in);const auto size=in.u64();in.end();p.service(event,gui::ui::ServiceKind::open_file);
            if(size>max_file_bytes || size>max_workspace_bytes-p.workspace_bytes)throw Error("file import exceeds worker quota");
            Impl::Upload upload;upload.path=p.new_path(event.value);upload.event=std::move(event);upload.size=size;upload.file.open(upload.path,std::ios::binary|std::ios::out);
            if(!upload.file)throw Error("cannot create private import file");
            p.workspace_bytes+=size;p.upload=std::move(upload);break;
        }
        case Type::upload_chunk:{
            if(!file_authority || !p.upload)throw Error("no active file import");
            auto& u=*p.upload;
            const auto target=in.u64(),position=in.u64();const auto count=in.u32();p.service(u.event,gui::ui::ServiceKind::open_file);
            if(target!=u.event.target||position!=u.written||!count||count>65536||count>u.size-u.written)throw Error("invalid file import chunk");
            // Bytes are opaque source data. They are never interpreted as UI text.
            const auto prefix=20U;if(frame.payload.size()!=prefix+count)throw Error("invalid file import chunk length");
            u.file.write(reinterpret_cast<const char*>(frame.payload.data()+prefix),count);if(!u.file)throw Error("file import write failed");u.written+=count;break;
        }
        case Type::upload_commit:{
            if(!file_authority || !p.upload)throw Error("no active file import");
            const auto target=in.u64();in.end();auto& u=*p.upload;p.service(u.event,gui::ui::ServiceKind::open_file);
            if(target!=u.event.target||u.written!=u.size)throw Error("incomplete file import");
            u.file.close();if(u.file.fail())throw Error("file import close failed");
            auto event=u.event;event.value=path_text(u.path);p.accept_event(std::move(event),true);p.upload.reset();break;
        }
        case Type::download_prepare:{
            if(!file_authority)throw Error("file export requires the host file channel");
            auto event=read_event(in);in.end();const auto request=p.service(event,gui::ui::ServiceKind::save_file);
            const auto name=filename(event.value);const auto path=p.new_path(name);const auto id=request->id;
            event.value=path_text(path);event.track_completion=true;p.saves.emplace(id,Impl::Save{event.target,path,name});
            try{p.accept_event(std::move(event),true);}catch(...){p.saves.erase(id);throw;}break;
        }
        default:throw Error("unsupported local host frame type");
        }
    } catch(const std::exception& error) {
        if(frame.type==Type::download_prepare && frame.payload.size()>=28) {
            Reader failed(frame.payload);(void)failed.u32();(void)failed.u64();(void)failed.u64();const auto target=failed.u64();p.file_end(target,error.what());
        }
        if(frame.type==Type::upload_begin || frame.type==Type::upload_chunk || frame.type==Type::upload_commit)p.upload.reset();
        p.error(error.what());
    }
}
void Runtime::tick() {
    auto& p=*impl_;execution::pump(std::chrono::milliseconds(4));const bool changed=p.application.tick();
    for(const auto& event:audio_endpoint().take_events()) {
        Writer out;out.u32(static_cast<std::uint32_t>(event.kind));out.u64(event.generation);out.u64(event.stream);out.u64(event.position);out.u32(event.rate);out.u32(static_cast<std::uint32_t>(event.channels));out.f64(event.gain);out.f64(event.presentation_epoch);out.u32(static_cast<std::uint32_t>(event.samples.size()));for(float sample:event.samples)out.f32(sample);p.send(Type::audio,std::move(out));
    }
    p.completed_files();
    if(changed||p.force_snapshot) {const auto value=p.bridge.snapshot(p.width,p.height);const auto bytes=std::as_bytes(std::span(value));p.send_frame({Type::snapshot,{bytes.begin(),bytes.end()}});p.force_snapshot=false;}
    if(p.application.closing()&&p.application.finished()&&!p.closed_sent){Writer out;out.u32(static_cast<std::uint32_t>(p.application.result()));p.send(Type::closed,std::move(out));p.closed_sent=true;}
}
void Runtime::close(){audio_endpoint().close();impl_->application.close();}
bool Runtime::finished() const{return impl_->closed_sent&&impl_->queue.empty();}
std::span<const std::byte> Runtime::output() const {const auto& p=*impl_;if(p.queue.empty())return {};const auto& out=p.queue.front();return std::span(out.bytes).subspan(out.offset);}
void Runtime::consume(std::size_t count) {auto& p=*impl_;if(p.queue.empty()||count>p.queue.front().bytes.size()-p.queue.front().offset)throw Error("invalid host output consumption");p.queue.front().offset+=count;p.queued_bytes-=count;if(p.queue.front().offset==p.queue.front().bytes.size())p.queue.pop_front();}
}

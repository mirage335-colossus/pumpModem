#include "datapump/fast/file_transfer.hpp"
#include "datapump/fast/codec.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stop_token>

using namespace datapump;
using namespace datapump::fast;
namespace {
void check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F action,const char* why) {
    try{action();}catch(const Error&){return;}
    throw std::runtime_error(why);
}
struct Directory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/(
        "datapump-fast-files-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory(){std::filesystem::create_directory(path);}
    ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
void write(const std::filesystem::path& path,const Bytes& content) {
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(content.data()),static_cast<std::streamsize>(content.size()));
    if(!file)throw std::runtime_error("Fixture write failed");
}
void put32(std::fstream& file,std::streamoff offset,std::uint32_t value) {
    std::array<char,4> bytes{};for(unsigned i=0;i<4;++i)bytes[i]=static_cast<char>(value>>(i*8));
    file.seekp(offset);file.write(bytes.data(),bytes.size());
}
void test_files() {
    Directory directory;Settings settings;settings.profile=profile(Channel::wire);settings.profile.constellation=4;
    settings.profile.interleave_depth=1;settings.key=Crypto(Bytes(32,4));settings.quota_bytes=1024*1024;
    const auto source=directory.path/"source.bin",wave=directory.path/"transfer.wav",destination=directory.path/"received.bin";
    const Bytes content{0,255,0x80,0,1,3,5,7,0,0};write(source,content);
    bool tx_progress=false,rx_progress=false;
    const auto tx=transmit_wave(settings,source,wave,[&](const Snapshot& s) {
        tx_progress=true;check(!s.complete && !s.file,"TX never claims receiver success");
    });
    check(tx.source_bytes==content.size() && tx_progress,"streamed WAV TX source counters");
    const auto rx=receive_wave(settings,wave,[&](const Snapshot& s) {
        rx_progress=true;check(!s.complete && !s.file,"WAV receive progress remains pending until physical completion");
    });
    if(!rx.complete)throw std::runtime_error("S16 WAV loopback incomplete: "+rx.status);
    check(rx.physical_complete && rx.file && Bytes(rx.file->bytes().begin(),rx.file->bytes().end())==content && rx_progress,"streamed S16 WAV exact roundtrip");
    rx.file->save(destination);check(std::filesystem::file_size(destination)==content.size(),"explicit save source size");
    rejects([&]{rx.file->save(destination);},"save must exclusively create destination");
    rejects([&]{transmit_wave(settings,source,wave);},"WAV generation must exclusively create destination");
    auto wrong=settings;wrong.key=Crypto(Bytes(32,5));const auto bad=receive_wave(wrong,wave);
    check(!bad.complete && !bad.file,"wrong key cannot release received WAV source");

    // The local WAV length remains valid after removing actual silence. Only
    // observed symbol absence completes; a clean container EOF is insufficient.
    const auto no_tail=directory.path/"without-tail.wav";std::filesystem::copy_file(wave,no_tail);
    const auto old_size=std::filesystem::file_size(no_tail);
    const auto removed=static_cast<std::uint64_t>(settings.profile.sample_rate)*6*2;
    check(old_size>removed+44,"tail fixture size");const auto new_size=old_size-removed;
    std::filesystem::resize_file(no_tail,new_size);
    {std::fstream file(no_tail,std::ios::in|std::ios::out|std::ios::binary);put32(file,4,static_cast<std::uint32_t>(new_size-8));put32(file,40,static_cast<std::uint32_t>(new_size-44));}
    const auto pending=receive_wave(settings,no_tail);check(!pending.physical_complete && !pending.complete && !pending.file,"EOF without six-second absence cannot release file");

    const auto malformed=directory.path/"malformed.wav";write(malformed,Bytes{1,2,3});
    rejects([&]{receive_wave(settings,malformed);},"short local WAV rejected");
    std::filesystem::copy_file(wave,malformed,std::filesystem::copy_options::overwrite_existing);
    {std::fstream file(malformed,std::ios::in|std::ios::out|std::ios::binary);put32(file,40,0xfffffff0U);}
    rejects([&]{receive_wave(settings,malformed);},"oversized local WAV chunk rejected before allocation");

    const auto large=directory.path/"large.bin",quota_wave=directory.path/"quota.wav";write(large,Bytes(65537,9));
    auto limited=settings;limited.quota_bytes=65536;
    rejects([&]{transmit_wave(limited,large,quota_wave);},"source quota enforced during bounded file reads");
    check(!std::filesystem::exists(quota_wave),"failed TX removes its incomplete output");
    std::stop_source cancellation;cancellation.request_stop();
    const auto cancelled_wave=directory.path/"cancelled.wav";
    rejects([&]{transmit_wave(settings,source,cancelled_wave,{},cancellation.get_token());},"cancelled TX incomplete");
    check(!std::filesystem::exists(cancelled_wave),"cancelled TX removes incomplete output");
    const auto cancelled_rx=receive_wave(settings,wave,{},cancellation.get_token());
    check(cancelled_rx.cancelled && !cancelled_rx.complete && !cancelled_rx.file,"cancelled RX cannot manufacture completion");
}
void text_wave_roundtrips() {
    Directory directory;Settings settings;settings.profile=profile(Channel::wire);
    settings.profile.interleave_depth=1;settings.quota_bytes=1024*1024;
    const std::string text=std::string("Fast text: caf\xc3\xa9\nline two")+'\0'+std::string(" tail\0",6);
    const Bytes expected(text.begin(),text.end());
    for(bool encrypted:{false,true}) {
        settings.key=encrypted?std::optional<Crypto>(Crypto(Bytes(32,4))):std::nullopt;
        const auto prefix=encrypted?"encrypted":"public";
        const auto wave=directory.path/(std::string(prefix)+"-text.wav");
        const auto tx=transmit_text_wave(settings,text,wave);
        check(tx.source_bytes==expected.size() && tx.encrypted==encrypted && !tx.authenticated && !tx.complete,
              "text TX counts bytes without claiming RX authentication/completion");
        const auto rx=receive_wave(settings,wave,[&](const Snapshot& progress) {
            check(progress.encrypted==encrypted && !progress.authenticated && !progress.complete && !progress.file,
                  "text progress retains chosen protection and withholds source");
        });
        check(rx.complete && rx.physical_complete && rx.file && Bytes(rx.file->bytes().begin(),rx.file->bytes().end())==expected,
              "text S16 WAV preserves UTF-8, newline and embedded zero bytes");
        check(rx.encrypted==encrypted && rx.authenticated==encrypted,
              "public text never claims authentication");
        check(encrypted?(rx.authenticated_groups>0 && rx.checksum_groups==0):
                        (rx.authenticated_groups==0 && rx.checksum_groups>0),
              "public checksum counts remain distinct from authenticated groups");
        auto opposite=settings;
        opposite.key=encrypted?std::nullopt:std::optional<Crypto>(Crypto(Bytes(32,4)));
        const auto mismatch=receive_wave(opposite,wave);
        check(!mismatch.complete && !mismatch.file && !mismatch.authenticated,
              "opposite locally selected encryption mode must fail without fallback");
        const auto source=directory.path/(std::string(prefix)+"-source.bin");write(source,expected);
        const auto file_wave=directory.path/(std::string(prefix)+"-file.wav");
        transmit_wave(settings,source,file_wave);
        const auto file_rx=receive_wave(settings,file_wave);
        check(file_rx.complete && file_rx.file && Bytes(file_rx.file->bytes().begin(),file_rx.file->bytes().end())==expected,
              "file and text use identical source interpretation");
        const auto empty_wave=directory.path/(std::string(prefix)+"-empty.wav");
        transmit_text_wave(settings,"",empty_wave);
        const auto empty=receive_wave(settings,empty_wave);
        check(empty.complete && empty.file && empty.source_bytes==0 && Bytes(empty.file->bytes().begin(),empty.file->bytes().end()).empty(),
              "empty text preserves exact endpoint in both protection modes");
        const auto oversize_wave=directory.path/(std::string(prefix)+"-oversize.wav");
        rejects([&]{transmit_text_wave(settings,std::string(text_byte_limit+1,'x'),oversize_wave);},
                "text local size limit rejected");
        check(!std::filesystem::exists(oversize_wave),"oversize text rejected before creating WAV");
    }
}
}
void dense_s16_file() {
    Directory directory;
    Settings settings;settings.profile=profile(Channel::wire);
    settings.profile.constellation=256;settings.profile.code_rate=CodeRate::seven_eighths;
    settings.key=Crypto(Bytes(32,7));settings.quota_bytes=1024*1024;
    const auto source=directory.path/"dense.bin",wave=directory.path/"dense.wav";
    Bytes content(32768);
    for(std::size_t i=0;i<content.size();++i)content[i]=static_cast<std::uint8_t>(i);
    write(source,content);
    transmit_wave(settings,source,wave);
    const auto received=receive_wave(settings,wave);
    check(received.complete&&received.physical_complete&&received.file,
          "256-APSK must survive the production 16-bit PCM path");
    const auto destination=directory.path/"dense-received.bin";
    received.file->save(destination);
    std::ifstream input(destination,std::ios::binary);
    Bytes actual(content.size());input.read(reinterpret_cast<char*>(actual.data()),static_cast<std::streamsize>(actual.size()));
    check(input.gcount()==static_cast<std::streamsize>(content.size())&&actual==content&&input.peek()==std::char_traits<char>::eof(),
          "dense S16 transfer must preserve every byte and its exact endpoint");
}
int main() {
    try{test_files();text_wave_roundtrips();dense_s16_file();std::cout<<"fast bounded local WAV/file tests passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

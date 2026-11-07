#include "datapump/audio_entropy.hpp"
#include "datapump/crypto.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace datapump;
namespace {
void check(bool condition,const char* message) {
    if (!condition) throw Error(message);
}
struct Files {
    std::filesystem::path directory;
    Files() {
        auto pattern=(std::filesystem::temp_directory_path()/"datapump-audio-e2e-XXXXXX").string();
        char* made=::mkdtemp(pattern.data());
        if (!made) throw Error("could not create audio end-to-end temporary directory");
        directory=made;
    }
    ~Files() { std::error_code ignored; std::filesystem::remove_all(directory,ignored); }
};
void generate_and_reload(const Files& files,const char* mode,bool expect_audio) {
    KeyfileGenerationOptions options;
    options.audio=audio::keyfile_audio_source(mode);
    options.allow_audio_fallback=true;
    const auto path=files.directory/mode;
    const auto start=std::chrono::steady_clock::now();
    const auto result=create_keyring(path,{"Native process"},std::nullopt,options);
    check(result.audio_used==expect_audio,"incorrect audio-used status from process capture");
    check(expect_audio ? result.warning.empty() : !result.warning.empty(),
          "incorrect warning from process capture");
    if (!expect_audio) check(result.warning.find("audio entropy input is silent or constant")!=std::string::npos,
                             "silence case failed for a reason other than unusable input");
    if (expect_audio) check(std::chrono::steady_clock::now()-start>=std::chrono::seconds(4),
                            "paced collection ended before discard and sample interval");
    const auto loaded=load_keyring(path);
    check(loaded.size()==1 && loaded[0].name=="Native process", "generated keyring did not reload");
}
}
int main() {
    try {
        Files files;
        generate_and_reload(files,"e2e-varying",true);
        generate_and_reload(files,"e2e-silent",false);
        std::cout << "native process keyfile audio end-to-end tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

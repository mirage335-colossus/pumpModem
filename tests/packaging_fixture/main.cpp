#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __linux__
#include <dlfcn.h>
#endif
extern "C" int middle();
int main(int argc, char** argv) {
#ifdef __linux__
    void* audio=dlopen("libasound.so.2",RTLD_NOW|RTLD_LOCAL);
    if(!audio) {std::fputs("Packaged ALSA SONAME cannot be loaded\n",stderr);return 1;}
    const auto fixture=reinterpret_cast<int (*)()>(dlsym(audio,"datapump_packaging_audio_fixture"));
    const bool valid=fixture&&fixture()==0;
    dlclose(audio);
    if(!valid) {std::fputs("Audio resolved outside the packaged fixture\n",stderr);return 1;}
#endif
    for(int i=1;i<argc;++i) if(std::strcmp(argv[i],"--smoke-test")==0) {
        std::puts("WARNING REV_REPLAY_CADENCE: fixture stdout warning");
        std::fputs("WARNING REV_REPLAY_CADENCE: fixture stderr warning\n",stderr);
        if(std::getenv("DATAPUMP_PACKAGING_SMOKE_FAIL")) return 1;
    }
    std::puts("{\"validated\":true}");
    return middle();
}

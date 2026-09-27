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
        const auto mode=std::getenv("DATAPUMP_PACKAGING_SMOKE_MODE");
        if(mode&&*mode) {
            const auto path=std::getenv("PATH");
            const auto home=std::getenv("PYTHONHOME");
            const auto python=std::getenv("PYTHONPATH");
            if(!path||*path||!home||std::strcmp(home,"/nonexistent")||
                !python||std::strcmp(python,"/nonexistent")) {
                std::fputs("GUI runtime environment was not isolated\n",stderr);return 1;
            }
            if(std::strcmp(mode,"assertion")==0) {
                std::fputs("Shared GUI smoke assertion failed\n",stderr);return 1;
            }
            if(std::strcmp(mode,"sanitizer")==0)
                std::fputs("AddressSanitizer: fixture failure\n",stderr);
            const char* age=std::strcmp(mode,"stale")==0?"31.000000":"0.000007";
            std::fprintf(stderr,"INCOMPLETE GUI_SMOKE_BUDGET: phase=17 elapsed=600.327362 "
                "budget=600.000000 tx_id=14 fraction=0.766936 media_seconds=103.311389 "
                "samples=6418106 tail=0 progress_age=%s result=incomplete\n",age);
            return 75;
        }
        std::puts("WARNING REV_REPLAY_CADENCE: fixture stdout warning");
        std::fputs("WARNING REV_REPLAY_CADENCE: fixture stderr warning\n",stderr);
        if(std::getenv("DATAPUMP_PACKAGING_SMOKE_FAIL")) return 1;
    }
    std::puts("{\"validated\":true}");
    return middle();
}

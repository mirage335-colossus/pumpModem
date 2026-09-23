#include <cstdio>
#ifdef __linux__
#include <dlfcn.h>
#endif
extern "C" int middle();
int main() {
#ifdef __linux__
    void* audio=dlopen("libasound.so.2",RTLD_NOW|RTLD_LOCAL);
    if(!audio) {std::fputs("Packaged ALSA SONAME cannot be loaded\n",stderr);return 1;}
    const auto fixture=reinterpret_cast<int (*)()>(dlsym(audio,"datapump_packaging_audio_fixture"));
    const bool valid=fixture&&fixture()==0;
    dlclose(audio);
    if(!valid) {std::fputs("Audio resolved outside the packaged fixture\n",stderr);return 1;}
#endif
    std::puts("{\"validated\":true}");
    return middle();
}

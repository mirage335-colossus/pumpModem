/* Optional offline fixture recorder. Original test harness; no FLDigi source.
 * Interpose only libpulse-simple's audio boundary. The installed application
 * retains its own unmodified modulation, varicode, timing, and text handling. */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct sample_spec {int format;uint32_t rate;uint8_t channels;};
struct fake_stream {struct sample_spec spec;};

void* pa_simple_new(const char* server,const char* name,int direction,
                   const char* device,const char* description,
                   const struct sample_spec* spec,const void* map,
                   const void* attributes,int* error) {
    (void)server;(void)name;(void)direction;(void)device;
    (void)description;(void)map;(void)attributes;
    if(error)*error=0;
    /* PA_SAMPLE_FLOAT32LE, the format used by FLDigi's PulseAudio adapter. */
    if(!spec||spec->format!=5||spec->rate!=8000||!spec->channels) {
        if(error)*error=3;
        return NULL;
    }
    struct fake_stream* stream=calloc(1,sizeof(*stream));
    if(stream)stream->spec=*spec;
    return stream;
}

int pa_simple_read(void* handle,void* buffer,size_t bytes,int* error) {
    const struct fake_stream* stream=handle;
    memset(buffer,0,bytes);
    usleep(bytes/sizeof(float)/stream->spec.channels*1000000/stream->spec.rate);
    if(error)*error=0;
    return 0;
}

int pa_simple_write(void* handle,const void* buffer,size_t bytes,int* error) {
    const struct fake_stream* stream=handle;
    const float* input=buffer;
    const size_t frames=bytes/sizeof(float)/stream->spec.channels;
    const char* path=getenv("LEGACY_PSK_REFERENCE_OUTPUT");
    FILE* output=path?fopen(path,"ab"):NULL;
    if(!output) {if(error)*error=1;return -1;}
    for(size_t i=0;i<frames;++i)
        if(fwrite(input+i*stream->spec.channels,sizeof(float),1,output)!=1) {
            fclose(output);if(error)*error=1;return -1;
        }
    fclose(output);
    usleep(frames*1000000/stream->spec.rate);
    if(error)*error=0;
    return 0;
}
int pa_simple_flush(void* stream,int* error) {(void)stream;if(error)*error=0;return 0;}
int pa_simple_drain(void* stream,int* error) {(void)stream;if(error)*error=0;return 0;}
void pa_simple_free(void* stream) {free(stream);}
uint64_t pa_simple_get_latency(void* stream,int* error) {(void)stream;if(error)*error=0;return 0;}

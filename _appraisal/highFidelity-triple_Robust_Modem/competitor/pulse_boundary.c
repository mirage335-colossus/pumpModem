/* Appraisal-only PulseAudio boundary for the unmodified installed fldigi.
 * No modem algorithms or time resampling: every 8-kHz frame reaches fldigi
 * once and in order. Wall-clock pacing is independently selectable. */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

struct sample_spec { int format; uint32_t rate; uint8_t channels; };
struct fake_stream {
    struct sample_spec spec; FILE* input;
    uint64_t frames, padding, requested_padding, alignment_padding, file_frames, block_frames;
    int done, eof;
};

static void finish(struct fake_stream* stream,const char* status) {
    stream->done=1;
    const char* path=getenv("HF_COMPETITOR_DONE");
    char temporary[4096];
    const int length=path?snprintf(temporary,sizeof(temporary),"%s.tmp",path):-1;
    FILE* report=length>0&&(size_t)length<sizeof(temporary)?fopen(temporary,"w"):NULL;
    if(report) {
        fprintf(report,"{\"status\":\"%s\",\"delivered_frames\":%llu,\"source_frames\":%llu,"
                "\"requested_zero_padding_frames\":%llu,\"alignment_zero_padding_frames\":%llu,"
                "\"audio_block_frames\":%llu,\"post_completion_frames\":0}\n",status,
                (unsigned long long)stream->frames,(unsigned long long)stream->file_frames,
                (unsigned long long)stream->requested_padding,(unsigned long long)stream->alignment_padding,
                (unsigned long long)stream->block_frames);
        fclose(report);
        rename(temporary,path);
    }
    /* XMLRPC lives on another thread. EOF stops this sample clock instead of
     * supplying an unbounded noiseless tail while the UI reads decoded text. */
    for(;;)pause();
}

static void pace(size_t frames, uint32_t rate) {
    const char* text=getenv("HF_COMPETITOR_PACE");
    double speed=text?strtod(text,NULL):1;
    if(speed<1||speed>16)speed=1;
    usleep((useconds_t)(frames*1000000./rate/speed));
}
void* pa_simple_new(const char* server,const char* name,int direction,
                   const char* device,const char* description,
                   const struct sample_spec* spec,const void* map,
                   const void* attributes,int* error) {
    (void)server;(void)name;(void)direction;(void)device;(void)description;(void)map;(void)attributes;
    if(error)*error=0;
    if(!spec||spec->format!=5||spec->rate!=8000||!spec->channels) {if(error)*error=3;return NULL;}
    struct fake_stream* stream=calloc(1,sizeof(*stream));
    if(stream) {
        stream->spec=*spec;
        const char* pad=getenv("HF_COMPETITOR_EOF_PAD_FRAMES");
        stream->padding=pad?strtoull(pad,NULL,10):0;
        if(stream->padding>60*8000)stream->padding=0;
        stream->requested_padding=stream->padding;
    }
    return stream;
}
int pa_simple_read(void* handle,void* buffer,size_t bytes,int* error) {
    struct fake_stream* stream=handle;
    float* values=buffer;
    const size_t frames=bytes/sizeof(float)/stream->spec.channels;
    const char* path=getenv("HF_COMPETITOR_INPUT");
    if(!stream->input&&!stream->done&&path) {
        stream->input=fopen(path,"rb");
        if(stream->input) {
            struct stat st;
            if(fstat(fileno(stream->input),&st)!=0||st.st_size<0||st.st_size%sizeof(float)!=0)
                finish(stream,"invalid_file");
            stream->file_frames=(uint64_t)st.st_size/sizeof(float);
            stream->block_frames=frames;
            stream->alignment_padding=(frames-(stream->file_frames+stream->padding)%frames)%frames;
            stream->padding+=stream->alignment_padding;
        }
    }
    memset(buffer,0,bytes);
    if(stream->input) {
        size_t received=0;
        for(size_t i=0;i<frames;++i) {
            float x=0;
            if(stream->eof || fread(&x,sizeof(x),1,stream->input)!=1) {
                stream->eof=1;
                if(!stream->padding)break;
                --stream->padding;
            }
            for(unsigned channel=0;channel<stream->spec.channels;++channel)values[i*stream->spec.channels+channel]=x;
            ++received;
        }
        if(received==0)finish(stream,"completed");
        if(received!=frames)finish(stream,"partial_block_rejected");
        stream->frames+=received;
    }
    pace(frames,stream->spec.rate);
    if(error)*error=0;
    return 0;
}
int pa_simple_write(void* handle,const void* buffer,size_t bytes,int* error) {
    const struct fake_stream* stream=handle;const float* input=buffer;
    const size_t frames=bytes/sizeof(float)/stream->spec.channels;
    const char* path=getenv("HF_COMPETITOR_OUTPUT");FILE* output=path?fopen(path,"ab"):NULL;
    if(!output){if(error)*error=1;return -1;}
    for(size_t i=0;i<frames;++i)if(fwrite(input+i*stream->spec.channels,sizeof(float),1,output)!=1) {
        fclose(output);if(error)*error=1;return -1;
    }
    fclose(output);pace(frames,stream->spec.rate);if(error)*error=0;return 0;
}
int pa_simple_flush(void* stream,int* error){(void)stream;if(error)*error=0;return 0;}
int pa_simple_drain(void* stream,int* error){(void)stream;if(error)*error=0;return 0;}
void pa_simple_free(void* handle){struct fake_stream* stream=handle;if(stream&&stream->input)fclose(stream->input);free(stream);}
uint64_t pa_simple_get_latency(void* stream,int* error){(void)stream;if(error)*error=0;return 0;}

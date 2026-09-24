#include "datapump/speculation.h"
#include <stdint.h>
#include <stdio.h>

#if defined(_MSC_VER)
#define PROBE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define PROBE_NOINLINE __attribute__((noinline))
#else
#define PROBE_NOINLINE
#endif

/* Externally visible witnesses let the generated-code test inspect the exact
 * optimized helper used by C consumers, including the dependent memory load.
 */
PROBE_NOINLINE unsigned char datapump_nospec_load(const unsigned char* bytes,size_t index,size_t size) {
    return bytes[datapump_index_nospec(index,size)];
}
PROBE_NOINLINE unsigned char datapump_barrier_load(const volatile unsigned char* bytes) {
    datapump_speculation_barrier();
    return *bytes;
}

int main(void) {
    unsigned char bytes[257];
    size_t size,index;
    const size_t extremes[]={0,1,2,255,256,257,SIZE_MAX/2,SIZE_MAX/2+1,SIZE_MAX-1,SIZE_MAX};
    for(index=0;index<sizeof(bytes);++index)bytes[index]=(unsigned char)(index*43+17);
    for(size=1;size<=sizeof(bytes);++size)for(index=0;index<1024;++index) {
        const size_t expected=index<size?index:0;
        if(datapump_index_nospec(index,size)!=expected ||
           datapump_nospec_load(bytes,index,size)!=bytes[expected])return 1;
    }
    for(size=0;size<sizeof(extremes)/sizeof(extremes[0]);++size)
        for(index=0;index<sizeof(extremes)/sizeof(extremes[0]);++index) {
            const size_t bound=extremes[size],value=extremes[index];
            if(datapump_index_nospec(value,bound)!=(value<bound?value:0))return 2;
        }
    if(datapump_barrier_load(bytes)!=bytes[0])return 3;
    printf("speculation semantics passed - %s - index=%d barrier=%d\n",
        DATAPUMP_SPECULATION_BACKEND,DATAPUMP_INDEX_NOSPEC_SUPPORTED,DATAPUMP_SPECULATION_BARRIER_SUPPORTED);
    return 0;
}

#include "compression.h"
#include <stdlib.h>
#include <stdbool.h>
#ifdef PC_HOST_CHECKS
#include <zlib.h>
static size_t inflate_raw(void *out,size_t capacity,const void *input,size_t length)
{
    z_stream stream={.next_in=(Bytef *)input,.avail_in=length,.next_out=out,.avail_out=capacity};
    if (inflateInit2(&stream,-MAX_WBITS)!=Z_OK) return (size_t)-1;
    int result=inflate(&stream,Z_FINISH);
    size_t count=result==Z_STREAM_END && stream.avail_in==0?stream.total_out:(size_t)-1;
    inflateEnd(&stream); return count;
}
static uint32_t checksum(const void *input,size_t length) { return crc32(0,input,length); }
#else
#include "miniz.h"
#include "esp_heap_caps.h"
static size_t inflate_raw(void *out,size_t capacity,const void *input,size_t length)
{
    tinfl_decompressor *state=heap_caps_calloc(1,sizeof(*state),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!state) return (size_t)-1;
    tinfl_init(state);
    size_t consumed=length,produced=capacity;
    tinfl_status status=tinfl_decompress(state,input,&consumed,out,out,&produced,TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(state);
    return status==TINFL_STATUS_DONE && consumed==length ? produced : (size_t)-1;
}
static uint32_t checksum(const void *input,size_t length) { return mz_crc32(0,input,length); }
#endif
static uint32_t little32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
char *pc_gzip_decode(const uint8_t *bytes,size_t length,size_t maximum)
{
    if (!bytes || length<18 || bytes[0]!=0x1f || bytes[1]!=0x8b || bytes[2]!=8 || (bytes[3]&0xe0)) return NULL;
    size_t at=10,stop=length-8;
    if (bytes[3]&4) {
        if (at+2>stop) return NULL;
        size_t n=bytes[at]|(bytes[at+1]<<8); at+=2;
        if (n>stop-at) return NULL;
        at+=n;
    }
    for (int flag=8;flag<=16;flag*=2) if (bytes[3]&flag) {
        while (at<stop && bytes[at]) at++;
        if (at>=stop) return NULL;
        at++;
    }
    if (bytes[3]&2) {
        if (at+2>stop || (checksum(bytes,at)&0xffff)!=(uint32_t)(bytes[at]|(bytes[at+1]<<8))) return NULL;
        at+=2;
    }
    if (at>=stop) return NULL;
    uint32_t size=little32(bytes+length-4);
    if (!size || size>maximum) return NULL;
    char *data=malloc(size+1); if (!data) return NULL;
    size_t actual=inflate_raw(data,size,bytes+at,stop-at);
    if (actual!=size || checksum(data,size)!=little32(bytes+length-8)) { free(data); return NULL; }
    data[size]=0; return data;
}

#include "fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE 512
static const int SIZES[]={18,24,36};
static char root[96];
static uint32_t counts[3];
typedef struct { int size; uint32_t cp; bool used, found; pc_glyph_t glyph; uint8_t *bitmap; } slot_t;
static slot_t *cache;

static uint32_t le32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int size_index(int size)
{
    for (int i=0;i<3;i++) if (SIZES[i]==size) return i;
    return -1;
}
static FILE *open_pack(int size)
{
    char path[128]; snprintf(path,sizeof(path),"%s/font%d.bin",root,size);
    return fopen(path,"rb");
}
void pc_fonts_open(const char *directory)
{
    if (cache) for (int i=0;i<CACHE;i++) free(cache[i].bitmap);
    free(cache); cache=NULL; memset(counts,0,sizeof(counts)); root[0]=0;
    if (!directory || strlen(directory)>=sizeof(root)) return;
    strcpy(root,directory);
    for (int i=0;i<3;i++) {
        FILE *f=open_pack(SIZES[i]); uint8_t header[12];
        if (!f) continue;
        if (fread(header,1,12,f)==12 && !memcmp(header,"PCF1",4) && (header[4]|(header[5]<<8))==SIZES[i]) counts[i]=le32(header+8);
        fclose(f);
    }
    if (counts[0]||counts[1]||counts[2]) cache=calloc(CACHE,sizeof(slot_t));
}
bool pc_fonts_available(int size)
{
    int i=size_index(size);
    return i>=0 && counts[i] && cache;
}
static bool search(FILE *f,int index,uint32_t cp,uint8_t e[20])
{
    int lo=0,hi=(int)counts[index]-1;
    while (lo<=hi) {
        int mid=(lo+hi)/2;
        if (fseek(f,12+(long)mid*20,SEEK_SET) || fread(e,1,20,f)!=20) return false;
        uint32_t code=le32(e);
        if (code==cp) return true;
        if (cp<code) hi=mid-1; else lo=mid+1;
    }
    return false;
}
bool pc_fonts_has(int size,uint32_t cp)
{
    if (!pc_fonts_available(size)) return false;
    FILE *f=open_pack(size);
    if (!f) return false;
    uint8_t e[20]; bool found=search(f,size_index(size),cp,e);
    fclose(f); return found;
}
static bool load(int size,uint32_t cp,slot_t *s)
{
    int index=size_index(size);
    FILE *f=open_pack(size);
    if (!f) return false;
    uint8_t e[20]; bool found=search(f,index,cp,e);
    if (found) {
        uint16_t runs=e[8]|(e[9]<<8);
        s->bitmap=malloc(runs?runs:1);
        long base=12+(long)counts[index]*20;
        if (!s->bitmap || fseek(f,base+(long)le32(e+4),SEEK_SET) || fread(s->bitmap,1,runs,f)!=runs) found=false;
        else s->glyph=(pc_glyph_t){e[10],e[11],e[12],(int8_t)e[13],(int8_t)e[14],runs,s->bitmap};
    }
    fclose(f);
    return found;
}
bool pc_fonts_glyph(int size,uint32_t cp,pc_glyph_t *out)
{
    if (!pc_fonts_available(size)) return false;
    slot_t *s=&cache[(cp*31u+(uint32_t)size)%CACHE];
    if (!s->used || s->cp!=cp || s->size!=size) {
        free(s->bitmap);
        *s=(slot_t){.size=size,.cp=cp,.used=true};
        s->found=load(size,cp,s);
    }
    if (s->found) *out=s->glyph;
    return s->found;
}

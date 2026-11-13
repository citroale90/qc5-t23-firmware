/* SPDX-License-Identifier: GPL-2.0-or-later
 * Check the newly exposed RAM through Linux allocation, never raw DDR writes.
 */
#ifndef QC5_MEMORY_PROOF_H
#define QC5_MEMORY_PROOF_H
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define QC5_MEMORY_BYTES 2097152u
#define QC5_MEMORY_WORKING_BYTES 32768u
#define QC5_MEMORY_HASH UINT32_C(0xdef16c19)
struct qc5_memory_note {uint32_t total_kb,heap_bytes,heap_hash,flags;};
static uint32_t qc5_memory_word(unsigned n){return 0x51434d37u^(n*0x9e3779b9u);}
static int qc5_memory_verify(const volatile uint32_t *words,uint32_t *hash)
{
    if(!words||!hash)return 0;uint32_t h=2166136261u;
    for(unsigned n=0;n<QC5_MEMORY_BYTES/4;++n){uint32_t value=words[n];if(value!=qc5_memory_word(n))return 0;
        for(unsigned b=0;b<4;++b)h=(h^((value>>(b*8))&255u))*16777619u;}
    *hash=h;return h==QC5_MEMORY_HASH;
}
static int qc5_memory_note_valid(const struct qc5_memory_note *p)
{return p&&p->total_kb>=14000&&p->total_kb<=23552&&p->heap_bytes==QC5_MEMORY_WORKING_BYTES&&p->heap_hash==QC5_MEMORY_HASH&&p->flags==7;}
static int qc5_memory_inventory(FILE *meminfo,FILE *iomem,struct qc5_memory_note *p)
{
    if(!meminfo||!iomem||!p)return 0;char line[256];unsigned lines=0;
    while(fgets(line,sizeof(line),meminfo)){unsigned kb;
        if(sscanf(line,"MemTotal: %u kB",&kb)==1)p->total_kb=kb;
        if(++lines>256)return 0;
    }
    if(ferror(meminfo)||p->total_kb<14000||p->total_kb>23552)return 0;
    lines=0;
    while(fgets(line,sizeof(line),iomem)){
        unsigned long long first,last;char name[96];
        if(sscanf(line,"%llx-%llx : %95[^\n]",&first,&last,name)==3&&
           first==0x1d00000&&last==0x1ffffff&&!strcmp(name,"System RAM"))p->flags|=4;
        if(++lines>256)return 0;
    }
    return !ferror(iomem)&&(p->flags&4);
}
/* Logical coverage is2MiB; the maximum simultaneous heap allocation is32KiB.
 * This is not a claim that a contiguous2MiB heap allocation was stress-tested.
 * Keep the full-bank inventory check and the exact global word/hash sequence.
 */
static int qc5_memory_verify_span(const volatile uint32_t *words,unsigned first,unsigned count,uint32_t *hash)
{
 if(!words||!hash||!count||count>QC5_MEMORY_WORKING_BYTES/4||
    first>QC5_MEMORY_BYTES/4||count>QC5_MEMORY_BYTES/4-first)return 0;
 uint32_t h=*hash;
 for(unsigned n=0;n<count;n++){
  uint32_t value=words[n];if(value!=qc5_memory_word(first+n))return 0;
  for(unsigned b=0;b<4;b++)h=(h^((value>>(b*8))&255u))*16777619u;
 }
 *hash=h;return 1;
}
static int qc5_memory_window_pattern(volatile uint32_t *words,uint32_t *hash)
{
 if(!words||!hash)return 0;uint32_t h=2166136261u;
 for(unsigned first=0;first<QC5_MEMORY_BYTES/4;first+=QC5_MEMORY_WORKING_BYTES/4){
  for(unsigned n=0;n<QC5_MEMORY_WORKING_BYTES/4;n++)words[n]=qc5_memory_word(first+n);
  if(!qc5_memory_verify_span(words,first,QC5_MEMORY_WORKING_BYTES/4,&h))return 0;
 }
 *hash=h;return h==QC5_MEMORY_HASH;
}
static int qc5_memory_test(struct qc5_memory_note *p)
{
 if(!p)return 0;memset(p,0,sizeof *p);
 FILE *meminfo=fopen("/proc/meminfo","r"),*iomem=fopen("/proc/iomem","r");
 int inventory=qc5_memory_inventory(meminfo,iomem,p);
 if(meminfo)fclose(meminfo);if(iomem)fclose(iomem);if(!inventory)return 0;
 volatile uint32_t *words=malloc(QC5_MEMORY_WORKING_BYTES);if(!words)return 0;
 p->flags|=1;p->heap_bytes=QC5_MEMORY_WORKING_BYTES;
 if(qc5_memory_window_pattern(words,&p->heap_hash))p->flags|=2;
 free((void *)words);return qc5_memory_note_valid(p);
}
#endif

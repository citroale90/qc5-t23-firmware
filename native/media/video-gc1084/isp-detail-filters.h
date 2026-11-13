/* SPDX-License-Identifier: GPL-2.0-or-later
 * Coefficient-only YDNS/DPC executor. No DMA buffers or global bypass writes.
 * Private per-sensor calibration is generated separately from the OEM image.
 */
#ifndef QC5_ISP_DETAIL_FILTERS_H
#define QC5_ISP_DETAIL_FILTERS_H
#include <stdint.h>
#include <string.h>
#include "filter-counts.h"
#define QC5_DETAIL_INIT 40u
#define QC5_DETAIL_PATCH 27u
#define QC5_DETAIL_READ 38u
#define QC5_DETAIL_GAINS 25u
static const uint32_t qc5_detail_initial_offsets[QC5_DETAIL_INIT]={
 0x7af0,0x7af4,0x7af8,0x7afc,0x2864,0x2868,0x286c,0x2870,0x2874,0x2878,0x287c,0x2880,
 0x2800,0x2838,0x281c,0x280c,0x283c,0x2820,0x2840,0x2824,0x2844,0x2828,0x2848,0x282c,0x284c,
 0x2830,0x2808,0x2818,0x2850,0x2834,0x2814,0x2810,0x2804,0x2884,0x2854,0x2858,0x285c,0x2860,0x2898,0x2898};
static const uint32_t qc5_detail_patch_offsets[QC5_DETAIL_PATCH]={
 0x7af0,0x7af4,0x7af8,0x7afc,0x2838,0x281c,0x280c,0x283c,0x2820,0x2840,0x2824,0x2844,0x2828,0x2848,
 0x282c,0x284c,0x2830,0x2808,0x2818,0x2850,0x2834,0x2814,0x2854,0x2858,0x285c,0x2860,0x2898};
static const uint32_t qc5_detail_read_offsets[QC5_DETAIL_READ]={
 0x2800,0x2804,0x2808,0x280c,0x2810,0x2814,0x2818,0x281c,0x2820,0x2824,0x2828,0x282c,0x2830,0x2834,
 0x2838,0x283c,0x2840,0x2844,0x2848,0x284c,0x2850,0x2854,0x2858,0x285c,0x2860,0x2864,0x2868,0x286c,
 0x2870,0x2874,0x2878,0x287c,0x2880,0x2884,0x7af0,0x7af4,0x7af8,0x7afc};
struct qc5_detail_profile {
 uint32_t initial[2][QC5_DETAIL_INIT];
 uint32_t patch[2][QC5_DETAIL_GAINS][QC5_DETAIL_PATCH];
 uint32_t expected[2][QC5_DETAIL_GAINS][QC5_DETAIL_READ];
 uint32_t checksum;
};
_Static_assert(sizeof(struct qc5_detail_profile)==13324,"Calibration has no padding");
struct qc5_detail_state {const struct qc5_detail_profile *profile;uint32_t ready,bank,gain,read_hash,fault_offset,fault_value;qc5_filter_count writes,updates;};
typedef int (*qc5_detail_write)(void *,uint32_t,uint32_t);
typedef uint32_t (*qc5_detail_read)(void *,uint32_t);
static uint32_t qc5_detail_hash_word(uint32_t h,uint32_t w)
{for(unsigned n=0;n<4;n++)h=(h^((w>>(8*n))&255u))*16777619u;return h;}
static uint32_t qc5_detail_checksum(const struct qc5_detail_profile *p)
{
 uint32_t h=2166136261u;
 for(unsigned b=0;b<2;b++)for(unsigned n=0;n<QC5_DETAIL_INIT;n++)h=qc5_detail_hash_word(h,p->initial[b][n]);
 for(unsigned b=0;b<2;b++)for(unsigned g=0;g<QC5_DETAIL_GAINS;g++)for(unsigned n=0;n<QC5_DETAIL_PATCH;n++)h=qc5_detail_hash_word(h,p->patch[b][g][n]);
 for(unsigned b=0;b<2;b++)for(unsigned g=0;g<QC5_DETAIL_GAINS;g++)for(unsigned n=0;n<QC5_DETAIL_READ;n++)h=qc5_detail_hash_word(h,p->expected[b][g][n]);
 return h;
}
static int qc5_detail_valid(const struct qc5_detail_profile *p)
{
 if(!p||!p->checksum||qc5_detail_checksum(p)!=p->checksum)return 0;
 for(unsigned b=0;b<2;b++){
  if(p->initial[b][38]!=1||p->initial[b][39]!=1)return 0;
  for(unsigned g=0;g<QC5_DETAIL_GAINS;g++)if(p->patch[b][g][26]!=1)return 0;
 }
 return 1;
}
static int qc5_detail_load(struct qc5_detail_state *s,const struct qc5_detail_profile *p,unsigned bank,qc5_detail_write write,void *opaque)
{
 if(!s)return 0;memset(s,0,sizeof *s);
 if(bank>1||!write||!qc5_detail_valid(p))return 0;
 for(unsigned n=0;n<QC5_DETAIL_INIT;n++){
  if(!write(opaque,qc5_detail_initial_offsets[n],p->initial[bank][n]))return 0;++s->writes;
 }
 s->profile=p;s->bank=bank;s->gain=4;s->ready=1;return 1;
}
static int qc5_detail_gain(struct qc5_detail_state *s,unsigned gain,qc5_detail_write write,void *opaque)
{
 if(!s||!s->ready||!s->profile||s->bank>1||s->gain>=25||gain>=25||!write)return 0;
#ifndef QC5_IMAGE_CONTINUOUS
 if(s->updates>=121)return 0;
#endif
 if(!qc5_filter_room(s->updates,1)||!qc5_filter_room(s->writes,gain==s->gain?1:QC5_DETAIL_PATCH))return 0;
 s->read_hash=0;
 if(gain==s->gain){if(!write(opaque,0x2898,1)){s->ready=0;return 0;}++s->writes;}
 else for(unsigned n=0;n<QC5_DETAIL_PATCH;n++){
  if(!write(opaque,qc5_detail_patch_offsets[n],s->profile->patch[s->bank][gain][n])){s->ready=0;return 0;}++s->writes;
 }
 s->gain=gain;++s->updates;return 1;
}
static int qc5_detail_verify(struct qc5_detail_state *s,qc5_detail_read read,void *opaque)
{
 if(!s)return 0;s->read_hash=0;s->fault_offset=s->fault_value=0;
 if(!s->ready||!s->profile||s->bank>1||s->gain>=25||!read)return 0;
 uint32_t h=2166136261u;
 for(unsigned n=0;n<QC5_DETAIL_READ;n++){
  uint32_t value=read(opaque,qc5_detail_read_offsets[n]);if(value!=s->profile->expected[s->bank][s->gain][n]){
   s->fault_offset=qc5_detail_read_offsets[n];s->fault_value=value;return 0;
  }
  h=qc5_detail_hash_word(qc5_detail_hash_word(h,qc5_detail_read_offsets[n]),value);
 }
 s->read_hash=h;return h!=0;
}
#endif

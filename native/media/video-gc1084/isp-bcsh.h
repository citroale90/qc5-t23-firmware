/* SPDX-License-Identifier: GPL-2.0-or-later
 * The exact T23 CT dispatcher gates BCSH with ISP bypass bit 16.
 * This block owns no DMA. Its clipping/colour coefficients must be loaded
 * before enabling that bit; all 29 words remain checked on actual frames.
 */
#ifndef QC5_ISP_BCSH_H
#define QC5_ISP_BCSH_H
#include <stdint.h>
#include "filter-counts.h"
#include <string.h>
struct qbcsh_profile {uint32_t initial[2][29],checksum;};
struct qbcsh_state {const struct qbcsh_profile *profile;const uint32_t *expected;uint32_t loaded,bank,writes,read_hash,fault_offset,fault_value;qc5_filter_count frames;};
typedef uint32_t (*qbcsh_read)(void *,uint32_t);
typedef int (*qbcsh_write)(void *,uint32_t,uint32_t);
_Static_assert(sizeof(struct qbcsh_profile)==236,"Exact two-bank BCSH profile");
static uint32_t qbcsh_mix(uint32_t hash,uint32_t value)
{for(unsigned n=0;n<4;n++)hash=(hash^((value>>(8*n))&255u))*16777619u;return hash;}
static uint32_t qbcsh_checksum(const struct qbcsh_profile *p)
{
 uint32_t h=2166136261u;
 for(unsigned b=0;b<2;b++)for(unsigned n=0;n<29;n++)h=qbcsh_mix(h,p->initial[b][n]);
 return h;
}
static int qbcsh_loaded(const struct qbcsh_state *s)
{return s&&s->loaded&&s->profile&&s->bank<2&&s->writes==29&&s->profile->checksum&&qbcsh_checksum(s->profile)==s->profile->checksum;}
static int qbcsh_load(struct qbcsh_state *s,const struct qbcsh_profile *p,unsigned bank,qbcsh_write write,void *opaque)
{
 if(!s)return 0;memset(s,0,sizeof *s);
 if(!p||bank>1||!write||!p->checksum||qbcsh_checksum(p)!=p->checksum)return 0;
 for(unsigned n=0;n<29;n++){
  if(!write(opaque,0x8000+4*n,p->initial[bank][n]))return 0;
  ++s->writes;
 }
 s->profile=p;s->bank=bank;s->loaded=1;return 1;
}
static int qbcsh_readback(struct qbcsh_state *s,qbcsh_read read,void *opaque)
{
 if(!qbcsh_loaded(s)||!read)return 0;
 s->read_hash=s->fault_offset=s->fault_value=0;uint32_t hash=2166136261u;
 for(unsigned n=0;n<29;n++){
  uint32_t offset=0x8000+4*n,value=read(opaque,offset);
  uint32_t expected=s->expected?s->expected[n]:s->profile->initial[s->bank][n];
  if(value!=expected){s->fault_offset=offset;s->fault_value=value;return 0;}
  hash=qbcsh_mix(qbcsh_mix(hash,offset),value);
 }
 s->read_hash=hash;return hash!=0;
}
static int qbcsh_frame(struct qbcsh_state *s,qbcsh_read read,void *opaque,qc5_filter_count completed)
{
 if(!s||!completed||s->frames==QC5_FILTER_COUNT_MAX||
#ifndef QC5_IMAGE_CONTINUOUS
    completed>248||
#endif
    completed!=s->frames+1||!qbcsh_readback(s,read,opaque))return 0;
 s->frames=completed;return 1;
}
#endif

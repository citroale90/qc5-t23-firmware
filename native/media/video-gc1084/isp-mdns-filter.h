/* SPDX-License-Identifier: GPL-2.0-or-later
 * Temporal-filter coefficient replay ONLY. DMA enable is always held low.
 * No buffer addresses, global bypass, sensor, interrupts or actuators here.
 * The capture adapter owns the separate, verified DMA commit.
 */
#ifndef QC5_ISP_MDNS_FILTER_H
#define QC5_ISP_MDNS_FILTER_H
#include <stdint.h>
#include <string.h>
#include "mdns-offsets.h"
#include "mdns-counts.h"
#define QC5_MDNS_GAINS 25u
struct qc5_mdns_profile {
 uint32_t initial[2][QC5_MDNS_INITIAL];
 uint32_t patch[2][QC5_MDNS_GAINS][QC5_MDNS_PATCH];
 uint32_t expected[2][QC5_MDNS_GAINS][QC5_MDNS_READ];
 uint32_t checksum;
};
_Static_assert(sizeof(struct qc5_mdns_profile)==66364,"Private coefficient layout has no padding");
struct qc5_mdns_state {const struct qc5_mdns_profile *profile;uint32_t ready,bank,gain;
 qc5_mdns_count writes,updates;uint32_t read_hash,fault_offset,fault_value;};
typedef int (*qc5_mdns_write)(void *,uint32_t,uint32_t);
typedef uint32_t (*qc5_mdns_read)(void *,uint32_t);
static uint32_t qc5_mdns_hash_word(uint32_t h,uint32_t v)
{for(unsigned n=0;n<4;n++)h=(h^((v>>(8*n))&255u))*16777619u;return h;}
static uint32_t qc5_mdns_checksum(const struct qc5_mdns_profile *p)
{
 uint32_t h=2166136261u;
 for(unsigned b=0;b<2;b++)for(unsigned n=0;n<QC5_MDNS_INITIAL;n++)h=qc5_mdns_hash_word(h,p->initial[b][n]);
 for(unsigned b=0;b<2;b++)for(unsigned g=0;g<25;g++)for(unsigned n=0;n<QC5_MDNS_PATCH;n++)h=qc5_mdns_hash_word(h,p->patch[b][g][n]);
 for(unsigned b=0;b<2;b++)for(unsigned g=0;g<25;g++)for(unsigned n=0;n<QC5_MDNS_READ;n++)h=qc5_mdns_hash_word(h,p->expected[b][g][n]);
 return h;
}
static int qc5_mdns_offset_valid(uint32_t off)
{return off>=0x7804&&off<0x7af0&&!(off&3u)&&!(off>=0x7820&&off<=0x786c);}
static uint32_t qc5_mdns_hold_dma(uint32_t off,uint32_t value)
{return off==0x7804?value&~1u:value;}
static int qc5_mdns_write_coefficient(qc5_mdns_write write,void *opaque,uint32_t off,uint32_t value)
{return write&&qc5_mdns_offset_valid(off)&&write(opaque,off,qc5_mdns_hold_dma(off,value));}
static int qc5_mdns_load(struct qc5_mdns_state *s,const struct qc5_mdns_profile *p,unsigned bank,qc5_mdns_write write,void *opaque)
{
 if(!s)return 0;memset(s,0,sizeof *s);
 if(!p||bank>1||!write||!p->checksum||qc5_mdns_checksum(p)!=p->checksum)return 0;
 for(unsigned n=0;n<QC5_MDNS_INITIAL;n++){
  if(!qc5_mdns_write_coefficient(write,opaque,qc5_mdns_initial_offsets[n],p->initial[bank][n]))return 0;++s->writes;
 }
 s->profile=p;s->bank=bank;s->gain=4;s->ready=1;return 1;
}
static int qc5_mdns_gain(struct qc5_mdns_state *s,unsigned gain,qc5_mdns_write write,void *opaque)
{
 if(!s||!s->ready||!s->profile||s->bank>1||s->gain>=25||gain>=25||!write||
    !qc5_mdns_update_available(s->updates,s->writes,gain!=s->gain))return 0;
 s->read_hash=0;
 if(gain!=s->gain)for(unsigned n=0;n<QC5_MDNS_PATCH;n++){
  if(!qc5_mdns_write_coefficient(write,opaque,qc5_mdns_patch_offsets[n],s->profile->patch[s->bank][gain][n])){s->ready=0;return 0;}++s->writes;
 }
 s->gain=gain;++s->updates;return 1;
}
static int qc5_mdns_verify(struct qc5_mdns_state *s,qc5_mdns_read read,void *opaque)
{
 if(!s)return 0;s->read_hash=s->fault_offset=s->fault_value=0;
 if(!s->ready||!s->profile||s->bank>1||s->gain>=25||!read)return 0;
 uint32_t h=2166136261u;
 for(unsigned n=0;n<QC5_MDNS_READ;n++){
  uint32_t off=qc5_mdns_read_offsets[n],value=read(opaque,off);
  if(value!=qc5_mdns_hold_dma(off,s->profile->expected[s->bank][s->gain][n])){s->fault_offset=off;s->fault_value=value;return 0;}
  h=qc5_mdns_hash_word(qc5_mdns_hash_word(h,off),value);
 }
 s->read_hash=h;return h!=0;
}
#endif

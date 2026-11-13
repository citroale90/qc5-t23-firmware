/* SPDX-License-Identifier: GPL-2.0-or-later
 * The existing final ISP statistics allocation, not an extra video buffer.
 * Exact OEM AF ISR selects one of four 4096-byte pages via register 0xb8bc.
 * No writer, interrupt, focus algorithm, lens or motor is enabled here.
 */
#ifndef QC5_AF_STATS_H
#define QC5_AF_STATS_H
#include "qc5-frame-layout.h"
#define QC5_AF_STATS_BASE (QC5_STATS_BASE+0x18000u)
#define QC5_AF_STATS_BYTES 0x4000u
#define QC5_AF_LAYOUT_ID UINT32_C(0x37334641)
_Static_assert(QC5_AF_STATS_BASE+QC5_AF_STATS_BYTES==QC5_STATS_END,"AF uses the already reserved final statistics span");
struct qc5_af_io {void *opaque;uint32_t (*read)(void *,uint32_t);};
struct qc5_af_stats {struct qc5_af_io io;uint32_t ready,read_hash,bank,changed_bytes,pages_mask,data_hash,fault_offset,fault_value;};
static uint32_t qc5_af_hash_word(uint32_t h,uint32_t w)
{for(unsigned n=0;n<4;n++)h=(h^((w>>(8*n))&255u))*16777619u;return h;}
static uint32_t qc5_af_profile_hash(void)
{
 const uint32_t words[]={QC5_AF_LAYOUT_ID,1,QC5_AF_STATS_BASE,QC5_AF_STATS_BYTES,4,4096,0,0xb8bc};
 uint32_t h=2166136261u;for(unsigned n=0;n<8;n++)h=qc5_af_hash_word(h,words[n]);return h;
}
static int qc5_af_verify_config(struct qc5_af_stats *s,int read_bank)
{
 if(!s||!s->io.read)return 0;s->ready=0;s->read_hash=0;s->fault_offset=s->fault_value=0;
 uint32_t h=2166136261u;
 for(unsigned n=0;n<5;n++){
  uint32_t off=0xb8a8+4*n,expected=n<4?QC5_AF_STATS_BASE+4096*n:0;
  uint32_t value=s->io.read(s->io.opaque,off);
  if(value!=expected){s->fault_offset=off;s->fault_value=value;return 0;}
  h=qc5_af_hash_word(qc5_af_hash_word(h,off),value);
 }
 if(read_bank){s->bank=s->io.read(s->io.opaque,0xb8bc);
  if(s->bank>3){s->fault_offset=0xb8bc;s->fault_value=s->bank;return 0;}}
 s->read_hash=h;s->ready=1;return 1;
}
static int qc5_af_prepare(struct qc5_af_stats *s,const struct qc5_af_io *io)
{
 if(!s)return 0;*s=(struct qc5_af_stats){0};if(!io||!io->read)return 0;s->io=*io;
 if(!qc5_af_verify_config(s,0))return 0;s->ready=1;return 1;
}
static int qc5_af_owns(const struct qc5_af_stats *s,uint32_t physical)
{return s&&s->ready&&physical>=QC5_AF_STATS_BASE&&physical-QC5_AF_STATS_BASE<QC5_AF_STATS_BYTES;}
static int qc5_af_finish(struct qc5_af_stats *s,const volatile uint8_t *ram,unsigned stop)
{
 if(!s||!s->ready||!ram||stop!=31||!qc5_af_verify_config(s,1))return 0;
 uint32_t changed=0,mask=0,h=2166136261u;
 for(unsigned n=0;n<QC5_AF_STATS_BYTES;n++){
  uint8_t value=ram[QC5_AF_STATS_BASE-QC5_CAPTURE_RAM_BASE+n];h=(h^value)*16777619u;
  if(value!=0xa5){++changed;mask|=1u<<(n/4096);}
 }
 s->changed_bytes=changed;s->pages_mask=mask;s->data_hash=h;return 1;
}
#endif

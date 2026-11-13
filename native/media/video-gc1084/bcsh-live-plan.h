/* SPDX-License-Identifier: GPL-3.0-only
 * Checked BCSH commits to the existing colour block. No DMA allocation,
 * clock, reset, GPIO, global-bypass or unrelated-register writes exist.
 */
#ifndef QC5_BCSH_LIVE_PLAN_H
#define QC5_BCSH_LIVE_PLAN_H
#include "bcsh-events.h"
#ifdef QC5_IMAGE_CONTINUOUS
#include "image-continuous.h"
#define QBH_WINDOW_SAMPLES (UINT32_MAX/58u)
#endif
struct qbh_io {void *opaque;uint32_t (*read)(void *,uint32_t);int (*write)(void *,uint32_t,uint32_t);};
struct qbh_state {struct qbf_state events;struct qbh_io io;uint32_t ready,failed,finished,samples,writes,read_hash,fault_offset,fault_value;
#ifdef QC5_IMAGE_CONTINUOUS
 uint32_t rollovers;
#endif
};
#ifdef QC5_IMAGE_CONTINUOUS
static int qbh_counts_valid(const struct qbh_state *s)
{
 return s->samples<=QBH_WINDOW_SAMPLES&&s->events.updates<=2*s->samples&&
  s->events.skipped<=2*s->samples&&s->events.updates+s->events.skipped==2*s->samples&&
  s->writes==29*s->events.updates;
}
#endif
static int qbh_fail(struct qbh_state *s,uint32_t off,uint32_t value)
{s->failed=1;s->fault_offset=off;s->fault_value=value;return 0;}
static int qbh_readback(struct qbh_state *s,const uint32_t expected[29])
{
 uint32_t hash=2166136261u;
 for(unsigned n=0;n<29;n++){
  uint32_t off=0x8000+4*n,value=s->io.read(s->io.opaque,off);
  if(value!=expected[n])return qbh_fail(s,off,value);
  for(unsigned k=0;k<8;k++)hash=(hash^(((k<4?off:value)>>(8*(k%4)))&255u))*16777619u;
 }
 s->read_hash=hash;return hash!=0;
}
static int qbh_commit(void *opaque,const uint32_t values[29])
{
 struct qbh_state *s=opaque;
 if(s->failed||s->events.failed)return 0;
 for(unsigned n=0;n<29;n++){
  uint32_t off=0x8000+4*n;
  if(!s->io.write(s->io.opaque,off,values[n]))return qbh_fail(s,off,values[n]);
  ++s->writes;
 }
 return qbh_readback(s,values);
}
static int qbh_prepare(struct qbh_state *s,const struct qbh_io *io,const struct qbd_profile *p)
{
 if(!s)return 0;memset(s,0,sizeof *s);
 if(!io||!io->read||!io->write||!p)return 0;s->io=*io;
 if(!qbf_start(&s->events,p,6000,400u<<10))return qbh_fail(s,0x7b010,0);
 if(memcmp(s->events.registers,p->exposure.initial,sizeof s->events.registers))return qbh_fail(s,0x7b014,0);
 if(!qbh_readback(s,s->events.registers))return 0;
 s->ready=1;return 1;
}
static int qbh_tick(struct qbh_state *s,uint32_t ct,uint32_t ev_q10)
{
 if(!s||!s->ready||s->failed||s->finished||s->events.failed)return 0;
#ifdef QC5_IMAGE_CONTINUOUS
 /* Preserve coefficients, native deadbands and input state when rolling
  * telemetry counters. No hardware register is reset or reinitialised. */
 if(!qbh_counts_valid(s))return qbh_fail(s,0x7b028,s->samples);
 if(s->samples==QBH_WINDOW_SAMPLES){
  s->samples=s->writes=s->events.updates=s->events.skipped=0;
  s->rollovers=qc5_image_inc(s->rollovers);
 }
#else
 if(s->samples>=32)return 0;
#endif
 if(s->io.read(s->io.opaque,0xc)&0x10000u)return qbh_fail(s,0xc,0x10000);
 if(!qbh_readback(s,s->events.registers))return 0;
 if(!qbf_event(&s->events,1,ev_q10,0,qbh_commit,s)||!qbf_event(&s->events,2,ct,0,qbh_commit,s)){
  if(!s->failed)qbh_fail(s,0x7b018,ct);return 0;
 }
 ++s->samples;return qbh_readback(s,s->events.registers);
}
static int qbh_finish(struct qbh_state *s)
{
 if(!s||!s->ready||s->failed||s->events.failed||
#ifdef QC5_IMAGE_CONTINUOUS
    (s->samples<16&&!s->rollovers)||!qbh_counts_valid(s)||
#else
    s->samples<16||
#endif
#ifndef QC5_IMAGE_CONTINUOUS
    s->samples>32||
#endif
    s->events.updates+s->events.skipped!=2*s->samples||s->writes!=29*s->events.updates)return 0;
 if(!qbh_readback(s,s->events.registers))return 0;
 s->finished=1;return 1;
}
#endif

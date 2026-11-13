/* SPDX-License-Identifier: GPL-2.0-only
 * Nonblocking original DAC sequence; callbacks touch only codec fields.
 * Minimum settle times are measured after each successful field update.
 * No sleep callback, allocation, ADC/clock/DMA/GPIO changes or early READY.
 */
#ifndef QC5_ASYNC_DAC_H
#define QC5_ASYNC_DAC_H
#include <stddef.h>
#include <stdint.h>
struct qac_step {uint32_t offset,mask,value,delay_us,gain_parameter;};
#include "codec-async-steps.generated.h"
enum qac_phase {QAC_OFF,QAC_STARTING,QAC_READY,QAC_STOPPING,QAC_FAULT};
enum qac_error {QAC_OK,QAC_ARGUMENT,QAC_WRITE,QAC_CLOCK,QAC_DEADLINE};
struct qac_io {void *opaque;uint32_t (*read)(void *,uint32_t);int (*write)(void *,uint32_t,uint32_t);};
struct qac_state {
 struct qac_io io;const struct qac_step *steps;
 uint64_t last_us,due_us,deadline_us;
 unsigned phase,error,index,count,gain,owned,muted;
};
static int qac_mute(struct qac_state *s)
{
 if(!s||!s->owned)return 0;s->muted=0;
 for(unsigned n=0;n<3;n++){
  uint32_t previous=s->io.read(s->io.opaque,0xa0);
  if(s->io.write(s->io.opaque,0xa0,previous&~31u)&&!(s->io.read(s->io.opaque,0xa0)&31u)){s->muted=1;break;}
 }
 return s->muted;
}
static int qac_fail(struct qac_state *s,unsigned error)
{if(!s->error)s->error=error;s->phase=QAC_FAULT;(void)qac_mute(s);return 0;}
static int qac_init(struct qac_state *s,const struct qac_io *io)
{
 if(!s||!io||!io->read||!io->write||s->owned||s->phase!=QAC_OFF)return 0;
 s->io=*io;
 if((io->read(io->opaque,0xa0)&255u)||(io->read(io->opaque,0x9c)&0xf0u)||(io->read(io->opaque,0x98)&8u))return 0;
 s->owned=1;return 1;
}
static int qac_begin(struct qac_state *s,unsigned gain,uint64_t now)
{
 if(!s||!s->owned||s->error||s->phase!=QAC_OFF||gain>31||!now||now>UINT64_MAX-600000)return 0;
 s->steps=qac_start_steps;s->count=sizeof qac_start_steps/sizeof *qac_start_steps;
 s->index=0;s->gain=gain;s->last_us=s->due_us=now;s->deadline_us=now+600000;
 s->phase=QAC_STARTING;s->muted=0;return 1;
}
/* DMA must be confirmed stopped before power-down. Cleanup remains possible
 * after a latched start failure; it never clears that failure or restarts. */
static int qac_begin_stop(struct qac_state *s,unsigned tx_stopped,uint64_t now)
{
 if(!s||!s->owned||tx_stopped!=1||s->phase==QAC_STOPPING||!now||now>UINT64_MAX-500000)return 0;
 s->steps=qac_stop_steps;s->count=sizeof qac_stop_steps/sizeof *qac_stop_steps;
 s->index=0;s->last_us=s->due_us=now;s->deadline_us=now+500000;s->phase=QAC_STOPPING;return 1;
}
/* 1 pending;2 READY/OFF reached;0 fault. Every call is bounded by15 writes.
 * A late poll does not compress the following native hardware settle delay. */
static int qac_poll(struct qac_state *s,uint64_t now)
{
 if(!s||!s->owned||(s->phase!=QAC_STARTING&&s->phase!=QAC_STOPPING))return 0;
 if(!now||now<s->last_us)return qac_fail(s,QAC_CLOCK);
 if(now>s->deadline_us)return qac_fail(s,QAC_DEADLINE);
 s->last_us=now;if(now<s->due_us)return 1;
 for(unsigned budget=0;budget<16;budget++){
  if(s->index==s->count){s->phase=s->phase==QAC_STOPPING?QAC_OFF:QAC_READY;s->muted=s->phase==QAC_OFF;return 2;}
  const struct qac_step *step=s->steps+s->index;
  uint32_t previous=s->io.read(s->io.opaque,step->offset),value=step->gain_parameter?s->gain:step->value;
  if(value&~step->mask)return qac_fail(s,QAC_ARGUMENT);
  if(!s->io.write(s->io.opaque,step->offset,(previous&~step->mask)|value)||
     (s->io.read(s->io.opaque,step->offset)&step->mask)!=value)return qac_fail(s,QAC_WRITE);
  s->index++;s->due_us=now+step->delay_us;if(step->delay_us)return 1;
 }
 return qac_fail(s,QAC_ARGUMENT);
}
#endif

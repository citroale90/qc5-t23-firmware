/* SPDX-License-Identifier: GPL-2.0-only
 * Continuous capture core in a separate codec process. Reuses the original
 * ADC init/clock/write whitelist and its existing reserved PCM area. No OS,
 * network, playback, flash, interrupt, additional DMA buffer or allocation.
 */
#ifndef QC5_AUDIO_STREAM_CAPTURE_H
#define QC5_AUDIO_STREAM_CAPTURE_H
/* Generated opt-in sidecar extension; no sidecar means baseline behavior. */
#include <signal.h>
#include "sample-clock.h"
#include "../audio-t23/audio-plan.h"
#include "../audio-continuous/stream-ownership.h"
#include "../audio-continuous/stream-start.generated.h"
struct qas_result {
 uint64_t periods,delivered,discarded,samples,first_us,last_us,polls;
 uint32_t pcm_hash,guards,stopped,fault,safe_to_release;
};
struct qas_capture;
struct qas_sidecar {
 void *opaque;
 int (*start)(void *,struct qas_capture *);
 int (*poll)(void *,uint64_t);
 /* stop:1 physically safe,2 still stopping,0 failed */
 int (*stop)(void *,uint64_t);
 int (*owns_guard)(void *,uint32_t);
 int (*guards)(void *);
};
struct qas_capture {
 const struct qas_sidecar *sidecar;unsigned sidecar_started,sidecar_safe;
 struct qa_state hw;struct qas_owner owner;struct qas_result result;
 uint64_t observations,copy_retries;
 uint32_t configured_dcs,first_dcs,last_dcs,last_target,last_count,last_again,last_fifo,last_command,last_dda,last_dmac;
 uint64_t nonzero,transitions,clips,absolute_sum;int32_t minimum,maximum,last_sample;unsigned stats_seen;
 uint8_t packet[QAS_PERIOD_BYTES];
 volatile sig_atomic_t stop_requested;
};
typedef int (*qas_consume)(void *,const uint8_t *,size_t,uint64_t,uint64_t,uint64_t);
static int qas_stream_guards(struct qas_capture *s)
{
 for(unsigned n=0;n<QA_SIZE;n++)if(!qas_owns_address(QA_BASE+n)&&!(s->sidecar_started&&s->sidecar->owns_guard(s->sidecar->opaque,QA_BASE+n))&&s->hw.io.ram[n]!=QA_GUARD)
  return qa_fail(&s->hw,QA_GUARD_ERROR,QA_PDMA,QA_BASE+n);
 if(s->sidecar_started&&!s->sidecar->guards(s->sidecar->opaque))return qa_fail(&s->hw,QA_GUARD_ERROR,QA_PDMA,QA_DESCRIPTOR);
 return 1;
}
static int qas_stream_observe(struct qas_capture *s,uint64_t *us)
{
 struct qa_state *h=&s->hw;
 uint32_t target=qa_read(h,QA_PDMA,QA_CH+4),count=qa_read(h,QA_PDMA,QA_CH+8);
 uint32_t again=qa_read(h,QA_PDMA,QA_CH+4),dcs=qa_read(h,QA_PDMA,QA_CH+0x10),fifo=qa_read(h,QA_AIC,0x14);
 if(!s->observations)s->first_dcs=dcs;
 s->observations++;s->last_dcs=dcs;s->last_target=target;s->last_count=count;s->last_again=again;s->last_fifo=fifo;
 s->last_command=qa_read(h,QA_PDMA,QA_CH+0x14);s->last_dda=qa_read(h,QA_PDMA,QA_CH+0x18);s->last_dmac=qa_read(h,QA_PDMA,0x1000);
 *us=h->io.time_us(h->io.opaque);
 int observed=qas_owner_observe(&s->owner,target,count,again,dcs,fifo,s->last_dda,*us);
 if(observed==1){
  const uint64_t period_us=(uint64_t)QAS_PERIOD_SAMPLES*1000000u/QA_RATE;
  if(s->owner.produced>UINT64_MAX/period_us)return qas_fault(&s->owner,QAS_COUNTER);
  uint64_t nominal=s->owner.produced*period_us,elapsed=*us-s->result.first_us;
  uint64_t slack=QAS_POLL_LIMIT_US+nominal/1000u;
  if((nominal>elapsed&&nominal-elapsed>slack)||(elapsed>nominal&&elapsed-nominal>slack+period_us))
   return qas_fault(&s->owner,QAS_CLOCK);
 }
 return observed;
}
/* DMA can advance during the pair of target reads after the private copy.
 * 1: validated copy; 2: unpublished copy expired, fresh ownership required;
 * 0: fault. No expired copy is accepted or published. */
static int qas_stream_finish_copy(struct qas_capture *s,const struct qas_ticket *ticket,uint64_t *now)
{
 for(unsigned attempt=0;attempt<32;attempt++){
  int observed=qas_stream_observe(s,now);
  if(!observed)return 0;
  if(observed==1){
   if(*now<s->owner.held_us)return qas_fault(&s->owner,QAS_CLOCK);
   if(*now-s->owner.held_us>QAS_COPY_LIMIT_US)return 2;
   return qas_owner_copied(&s->owner,ticket,*now);
  }
  if(*now<s->owner.held_us)return qas_fault(&s->owner,QAS_CLOCK);
  if(*now-s->owner.held_us>=QAS_COPY_LIMIT_US)return qas_fault(&s->owner,QAS_LEASE);
  qa_pause(&s->hw,1000);
 }
 return qas_fault(&s->owner,QAS_LEASE);
}
static int qas_stream_capture(struct qas_capture *s,uint64_t target_periods,qas_consume consume,void *opaque)
{
 if(!s||!consume||s->owner.phase!=QAS_NEW||s->result.first_us||target_periods>UINT64_MAX/QAS_PERIOD_SAMPLES)return 0;
 if(s->sidecar&&(!s->sidecar->start||!s->sidecar->poll||!s->sidecar->stop||!s->sidecar->owns_guard||!s->sidecar->guards))return 0;
 struct qa_state *h=&s->hw;int initialized=0,ok=qas_stream_prepare(h);
 if(!ok)goto done;
 initialized=1;for(unsigned n=0;n<QA_SIZE;n++)h->io.ram[n]=QA_GUARD;
 for(unsigned n=0;n<QAS_PERIODS;n++){
  struct qas_descriptor desc=qas_description(n);const uint8_t *p=(const uint8_t*)&desc;
  for(unsigned i=0;i<sizeof desc;i++)h->io.ram[n*sizeof desc+i]=p[i];
 }
 if(!qas_descriptors_valid((const struct qas_descriptor*)h->io.ram,QAS_PERIODS)||!qas_stream_guards(s)){ok=0;goto done;}
 qa_write(h,QA_PDMA,0x1000,0x003f0001u);qa_write(h,QA_PDMA,QA_CH+0xc,7);
 qa_write(h,QA_PDMA,QA_CH+8,QAS_PERIOD_BYTES/16u);
 /* Exact native prep_dma_cyclic -> issue_pending ordering: select 8-word
  * mode BEFORE DDA/doorbell, then enable. H56 selected mode only together
  * with enable after the doorbell. Do not weaken any DCS/error validation. */
 qa_write(h,QA_PDMA,QA_CH+0x10,1u<<30);
 s->configured_dcs=qa_read(h,QA_PDMA,QA_CH+0x10);
 if(h->fault||s->configured_dcs!=(1u<<30)){qa_fail(h,QA_DMA_ERROR,QA_PDMA,QA_CH+0x10);ok=0;goto done;}
 qa_write(h,QA_PDMA,QA_CH+0x18,QA_DESCRIPTOR);
 qa_write(h,QA_PDMA,0x100c,1u<<7);qa_write(h,QA_PDMA,QA_CH+0x10,0x40000001u);
 s->result.first_us=h->io.time_us(h->io.opaque);s->result.pcm_hash=2166136261u;
 s->minimum=32767;s->maximum=-32768;
 if(h->fault||!qas_owner_start(&s->owner,s->result.first_us)){ok=0;goto done;}
 qa_write(h,QA_AIC,4,0x00019001u);
 if(s->sidecar&&!h->fault){
  s->sidecar_started=1;
  if(!s->sidecar->start(s->sidecar->opaque,s)){qa_fail(h,QA_ARGUMENT,QA_AIC,4);ok=0;goto done;}
 }
 while(!h->fault&&s->owner.phase==QAS_RUN&&!s->stop_requested&&(!target_periods||s->result.periods<target_periods)){
  if(s->result.polls==UINT64_MAX){qas_fault(&s->owner,QAS_COUNTER);ok=0;break;}
  s->result.polls++;uint64_t now;
  int observed=qas_stream_observe(s,&now);if(!observed){ok=0;break;}
  if(s->sidecar_started&&!s->sidecar->poll(s->sidecar->opaque,now)){
   qa_fail(h,QA_ARGUMENT,QA_AIC,4);ok=0;break;
  }
  /* DAC/finite-TX work can be preempted. Reobserve RX before taking a
   * copy lease: neither a stale time nor a stale DMA slot grants ownership. */
  if(s->sidecar_started){observed=qas_stream_observe(s,&now);if(!observed){ok=0;break;}}
  if(observed==2){qa_pause(h,1000);continue;}
  struct qas_ticket t;int ready=qas_owner_begin(&s->owner,now,&t);
  if(!ready){ok=0;break;}
  if(ready==2){qa_pause(h,1000);continue;}
  unsigned copied=0;
  for(unsigned attempt=0;attempt<3;attempt++){
   unsigned offset=t.address-QA_BASE;
   for(unsigned i=0;i<QAS_PERIOD_BYTES;i++)s->packet[i]=h->io.ram[offset+i];
   int checked=qas_stream_finish_copy(s,&t,&now);
   if(checked==1){copied=1;break;}
   if(!checked){ok=0;break;}
   if(attempt==2){qas_fault(&s->owner,QAS_LEASE);ok=0;break;}
   if(s->copy_retries==UINT64_MAX){qas_fault(&s->owner,QAS_COUNTER);ok=0;break;}
   if(!qas_owner_retry_copy(&s->owner,&t,now)){ok=0;break;}
   s->copy_retries++;
   unsigned fresh=0;
   for(unsigned observation=0;observation<32;observation++){
    int observed=qas_stream_observe(s,&now);
    if(!observed){ok=0;break;}
    if(observed==1){fresh=1;break;}
    qa_pause(h,1000);
   }
   if(!ok)break;
   if(!fresh){qas_fault(&s->owner,QAS_LEASE);ok=0;break;}
   if(qas_owner_begin(&s->owner,now,&t)!=1){ok=0;break;}
  }
  if(!copied){ok=0;break;}
  for(unsigned i=0;i<QAS_PERIOD_BYTES;i++)s->result.pcm_hash=(s->result.pcm_hash^s->packet[i])*16777619u;
  for(unsigned i=0;i<QAS_PERIOD_BYTES;i+=2){
   unsigned word=(unsigned)s->packet[i]|(unsigned)s->packet[i+1]<<8;int value=word<32768?(int)word:(int)word-65536;
   unsigned magnitude=value<0?(unsigned)-value:(unsigned)value;
   if(s->absolute_sum>UINT64_MAX-magnitude){qas_fault(&s->owner,QAS_COUNTER);ok=0;break;}
   s->absolute_sum+=magnitude;s->nonzero+=value!=0;s->clips+=value<=-32760||value>=32760;
   if(s->stats_seen)s->transitions+=value!=s->last_sample;
   if(value<s->minimum)s->minimum=value;if(value>s->maximum)s->maximum=value;
   s->last_sample=value;s->stats_seen=1;
  }
  if(!ok)break;
  if(now>UINT64_MAX-20000){qas_fault(&s->owner,QAS_CLOCK);ok=0;break;}
  uint64_t sample_stamp;
  if(!qas_sample_stamp(s->owner.produced,t.serial,(s->last_target-QA_PCM)%QAS_PERIOD_BYTES,now,&sample_stamp)){qas_fault(&s->owner,QAS_CLOCK);ok=0;break;}
  int delivered=consume(opaque,s->packet,sizeof s->packet,t.serial,sample_stamp,now+20000);
  uint64_t end=h->io.time_us(h->io.opaque);
  if(!end||end<now||delivered<0||delivered>1){qas_fault(&s->owner,QAS_ARGUMENT);ok=0;break;}
  /* The packet is a verified private copy; a slow/no reader can drop that
   * consumer without ever exposing a slot that DMA is currently writing. */
  /* The consumer reports whether this verified private packet was sent.
   * Scheduling after a successful nonblocking send cannot undo delivery.
   * DMA ownership was released before this callback. */
  s->result.periods++;s->result.samples+=QAS_PERIOD_SAMPLES;
  if(delivered)s->result.delivered++;else s->result.discarded++;
 }
 if(h->fault||s->owner.phase!=QAS_RUN)ok=0;
done:
 /* TX must release shared codec/memory before the baseline stops RX. Even
  * after an earlier MMIO failure cleanup delays must still advance time. */
 if(s->sidecar_started){
  for(unsigned attempt=0;attempt<400;attempt++){
   int stopped=s->sidecar->stop(s->sidecar->opaque,h->io.time_us(h->io.opaque));
   if(stopped==1){s->sidecar_safe=1;break;}
   if(stopped!=2)break;
   h->io.delay_us(h->io.opaque,1000);
  }
  if(!s->sidecar_safe){qa_fail(h,QA_ARGUMENT,QA_AIC,4);ok=0;}
 }
 qa_stop(h);
 unsigned rx_off=h->owned&&!(qa_read(h,QA_AIC,4)&0x8001u),dma_off=h->owned&&!(qa_read(h,QA_PDMA,QA_CH+0x10)&1u);
 s->result.stopped=h->note.stopped;s->result.safe_to_release=(!s->sidecar_started||s->sidecar_safe)&&(!h->owned||(rx_off&&dma_off&&s->result.stopped));
 if(s->owner.phase==QAS_RUN||s->owner.phase==QAS_FAULT){if(!qas_owner_stopped(&s->owner,rx_off,dma_off))ok=0;}
 s->result.last_us=h->io.time_us?h->io.time_us(h->io.opaque):0;
 if(initialized){s->result.guards=qas_stream_guards(s);if(!s->result.guards)ok=0;}
 if(ok&&s->result.samples&&(!s->nonzero||!s->transitions)){qa_fail(h,QA_SILENT,QA_AIC,0x34);ok=0;}
 if(!s->result.safe_to_release||h->fault||s->owner.error)ok=0;
 s->result.fault=h->fault?h->fault:s->owner.error;
 return ok;
}
#endif

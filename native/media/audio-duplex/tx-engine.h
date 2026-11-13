/* SPDX-License-Identifier: GPL-2.0-only
 * Finite polled TX owner. IO callbacks remain isolated from the capture
 * engine: only AIC TX fields and PDMAchannel6 may be written. No clocks,
 * codec power, GPIO, ADC/channel7, global DMA reset, allocation or OS calls.
 */
#ifndef QC5_DUPLEX_TX_ENGINE_H
#define QC5_DUPLEX_TX_ENGINE_H
#include "aic-duplex-plan.h"
#include "talk-batch.h"
#include "../audio-continuous/stream-status.h"
enum qdt_region {QDT_AIC,QDT_PDMA};
enum qdt_phase {QDT_NEW,QDT_IDLE,QDT_RUN,QDT_DRAIN,QDT_FAULT};
enum qdt_error {QDT_OK,QDT_ARGUMENT,QDT_BUSY,QDT_RX,QDT_WRITE,QDT_CLOCK,QDT_DMA,QDT_SNAPSHOT,QDT_MEMORY,QDT_EARLY,QDT_TIMEOUT,QDT_UNDERRUN,QDT_STOP};
#define QDT_CH (6u*32u)
#define QDT_POLL_US UINT64_C(25000)
#define QDT_PREFETCH_US UINT64_C(5000)
/* Native TFL is six bits; at16kHz the entire64sample FIFO lasts4ms. */
#define QDT_FIFO_US UINT64_C(5000)
struct qdt_io {
 void *opaque;uint32_t (*read)(void *,unsigned,unsigned);
 int (*write)(void *,unsigned,unsigned,uint32_t);
 uint64_t (*time_us)(void *);void (*pause_us)(void *,unsigned);
};
struct qdt_engine {
 struct qdt_io io;volatile uint8_t *page;struct qad_aic_plan plan;
 uint64_t started_us,last_us,nominal_us,completed_batches,startup_underruns,halt_acks,completion_reobserved;
 uint32_t frames,bytes,hash,phase,error,owned,stopped,primed;
 uint32_t last_dcs,last_source,last_again,last_count,last_command,last_target,last_dda,last_fifo;
 uint32_t rx_control,rx_status,rx_dcs,rx_fault;
 uint32_t setup_fault,setup_dmac,setup_dcs,setup_frame,setup_control,TX_start_attempted;
 uint32_t ack_phase,ack_before,ack_after,ack_dcs;
};
static int qdt_fail(struct qdt_engine *s,unsigned error)
{if(s){if(!s->error)s->error=error;s->phase=QDT_FAULT;}return 0;}
static uint32_t qdt_read(struct qdt_engine *s,unsigned region,unsigned offset)
{return s->io.read(s->io.opaque,region,offset);}
static int qdt_write(struct qdt_engine *s,unsigned region,unsigned offset,uint32_t value)
{return s->io.write(s->io.opaque,region,offset,value)?1:qdt_fail(s,QDT_WRITE);}
static int qdt_rx_alive(struct qdt_engine *s)
{
 uint32_t control=qdt_read(s,QDT_AIC,4),dcs=qdt_read(s,QDT_PDMA,7u*32u+0x10),status=qdt_read(s,QDT_AIC,0x14);
 unsigned fault=((control&~QAD_AIC_TX_FIELDS)!=0x19001u?1u:0u)|(!qas_dma_status_valid(dcs)?2u:0u)|(status&0x40u?4u:0u);
 if(fault&&!s->rx_fault){s->rx_fault=fault;s->rx_control=control;s->rx_dcs=dcs;s->rx_status=status;}
 return !fault;
}
static int qdt_memory(struct qdt_engine *s)
{
 for(unsigned slot=0;slot<4;slot++){
  struct qas_descriptor expected={0};if(!slot)expected=qad_finite_description(s->frames);
  const uint8_t *p=(const uint8_t*)&expected;
  for(unsigned n=0;n<32;n++)if(s->page[QAS_DESC_BYTES+slot*32+n]!=p[n])return 0;
 }
 for(unsigned n=QAD_END-QA_DESCRIPTOR;n<4096;n++)if(s->page[n]!=0xa5)return 0;
 return 1;
}
static uint32_t qdt_hash(struct qdt_engine *s)
{
 uint32_t h=2166136261u;
 for(unsigned n=QAS_DESC_BYTES;n<QAD_END-QA_DESCRIPTOR;n++)h=(h^s->page[n])*16777619u;
 return h;
}
/* A deliberate abort can latch global HLT even after CTE was cleared.
 * Acknowledge only this owned TX stop, never AR, a foreign running channel,
 * or a global reset. Per-channel fault checks are repeated after the write. */
static int qdt_other_channels_safe(struct qdt_engine *s)
{
 for(unsigned ch=0;ch<32;ch++){
  uint32_t dcs=qdt_read(s,QDT_PDMA,ch*32u+0x10);
  if(ch==7){if(!qas_dma_status_valid(dcs))return 0;}
  else if(ch==6){if(dcs&0x11u)return 0;}
  else if(dcs&0x15u)return 0;
 }
 return 1;
}
static int qdt_ack_owned_halt(struct qdt_engine *s)
{
 uint32_t global=qdt_read(s,QDT_PDMA,0x1000);
 if(global==0x003f0001u)return 1;
 s->ack_phase=1;s->ack_before=global;
 if(global!=0x003f0009u||!s->TX_start_attempted||!s->stopped||s->halt_acks==UINT64_MAX)return qdt_fail(s,QDT_DMA);
 for(unsigned attempt=0;attempt<3;attempt++){
  s->ack_phase=2;
  if(!qdt_other_channels_safe(s)||!qdt_rx_alive(s))return qdt_fail(s,QDT_DMA);
  s->ack_dcs=qdt_read(s,QDT_PDMA,QDT_CH+0x10);
  if(s->ack_dcs&0x11u)return qdt_fail(s,QDT_DMA);
  /* HLT can arrive AFTER CTE was disabled. Retire the owned channel's
   * newly latched HLT before acknowledging the controller summary. */
  if(s->ack_dcs&4u){s->ack_phase=3;if(!qdt_write(s,QDT_PDMA,QDT_CH+0x10,0))return 0;}
  s->io.pause_us(s->io.opaque,2);
  s->ack_dcs=qdt_read(s,QDT_PDMA,QDT_CH+0x10);
  if(s->ack_dcs&0x11u)return qdt_fail(s,QDT_DMA);
  if(s->ack_dcs&4u)continue;
  s->ack_phase=4;s->ack_after=qdt_read(s,QDT_PDMA,0x1000);
  if(s->ack_after!=0x003f0001u){
   if(s->ack_after!=0x003f0009u||!qdt_other_channels_safe(s))return qdt_fail(s,QDT_DMA);
   if(!qdt_write(s,QDT_PDMA,0x1000,0x003f0001u))return 0;
  }
  s->io.pause_us(s->io.opaque,2);s->ack_phase=5;
  if(!qdt_other_channels_safe(s)||!qdt_rx_alive(s))return qdt_fail(s,QDT_DMA);
  s->ack_dcs=qdt_read(s,QDT_PDMA,QDT_CH+0x10);s->ack_after=qdt_read(s,QDT_PDMA,0x1000);
  if(s->ack_dcs&0x11u)return qdt_fail(s,QDT_DMA);
  if(s->ack_after==0x003f0001u&&!(s->ack_dcs&4u)){s->halt_acks++;s->ack_phase=6;return 1;}
  if(s->ack_after!=0x003f0009u)return qdt_fail(s,QDT_DMA);
 }
 return qdt_fail(s,QDT_DMA);
}

/* A known-safe stop permits buffer reuse even if the error stays latched.
 * Cleanup deliberately bypasses the previous failed write's error latch. */
static int qdt_stop(struct qdt_engine *s)
{
 if(!s||!s->owned)return 0;
 s->stopped=0;
 for(unsigned attempt=0;attempt<3;attempt++){
  uint32_t control=qdt_read(s,QDT_AIC,4);
  int a=s->io.write(s->io.opaque,QDT_AIC,4,control&~0x4102u);
  int b=s->io.write(s->io.opaque,QDT_PDMA,QDT_CH+0x10,0);
  if(a&&b&&!(qdt_read(s,QDT_AIC,4)&0x4102u)&&!(qdt_read(s,QDT_PDMA,QDT_CH+0x10)&1u)){s->stopped=1;break;}
 }
 if(!s->stopped)return qdt_fail(s,QDT_STOP);
 s->io.pause_us(s->io.opaque,2);
 if(!qdt_ack_owned_halt(s))return 0;
 if(!qdt_rx_alive(s))qdt_fail(s,QDT_RX);
 if(s->frames&&(!qdt_memory(s)||qdt_hash(s)!=s->hash))qdt_fail(s,QDT_MEMORY);
 s->phase=s->error?QDT_FAULT:QDT_IDLE;return 1;
}
static int qdt_init(struct qdt_engine *s,const struct qdt_io *io,volatile uint8_t *page,size_t capacity)
{
 if(!s||!io||!io->read||!io->write||!io->time_us||!io->pause_us||!page||capacity<4096||s->phase!=QDT_NEW||s->owned)return 0;
 s->io=*io;s->page=page;
 if(!qad_aic_plan(qdt_read(s,QDT_AIC,0),qdt_read(s,QDT_AIC,4),&s->plan)||
    qdt_read(s,QDT_PDMA,0x1000)!=0x003f0001u||(qdt_read(s,QDT_PDMA,QDT_CH+0x10)&1u))return qdt_fail(s,QDT_BUSY);
 if(!qdt_rx_alive(s))return qdt_fail(s,QDT_RX);
 s->owned=s->stopped=1;s->phase=QDT_IDLE;return 1;
}
static int qdt_start(struct qdt_engine *s,unsigned frames)
{
 if(!s||s->phase!=QDT_IDLE||s->error||!s->stopped||frames<1||frames>4)return 0;
 s->setup_fault=0;
 if(!qdt_rx_alive(s)){s->setup_fault=1;goto setup_failed;}
 s->setup_dmac=qdt_read(s,QDT_PDMA,0x1000);
 if(s->setup_dmac!=0x003f0001u){s->setup_fault=2;goto setup_failed;}
 s->setup_dcs=qdt_read(s,QDT_PDMA,QDT_CH+0x10);
 if(s->setup_dcs&1u){s->setup_fault=4;goto setup_failed;}
 s->setup_frame=qdt_read(s,QDT_AIC,0);s->setup_control=qdt_read(s,QDT_AIC,4);
 if(!qad_aic_plan(s->setup_frame,s->setup_control,&s->plan)){s->setup_fault=8;goto setup_failed;}
 goto setup_ok;
setup_failed:qdt_fail(s,QDT_RX);qdt_stop(s);return 0;
setup_ok:;
 s->frames=frames;s->bytes=frames*640u;s->nominal_us=(uint64_t)frames*20000u;s->primed=0;
 if(!qdt_memory(s))return qdt_fail(s,QDT_MEMORY);
 s->hash=qdt_hash(s);uint64_t begun=s->io.time_us(s->io.opaque),now=begun;
 if(!begun||begun>UINT64_MAX-s->nominal_us-QDT_POLL_US)return qdt_fail(s,QDT_CLOCK);
 s->stopped=0;
 if(!qdt_write(s,QDT_AIC,0,s->plan.frame_zero)||!qdt_write(s,QDT_AIC,4,s->plan.tx_flush))goto failed;
 for(unsigned tries=0;;tries++){
  now=s->io.time_us(s->io.opaque);
  if(!now||now<begun){qdt_fail(s,QDT_CLOCK);goto failed;}
  if(!(qdt_read(s,QDT_AIC,4)&0x100u))break;
  if(now-begun>=20000||tries>=20000){qdt_fail(s,QDT_TIMEOUT);goto failed;}
  s->io.pause_us(s->io.opaque,1);
 }
 if(qdt_read(s,QDT_AIC,0)!=s->plan.frame_zero||qdt_read(s,QDT_AIC,4)!=s->plan.tx_prepared){qdt_fail(s,QDT_WRITE);goto failed;}
 /* Native status bits clear on zero: preserve any new RXoverrun by writing
  * ones everywhere except TXunderrun. Never clear the microphone's flag. */
 if(!qdt_write(s,QDT_AIC,0x14,~0x20u)||!qdt_rx_alive(s)){if(!s->error)qdt_fail(s,QDT_RX);goto failed;}
 if(!qdt_write(s,QDT_PDMA,QDT_CH+0xc,6)||!qdt_write(s,QDT_PDMA,QDT_CH+8,s->bytes/16u)||
    !qdt_write(s,QDT_PDMA,QDT_CH+0x10,0x40000000u))goto failed;
 if(qdt_read(s,QDT_PDMA,QDT_CH+0x10)!=0x40000000u){qdt_fail(s,QDT_DMA);goto failed;}
 s->TX_start_attempted=1;
 if(!qdt_write(s,QDT_PDMA,QDT_CH+0x18,QAD_DESCRIPTOR)||!qdt_write(s,QDT_PDMA,0x100c,1u<<6)||
    !qdt_write(s,QDT_PDMA,QDT_CH+0x10,0x40000001u))goto failed;
 s->started_us=s->last_us=s->io.time_us(s->io.opaque);
 if(!s->started_us||s->started_us<now||s->started_us>UINT64_MAX-s->nominal_us-QDT_POLL_US){qdt_fail(s,QDT_CLOCK);goto failed;}
 if(!qdt_write(s,QDT_AIC,4,s->plan.tx_enabled))goto failed;
 s->phase=QDT_RUN;return 1;
failed:qdt_stop(s);return 0;
}
/* A finite transfer may finish between the channel snapshot and the FIFO
 * status read. TUR is expected after its last sample, but is a fault while
 * an incomplete transfer is still feeding the FIFO. Reobserve completion;
 * do not clear TUR or forgive an incomplete, corrupt or early transfer. */
static int qdt_completed_after_status(struct qdt_engine *s,uint64_t now)
{
 s->last_dcs=qdt_read(s,QDT_PDMA,QDT_CH+0x10);
 uint32_t control=s->last_dcs&~QAS_DCS_CDOA_MASK;
 if(control!=0x40000001u&&control!=0x40000008u&&control!=0x40000009u){qdt_fail(s,QDT_DMA);return -1;}
 if(!(s->last_dcs&8u))return 0;
 s->last_source=qdt_read(s,QDT_PDMA,QDT_CH);s->last_count=qdt_read(s,QDT_PDMA,QDT_CH+8);
 s->last_target=qdt_read(s,QDT_PDMA,QDT_CH+4);s->last_command=qdt_read(s,QDT_PDMA,QDT_CH+0x14);
 s->last_again=qdt_read(s,QDT_PDMA,QDT_CH);
 if(s->last_source!=QAD_PCM+s->bytes||s->last_again!=s->last_source||s->last_count||
    s->last_target!=0x10020034u||s->last_command!=0x0080a300u){qdt_fail(s,QDT_SNAPSHOT);return -1;}
 if(now-s->started_us+QDT_FIFO_US<s->nominal_us){qdt_fail(s,QDT_EARLY);return -1;}
 if(!qdt_rx_alive(s)){qdt_fail(s,QDT_RX);return -1;}
 if(qdt_read(s,QDT_PDMA,0x1000)!=0x003f0001u){qdt_fail(s,QDT_DMA);return -1;}
 if(s->completion_reobserved==UINT64_MAX){qdt_fail(s,QDT_ARGUMENT);return -1;}
 s->completion_reobserved++;s->phase=QDT_DRAIN;return 1;
}
/* 1 pending,2 completed and stopped,0 failed. A moving snapshot never
 * renews the absolute transfer deadline. No wait loop blocks capture. */
static int qdt_poll(struct qdt_engine *s)
{
 if(!s||s->error||(s->phase!=QDT_RUN&&s->phase!=QDT_DRAIN))return 0;
 uint64_t now=s->io.time_us(s->io.opaque);
 if(!now||now<s->last_us){qdt_fail(s,QDT_CLOCK);goto failed;}
 /* Scheduling latency is not a DMA failure. A complete finite transfer
  * may be confirmed late; an active transfer retains its absolute bound. */
 if(now-s->last_us>250000u){qdt_fail(s,QDT_TIMEOUT);goto failed;}
 unsigned overdue=now-s->started_us>s->nominal_us+QDT_POLL_US;
 unsigned was_primed=s->primed;
 s->last_us=now;
 if(!qdt_rx_alive(s)){qdt_fail(s,QDT_RX);goto failed;}
 if(qdt_read(s,QDT_PDMA,0x1000)!=0x003f0001u){qdt_fail(s,QDT_DMA);goto failed;}
 if(s->phase==QDT_RUN){
  s->last_source=qdt_read(s,QDT_PDMA,QDT_CH);s->last_count=qdt_read(s,QDT_PDMA,QDT_CH+8);
  s->last_target=qdt_read(s,QDT_PDMA,QDT_CH+4);s->last_command=qdt_read(s,QDT_PDMA,QDT_CH+0x14);
  s->last_dcs=qdt_read(s,QDT_PDMA,QDT_CH+0x10);s->last_dda=qdt_read(s,QDT_PDMA,QDT_CH+0x18);
  s->last_again=qdt_read(s,QDT_PDMA,QDT_CH);
  uint32_t control=s->last_dcs&~QAS_DCS_CDOA_MASK;
  if(control!=0x40000001u&&control!=0x40000008u&&control!=0x40000009u){qdt_fail(s,QDT_DMA);goto failed;}
  /* LINK is permanently clear. DDA/CDOA are unused after the sole initial
   * fetch; only this descriptor's bounded source/count can feed the FIFO. */
  int shape=s->last_target==0x10020034u&&s->last_command==0x0080a300u&&
   s->last_source>=QAD_PCM&&s->last_source-QAD_PCM<=s->bytes&&!(s->last_source&15u)&&
   s->last_again>=QAD_PCM&&s->last_again-QAD_PCM<=s->bytes&&!(s->last_again&15u)&&
   s->last_count<=s->bytes/16u;
  if(!shape){
   if(!s->primed&&now-s->started_us<QDT_PREFETCH_US)return 1;
   qdt_fail(s,QDT_SNAPSHOT);goto failed;
  }
  if(s->last_source!=s->last_again||s->last_source-QAD_PCM+s->last_count*16u!=s->bytes){
   if(overdue){qdt_fail(s,QDT_TIMEOUT);goto failed;}return 1;
  }
  if(s->last_source>QAD_PCM)s->primed=1;
  if(s->last_dcs&8u){
   if(s->last_count||s->last_source!=QAD_PCM+s->bytes){if(overdue){qdt_fail(s,QDT_TIMEOUT);goto failed;}return 1;}
   if(now-s->started_us+QDT_FIFO_US<s->nominal_us){qdt_fail(s,QDT_EARLY);goto failed;}
   s->phase=QDT_DRAIN;
  }
 }
 s->last_fifo=qdt_read(s,QDT_AIC,0x14);
 if(s->phase==QDT_RUN&&(s->last_fifo&0x20u)){
  int completed=qdt_completed_after_status(s,now);
  if(completed<0)goto failed;
 }
 if(s->phase==QDT_RUN){
  if(s->last_fifo&0x20u){
   /* Replay and TX DMA are enabled together. An empty initial FIFO may
    * latch TUR before the first request has filled it. Permit one initial
    * latch only during the original5ms fetch window, after a coherent
    * in-bounds DMA snapshot. Never forgive a later playback underrun. */
   if(was_primed||now-s->started_us>=QDT_PREFETCH_US){qdt_fail(s,QDT_UNDERRUN);goto failed;}
   if(!s->primed)return 1;
   if(s->startup_underruns==UINT64_MAX||!qdt_write(s,QDT_AIC,0x14,~0x20u)||!qdt_rx_alive(s)){if(!s->error)qdt_fail(s,QDT_RX);goto failed;}
   if(qdt_read(s,QDT_AIC,0x14)&0x20u){qdt_fail(s,QDT_UNDERRUN);goto failed;}
   s->startup_underruns++;
  }
  if(overdue){qdt_fail(s,QDT_TIMEOUT);goto failed;}return 1;
 }
 if(s->last_fifo&0x3f00u){if(overdue){qdt_fail(s,QDT_TIMEOUT);goto failed;}return 1;}
 if(!qdt_stop(s)||s->error)return 0;
 if(s->completed_batches==UINT64_MAX)return qdt_fail(s,QDT_ARGUMENT);
 s->completed_batches++;return 2;
failed:qdt_stop(s);return 0;
}
#endif

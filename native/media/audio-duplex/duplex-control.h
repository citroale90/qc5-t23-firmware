/* SPDX-License-Identifier: GPL-2.0-only
 * Single owner of DAC power, finite TX DMA and bounded input queue.
 * No OS/network/device access beyond the already-proved IO callbacks.
 * A ready notification is emitted only after the original DAC settle time;
 * a stopped notification requires DMA stop AND complete DAC power-down.
 */
#ifndef QC5_DUPLEX_CONTROL_H
#define QC5_DUPLEX_CONTROL_H
#include "codec-async.h"
#include "tx-engine.h"
#include "talk-queue.h"
enum qdu_phase {QDU_OFF,QDU_WARM,QDU_READY,QDU_PLAY,QDU_STOPPING,QDU_FAULT};
enum qdu_event {QDU_NONE,QDU_EVENT_READY,QDU_EVENT_STOPPED,QDU_EVENT_FAULT};
enum qdu_error {QDU_OK,QDU_INIT,QDU_CLOCK,QDU_CODEC,QDU_DMA,QDU_QUEUE};
struct qdu_control {
 struct qdt_engine tx;struct qac_state codec;struct qat_queue queue;struct qat_ticket ticket;
 uint64_t last_us;uint32_t id;unsigned phase,error,event,event_id,gain;
};
static void qdu_notify(struct qdu_control *s,unsigned event)
{s->event=event;s->event_id=s->id;}
static unsigned qdu_take_event(struct qdu_control *s,uint32_t *id)
{if(!s||!id)return QDU_NONE;unsigned event=s->event;*id=s->event_id;s->event=QDU_NONE;return event;}
static int qdu_shutdown(struct qdu_control *s,uint64_t now)
{
 qat_release(&s->queue);s->event=QDU_NONE;
 if(!s->tx.stopped&&!qdt_stop(&s->tx)){
  if(!s->error)s->error=QDU_DMA;s->phase=QDU_FAULT;(void)qac_mute(&s->codec);qdu_notify(s,QDU_EVENT_FAULT);return 0;
 }
 if(s->queue.held&&!qat_complete(&s->queue,&s->ticket,1,now)){
  if(!s->error)s->error=QDU_QUEUE;s->phase=QDU_FAULT;(void)qac_mute(&s->codec);qdu_notify(s,QDU_EVENT_FAULT);return 0;
 }
 if(s->codec.phase==QAC_STOPPING){s->phase=QDU_STOPPING;return 1;}
 if(!qac_begin_stop(&s->codec,1,now)){
  if(!s->error)s->error=QDU_CODEC;s->phase=QDU_FAULT;(void)qac_mute(&s->codec);qdu_notify(s,QDU_EVENT_FAULT);return 0;
 }
 s->phase=QDU_STOPPING;return 1;
}
static int qdu_fail(struct qdu_control *s,unsigned error,uint64_t now)
{if(!s->error)s->error=error;(void)qdu_shutdown(s,now);return s->phase==QDU_STOPPING?1:0;}
static int qdu_init(struct qdu_control *s,const struct qdt_io *tx,const struct qac_io *codec,
                    volatile uint8_t *page,size_t bytes,unsigned seconds,unsigned gain)
{
 if(!s||s->phase!=QDU_OFF||s->tx.owned||s->codec.owned||gain>31||!qat_configure(&s->queue,seconds))return 0;
 s->gain=gain;
 if(!qdt_init(&s->tx,tx,page,bytes)||!qac_init(&s->codec,codec)){
  s->error=QDU_INIT;s->phase=QDU_FAULT;return 0;
 }
 return 1;
}
static int qdu_start(struct qdu_control *s,uint32_t id,uint64_t now)
{
 if(!s||s->error||!s->tx.owned||!s->codec.owned||!id)return 0;
 if(s->phase==QDU_WARM||s->phase==QDU_READY||s->phase==QDU_PLAY)
  return s->id==id&&qat_start(&s->queue,id,now);
 if(s->phase!=QDU_OFF||!qat_start(&s->queue,id,now))return 0;
 s->id=id;s->last_us=now;s->event=QDU_NONE;
 if(!qac_begin(&s->codec,s->gain,now)){(void)qdu_fail(s,QDU_CODEC,now);return 0;}
 s->phase=QDU_WARM;return 1;
}
static int qdu_push(struct qdu_control *s,uint32_t id,const uint8_t *codes,size_t bytes,uint64_t now)
{
 if(!s||s->error||(s->phase!=QDU_READY&&s->phase!=QDU_PLAY)||s->id!=id)return 0;
 return qat_push(&s->queue,id,codes,bytes,now);
}
static int qdu_stop(struct qdu_control *s,uint32_t id,uint64_t now)
{
 if(!s||!id||s->id!=id||s->phase==QDU_OFF||s->phase==QDU_FAULT)return 0;
 return qdu_shutdown(s,now);
}
/* Call once per capture-loop poll, not once per125ms microphone packet.
 * 1 active or cleanup pending;2 cleanly idle;0 terminal fault. The error
 * field latches immediately; keep polling until power-down also completes.
 */
static int qdu_poll(struct qdu_control *s,uint64_t now)
{
 if(!s||s->phase==QDU_FAULT)return 0;
 if(s->phase==QDU_OFF)return s->error?0:2;
 if(!now||now<s->last_us)return qdu_fail(s,QDU_CLOCK,now);
 s->last_us=now;
 if(s->phase!=QDU_STOPPING){
  if(!qat_clock(&s->queue,now))return qdu_fail(s,QDU_CLOCK,now);
  if(!s->queue.active){if(!qdu_shutdown(s,now))return 0;}
 }
 if(s->phase==QDU_WARM){
  int result=qac_poll(&s->codec,now);
  if(!result)return qdu_fail(s,QDU_CODEC,now);
  if(result==2){s->phase=QDU_READY;qdu_notify(s,QDU_EVENT_READY);}
 }
 if(s->phase==QDU_PLAY){
  int result=qdt_poll(&s->tx);
  if(!result)return qdu_fail(s,QDU_DMA,now);
  if(result==2){
   if(!qat_complete(&s->queue,&s->ticket,1,now))return qdu_fail(s,QDU_QUEUE,now);
   s->phase=QDU_READY;
  }
 }
 if(s->phase==QDU_READY&&s->queue.count){
  if(!qat_prepare(&s->queue,s->tx.page,4096,s->tx.stopped,now,&s->ticket)){
   if(!s->queue.count&&!s->queue.clock_failed)return 1; /* stale voice discarded */
   return qdu_fail(s,QDU_QUEUE,now);
  }
  if(!qdt_start(&s->tx,s->ticket.frames))return qdu_fail(s,QDU_DMA,now);
  s->phase=QDU_PLAY;
 }
 if(s->phase==QDU_STOPPING){
  int result=qac_poll(&s->codec,now);
  if(!result){s->error=s->error?s->error:QDU_CODEC;s->phase=QDU_FAULT;qdu_notify(s,QDU_EVENT_FAULT);return 0;}
  if(result==2){
   s->phase=s->error?QDU_FAULT:QDU_OFF;qdu_notify(s,s->error?QDU_EVENT_FAULT:QDU_EVENT_STOPPED);return s->error?0:2;
  }
 }
 return 1;
}
#endif

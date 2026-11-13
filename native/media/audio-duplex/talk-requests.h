/* SPDX-License-Identifier: GPL-2.0-only
 * Child-side request/ack bridge. No socket is opened here. The existing
 * inherited descriptor transports these records alongside microphone IPC.
 * The caller sends pending bytes with DONTWAIT, then confirms the exact
 * send size. A full output socket cannot block the capture loop.
 */
#ifndef QC5_TALK_REQUESTS_H
#define QC5_TALK_REQUESTS_H
#include "talk-ipc.h"
#include "duplex-control.h"
struct qtr_bridge {
 struct qtip_inbox inbox;struct qdu_control *control;uint8_t nonce[16],pending[QTIP_HEADER+4];
 uint64_t last_sent,voice_received,voice_dropped;
 unsigned pending_bytes,pending_kind;
};
static int qtr_init(struct qtr_bridge *s,struct qdu_control *control,const uint8_t nonce[16])
{
 if(!s||!control||!nonce||s->control||s->pending_bytes||!control->tx.owned||!control->codec.owned)return 0;
 s->control=control;memcpy(s->nonce,nonce,16);return 1;
}
static int qtr_request(struct qtr_bridge *s,const uint8_t *raw,size_t bytes,uint64_t now)
{
 if(!s||!s->control)return 0;struct qtip_record r;struct qdu_control *c=s->control;
 if(!qtip_accept(&s->inbox,raw,bytes,s->nonce,1,now,&r)){
  (void)qdu_fail(c,QDU_QUEUE,now);return 0;
 }
 if(r.kind==QTIP_START){
  unsigned seconds=qasi_u32(r.payload);
  if(s->pending_bytes||c->error)return 0;
  if(c->phase==QDU_OFF&&!qat_configure(&c->queue,seconds))return 0;
  if(c->queue.limit_us!=(uint64_t)seconds*1000000u)return 0;
  return qdu_start(c,r.id,now);
 }
 if(r.kind==QTIP_STOP){
  if(c->id!=r.id||c->phase==QDU_OFF)return 1; /* stale browser release */
  if(s->pending_kind==QTIP_READY){s->pending_kind=s->pending_bytes=0;}
  return qdu_stop(c,r.id,now);
 }
 /* A validated current-session VOICE may arrive while a completed DMA
  * still holds its lease. Retire/rearm it before testing the full queue.
  * STOP keeps its original priority and never enters this path. A genuinely
  * in-flight/full queue still drops rather than overwrite or grow buffers. */
 if(r.id==c->id&&c->queue.count==QAD_PERIODS&&
    (c->phase==QDU_PLAY||c->phase==QDU_READY))(void)qdu_poll(c,now);
 if(s->voice_received==UINT64_MAX){(void)qdu_fail(c,QDU_QUEUE,now);return 0;}
 s->voice_received++;
 if(!qdu_push(c,r.id,r.payload,r.bytes,now)){
  if(s->voice_dropped==UINT64_MAX){(void)qdu_fail(c,QDU_QUEUE,now);return 0;}
  s->voice_dropped++;
 }
 return 1;
}
static int qtr_poll(struct qtr_bridge *s,uint64_t now)
{
 if(!s||!s->control)return 0;struct qdu_control *c=s->control;int result=qdu_poll(c,now);
 if(s->pending_kind==QTIP_READY&&(c->phase==QDU_STOPPING||c->phase==QDU_FAULT)){s->pending_kind=s->pending_bytes=0;}
 if(s->pending_bytes)return result;
 uint32_t id;unsigned event=qdu_take_event(c,&id);if(!event)return result;
 if(!id||s->last_sent==UINT64_MAX){
  if(!c->error)return qdu_fail(c,QDU_QUEUE,now);
  return result; /* terminal fault can have no valid acknowledgement ID */
 }
 unsigned kind=event==QDU_EVENT_READY?QTIP_READY:event==QDU_EVENT_STOPPED?QTIP_STOPPED:QTIP_FAULT;
 uint8_t error[4];qasi_le32(error,c->error);
 int bytes=qtip_pack(s->pending,sizeof s->pending,kind,id,s->last_sent+1,now,s->nonce,
  kind==QTIP_FAULT?error:NULL,kind==QTIP_FAULT?4:0);
 if(!bytes){(void)qdu_fail(c,QDU_QUEUE,now);return 0;}
 s->pending_bytes=(unsigned)bytes;s->pending_kind=kind;return result;
}
static int qtr_sent(struct qtr_bridge *s,size_t bytes)
{
 if(!s||!s->pending_bytes||bytes!=s->pending_bytes||s->last_sent==UINT64_MAX)return 0;
 s->last_sent++;s->pending_bytes=s->pending_kind=0;return 1;
}
#endif

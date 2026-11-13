/* SPDX-License-Identifier: GPL-2.0-or-later
 * Browser epoch -> unique backend press -> private child ACK. No codec,
 * MMIO, video data, heap or public listener in the parent process.
 */
#ifndef QC5_TALK_PARENT_H
#define QC5_TALK_PARENT_H
#include "talk-wire.h"
#include "talk-ipc.h"
enum qtp_phase {QTP_IDLE,QTP_STARTING,QTP_ACTIVE,QTP_STOPPING,QTP_FAILED};
enum qtp_event {QTP_READY,QTP_STOPPED,QTP_BUSY,QTP_ERROR};
typedef int (*qtp_reply)(void *,uint64_t,uint32_t,unsigned);
typedef ssize_t (*qtp_send)(void *,int,const void *,size_t);
struct qtp_parent {
 uint8_t nonce[16];struct qtip_inbox inbox;
 qtp_reply reply;void *reply_opaque;qtp_send send;void *send_opaque;
 uint64_t serial,epoch,requested_us,last_us,voice_sent,voice_dropped,replies,starts,stops;
 uint32_t browser_id,backend_id,next_backend_id;
 uint64_t last_record_stamp,last_record_now,last_record_serial;
 uint32_t last_record_kind,last_record_id,last_record_error,fault_reason,last_write_errno;
 uint64_t bad_clock_now,bad_clock_previous,live_resume_us;
 int fd;unsigned initialized,child_ready,phase,pending,seconds,error;
};
static ssize_t qtp_socket_send(void *opaque,int fd,const void *data,size_t bytes)
{(void)opaque;return send(fd,data,bytes,MSG_NOSIGNAL|MSG_DONTWAIT);}
static int qtp_init(struct qtp_parent *s,int fd,const uint8_t nonce[16],unsigned seconds,qtp_reply reply,void *opaque)
{
 if(!s||s->initialized||fd<0||!nonce||seconds<1||seconds>30||!reply)return 0;
 memcpy(s->nonce,nonce,16);s->fd=fd;s->seconds=seconds;s->reply=reply;s->reply_opaque=opaque;
 s->send=qtp_socket_send;s->initialized=1;return 1;
}
static int qtp_fault(struct qtp_parent *s)
{if(s){s->error=1;s->phase=QTP_FAILED;s->pending=0;}return 0;}
static int qtp_notify(struct qtp_parent *s,unsigned kind)
{
 if(!s->epoch||!s->browser_id)return 1;
 if(s->replies==UINT64_MAX)return qtp_fault(s);
 s->replies++;
 /* Failure to deliver to a browser never invalidates the microphone child.
  * The video socket owner closes only that reader and observe() stops TX. */
 (void)s->reply(s->reply_opaque,s->epoch,s->browser_id,kind);return 1;
}
static int qtp_available(const struct qtp_parent *s)
{return s&&s->initialized&&s->child_ready&&!s->error;}
/* Half duplex is enforced for every live consumer, not only this browser.
 * A buffered ADC frame retains its capture timestamp, including after STOP.
 * The microphone and local recording continue unchanged. */
static int qtp_live_muted(const struct qtp_parent *s,uint64_t capture_us)
{return s&&s->initialized&&(s->phase!=QTP_IDLE||capture_us<s->live_resume_us);}
static int qtp_clock(struct qtp_parent *s,uint64_t now)
{
 if(!s||!s->initialized||s->error)return 0;
 if(!now||now<s->last_us||now>UINT64_MAX-1000000){s->fault_reason=1;s->bad_clock_now=now;s->bad_clock_previous=s->last_us;return 0;}
 s->last_us=now;return 1;
}
static int qtp_write(struct qtp_parent *s,unsigned kind,const uint8_t *data,size_t bytes,uint64_t now)
{
 uint8_t raw[QTIP_MAX];
 if(s->serial==UINT64_MAX)return -1;
 int n=qtip_pack(raw,sizeof raw,kind,s->backend_id,s->serial+1,now,s->nonce,data,bytes);if(!n)return -1;
 ssize_t sent=s->send(s->send_opaque,s->fd,raw,(size_t)n);
 if(sent==n){s->serial++;return 1;}
 if(sent<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==ENOBUFS||errno==EINTR))return 0;
 s->fault_reason=2;s->last_write_errno=errno;return -1;
}
static int qtp_stop(struct qtp_parent *s,uint64_t now)
{
 if(s->phase==QTP_IDLE||s->phase==QTP_STOPPING)return 1;
 if(s->phase==QTP_STARTING&&s->pending==QTIP_START){
  /* Nothing reached the child; there is no physical output to stop. */
  s->pending=0;s->phase=QTP_IDLE;(void)qtp_notify(s,QTP_STOPPED);return 1;
 }
 s->pending=QTIP_STOP;s->phase=QTP_STOPPING;s->requested_us=now;return 1;
}
static int qtp_flush(struct qtp_parent *s,uint64_t now)
{
 if(!qtp_clock(s,now))return qtp_fault(s);
 if(s->pending){
  uint8_t seconds[4];qasi_le32(seconds,s->seconds);unsigned kind=s->pending;
  int sent=qtp_write(s,kind,kind==QTIP_START?seconds:NULL,kind==QTIP_START?4:0,now);
  if(sent<0)return qtp_fault(s);
  if(sent){s->pending=0;if(kind==QTIP_START)s->starts++;else s->stops++;}
 }
 if((s->phase==QTP_STARTING||s->phase==QTP_STOPPING)&&now-s->requested_us>=1000000){
  if(s->phase==QTP_STOPPING){s->fault_reason=3;return qtp_fault(s);}
  return qtp_stop(s,now);
 }
 return 1;
}
static int qtp_observe(struct qtp_parent *s,uint64_t epoch,unsigned connected,uint64_t now)
{
 if(!qtp_clock(s,now))return qtp_fault(s);
 if(s->phase!=QTP_IDLE&&(!connected||epoch!=s->epoch)){
  /* Old-session acknowledgements are never delivered to the new epoch. */
  s->epoch=0;if(!qtp_stop(s,now))return 0;
 }
 return qtp_flush(s,now);
}
static int qtp_browser(struct qtp_parent *s,uint64_t epoch,unsigned opcode,const uint8_t *data,size_t bytes,uint64_t now)
{
 struct qtw_event e;
 if(!epoch||!qtp_clock(s,now)||!qtw_decode(opcode,data,bytes,&e))return 0;
 if(e.kind==QTW_START){
  if(s->phase!=QTP_IDLE){
   if(s->epoch==epoch&&s->browser_id==e.id&&(s->phase==QTP_STARTING||s->phase==QTP_ACTIVE))return 1;
   (void)s->reply(s->reply_opaque,epoch,e.id,QTP_BUSY);return 1;
  }
  if(!qtp_available(s)){(void)s->reply(s->reply_opaque,epoch,e.id,QTP_BUSY);return 1;}
  if(s->next_backend_id==UINT32_MAX)return qtp_fault(s);
  s->backend_id=++s->next_backend_id;s->browser_id=e.id;s->epoch=epoch;
  s->phase=QTP_STARTING;s->pending=QTIP_START;s->requested_us=now;return qtp_flush(s,now);
 }
 if(e.kind==QTW_STOP){
  if(s->epoch!=epoch||s->browser_id!=e.id||s->phase==QTP_IDLE)return 1;
  return qtp_stop(s,now)&&qtp_flush(s,now);
 }
 if(s->phase!=QTP_ACTIVE||s->epoch!=epoch)return 1;
 if(s->voice_sent==UINT64_MAX||s->voice_dropped==UINT64_MAX)return qtp_fault(s);
 int sent=qtp_write(s,QTIP_VOICE,e.voice,320,now);
 if(sent<0)return qtp_fault(s);
 if(sent)s->voice_sent++;else s->voice_dropped++;return 1;
}
/* 0 another protocol,1 accepted QT73,-1 invalid. ACKs never count as a
 * microphone heartbeat; the child's PCM deadline stays independent. */
static int qtp_record(struct qtp_parent *s,const uint8_t *raw,size_t bytes,uint64_t now)
{
 if(bytes<4||qasi_u32(raw)!=QTIP_MAGIC)return 0;
 if(bytes>=QTIP_HEADER){s->last_record_kind=qasi_u32(raw+8);s->last_record_id=qasi_u32(raw+52);
  s->last_record_serial=qasi_u64(raw+16);s->last_record_stamp=qasi_u64(raw+24);s->last_record_now=now;
  s->last_record_error=s->last_record_kind==QTIP_FAULT&&bytes>=QTIP_HEADER+4?qasi_u32(raw+QTIP_HEADER):0;}
 struct qtip_record r;
 if(!qtp_clock(s,now)||!qtip_accept(&s->inbox,raw,bytes,s->nonce,0,now,&r))return qtp_fault(s),-1;
 if(r.id!=s->backend_id||s->phase==QTP_IDLE)return qtp_fault(s),-1;
 if(r.kind==QTIP_READY){
  if(s->phase==QTP_STOPPING)return 1; /* cancelled while DAC was warming */
  if(s->phase!=QTP_STARTING)return qtp_fault(s),-1;
  s->phase=QTP_ACTIVE;return qtp_notify(s,QTP_READY)?1:-1;
 }
 if(r.kind==QTIP_STOPPED){s->live_resume_us=now+700000;s->phase=QTP_IDLE;s->pending=0;return qtp_notify(s,QTP_STOPPED)?1:-1;}
 (void)qtp_notify(s,QTP_ERROR);return qtp_fault(s),-1;
}
#endif

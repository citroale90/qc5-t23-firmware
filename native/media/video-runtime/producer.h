/* SPDX-License-Identifier: GPL-2.0-or-later
 * One synchronous persistent producer, using the already reserved NV12/Helix
 * arena. OS adapters supply verified metering, encoding, bounded publishing
 * and native FIFO writes. The stream is not restarted for each GOP/client.
 */
#ifndef QC5_VIDEO_PRODUCER_H
#define QC5_VIDEO_PRODUCER_H
#include "ownership.h"
#include "../video-gc1084/helix-sequence.h"
#include "../video-gc1084/helix-layout.h"
#define QP_PUBLISH_BUDGET_US 180000u
enum qc5_producer_error { QP_OK=0,QP_ARGUMENT,QP_CAPTURE,QP_METER,QP_SEQUENCE,QP_ENCODE,QP_OUTPUT,QP_CLOCK,QP_COUNTER,QP_QUEUE };
struct qc5_producer {
 struct qc5_video_owner input;
 struct qc5_helix_sequence codec;
 uint8_t *arena;
 uint64_t bytes,published,disconnects,client_epoch;
 unsigned error,record_disabled;uint64_t recorded,record_failures;
};
struct qc5_encoded_output {const uint8_t *data;unsigned bytes,reset_mask;};
struct qc5_frame_output {
 const uint8_t *data;unsigned bytes,idr;
 uint64_t serial,captured_us;
};
struct qc5_producer_io {
 void *opaque;
 int (*meter)(void *,const struct qc5_frame_ticket *,uint64_t);
 int (*encode)(void *,const struct qc5_helix_picture *,struct qc5_encoded_output *);
 uint64_t (*clock_us)(void *);
 /* 1 delivered, 0 reader closed/timed out. All waiting honors the absolute deadline; bounded nonblocking
  * completion is permitted after scheduling delay.
  * Consume synchronously; the AU is overwritten by the next encode. */
 int (*publish)(void *,const struct qc5_frame_output *,uint64_t deadline_us);
 /* Idempotent close, including when publish already closed a slow reader. */
 int (*drop_reader)(void *,uint64_t client_epoch);
 qc5_queue_pair queue_pair;
 int (*record)(void *,const struct qc5_frame_output *,uint64_t deadline_us);
};
static int qp_init(struct qc5_producer *s,uint8_t *arena,size_t bytes)
{
 /* Caller supplies a zeroed object, or one verified OFF by qp_stopped. */
 if(!s||!arena||((uintptr_t)arena&4095)||bytes!=QC5_CAPTURE_RAM_END-QC5_CAPTURE_RAM_BASE||
    (s->arena&&s->input.phase!=QO_OFF))return 0;
 memset(s,0,sizeof *s);s->arena=arena;s->codec=(struct qc5_helix_sequence){.gop=32,.qp=30};
 return qo_init(&s->input);
}
static int qp_fail(struct qc5_producer *s,unsigned error)
{
 if(s){if(!s->error)s->error=error;qo_fault(&s->input);}
 return 0;
}
static int qp_frame_mode(struct qc5_producer *s,const struct qc5_producer_io *io,
 uint32_t y,uint32_t uv,uint64_t captured_us,uint64_t client_epoch,unsigned retire)
{
 if(!s)return 0;
 if(s->error||(s->input.phase!=QO_RUN&&s->input.phase!=QO_DRAIN)||
    (s->input.phase==QO_DRAIN&&!retire))return 0;
 if(!io||!io->meter||!io->encode||!io->clock_us||!io->queue_pair||
    (client_epoch&&(!io->publish||!io->drop_reader))||!s->arena||retire>1)return qp_fail(s,QP_ARGUMENT);
 struct qc5_frame_ticket ticket;
 if(!qo_complete(&s->input,y,uv,captured_us,&ticket))return qp_fail(s,QP_CAPTURE);
 if(!io->meter(io->opaque,&ticket,captured_us))return qp_fail(s,QP_METER);
 if(s->input.completed>120){
  if(client_epoch&&client_epoch!=s->client_epoch){
   if(s->codec.pending)return qp_fail(s,QP_SEQUENCE);
   s->codec.have_reference=0;s->codec.next_frame=s->codec.since_idr=0;
  }
  s->client_epoch=client_epoch;
  struct qc5_helix_picture picture;
  if(!qo_encode_begin(&s->input,&ticket)||!qc5_helix_sequence_next(&s->codec,ticket.bank,&picture))return qp_fail(s,QP_SEQUENCE);
  struct qc5_encoded_output output={0};
  int encoded=io->encode(io->opaque,&picture,&output);
  if(!encoded){(void)qc5_helix_sequence_finish(&s->codec,&picture,0);return qp_fail(s,QP_ENCODE);}
  if(output.data!=s->arena+(QC5_HELIX_AU_PHYS-QC5_CAPTURE_RAM_BASE)||
     !output.bytes||output.bytes>QC5_HELIX_AU_BYTES||output.reset_mask!=3){
   (void)qc5_helix_sequence_finish(&s->codec,&picture,0);return qp_fail(s,QP_OUTPUT);
  }
  if(!qc5_helix_sequence_finish(&s->codec,&picture,1)||!qo_encode_finish(&s->input,&ticket,1))return qp_fail(s,QP_SEQUENCE);
  if(s->bytes>UINT64_MAX-output.bytes)return qp_fail(s,QP_COUNTER);
  s->bytes+=output.bytes;
  if(io->record&&!s->record_disabled){
   uint64_t begin=io->clock_us(io->opaque);
   if(!begin||begin<captured_us||begin>UINT64_MAX-8000)return qp_fail(s,QP_CLOCK);
   struct qc5_frame_output frame={output.data,output.bytes,picture.idr,ticket.serial,captured_us};
   int saved=io->record(io->opaque,&frame,begin+8000);
   uint64_t end=io->clock_us(io->opaque);
   if(!end||end<begin)return qp_fail(s,QP_CLOCK);
   if(saved!=1||end-begin>8000){s->record_disabled=1;s->record_failures++;}else s->recorded++;
  }
  if(client_epoch){
   uint64_t now=io->clock_us(io->opaque);
   if(!now||now<captured_us||now>UINT64_MAX-QP_PUBLISH_BUDGET_US)return qp_fail(s,QP_CLOCK);
   struct qc5_frame_output frame={output.data,output.bytes,picture.idr,ticket.serial,captured_us};
   int delivered=io->publish(io->opaque,&frame,now+QP_PUBLISH_BUDGET_US);
   uint64_t end=io->clock_us(io->opaque);
   if(!end||end<now)return qp_fail(s,QP_CLOCK);
   if(delivered!=0&&delivered!=1)return qp_fail(s,QP_OUTPUT);
   /* Reader timeout/scheduling delay does not reset the ISP or references.
    * Close the consumer and safely return the input bank to the FIFO. */
   /* Successful bounded nonblocking delivery cannot be undone by a
    * scheduler pause AFTER the adapter has already written all bytes. */
   if(delivered){if(s->published==UINT64_MAX)return qp_fail(s,QP_COUNTER);s->published++;}
   else{
    if(s->disconnects==UINT64_MAX)return qp_fail(s,QP_COUNTER);
    if(io->drop_reader(io->opaque,client_epoch)!=1)return qp_fail(s,QP_OUTPUT);
    s->disconnects++;s->client_epoch=0;
   }
  }
 }
 if(!(retire?qo_retire(&s->input,&ticket):qo_requeue(&s->input,&ticket,io->queue_pair,io->opaque)))return qp_fail(s,QP_QUEUE);
 return 1;
}
static int qp_frame(struct qc5_producer *s,const struct qc5_producer_io *io,
 uint32_t y,uint32_t uv,uint64_t captured_us,uint64_t client_epoch)
{return qp_frame_mode(s,io,y,uv,captured_us,client_epoch,0);}
static int qp_request_stop(struct qc5_producer *s)
{return s&&!s->error&&qo_request_stop(&s->input);}
static int qp_stopped(struct qc5_producer *s,unsigned isp_stop,unsigned encoder_idle)
{
 if(!s||!qo_stopped(&s->input,isp_stop,encoder_idle))return 0;
 /* The hardware is now confirmed quiescent even after a failed encode. */
 memset(&s->codec,0,sizeof s->codec);s->client_epoch=0;return 1;
}
#endif

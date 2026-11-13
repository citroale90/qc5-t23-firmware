/* SPDX-License-Identifier: GPL-2.0-or-later
 * Persistent orchestration with the actual native-geometry descriptor/syntax
 * builder. CABAC bytes and I/O are simulated; no real H264 capture claimed.
 */
#include <assert.h>
#include <stdio.h>
#include "producer.h"
static uint8_t arena[QC5_CAPTURE_RAM_END-QC5_CAPTURE_RAM_BASE] __attribute__((aligned(4096)));
enum { F_NONE,F_METER,F_ENCODE,F_POINTER,F_LENGTH,F_RESET,F_QUEUE1,F_QUEUE2,F_CLOCK,F_BACKWARDS,F_SLOW,F_CLOSED };
struct model {
 struct qc5_producer p;
 uint64_t now,captured,epoch;
 unsigned fault,metered,plans,sent,drops,queue_writes,idrs;
 struct qc5_helix_picture picture;
};
static int meter(void *opaque,const struct qc5_frame_ticket *t,uint64_t captured)
{
 struct model *m=opaque;
 assert(t->serial==m->p.input.completed&&t->bank==((t->serial-1)&1));
 assert(m->p.input.owner[t->bank]==QO_CPU&&captured==m->captured);
 m->metered++;return m->fault!=F_METER;
}
static int encode(void *opaque,const struct qc5_helix_picture *picture,struct qc5_encoded_output *out)
{
 struct model *m=opaque;struct qc5_helix_layout layout;
 assert(m->p.input.owner[picture->input_bank]==QO_ENCODER&&picture->qp==30);
 assert(qc5_helix_layout(1280,720,&layout));
 uint32_t *desc=(uint32_t *)(arena+(layout.region[H_DESCRIPTOR].start-QC5_CAPTURE_RAM_BASE));
 uint8_t *bs=arena+(layout.region[H_BITSTREAM].start-QC5_CAPTURE_RAM_BASE);
 uint8_t *au=arena+(QC5_HELIX_AU_PHYS-QC5_CAPTURE_RAM_BASE);
 struct qc5_helix_plan plan;
 assert(qc5_helix_build_picture(&plan,picture,desc,4096,bs));
 assert(qc5_helix_commands_valid(desc,&plan));
 for(unsigned n=0;n<16;n++)bs[256+n]=(uint8_t)(n+1);
 int bytes=qc5_helix_annexb(&plan,bs,bs+256,16,au,QC5_HELIX_AU_BYTES);assert(bytes>0);
 *out=(struct qc5_encoded_output){au,(unsigned)bytes,3};
 if(m->fault==F_POINTER)out->data++;
 if(m->fault==F_LENGTH)out->bytes=QC5_HELIX_AU_BYTES+1;
 if(m->fault==F_RESET)out->reset_mask=2;
 m->picture=*picture;m->plans++;m->idrs+=picture->idr;
 return m->fault!=F_ENCODE;
}
static uint64_t clock_us(void *opaque)
{struct model *m=opaque;return m->fault==F_CLOCK?m->captured-1:m->now;}
static int publish(void *opaque,const struct qc5_frame_output *frame,uint64_t deadline)
{
 struct model *m=opaque;
 assert(frame->serial==m->p.input.completed&&frame->captured_us==m->captured);
 assert(frame->idr==m->picture.idr&&frame->data==arena+(QC5_HELIX_AU_PHYS-QC5_CAPTURE_RAM_BASE));
 assert(frame->bytes&&frame->bytes<=QC5_HELIX_AU_BYTES&&deadline==m->now+QP_PUBLISH_BUDGET_US);
 assert(m->p.input.owner[(frame->serial-1)&1]==QO_CPU&&m->p.input.held_encoded);
 assert(!memcmp(frame->data,"\0\0\0\1",4));
 assert((frame->data[4]&31)==(frame->idr?7:1));
 m->sent++;
 m->now=m->fault==F_BACKWARDS?m->captured:m->now+(m->fault==F_SLOW?QP_PUBLISH_BUDGET_US+1:1000);
 return m->fault!=F_CLOSED;
}
static int drop(void *opaque,uint64_t epoch)
{struct model *m=opaque;assert(epoch&&epoch==m->epoch);m->drops++;return 1;}
static int queue(void *opaque,uint32_t y,uint32_t uv)
{
 struct model *m=opaque;unsigned bank=m->p.input.held.bank;
 assert(m->p.input.owner[bank]==QO_CPU&&y==m->p.input.planes[bank].y&&uv==m->p.input.planes[bank].uv);
 m->queue_writes++;if(m->fault==F_QUEUE1)return 0;
 m->queue_writes++;return m->fault!=F_QUEUE2;
}
static struct qc5_producer_io io(struct model *m)
{return (struct qc5_producer_io){m,meter,encode,clock_us,publish,drop,queue};}
static void fresh(struct model *m)
{memset(m,0,sizeof *m);assert(qp_init(&m->p,arena,sizeof arena));}
static void stamp(struct model *m,uint64_t epoch)
{
 assert(m->p.input.last_us<=UINT64_MAX-40000&&m->now<=UINT64_MAX-501);
 uint64_t next=m->p.input.last_us+40000;
 if(next<=m->now)next=m->now+1;
 m->epoch=epoch;m->captured=next;m->now=next+500;
}
static int step_mode(struct model *m,uint64_t epoch,unsigned retire)
{
 unsigned bank=(unsigned)(m->p.input.completed&1);
 stamp(m,epoch);
 struct qc5_producer_io callbacks=io(m);
 return qp_frame_mode(&m->p,&callbacks,m->p.input.planes[bank].y,m->p.input.planes[bank].uv,m->captured,epoch,retire);
}
static int step(struct model *m,uint64_t epoch){return step_mode(m,epoch,0);}
static void warm(struct model *m)
{fresh(m);for(unsigned n=0;n<120;n++)assert(step(m,0));assert(!m->plans);}
static int original_main(void)
{
 struct model m;fresh(&m);assert(!qp_init(&m.p,arena,sizeof arena));
 for(unsigned n=1;n<=4096;n++){
  uint64_t epoch=n<176?7:n<182?8:n<201?0:n<701?9:n<705?0:10;
  m.fault=n==181?F_CLOSED:n==700?F_SLOW:F_NONE;
  assert(step(&m,epoch)&&qo_valid(&m.p.input));
  if(n==121||n==176||n==201||n==705)assert(m.picture.idr);
 }
 assert(m.p.input.completed==4096&&m.p.input.encoded==3976&&m.plans==3976);
 assert(m.drops==2&&m.p.disconnects==2&&m.p.codec.qp==30&&m.p.codec.gop==32);
 assert(qp_request_stop(&m.p)&&!step(&m,10));
 assert(!qp_stopped(&m.p,30,1)&&!qp_stopped(&m.p,31,0)&&qp_stopped(&m.p,31,1));
 assert(qp_init(&m.p,arena,sizeof arena));
 unsigned faults=0;
 for(unsigned fault=F_METER;fault<=F_BACKWARDS;fault++){
  warm(&m);m.fault=fault;unsigned before=m.queue_writes;
  assert(!step(&m,3)&&m.p.error&&m.p.input.phase==QO_FAULT);
  assert(m.queue_writes==before+(fault==F_QUEUE1?1:fault==F_QUEUE2?2:0));
  assert(!step(&m,3)&&!qp_init(&m.p,arena,sizeof arena));
  assert(!qp_stopped(&m.p,31,0)&&qp_stopped(&m.p,31,1));faults++;
 }
 warm(&m);m.p.bytes=UINT32_MAX-1;assert(step(&m,1)&&m.p.bytes>UINT32_MAX);
 for(unsigned which=0;which<3;which++){
  warm(&m);
  if(which==0)m.p.bytes=UINT64_MAX;
  if(which==1)m.p.published=UINT64_MAX;
  if(which==2){m.p.disconnects=UINT64_MAX;m.fault=F_CLOSED;}
  assert(!step(&m,1)&&m.p.error==QP_COUNTER&&m.p.input.phase==QO_FAULT);
  assert(qp_stopped(&m.p,31,1));faults++;
 }
 warm(&m);struct qc5_producer_io callbacks=io(&m);callbacks.drop_reader=NULL;
 assert(!qp_frame(&m.p,&callbacks,m.p.input.planes[0].y,m.p.input.planes[0].uv,5000000,1)&&m.p.error==QP_ARGUMENT);faults++;
 assert(qp_stopped(&m.p,31,1));
 fresh(&m);
 for(unsigned n=1;n<=632;n++){
  unsigned retire=m.p.input.queued==632;
  assert(step_mode(&m,1,retire));
  if(n==631)assert(m.p.input.phase==QO_DRAIN&&m.p.input.owner[0]==QO_STOPPED&&m.p.input.owner[1]==QO_DMA);
 }
 assert(m.p.input.completed==632&&m.p.input.queued==632&&m.p.input.encoded==512);
 assert(m.queue_writes==1260&&m.p.input.phase==QO_DRAIN&&m.p.input.owner[0]==QO_STOPPED&&m.p.input.owner[1]==QO_STOPPED);
 assert(!qp_stopped(&m.p,15,1)&&qp_stopped(&m.p,31,1));
 printf("{\"producer_frames\":4096,\"actual_descriptor_plans\":3976,\"late_or_closed_reader_recoveries\":2,\"forced_reconnect_idrs\":4,\"terminal_fault_cases\":%u,\"counter32_bytes_crossed\":true,\"native_resolution\":[1280,720],\"qp\":30,\"gop\":32,\"new_frame_buffers\":0,\"hardware_capture_claimed\":false,\"camera_accessed\":false}\n",faults);
 return 0;
}

static unsigned rec_mode,rec_calls;
static int record_frame(void *opaque,const struct qc5_frame_output *f,uint64_t deadline)
{
 struct model *m=opaque;assert(f->serial==m->p.input.completed&&f->data==arena+(QC5_HELIX_AU_PHYS-QC5_CAPTURE_RAM_BASE));
 assert(m->p.input.owner[(f->serial-1)&1]==QO_CPU&&m->p.input.held_encoded&&deadline==m->now+8000);
 rec_calls++;if(rec_mode==2)m->now+=8001;return rec_mode==1?0:1;
}
int main(void)
{
 assert(original_main()==0);
 for(rec_mode=0;rec_mode<3;rec_mode++){
  struct model m;warm(&m);rec_calls=0;
  for(unsigned frame=0;frame<512;frame++){
   uint64_t epoch=frame<256?0:7;unsigned bank=m.p.input.completed&1;stamp(&m,epoch);
   struct qc5_producer_io callbacks=io(&m);callbacks.record=record_frame;
   assert(qp_frame(&m.p,&callbacks,m.p.input.planes[bank].y,m.p.input.planes[bank].uv,m.captured,epoch));
   assert(qo_valid(&m.p.input)&&!m.p.error);
  }
  assert(m.p.input.encoded==512&&m.sent==256&&m.p.codec.qp==30);
  assert(rec_calls==(rec_mode?1:512)&&m.p.record_disabled==!!rec_mode&&m.p.record_failures==!!rec_mode);
 }
 puts("QC5_FEATURE_PRODUCER_PASS owned_before_requeue=1 recording_without_browser=1 storage_failure_does_not_stop_video=1 unchanged_quality=1");return 0;
}

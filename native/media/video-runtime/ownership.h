/* SPDX-License-Identifier: GPL-2.0-or-later
 * Continuous producer ownership. No allocations or hardware accesses here.
 * FIFO addresses are returned only after the sole reader has finished. A
 * failed pair write has unknown hardware state and requires verified stop.
 */
#ifndef QC5_VIDEO_OWNERSHIP_H
#define QC5_VIDEO_OWNERSHIP_H
#include <stdint.h>
#include <string.h>
#include "../video-gc1084/qc5-nv12-layout.h"
enum qc5_owner_phase { QO_RUN=1,QO_DRAIN,QO_FAULT,QO_OFF };
enum qc5_bank_owner { QO_DMA=1,QO_CPU,QO_ENCODER,QO_STOPPED };
struct qc5_frame_ticket { uint64_t serial;uint32_t bank; };
struct qc5_video_owner {
 uint64_t queued,completed,encoded,last_us;
 struct qc5_nv12_bank planes[2];
 struct qc5_frame_ticket held;
 unsigned phase,owner[2],held_encoded;
};
typedef int (*qc5_queue_pair)(void *,uint32_t,uint32_t);
static int qo_planes(const struct qc5_video_owner *s)
{
 for(unsigned n=0;n<2;n++){
  struct qc5_nv12_bank p;
  if(!qc5_nv12_plan(n,1280,720,&p)||memcmp(&p,&s->planes[n],sizeof p))return 0;
 }
 return 1;
}
static int qo_valid(const struct qc5_video_owner *s)
{
 if(!s||!qo_planes(s)||s->encoded>s->completed||s->held_encoded>1)return 0;
 if(s->phase==QO_OFF)return s->owner[0]==QO_STOPPED&&s->owner[1]==QO_STOPPED&&!s->held.serial&&!s->held_encoded;
 if(s->phase!=QO_RUN&&s->phase!=QO_DRAIN)return 0;
 if(s->queued<2||s->queued<s->completed||s->queued-s->completed>2)return 0;
 unsigned dma=0,held=0,stopped=0;
 for(unsigned n=0;n<2;n++){
  if(s->owner[n]==QO_DMA)dma++;
  else if(s->owner[n]==QO_CPU||s->owner[n]==QO_ENCODER){
   if(s->held.bank!=n||n!=((s->completed-1)&1)||s->held.serial!=s->completed||!s->held.serial||
      (s->held_encoded&&s->owner[n]!=QO_CPU))return 0;
   held++;
  }else if(s->phase==QO_DRAIN&&s->owner[n]==QO_STOPPED)stopped++;
  else return 0;
 }
 return dma==s->queued-s->completed&&held<=1&&dma+held+stopped==2&&
        (!dma||s->owner[s->completed&1]==QO_DMA)&&
        (held||(!s->held.serial&&!s->held.bank&&!s->held_encoded));
}
static int qo_init(struct qc5_video_owner *s)
{
 if(!s)return 0;
 memset(s,0,sizeof *s);
 for(unsigned n=0;n<2;n++)if(!qc5_nv12_plan(n,1280,720,&s->planes[n]))return 0;
 s->phase=QO_RUN;s->owner[0]=s->owner[1]=QO_DMA;s->queued=2;
 return qo_valid(s);
}
static int qo_fault(struct qc5_video_owner *s)
{if(s)s->phase=QO_FAULT;return 0;}
static int qo_complete(struct qc5_video_owner *s,uint32_t y,uint32_t uv,uint64_t us,struct qc5_frame_ticket *out)
{
 if(!s||!out)return 0;
 if(!qo_valid(s)||(s->phase!=QO_RUN&&s->phase!=QO_DRAIN)||s->held.serial||!us||us<=s->last_us||s->completed==UINT64_MAX)return qo_fault(s);
 unsigned bank=(unsigned)(s->completed&1);
 if(s->owner[bank]!=QO_DMA||(y&~7u)!=s->planes[bank].y||(uv&~7u)!=s->planes[bank].uv)return qo_fault(s);
 s->completed++;s->owner[bank]=QO_CPU;s->last_us=us;s->held_encoded=0;
 *out=s->held=(struct qc5_frame_ticket){s->completed,bank};
 return qo_valid(s);
}
static int qo_ticket(const struct qc5_video_owner *s,const struct qc5_frame_ticket *t)
{return s&&t&&t->bank<2&&t->serial&&t->bank==s->held.bank&&t->serial==s->held.serial;}
static int qo_encode_begin(struct qc5_video_owner *s,const struct qc5_frame_ticket *t)
{
 if(!qo_valid(s)||(s->phase!=QO_RUN&&s->phase!=QO_DRAIN)||!qo_ticket(s,t)||s->owner[t->bank]!=QO_CPU||s->held_encoded)return 0;
 s->owner[t->bank]=QO_ENCODER;return 1;
}
static int qo_encode_finish(struct qc5_video_owner *s,const struct qc5_frame_ticket *t,int valid)
{
 if(!qo_valid(s)||!qo_ticket(s,t)||s->owner[t->bank]!=QO_ENCODER||valid<0||valid>1)return 0;
 if(!valid||s->encoded==UINT64_MAX)return qo_fault(s);
 s->encoded++;s->owner[t->bank]=QO_CPU;s->held_encoded=1;return qo_valid(s);
}
static int qo_requeue(struct qc5_video_owner *s,const struct qc5_frame_ticket *t,qc5_queue_pair pair,void *opaque)
{
 if(!qo_valid(s)||s->phase!=QO_RUN||!qo_ticket(s,t)||s->owner[t->bank]!=QO_CPU||!pair)return 0;
 if(s->queued==UINT64_MAX)return qo_fault(s);
 const struct qc5_nv12_bank *p=&s->planes[t->bank];
 if(pair(opaque,p->y,p->uv)!=1)return qo_fault(s);
 s->queued++;s->owner[t->bank]=QO_DMA;s->held_encoded=0;memset(&s->held,0,sizeof s->held);
 return qo_valid(s);
}
static int qo_request_stop(struct qc5_video_owner *s)
{
 if(!qo_valid(s)||s->phase!=QO_RUN)return 0;
 s->phase=QO_DRAIN;return 1;
}
/* Consume the tail already queued in hardware, without inventing another
 * FIFO entry. No bank becomes reusable until the native stop is verified. */
static int qo_retire(struct qc5_video_owner *s,const struct qc5_frame_ticket *t)
{
 if(!qo_valid(s)||!qo_ticket(s,t)||s->owner[t->bank]!=QO_CPU)return 0;
 s->phase=QO_DRAIN;s->owner[t->bank]=QO_STOPPED;s->held_encoded=0;
 memset(&s->held,0,sizeof s->held);return qo_valid(s);
}
/* Both the ISP's existing five stop checks and encoder idle are mandatory.
 * A failed producer is never considered quiescent just because it exited. */
static int qo_stopped(struct qc5_video_owner *s,unsigned isp_stop,unsigned encoder_idle)
{
 if(!s||!qo_planes(s)||(s->phase!=QO_DRAIN&&s->phase!=QO_FAULT)||isp_stop!=31||encoder_idle!=1)return 0;
 s->owner[0]=s->owner[1]=QO_STOPPED;s->phase=QO_OFF;s->held_encoded=0;memset(&s->held,0,sizeof s->held);
 return qo_valid(s);
}
#endif

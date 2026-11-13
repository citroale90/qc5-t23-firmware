/* SPDX-License-Identifier: GPL-2.0-or-later
 * Browser push-to-talk lifetime and finite DMA ownership. No hardware or
 * allocation here. The DMA batch stays immutable until confirmed stop.
 */
#ifndef QC5_DUPLEX_TALK_QUEUE_H
#define QC5_DUPLEX_TALK_QUEUE_H
#include "talk-batch.h"
#define QAT_LIMIT_US UINT64_C(30000000)
#define QAT_STALE_US UINT64_C(250000)
struct qat_queue {
 uint8_t codes[QAD_PERIODS*QAD_SAMPLES];
 uint64_t arrived[QAD_PERIODS],started_us,last_us,generation,held_generation,limit_us,last_audio_us,received;
 uint32_t press_id,last_press_id;unsigned active,count,held,clock_failed;
};
struct qat_ticket {uint64_t generation;unsigned frames;};
static int qat_configure(struct qat_queue *s,unsigned max_seconds)
{
 if(!s||s->active||s->held||s->count||max_seconds<1||max_seconds>30)return 0;
 s->limit_us=(uint64_t)max_seconds*1000000;return 1;
}
static void qat_release(struct qat_queue *s)
{
 if(!s)return;
 s->active=0;s->press_id=0;
 /* The inflight batch is its own stable PCM copy in reserved RAM. Clear
  * pending PCMA here without touching its DMA page or ownership ticket. */
 s->count=0;memset(s->codes,0,sizeof s->codes);memset(s->arrived,0,sizeof s->arrived);
}
static int qat_clock(struct qat_queue *s,uint64_t now)
{
 if(!s||s->clock_failed)return 0;
 if(!now||now<s->last_us){s->clock_failed=1;qat_release(s);return 0;}
 s->last_us=now;
 uint64_t limit=s->limit_us?s->limit_us:QAT_LIMIT_US;
 if(s->active&&(now-s->started_us>=limit||now-s->last_audio_us>=(s->received?700000u:2000000u)))qat_release(s);
 return 1;
}
static int qat_start(struct qat_queue *s,uint32_t id,uint64_t now)
{
 if(!id||!qat_clock(s,now)||now>UINT64_MAX-QAT_LIMIT_US)return 0;
 if(s->active)return s->press_id==id?1:0; /* Cannot renew the30s limit. */
 if(s->held||s->count||s->last_press_id==id)return 0;
 s->press_id=s->last_press_id=id;s->started_us=s->last_audio_us=now;s->received=0;s->active=1;return 1;
}
static int qat_push(struct qat_queue *s,uint32_t id,const uint8_t *data,size_t size,uint64_t now)
{
 if(!s||!data||size!=QAD_SAMPLES||!id||!qat_clock(s,now)||!s->active||s->press_id!=id||s->count>=QAD_PERIODS||s->received==UINT64_MAX)return 0;
 memcpy(s->codes+s->count*QAD_SAMPLES,data,QAD_SAMPLES);s->arrived[s->count]=now;s->count++;s->received++;s->last_audio_us=now;return 1;
}
static void qat_remove(struct qat_queue *s,unsigned count)
{
 unsigned remaining=s->count-count;
 memmove(s->codes,s->codes+count*QAD_SAMPLES,remaining*QAD_SAMPLES);
 memset(s->codes+remaining*QAD_SAMPLES,0,sizeof s->codes-remaining*QAD_SAMPLES);
 memmove(s->arrived,s->arrived+count,remaining*sizeof *s->arrived);
 memset(s->arrived+remaining,0,(QAD_PERIODS-remaining)*sizeof *s->arrived);
 s->count=remaining;
}
static int qat_prepare(struct qat_queue *s,volatile uint8_t *page,size_t capacity,
                       unsigned tx_stopped,uint64_t now,struct qat_ticket *ticket)
{
 if(!s||!ticket||!qat_clock(s,now)||!s->active||s->held||tx_stopped!=1||s->generation==UINT64_MAX)return 0;
 unsigned stale=0;
 while(stale<s->count&&now-s->arrived[stale]>QAT_STALE_US)stale++;
 if(stale)qat_remove(s,stale);
 if(!s->count)return 0;
 struct qad_batch batch;
 if(!qad_batch_prepare(page,capacity,s->codes,s->count*QAD_SAMPLES,tx_stopped,&batch))return 0;
 s->held=s->count;s->held_generation=++s->generation;
 /* Incoming packets use this queue while DMA reads the separate PCM
  * page. Keeping inflight packets here would drop up to half the voice. */
 qat_remove(s,s->count);
 *ticket=(struct qat_ticket){s->held_generation,s->held};return 1;
}
static int qat_complete(struct qat_queue *s,const struct qat_ticket *ticket,
                        unsigned tx_stopped,uint64_t now)
{
 if(!s||!ticket||!s->held||ticket->generation!=s->held_generation||ticket->frames!=s->held||tx_stopped!=1)return 0;
 /* Even a clock fault or user release must allow a confirmed DMA stop to
  * retire its lease. They must not permit another batch to start. */
 (void)qat_clock(s,now);
 s->held=0;s->held_generation=0;return 1;
}
#endif

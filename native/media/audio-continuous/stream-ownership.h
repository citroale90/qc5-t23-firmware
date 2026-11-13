/* SPDX-License-Identifier: GPL-2.0-or-later
 * Polled cyclic RX ownership, no hardware/OS/network access. A caller must
 * validate a stable DTA/DTC snapshot and copy one period into its small
 * private packet, then observe again before publishing that copy. Never
 * send directly from a ring slot that DMA may overwrite while blocked.
 */
#ifndef QC5_AUDIO_STREAM_OWNERSHIP_H
#define QC5_AUDIO_STREAM_OWNERSHIP_H
#include "stream-descriptors.h"
#include "stream-status.h"
enum qas_phase {QAS_NEW,QAS_RUN,QAS_FAULT,QAS_OFF};
enum qas_error {QAS_OK,QAS_ARGUMENT,QAS_CLOCK,QAS_POLL_GAP,QAS_DMA,QAS_FIFO,QAS_STALL,QAS_OVERRUN,QAS_COUNTER,QAS_LEASE,QAS_STOP};
#define QAS_POLL_LIMIT_US UINT64_C(250000)
#define QAS_FRESH_US UINT64_C(5000)
#define QAS_COPY_LIMIT_US UINT64_C(20000)
struct qas_owner {
 uint64_t started_us,observed_us,progress_us,produced,consumed,discarded;
 uint64_t held,held_us;
 unsigned cursor,phase,error;
};
struct qas_ticket {uint64_t serial;uint32_t address,bytes;};
static int qas_fault(struct qas_owner *s,unsigned error)
{if(s){if(!s->error)s->error=error;s->phase=QAS_FAULT;}return 0;}
static int qas_owner_start(struct qas_owner *s,uint64_t us)
{
 if(!s||!us||s->phase!=QAS_NEW||s->started_us||s->produced||s->consumed||s->held)return 0;
 *s=(struct qas_owner){.started_us=us,.observed_us=us,.progress_us=us,.phase=QAS_RUN};return 1;
}
/* Snapshot cursor0 means the first descriptor (or the just-completed final
 * descriptor at the wrap boundary). The source SDK uses a24-bit count and
 * the8-bit page-relative next-descriptor field above it. */
static int qas_cursor(uint32_t target,uint32_t count,unsigned *cursor)
{
 if(!cursor||target<QA_PCM||target>QA_PCM+QAS_PCM_BYTES||target%16)return 0;
 unsigned remaining=count&0xffffffu;if(remaining>QAS_PERIOD_BYTES/16)return 0;
 unsigned offset=target-QA_PCM;
 if(!remaining&&offset&&offset%QAS_PERIOD_BYTES==0){*cursor=(offset/QAS_PERIOD_BYTES)%QAS_PERIODS;return 1;}
 if(offset>=QAS_PCM_BYTES)return 0;
 unsigned in_period=offset%QAS_PERIOD_BYTES;
 if(in_period+remaining*16u!=QAS_PERIOD_BYTES)return 0;
 *cursor=offset/QAS_PERIOD_BYTES;return 1;
}
/* 1 accepted, 2 incoherent moving snapshot (retry), 0 terminal failure.
 * An incoherent snapshot cannot keep renewing the observation deadline. */
static int qas_owner_observe(struct qas_owner *s,uint32_t first_target,uint32_t count,
                              uint32_t second_target,uint32_t dcs,uint32_t fifo,uint32_t dda,uint64_t us)
{
 if(!s||s->phase!=QAS_RUN||s->error)return 0;
 if(!us||us<s->observed_us)return qas_fault(s,QAS_CLOCK);
 if(us-s->observed_us>QAS_POLL_LIMIT_US)return qas_fault(s,QAS_POLL_GAP);
 if(!qas_dma_status_valid(dcs)||!qas_dma_address_owned(dda))return qas_fault(s,QAS_DMA);
 if(fifo&0x40u)return qas_fault(s,QAS_FIFO);
 if(dda!=qas_dma_descriptor_address(dcs))return 2;
 if(first_target!=second_target)return 2;
 unsigned cursor;
 if(!qas_cursor(first_target,count,&cursor))return 2;
 unsigned delta=(cursor+QAS_PERIODS-s->cursor)%QAS_PERIODS;
 if(s->produced>UINT64_MAX/QAS_PERIOD_SAMPLES-delta)return qas_fault(s,QAS_COUNTER);
 uint64_t produced=s->produced+delta;
 /* Thirty completed periods leave at least one whole period before the
  * oldest unread slot can be rewritten, even at the end of current DMA. */
 if(produced<s->consumed||produced-s->consumed>QAS_PERIODS-2)return qas_fault(s,QAS_OVERRUN);
 if(delta)s->progress_us=us;
 else if(us-s->progress_us>QAS_POLL_LIMIT_US)return qas_fault(s,QAS_STALL);
 s->produced=produced;s->cursor=cursor;s->observed_us=us;return 1;
}
static int qas_owner_begin(struct qas_owner *s,uint64_t us,struct qas_ticket *ticket)
{
 if(!s||!ticket||s->phase!=QAS_RUN||s->error||s->held)return 0;
 if(!us||us<s->observed_us||us-s->observed_us>QAS_FRESH_US)return qas_fault(s,QAS_LEASE);
 if(s->consumed==s->produced)return 2;
 if(s->produced<s->consumed||s->produced-s->consumed>QAS_PERIODS-2||s->consumed==UINT64_MAX)return qas_fault(s,QAS_OVERRUN);
 s->held=s->consumed+1;s->held_us=us;
 *ticket=(struct qas_ticket){s->held,QA_PCM+(uint32_t)(s->consumed%QAS_PERIODS)*QAS_PERIOD_BYTES,QAS_PERIOD_BYTES};return 1;
}
/* Caller has copied exactly2000bytes to a private packet and taken a second
 * stable observation. A late/overrun copy is never marked publishable. */
static int qas_owner_copied(struct qas_owner *s,const struct qas_ticket *ticket,uint64_t us)
{
 if(!s||!ticket||s->phase!=QAS_RUN||s->error)return 0;
 if(!s->held||ticket->serial!=s->held||s->held!=s->consumed+1||
    ticket->address!=QA_PCM+(uint32_t)(s->consumed%QAS_PERIODS)*QAS_PERIOD_BYTES||ticket->bytes!=QAS_PERIOD_BYTES||
    !us||us<s->held_us||us-s->held_us>QAS_COPY_LIMIT_US||us<s->observed_us||us-s->observed_us>QAS_FRESH_US||
    s->observed_us<s->held_us||s->produced<s->held||s->produced-s->consumed>QAS_PERIODS-2)
  return qas_fault(s,QAS_LEASE);
 s->consumed++;s->held=s->held_us=0;return 1;
}
/* Abandon an unpublished, late private copy, without consuming its period.
 * Only a new stable DMA observation may grant another copy attempt. The
 * ring overrun, clock and original twenty-ms lease checks remain intact. */
static int qas_owner_retry_copy(struct qas_owner *s,const struct qas_ticket *ticket,uint64_t us)
{
 if(!s||!ticket||s->phase!=QAS_RUN||s->error)return 0;
 if(!s->held||s->consumed==UINT64_MAX||ticket->serial!=s->held||s->held!=s->consumed+1||
    ticket->address!=QA_PCM+(uint32_t)(s->consumed%QAS_PERIODS)*QAS_PERIOD_BYTES||ticket->bytes!=QAS_PERIOD_BYTES||
    !us||us<s->held_us||us!=s->observed_us||us-s->held_us<=QAS_COPY_LIMIT_US)
  return qas_fault(s,QAS_LEASE);
 if(s->produced<s->held||s->produced-s->consumed>QAS_PERIODS-2)return qas_fault(s,QAS_OVERRUN);
 s->held=s->held_us=0;return 1;
}
static int qas_owner_discard(struct qas_owner *s)
{
 if(!s||s->phase!=QAS_RUN||s->error||s->held||s->consumed>s->produced)return 0;
 uint64_t delta=s->produced-s->consumed;
 if(s->discarded>UINT64_MAX-delta)return qas_fault(s,QAS_COUNTER);
 s->discarded+=delta;s->consumed=s->produced;return 1;
}
static int qas_owner_stopped(struct qas_owner *s,unsigned rx_off,unsigned dma_off)
{
 if(!s||(s->phase!=QAS_RUN&&s->phase!=QAS_FAULT))return 0;
 if(rx_off!=1||dma_off!=1)return qas_fault(s,QAS_STOP);
 /* A fault stays recorded even after verified stop permits buffer release. */
 s->held=s->held_us=0;s->phase=QAS_OFF;return 1;
}
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared native FIFO accounting. The finite diagnostic remains the default;
 * a service must opt in before any destination is queued. A stop drains
 * exactly the destinations already given to hardware, without a new queue.
 */
#ifndef QC5_MSCA_STATE_H
#define QC5_MSCA_STATE_H
#include <stdint.h>
#include "qc5-nv12-layout.h"
struct msca_state {
 struct qc5_nv12_bank planes[2];
 uint64_t queued,completed,last_poll,target;
 unsigned service,control_hold,completion_pending;
};
static int msca_policy_valid(const struct msca_state *s)
{return s&&s->service<=1&&s->control_hold<=1&&s->completion_pending<=1&&(!s->control_hold||s->service)&&(!s->service||!s->target||s->target>=2);}
static uint64_t msca_limit(const struct msca_state *s)
{return s->service?s->target:QC5_NV12_TARGET;}
static uint32_t msca_note_count(uint64_t n)
{return n>UINT32_MAX?UINT32_MAX:(uint32_t)n;}
static int msca_service_begin(struct msca_state *s)
{
 if(!s||s->service||s->control_hold||s->target||s->queued||s->completed||s->last_poll)return 0;
 s->service=1;return 1;
}
static int msca_service_hold_controls(struct msca_state *s)
{
 if(!msca_policy_valid(s)||!s->service||s->target)return 0;
 s->control_hold=1;return 1;
}
static int msca_updates_allowed(const struct msca_state *s,unsigned margin)
{
 if(!msca_policy_valid(s)||s->control_hold)return 0;
 uint64_t limit=msca_limit(s);
 return !limit||(s->completed<limit&&limit-s->completed>margin);
}
static int msca_service_drain(struct msca_state *s,unsigned pending_controls)
{
 if(!msca_policy_valid(s)||!s->service||!s->control_hold||pending_controls||s->target||s->queued<2||s->queued<s->completed||s->queued-s->completed!=2)return 0;
 s->target=s->queued;return 1;
}
#endif

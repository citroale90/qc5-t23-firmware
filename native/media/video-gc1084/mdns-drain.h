/* SPDX-License-Identifier: GPL-2.0-or-later
 * Latch native MDNS bypass while frames can still reach the block, then
 * stop the input/core and verify the retained reference memory separately.
 */
#ifndef QC5_MDNS_DRAIN_H
#define QC5_MDNS_DRAIN_H
#include "mdns-strobe.h"
struct qc5_mdns_drain {uint32_t requested,confirmed,polls,active_hash;};
static int qc5_mdns_drain_begin(struct qc5_mdns_drain *d,struct qc5_mdns_reference *s,struct qc5_mdns_state *c)
{
 if(!d||d->requested||!s||!s->prepared||!s->armed||s->finished||!s->read||!s->write||
    !c||!c->ready||!c->profile||c->bank>1||c->gain>=25||!c->profile->checksum||
    qc5_mdns_checksum(c->profile)!=c->profile->checksum)return 0;
 if((s->read(s->opaque,0x800)&~1u)||s->read(s->opaque,0xd040)!=1||
    s->read(s->opaque,0xc)!=0xb57468c9||!qc5_mdns_reference_readback(s)||
    !qc5_mdns_strobe_verify(s,c,0)||!qc5_mdns_reference_guards(s))return 0;
 d->active_hash=c->read_hash;d->requested=1;
 /* Exactly tisp_mdns_bypass(1), with its writes held until the trigger.
  * Existing buffer authority remains owned throughout this transition. */
 static const struct qc5_mdns_pair plan[]={
  {0x7804,0x110},{0x7810,0x10001},{0x7814,0xf01100},{0x7808,0},{0x7818,0x21},{0x7804,0x111}
 };
 for(unsigned n=0;n<sizeof plan/sizeof *plan;n++)
  if(!s->write(s->opaque,plan[n].offset,plan[n].value))return qc5_mdns_reference_fail(s,plan[n].offset,plan[n].value);
 return 1;
}
static int qc5_mdns_drain_poll(struct qc5_mdns_drain *d,struct qc5_mdns_reference *s,struct qc5_mdns_state *c)
{
 if(!d||!d->requested||d->confirmed||d->polls>=200||!s||!s->armed||s->finished||!s->read||!c)return -1;
 ++d->polls;
 if((s->read(s->opaque,0x800)&~1u)||s->read(s->opaque,0xd040)!=1||
    s->read(s->opaque,0xc)!=0xb57468c9||!qc5_mdns_reference_readback(s))return -1;
 if(qc5_mdns_strobe_verify(s,c,1)){d->confirmed=1;return 1;}
 /* The sole changing coefficient port is top-control. All other changed
  * words are faults, not a reason to relax the profile comparison. */
 if(c->fault_offset!=0x7810||c->fault_value!=0x11111111)return -1;
 return 0;
}
static int qc5_mdns_drain_stop(struct qc5_mdns_drain *d,struct qc5_mdns_strobe *state,
 struct qc5_mdns_reference *s,struct qc5_mdns_state *c)
{
 if(!d||!d->confirmed||!d->active_hash||!state||state->stopped||!s||!s->armed||!s->read||!s->write||
    s->read(s->opaque,0x800)||s->read(s->opaque,0xd040)||!qc5_mdns_reference_readback(s)||
    !qc5_mdns_strobe_verify(s,c,1))return 0;
 if(!s->write(s->opaque,0x7804,0x110)||s->read(s->opaque,0x7804)!=0x110)return 0;
 state->active_hash=d->active_hash;state->stop_commits=1;state->stopped=1;s->armed=0;
 return 1;
}
#endif

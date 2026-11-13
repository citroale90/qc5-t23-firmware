/* SPDX-License-Identifier: GPL-2.0-or-later
 * Register-view confirmation belongs to an actual completed ISP frame.
 * DMA authority, table checksum and destinations are checked before queueing.
 */
#ifndef QC5_MDNS_DEFERRED_H
#define QC5_MDNS_DEFERRED_H
#include "mdns-strobe.h"
struct qc5_mdns_deferred {uint32_t pending;qc5_mdns_count confirmed;uint32_t first_confirmed;
 qc5_mdns_count last_frame,issued_after_frame;};
static int qc5_mdns_deferred_queue(struct qc5_mdns_deferred *d,struct qc5_mdns_strobe *state,
 struct qc5_mdns_reference *s,struct qc5_mdns_state *c)
{
 if(!d||d->pending||d->confirmed==QC5_MDNS_COUNT_MAX||!state||state->stopped||!s||!s->prepared||s->finished||!qc5_mdns_commit_available(s->arms)||
    !s->read||!s->write||!c||!c->ready||!c->profile||c->bank>1||c->gain>=25||
    !c->profile->checksum||qc5_mdns_checksum(c->profile)!=c->profile->checksum)return 0;
 uint32_t control=s->read(s->opaque,0x7804);
 if(control!=0x110)return qc5_mdns_reference_fail(s,0x7804,control);
 if(s->read(s->opaque,0x0c)!=0xb57468c9)return qc5_mdns_reference_fail(s,0x0c,s->read(s->opaque,0x0c));
 if(!s->ever_armed&&(s->read(s->opaque,0x800)||s->read(s->opaque,0xd040)))return 0;
 if(s->ever_armed&&(!d->confirmed||!d->last_frame||(s->read(s->opaque,0x800)&~1u)||s->read(s->opaque,0xd040)!=1))return 0;
 if(!qc5_mdns_reference_readback(s)||(!s->ever_armed&&!qc5_mdns_reference_guards(s)))return 0;
 s->armed=s->ever_armed=1;++s->arms;d->pending=1;d->issued_after_frame=d->last_frame;c->read_hash=0;
 if(!s->write(s->opaque,0x7804,0x111))return qc5_mdns_reference_fail(s,0x7804,0x111);
 control=s->read(s->opaque,0x7804);
 return (control&~1u)==0x110&&qc5_mdns_reference_readback(s);
}
static int qc5_mdns_deferred_confirm(struct qc5_mdns_deferred *d,struct qc5_mdns_reference *s,struct qc5_mdns_state *c,qc5_mdns_count completed)
{
 if(!d||!s||!s->armed||!s->prepared||s->finished||!s->read||!c||!c->ready||
    !qc5_mdns_next_frame(d->last_frame,completed)||completed<=d->issued_after_frame||
    (s->read(s->opaque,0x800)&~1u)||s->read(s->opaque,0xd040)!=1||s->read(s->opaque,0x0c)!=0xb57468c9)return 0;
 if(!d->pending){
  if(!d->confirmed||!c->read_hash||(s->read(s->opaque,0x7804)&~1u)!=0x110)return 0;
  d->last_frame=completed;return 1;
 }
 if(d->confirmed==QC5_MDNS_COUNT_MAX||!qc5_mdns_reference_readback(s)||!qc5_mdns_strobe_verify(s,c,0))return 0;
 d->pending=0;d->last_frame=completed;++d->confirmed;return 1;
}
#endif

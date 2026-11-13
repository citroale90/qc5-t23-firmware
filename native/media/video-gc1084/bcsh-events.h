/* SPDX-License-Identifier: GPL-3.0-only
 * Native BCSH event deadbands around the independently checked calculator.
 * A failed commit is terminal: never continue after partial colour writes.
 */
#ifndef QC5_BCSH_EVENTS_H
#define QC5_BCSH_EVENTS_H
#include "bcsh-dynamic.h"
struct qbf_state {
 const struct qbd_profile *profile;
 uint32_t ct,ev_q10,last_ct,last_ev,force,failed,updates,skipped,registers[29];
};
typedef int (*qbf_commit)(void *,const uint32_t [29]);
static int qbf_start(struct qbf_state *s,const struct qbd_profile *profile,uint32_t ct,uint32_t ev_q10)
{
 if(!s)return 0;memset(s,0,sizeof *s);
 if(!profile||!qbd_calculate(profile,ct,ev_q10,s->registers))return 0;
 s->profile=profile;s->ct=s->last_ct=ct;s->ev_q10=ev_q10;s->last_ev=ev_q10>>10;return 1;
}
static int qbf_event(struct qbf_state *s,unsigned type,uint32_t value,unsigned force,qbf_commit commit,void *opaque)
{
 if(!s||!s->profile||s->failed||!commit||type<1||type>2||force>1)return 0;
 if(type==2&&(value<1000||value>30000))return 0;
 if(type==1)s->ev_q10=value;else s->ct=value;
 s->force|=force;
 uint32_t current=type==1?value>>10:value,previous=type==1?s->last_ev:s->last_ct;
 uint32_t distance=current>previous?current-previous:previous-current;
 if(!s->force&&distance<=(type==1?40u:32u)){++s->skipped;return 1;}
 uint32_t next[29];
 if(!qbd_calculate(s->profile,s->ct,s->ev_q10,next)||!commit(opaque,next)){s->failed=1;return 0;}
 memcpy(s->registers,next,sizeof next);
 if(type==1)s->last_ev=current;else s->last_ct=current;
 s->force=0;++s->updates;return 1;
}
#endif

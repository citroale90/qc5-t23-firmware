/* SPDX-License-Identifier: GPL-2.0-only
 * Counter-completion policy, not a timer masquerading as a reached target.
 * One command owns an axis. The caller supplies checked, stable OEM-decoded
 * counters. A direction change starts a new counter phase, as in the OEM.
 */
#ifndef QC5_MOVE_PROGRESS_H
#define QC5_MOVE_PROGRESS_H
#ifndef __KERNEL__
#include <stdint.h>
#endif
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
enum {QMP_RUNNING=0,QMP_COMPLETE=1,QMP_SHORT=2,QMP_TIMEOUT=3,QMP_COUNTER=4,QMP_IO=5,QMP_CANCEL=6,QMP_HOME_COMPLETE=7};
struct qmp_axis {unsigned raw,direction,known;};
struct qmp_move {struct qmp_axis axis[2];unsigned active,selected,requested,base,last,done,reason,homing;uint64_t started,changed,last_us,deadline;};
static unsigned qmp_completion(const struct qmp_move *s)
{
 /* A full-span homing command establishes a hard-stop convention, not an
  * exact displacement measurement. Keep its actual emitted count visible.
  * Jog, absolute and center commands retain exact completion semantics. */
 if(s->homing&&s->done>=s->requested)return QMP_HOME_COMPLETE;
 return s->done==s->requested?QMP_COMPLETE:QMP_SHORT;
}
static int qmp_begin(struct qmp_move *s,unsigned axis,unsigned direction,unsigned steps,uint64_t now)
{
 if(!s||s->active||axis>1||direction>1||!steps||steps>8000||(steps&3)||!now||now>UINT64_MAX-20000000)return 0;
 struct qmp_axis *a=&s->axis[axis];unsigned base=a->known&&a->direction==direction?a->raw:0;
 if(base>32764-steps)return 0;
 a->direction=direction;a->known=1;
 s->active=1;s->selected=axis;s->requested=steps;s->base=s->last=base;s->done=0;s->reason=QMP_RUNNING;s->homing=0;
 s->started=s->changed=s->last_us=now;
 uint64_t span=1000000+(uint64_t)steps*5000;if(span<2500000)span=2500000;if(span>20000000)span=20000000;s->deadline=now+span;return 1;
}
static int qmp_begin_home(struct qmp_move *s,unsigned axis,unsigned direction,unsigned steps,uint64_t now)
{if(!qmp_begin(s,axis,direction,steps,now))return 0;s->homing=1;return 1;}
static unsigned qmp_sample(struct qmp_move *s,unsigned raw,unsigned valid,uint64_t now)
{
 if(!s||!s->active)return QMP_COUNTER;
 if(!valid)s->reason=QMP_IO;
 else if(!now||now<s->last_us||raw>32764||(raw&3)||raw<s->last||raw<s->base)s->reason=QMP_COUNTER;
 else{
  s->last_us=now;if(raw!=s->last)s->changed=now;s->last=raw;s->done=raw-s->base;
  s->axis[s->selected].raw=raw;
  if(now>=s->deadline)s->reason=QMP_TIMEOUT;
  else if(!s->done){if(now-s->started>=1000000)s->reason=QMP_SHORT;}
  else if(now-s->started>=250000&&now-s->changed>=200000)s->reason=qmp_completion(s);
 }
 return s->reason;
}
static unsigned qmp_finish(struct qmp_move *s,unsigned raw,unsigned valid,uint64_t now,unsigned cancelled)
{
 if(!s||!s->active)return QMP_COUNTER;
 unsigned before=s->reason;
 /* The acknowledged stop can reset the MCU counter to zero. Preserve a
  * completion already established from stable, full-length pre-stop reads;
  * zero is the next phase's baseline, not loss of the completed movement.
  * Never accept reset during a running/short/faulted/cancelled move. */
 if(!cancelled&&(before==QMP_COMPLETE||before==QMP_HOME_COMPLETE)&&valid&&raw==0&&now&&now>=s->last_us&&
    s->selected<2&&s->requested&&before==qmp_completion(s)&&
    s->last>=s->base&&s->last<=32764&&s->last-s->base==s->done){
  s->axis[s->selected].raw=0;s->axis[s->selected].known=1;
  s->last_us=now;s->active=0;return before;
 }
 (void)qmp_sample(s,raw,valid,now);
 if(cancelled)s->reason=QMP_CANCEL;
 else if(before!=QMP_RUNNING&&before!=QMP_COMPLETE&&before!=QMP_HOME_COMPLETE)s->reason=before;
 else if(valid&&now>=s->last_us&&raw<=32764&&!(raw&3)&&raw>=s->base&&raw>=s->last&&s->reason!=QMP_COUNTER&&s->reason!=QMP_IO&&s->reason!=QMP_TIMEOUT)s->reason=qmp_completion(s);
 s->active=0;if(s->reason!=QMP_COMPLETE&&s->reason!=QMP_HOME_COMPLETE)s->axis[s->selected].known=0;return s->reason;
}
#endif

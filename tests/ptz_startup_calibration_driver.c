/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ptz-controller.h"
#include "startup-calibration.h"
struct mock {struct qcm_status status;struct qcm_progress progress;struct qcm_request last;unsigned calls,fail_read;};
static unsigned checks;
#define CHECK(x) do{assert(x);checks++;}while(0)
static int read_hw(void *p,struct qcm_status *s,struct qcm_progress *v){struct mock *m=p;*s=m->status;*v=m->progress;return !m->fail_read;}
static int submit(void *p,const struct qcm_request *r){struct mock *m=p;m->calls++;m->last=*r;m->status.busy=1;m->status.last_error=0;if(r->command==QCM_INITIALIZE)m->progress=(struct qcm_progress){.version=QCM_PROGRESS_VERSION};else if(r->command!=QCM_STOP)m->progress=(struct qcm_progress){.version=QCM_PROGRESS_VERSION,.generation=m->progress.generation+1,.axis=r->axis,.requested=r->value,.active=1};return 1;}
static void done(struct mock *m,unsigned steps,unsigned reason){m->status.busy=0;m->status.sequence++;if(m->last.command==QCM_INITIALIZE)m->status.initialized=1;else{m->progress.steps=steps;m->progress.reason=reason;m->progress.active=0;m->progress.valid=reason==QMP_COMPLETE||reason==QMP_HOME_COMPLETE;}}
static void begin(struct qpt_controller *s,struct mock *m,uint64_t *now){memset(m,0,sizeof *m);*now=1000;CHECK(qpt_init(s,(struct qpt_io){m,read_hw,submit}));CHECK(qpt_home(s,*now));CHECK(m->last.command==QCM_INITIALIZE);done(m,0,0);CHECK(qpt_tick(s,*now+=100000));}
static void advance(struct qpt_controller *s,struct mock *m,uint64_t *now,unsigned phase,unsigned excess){struct qhc_step h;CHECK(qhc_plan(phase,8000,2500,&h));CHECK(!s->homed&&!s->position.axis[0].known&&!s->position.axis[1].known);CHECK(m->last.command==(h.endpoint?QCM_HOME:QCM_HOME_CENTER));CHECK(m->last.axis==h.axis&&m->last.direction==h.direction&&m->last.value==h.steps);done(m,h.steps+excess,h.endpoint?QMP_HOME_COMPLETE:QMP_COMPLETE);CHECK(qpt_tick(s,*now+=1000000));}
static int busy_always(void *p,uint64_t now){(void)now;(*(unsigned *)p)++;return 0;}
static int fail_start(void *p,uint64_t now){(void)now;(*(unsigned *)p)++;return -1;}
static int start_once(void *p,uint64_t now){unsigned *n=p;(void)now;(*n)++;return *n>=3;}
int main(void){
 struct qpt_controller s;struct mock m;uint64_t now;begin(&s,&m,&now);
 for(unsigned phase=0;phase<6;phase++)advance(&s,&m,&now,phase,phase==0?4:0);
 CHECK(s.homed&&s.action==QPT_IDLE&&s.position.axis[0].known&&s.position.axis[1].known);
 CHECK(s.position.axis[0].position==4000&&s.position.axis[1].position==1252);
 CHECK(qpt_jog(&s,1,0,now));done(&m,256,QMP_COMPLETE);CHECK(qpt_tick(&s,now+=1000000)&&s.position.axis[1].position==1508);
 CHECK(qpt_jog(&s,1,1,now));done(&m,260,QMP_SHORT);CHECK(!qpt_tick(&s,now+=1000000)&&!s.homed&&s.action==QPT_STOP);
 for(unsigned phase=0;phase<6;phase++)for(unsigned fault=0;fault<6;fault++){
  begin(&s,&m,&now);for(unsigned n=0;n<phase;n++)advance(&s,&m,&now,n,0);
  struct qhc_step h;CHECK(qhc_plan(phase,8000,2500,&h));done(&m,h.steps,h.endpoint?QMP_HOME_COMPLETE:QMP_COMPLETE);
  if(fault==0)m.progress.steps=h.steps-4;
  if(fault==1)m.progress.generation++;
  if(fault==2)m.progress.valid=0;
  if(fault==3)m.progress.active=1;
  if(fault==4)m.fail_read=1;
  if(fault==5){m.progress.steps=h.steps+4;m.progress.reason=h.endpoint?QMP_COMPLETE:QMP_COMPLETE;}
  CHECK(!qpt_tick(&s,now+=1000000));CHECK(!s.homed&&!s.position.axis[0].known&&!s.position.axis[1].known&&s.action==QPT_STOP);
 }
 for(unsigned phase=0;phase<6;phase++){
  begin(&s,&m,&now);for(unsigned n=0;n<phase;n++)advance(&s,&m,&now,n,0);CHECK(qpt_stop(&s,now));done(&m,0,QMP_CANCEL);CHECK(qpt_tick(&s,now+=1000000)&&s.action==QPT_IDLE&&!s.homed);
 }
 /* Exactness stays mandatory outside full-span homing; progress isn't faked. */
 for(unsigned home=0;home<2;home++)for(unsigned over=0;over<2;over++){
  struct qmp_move a={0};unsigned requested=home?8000:256,raw=requested+4*over;
  CHECK(home?qmp_begin_home(&a,0,0,requested,1000):qmp_begin(&a,0,0,requested,1000));
  CHECK(qmp_sample(&a,raw,1,301000)==QMP_RUNNING);unsigned reason=qmp_sample(&a,raw,1,551000);
  CHECK(reason==(home?QMP_HOME_COMPLETE:over?QMP_SHORT:QMP_COMPLETE));CHECK(a.done==raw&&a.requested==requested);
  CHECK(qmp_finish(&a,raw,1,651000,0)==reason);
 }
 for(unsigned failure=0;failure<3;failure++){
  struct qmp_move a={0};CHECK(qmp_begin_home(&a,0,0,8000,1000));CHECK(qmp_sample(&a,8004,1,301000)==QMP_RUNNING);CHECK(qmp_sample(&a,8004,1,551000)==QMP_HOME_COMPLETE);
  unsigned reason=qmp_finish(&a,failure==0?0:failure==1?8004:4,failure!=1,651000,0);
  CHECK(reason==(failure==0?QMP_HOME_COMPLETE:failure==1?QMP_IO:QMP_COUNTER));CHECK(a.done==8004);
 }
 struct qsc_state boot={0};unsigned attempts=0;CHECK(qsc_tick(&boot,1,start_once,&attempts)==QSC_WAIT);CHECK(qsc_tick(&boot,1000,start_once,&attempts)==QSC_WAIT);CHECK(qsc_tick(&boot,2000,start_once,&attempts)==QSC_STARTED);
 for(unsigned n=0;n<1000;n++)CHECK(qsc_tick(&boot,3000+n,start_once,&attempts)==QSC_STARTED);CHECK(attempts==3);
 struct qsc_state unavailable={0};unsigned busy_calls=0;CHECK(qsc_tick(&unavailable,1,busy_always,&busy_calls)==QSC_WAIT);CHECK(qsc_tick(&unavailable,10000001,busy_always,&busy_calls)==QSC_UNAVAILABLE);CHECK(busy_calls==1);CHECK(qsc_tick(&unavailable,10000002,busy_always,&busy_calls)==QSC_UNAVAILABLE&&busy_calls==1);
 struct qsc_state failed={0};unsigned failed_calls=0;CHECK(qsc_tick(&failed,1,fail_start,&failed_calls)==QSC_FAILED);CHECK(qsc_tick(&failed,2,fail_start,&failed_calls)==QSC_FAILED&&failed_calls==1);
 struct qsc_state next_boot={0};CHECK(qsc_tick(&next_boot,1,start_once,&attempts)==QSC_STARTED);CHECK(attempts==4);
 puts("Six endpoint/center phases, faults and Stop at every phase, exact jogs and one automatic attempt per boot passed");printf("checks=%u\n",checks);return 0;
}

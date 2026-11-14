/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include "ptz/motor-sequence.h"
#include "ptz/move-progress.h"
struct mock {uint64_t now;unsigned writes,fail_at,power;};
static int write_reg(void *p,unsigned reg,unsigned value){struct mock *m=p;m->writes++;if(m->writes==m->fail_at)return 0;if(reg==0x17)m->power=value;return 1;}
static uint64_t clock_us(void *p){return ((struct mock *)p)->now;}
int main(void){
 struct qmc_plan plan;assert(qmc_initial(&plan));
 for(unsigned i=0;i<plan.count;i++)printf("STEP %u %u %u\n",plan.step[i].reg,plan.step[i].value,plan.step[i].delay_us);
 for(unsigned fail=0;fail<=plan.count;fail++){
  struct mock m={.now=1000,.fail_at=fail};struct qmc_state seq={0};struct qmc_io io={&m,write_reg,clock_us};
  assert(qmc_begin(&seq,&plan,m.now,0));int result=0;
  for(unsigned n=0;n<128&&!result;n++){result=qmc_tick(&seq,&io,m.now);if(seq.due>m.now)m.now=seq.due;else m.now++;}
  if(fail){assert(result==-1&&seq.failed&&!seq.active&&m.power==0);}else assert(result==1&&!seq.failed&&!seq.active&&m.power==15);
 }
 /* Real short/overrun counters are still errors; no four-step tolerance. */
 for(unsigned raw=252;raw<=260;raw+=4){
  struct qmp_move move={0};assert(qmp_begin(&move,1,0,256,1000));
  assert(qmp_sample(&move,raw,1,301000)==QMP_RUNNING);
  unsigned r=qmp_sample(&move,raw,1,551000);assert(r==(raw==256?QMP_COMPLETE:QMP_SHORT));
  assert(qmp_finish(&move,raw,1,651000,0)==r);
 }
 struct qmp_move move={0};assert(qmp_begin(&move,1,0,256,1000));
 assert(qmp_sample(&move,256,1,301000)==QMP_RUNNING);
 assert(qmp_sample(&move,256,1,551000)==QMP_COMPLETE);
 assert(qmp_finish(&move,0,1,651000,0)==QMP_COMPLETE&&move.axis[1].raw==0);
 puts("PASS initialization success and 23 injected transfer faults; exact counter completion preserved");return 0;
}

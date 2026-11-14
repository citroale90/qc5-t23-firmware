/* SPDX-License-Identifier: GPL-2.0-only
 * Bounded asynchronous actuator sequencer. Register transfers are supplied
 * by the exclusive I2C2 adapter; no vendor process, raw GPIO or flash access.
 */
#ifndef QC5_MOTOR_SEQUENCE_H
#define QC5_MOTOR_SEQUENCE_H
#ifndef __KERNEL__
#include <stdint.h>
#endif
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
#include "ptz-manual-plan.h"
struct qmc_step {uint16_t reg,value;uint32_t delay_us;};
struct qmc_plan {unsigned count;struct qmc_step step[96];};
struct qmc_io {void *opaque;int (*write)(void *,unsigned,unsigned);uint64_t (*time_us)(void *);};
struct qmc_state {struct qmc_plan plan;unsigned next,active,failed,ir_energized;uint64_t due,deadline,last_us;};
static int qmc_add(struct qmc_plan *p,unsigned reg,unsigned value,unsigned delay)
{if(!p||p->count>=96||reg>0x1b||value>0xff||delay>200000||(reg==0x16&&value>2))return 0;p->step[p->count++]=(struct qmc_step){reg,value,delay};return 1;}
static int qmc_initial(struct qmc_plan *p)
{
 if(!p)return 0;memset(p,0,sizeof *p);
 /* ID0x53 init_gc6153 groups paired setup and all four counter clears.
  * Do not insert a delay between their bytes. The bridge-off operation is
  * our explicit safety addition, while motor power is still disabled. */
 static const struct qmc_step init[]={
  {0x17,0,50},{0x16,0,50},{0x17,0x1f,50},
  {0x12,0xc4,50},{0x13,0x33,50},{0x14,0x0f,50},
  {0,0x10,50},{1,0,50},{9,0x10,50},{10,0,50},
  {3,0,0},{12,0,50},{2,4,0},{11,4,50},
  {5,8,50},{14,8,50},{6,0x22,50},{15,0x22,50},
  {24,0,0},{25,0,0},{26,0,0},{27,0,10},{0x17,0xf,50}};
 for(unsigned n=0;n<sizeof init/sizeof *init;n++)
  if(!qmc_add(p,init[n].reg,init[n].value,init[n].delay_us))return 0;
 return 1;
}
static int qmc_jog(struct qmc_plan *p,unsigned axis,unsigned direction,unsigned steps,unsigned maximum,unsigned speed)
{
 if(!p||steps>maximum)return 0;struct qpm_input in={axis,steps,maximum,direction,speed,0,31};struct qpm_plan base;
 if(!qpm_build(&in,&base))return 0;memset(p,0,sizeof *p);
 for(unsigned n=0;n<base.count;n++){
  struct qpm_event *e=&base.events[n];
  if(e->kind==1){if(!qmc_add(p,e->reg,e->value,0))return 0;}
  else if(e->kind==2){if(!p->count)return 0;p->step[p->count-1].delay_us=e->value;}
  else return 0;
 }
 return 1;
}
static int qmc_stop_directed(struct qmc_plan *p,unsigned axes,unsigned directions)
{
 if(!p||!axes||axes>3||directions>3)return 0;memset(p,0,sizeof *p);
 /* Exact ID0x53 native ramp and power-down sequence. Delays are elapsed
  * asynchronously, so HTTP/IR/stream handling never sleeps for100ms. */
 static const uint8_t lo[]={0x44,0xd8,0x90,0x60,0x40,0,0,0};
 for(unsigned a=0;a<2;a++)if(axes&(1u<<a)){
  unsigned hi=a?7:16;if(!qmc_add(p,a?6:15,7,0)||!qmc_add(p,a?1:10,0,0))return 0;
  unsigned dir=((directions>>a)&1u)<<6;
  for(unsigned n=0;n<8;n++)if(!qmc_add(p,hi,dir|(n?0:0x81),0)||!qmc_add(p,hi+1,lo[n],n==7?50000:0))return 0;
 }
 return qmc_add(p,0x17,0x0f,0);
}
/* Retained for pure initial-state callers. The hardware owner always supplies
 * each axis' last requested direction, as the original disable functions do. */
static inline int qmc_stop(struct qmc_plan *p,unsigned axes)
{return qmc_stop_directed(p,axes,0);}
static int qmc_filter(struct qmc_plan *p,unsigned night)
{
 if(!p||night>1)return 0;memset(p,0,sizeof *p);
 /* Native value1/2 selects the H-bridge polarity. Never permit3 (both).
  * Every pulse is followed by bridge-off and normal motor power. */
 return qmc_add(p,0x17,0x1f,50)&&qmc_add(p,0x15,0x7f,0)&&qmc_add(p,0x16,night?1:2,200000)&&
  qmc_add(p,0x16,0,50)&&qmc_add(p,0x17,0x0f,0);
}
static int qmc_begin(struct qmc_state *s,const struct qmc_plan *p,uint64_t now,unsigned preempt)
{
 if(!s||!p||!p->count||p->count>96||!now||preempt>1||(s->active&&!preempt)||now>UINT64_MAX-2000000)return 0;
 /* A stop cannot silently cancel an energized IR pulse: its bridge-off
  * must first succeed through qmc_abort. */
 if(s->ir_energized)return 0;
 for(unsigned n=0;n<p->count;n++)if(p->step[n].reg>0x1b||p->step[n].value>255||p->step[n].delay_us>200000||(p->step[n].reg==0x16&&p->step[n].value>2))return 0;
 *s=(struct qmc_state){.plan=*p,.active=1,.due=now,.deadline=now+2000000,.last_us=now};return 1;
}
static int qmc_abort(struct qmc_state *s,const struct qmc_io *io)
{
 if(!s||!io||!io->write)return 0;int ok=1;
 if(s->ir_energized){ok=io->write(io->opaque,0x16,0);if(ok)s->ir_energized=0;}
 /* No movement is left running after a partial command or deadline. */
 if(!io->write(io->opaque,0x17,0))ok=0;
 s->active=0;s->failed=1;return ok;
}
static int qmc_tick(struct qmc_state *s,const struct qmc_io *io,uint64_t now)
{
 if(!s||!io||!io->write||!now)return -1;if(s->failed)return -1;if(!s->active)return 1;
 if(now>s->deadline||now<s->last_us){
  (void)qmc_abort(s,io);return -1;
 }
 s->last_us=now;
 for(unsigned budget=0;budget<6&&s->next<s->plan.count;budget++){
  if(now<s->due)return 0;struct qmc_step *e=&s->plan.step[s->next];
  if(e->reg==0x16&&e->value)s->ir_energized=1; /* an error may follow a real write */
  if(!io->write(io->opaque,e->reg,e->value)){(void)qmc_abort(s,io);return -1;}
  if(e->reg==0x16)s->ir_energized=e->value!=0;
  uint64_t end=io->time_us?io->time_us(io->opaque):now;
  if(end<now||end>s->deadline){(void)qmc_abort(s,io);return -1;}
  s->next++;s->due=end+e->delay_us;if(e->delay_us)return 0;
 }
 if(s->next==s->plan.count){s->active=0;return 1;}return 0;
}
#endif

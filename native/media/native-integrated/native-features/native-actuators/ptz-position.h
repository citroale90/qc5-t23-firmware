/* SPDX-License-Identifier: GPL-2.0-only
 * Software position accounting for GC6153 rounded step counters. It tracks
 * acknowledged counts, never assumes that sending a target reached it.
 * Absolute presets stay unavailable until homing establishes both axes.
 */
#ifndef QC5_PTZ_POSITION_H
#define QC5_PTZ_POSITION_H
#include <stdint.h>
#include <string.h>
struct qps_axis {unsigned maximum,position,target,requested,completed,last_counter,negative,homing,moving,known;};
struct qps_preset {uint16_t x,y;uint8_t valid;};
struct qps_position {struct qps_axis axis[2];struct qps_preset preset[32];unsigned failed;};
static int qps_init(struct qps_position *s,unsigned x,unsigned y)
{if(!s||x<4||y<4||x>32764||y>32764||(x&3)||(y&3))return 0;memset(s,0,sizeof *s);s->axis[0].maximum=x;s->axis[1].maximum=y;return 1;}
static int qps_begin(struct qps_position *s,unsigned axis,unsigned target,unsigned homing)
{
 if(!s||s->failed||axis>1||homing>1)return 0;struct qps_axis *a=&s->axis[axis];
 if(a->moving||target>a->maximum||(target&3)||(!homing&&!a->known)||(homing&&target))return 0;
 a->target=target;a->negative=homing||target<a->position;
 a->requested=homing?a->maximum:target>a->position?target-a->position:a->position-target;
 a->completed=a->last_counter=0;a->homing=homing;a->moving=a->requested!=0;if(homing)a->known=0;return 1;
}
static int qps_observe(struct qps_position *s,unsigned axis,unsigned counter,unsigned valid)
{
 if(!s||s->failed||axis>1||valid>1)return 0;struct qps_axis *a=&s->axis[axis];
 if(!a->moving)return 1;
 if(!valid||counter>32764||(counter&3)){a->known=0;s->failed=1;return 0;}
 /* OEM phase-clear behavior: a reset/wrap lowers the cursor and contributes
  * no movement in this sample; further increments are counted once. */
 unsigned delta=counter>=a->last_counter?counter-a->last_counter:0;a->last_counter=counter;
 if(delta>a->requested-a->completed){a->known=0;s->failed=1;return 0;}
 a->completed+=delta;
 if(!a->homing){
  if(a->negative){if(delta>a->position){a->known=0;s->failed=1;return 0;}a->position-=delta;}
  else{if(delta>a->maximum-a->position){a->known=0;s->failed=1;return 0;}a->position+=delta;}
 }return 1;
}
static int qps_stopped(struct qps_position *s,unsigned axis,unsigned hardware_stop_confirmed)
{
 if(!s||axis>1||hardware_stop_confirmed>1)return 0;struct qps_axis *a=&s->axis[axis];
 if(!hardware_stop_confirmed||s->failed){a->known=0;a->moving=0;s->failed=1;return 0;}
 if(a->homing){if(a->completed!=a->requested){a->known=0;a->moving=0;return 0;}a->position=0;a->known=1;}
 a->moving=a->homing=0;return 1;
}
static int qps_save(struct qps_position *s,unsigned slot)
{
 if(!s||slot>=32||s->failed||!s->axis[0].known||!s->axis[1].known||s->axis[0].moving||s->axis[1].moving)return 0;
 s->preset[slot]=(struct qps_preset){s->axis[0].position,s->axis[1].position,1};return 1;
}
static int qps_target(const struct qps_position *s,unsigned slot,unsigned *x,unsigned *y)
{
 if(!s||!x||!y||slot>=32||s->failed||!s->axis[0].known||!s->axis[1].known||s->axis[0].moving||s->axis[1].moving||!s->preset[slot].valid)return 0;
 unsigned a=s->preset[slot].x,b=s->preset[slot].y;if(a>s->axis[0].maximum||b>s->axis[1].maximum||(a&3)||(b&3))return 0;*x=a;*y=b;return 1;
}
#endif

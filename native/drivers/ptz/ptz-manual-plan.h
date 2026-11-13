/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pure protocol planning, no MMIO/I2C or actuator access. Initial native
 * controller profile only: trigger bit7, direction bit6, power0x1f.
 * Mechanical limits/calibration must be supplied before any future executor.
 */
#ifndef QC5_PTZ_MANUAL_PLAN_H
#define QC5_PTZ_MANUAL_PLAN_H
#ifndef __KERNEL__
#include <stdint.h>
#endif
#ifndef __KERNEL__
#include <stddef.h>
#endif
struct qpm_input { uint32_t axis,steps,maximum,direction,speed,mode,power; };
struct qpm_event { uint32_t kind,reg,value; };
struct qpm_plan { uint32_t count;struct qpm_event events[28]; };
static void qpm_add(struct qpm_plan *p,uint32_t kind,uint32_t reg,uint32_t value) {
 p->events[p->count++]=(struct qpm_event){kind,reg,value};
}
static int qpm_build(const struct qpm_input *in,struct qpm_plan *out) {
 if(!in||!out||in->axis>1||!in->steps||in->steps>65535||!in->maximum||in->maximum>65535||
    in->direction>1||in->speed>255||in->mode>2||in->power!=31)return 0;
 struct qpm_plan p={0};
 uint32_t steps=in->steps>in->maximum?in->maximum:in->steps;
 uint32_t remainder=steps>256?steps-256:steps;
 uint32_t high=in->axis?7:16,low=high+1,dir=in->direction<<6;
 uint32_t speed=30*in->speed+215;
 uint32_t ramp[5]={speed,speed*9/10,speed*8/10,speed*6/10,speed/2};
 qpm_add(&p,1,23,in->power);qpm_add(&p,2,0,50);
 qpm_add(&p,1,in->axis?6:15,2);
 qpm_add(&p,1,in->axis?1:10,in->mode==2?64:in->mode==1?96:127);
 for(unsigned i=0;i<5;i++) {
  qpm_add(&p,1,high,dir|((ramp[i]>>8)&3)|(i==0?128:0));
  qpm_add(&p,1,low,ramp[i]&255);qpm_add(&p,2,0,10);
 }
 /* The original Y path clears direction for this marker; X retains it. */
 qpm_add(&p,1,high,in->axis?0:dir);qpm_add(&p,1,low,steps>256?32:0);qpm_add(&p,2,0,10);
 qpm_add(&p,1,high,(remainder>>8)&3);qpm_add(&p,1,low,remainder&255);qpm_add(&p,2,0,10);
 qpm_add(&p,1,high,dir);qpm_add(&p,1,low,(remainder>>10)&255);qpm_add(&p,2,0,10);
 if(p.count!=28)return 0;
 *out=p;return 1;
}
#endif

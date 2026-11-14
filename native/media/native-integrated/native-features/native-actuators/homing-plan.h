/* SPDX-License-Identifier: GPL-2.0-or-later
 * Six cooperative phases from original motor_center (reverse=1):
 * X dir1 -> dir2 -> center dir1; Y dir2 -> dir1 -> center dir2.
 * Native logical0=left/up,1=right/down. Existing validated board limits
 * replace the OEM probe's generic limits that the application can override.
 * This is a full-span hard-stop convention, never encoder feedback.
 */
#ifndef QC5_HOMING_PLAN_H
#define QC5_HOMING_PLAN_H
#include <stdint.h>
#include "../../../../drivers/ptz/peripheral-abi.h"
#define QHC_PHASES 6u
struct qhc_step {unsigned axis,direction,steps,endpoint,position;};
static int qhc_plan(unsigned phase,unsigned xmax,unsigned ymax,struct qhc_step *out)
{
 if(!out||phase>=QHC_PHASES||xmax<8||ymax<8||xmax>32764||ymax>32764||(xmax&3)||(ymax&3))return 0;
 /* Original center-setting ioctls round upward to the four-step quantum. */
 unsigned xc=(xmax/2+3)&~3u,yc=(ymax/2+3)&~3u;
 static const unsigned axis[6]={0,0,0,1,1,1};
 static const unsigned direction[6]={0,1,0,1,0,1};
 struct qhc_step p={.axis=axis[phase],.direction=direction[phase],.endpoint=phase%3!=2};
 unsigned maximum=p.axis?ymax:xmax,center=p.axis?yc:xc;
 p.steps=p.endpoint?maximum:p.direction?maximum-center:center;
 p.position=p.endpoint?(p.direction?0:maximum):center;
 *out=p;return 1;
}
#endif

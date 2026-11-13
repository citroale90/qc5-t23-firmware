/* SPDX-License-Identifier: GPL-3.0-only
 * Default-control day/night BCSH calculator. No hardware or allocation.
 * Refresh/deadband policy and hardware commit remain the caller's job.
 */
#ifndef QC5_BCSH_DYNAMIC_H
#define QC5_BCSH_DYNAMIC_H
#include "bcsh-ev.h"
#include "bcsh-matrix.h"
struct qbd_profile {struct qbe_profile exposure;struct qbm_profile colour;};
_Static_assert(sizeof(struct qbd_profile)==552,"Calibrated dynamic BCSH profile");
static int qbd_calculate(const struct qbd_profile *p,uint32_t ct,uint32_t ev_q10,uint32_t out[29])
{
 if(!p||!out)return 0;
 uint32_t result[29];struct qbm_result colour;
 if(!qbe_calculate(&p->exposure,ev_q10,result)||!qbm_calculate(&p->colour,ct,&colour))return 0;
 for(unsigned n=0;n<6;n++)result[9+n]=colour.registers[n];
 memcpy(out,result,sizeof result);return 1;
}
#endif

/* SPDX-License-Identifier: GPL-3.0-only
 * Pure, allocation-free exposure adaptation for the measured neutral GC1084
 * BCSH calibration. No MMIO, camera access or user colour-control changes.
 * Every result must match the pinned original MIPS oracle before integration.
 */
#ifndef QC5_BCSH_EV_H
#define QC5_BCSH_EV_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
struct qbe_profile {uint32_t initial[29],ev[9],saturation[4][9],threshold[3],checksum;};
_Static_assert(sizeof(struct qbe_profile)==312,"Private exposure profile layout");
static uint32_t qbe_hash(const struct qbe_profile *p)
{
 const unsigned char *b=(const unsigned char *)p;uint32_t h=2166136261u;
 for(unsigned n=0;n<offsetof(struct qbe_profile,checksum);n++)h=(h^b[n])*16777619u;
 return h;
}
static int qbe_valid(const struct qbe_profile *p)
{
 if(!p||!p->checksum||p->checksum!=qbe_hash(p))return 0;
 if(p->threshold[0]>7||p->threshold[1]>=p->threshold[2]||p->threshold[2]>65535)return 0;
 for(unsigned n=0;n<9;n++){
  if(p->ev[n]>4194303u||(n&&p->ev[n]<=p->ev[n-1]))return 0;
  for(unsigned k=0;k<4;k++)if(p->saturation[k][n]>65535)return 0;
 }
 return 1;
}
static uint32_t qbe_interpolate(uint32_t lo,uint32_t hi,uint32_t position,uint32_t span)
{
 uint32_t delta=(uint32_t)((uint64_t)(lo<hi?hi-lo:lo-hi)*position/span);
 return hi>=lo?lo+delta:lo-delta;
}
static int qbe_calculate(const struct qbe_profile *p,uint32_t ev_q10,uint32_t out[29])
{
 if(!out||!qbe_valid(p))return 0;
 uint32_t ev=ev_q10>>10,values[4];unsigned low=0,high=0;
 if(ev>=p->ev[8])low=high=8;
 else if(ev>p->ev[0])for(unsigned n=1;n<9;n++)if(ev<=p->ev[n]){low=n-1;high=n;break;}
 for(unsigned k=0;k<4;k++)values[k]=low==high?p->saturation[k][low]:
  qbe_interpolate(p->saturation[k][low],p->saturation[k][high],ev-p->ev[low],p->ev[high]-p->ev[low]);
 uint32_t range=p->threshold[2]-p->threshold[1];
 uint32_t step0=(values[1]>=values[0]?values[1]-values[0]:values[0]-values[1])/range;
 uint32_t step1=(values[3]>=values[2]?values[3]-values[2]:values[2]-values[3])/range;
 if(step0>8191||step1>65535)return 0;
 memcpy(out,p->initial,sizeof p->initial);
 out[25]=(step1<<16)|(step0<<3)|p->threshold[0];
 out[27]=(values[0]<<16)|values[1];out[28]=(values[2]<<16)|values[3];
 return 1;
}
#endif

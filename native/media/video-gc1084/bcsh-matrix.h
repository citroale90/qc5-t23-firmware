/* SPDX-License-Identifier: GPL-3.0-only
 * Pure default-hue BCSH matrix adaptation, based on open-tx-isp's T23
 * fixed-point transform. Calibration stays private and external. No MMIO.
 */
#ifndef QC5_BCSH_MATRIX_H
#define QC5_BCSH_MATRIX_H
#include <stddef.h>
#include <stdint.h>
struct qbm_profile {uint32_t version,ct[4];int32_t matrix[4][9],m[9],minv[9];uint32_t checksum;};
struct qbm_result {int32_t interpolated[9];uint32_t registers[6],region;};
_Static_assert(sizeof(struct qbm_profile)==240,"Calibrated matrix profile");
static uint32_t qbm_hash(const struct qbm_profile *p)
{
 const unsigned char *b=(const unsigned char *)p;uint32_t h=2166136261u;
 for(unsigned n=0;n<offsetof(struct qbm_profile,checksum);n++)h=(h^b[n])*16777619u;
 return h;
}
static int qbm_valid(const struct qbm_profile *p)
{
 if(!p||p->version!=1||!p->checksum||p->checksum!=qbm_hash(p))return 0;
 for(unsigned n=0;n<4;n++){
  if(p->ct[n]<1000||p->ct[n]>10000||(n&&p->ct[n]<=p->ct[n-1]+400u))return 0;
  for(unsigned k=0;k<9;k++)if(p->matrix[n][k]<-8192||p->matrix[n][k]>8191)return 0;
 }
 for(unsigned k=0;k<9;k++)if(p->m[k]<-200000||p->m[k]>200000||p->minv[k]<-200000||p->minv[k]>200000)return 0;
 return 1;
}
static int32_t qbm_mul(int32_t a,int32_t b,unsigned shift)
{
 uint64_t am=a<0?-(uint64_t)(int64_t)a:(uint64_t)a,bm=b<0?-(uint64_t)(int64_t)b:(uint64_t)b;
 int32_t magnitude=(int32_t)((am*bm)>>shift);
 return (a<0)!=(b<0)?-magnitude:magnitude;
}
static int qbm_calculate(const struct qbm_profile *p,uint32_t ct,struct qbm_result *out)
{
 if(!out||!qbm_valid(p)||ct<1000||ct>30000)return 0;
 unsigned low=3,high=3,region=6;uint32_t position=0,span=1;
 if(ct<=p->ct[0]+200){low=high=0;region=0;}
 else for(unsigned n=1;n<4;n++){
  if(ct<p->ct[n]-200){low=n-1;high=n;region=2*n-1;position=ct-p->ct[n-1]-200;span=p->ct[n]-p->ct[n-1]-400;break;}
  if(ct<=p->ct[n]+200){low=high=n;region=2*n;break;}
 }
 struct qbm_result result={.region=region};int32_t first[9],second[9];uint32_t packed[9];
 for(unsigned k=0;k<9;k++){
  int32_t a=p->matrix[low][k],b=p->matrix[high][k];
  result.interpolated[k]=a+(int32_t)(((int64_t)b-a)*position/span);
 }
 for(unsigned row=0;row<3;row++)for(unsigned col=0;col<3;col++){
  int32_t sum=0;for(unsigned k=0;k<3;k++)sum+=qbm_mul(p->m[3*row+k],result.interpolated[3*k+col],10);
  first[3*row+col]=sum;
 }
 for(unsigned row=0;row<3;row++)for(unsigned col=0;col<3;col++){
  int32_t sum=0;for(unsigned k=0;k<3;k++)sum+=qbm_mul(first[3*row+k],p->minv[3*k+col],16);
  second[3*row+col]=sum<0?-((-sum)>>6):sum>>6;
 }
 for(unsigned k=0;k<9;k++){
  if(second[k]<-8192||second[k]>8191)return 0;
  packed[k]=(uint32_t)second[k]&0x3fffu;
 }
 for(unsigned row=0;row<3;row++){
  result.registers[2*row]=packed[3*row]|(packed[3*row+1]<<16);
  result.registers[2*row+1]=packed[3*row+2];
 }
 *out=result;return 1;
}
#endif

/* SPDX-License-Identifier: GPL-3.0-only
 * Pure userspace colour matrix adaptation from opensensor/open-tx-isp
 * 2fb6534b9ecdc8d39a6041debee6455ec2da512f, tx_isp_t23_core.c.
 * No I/O, allocation or kernel code. Private calibration remains external.
 */
#ifndef QC5_CCM_ALGORITHM_H
#define QC5_CCM_ALGORITHM_H
#include <stdint.h>
#include <stddef.h>
struct qc5_ccm_profile {uint32_t version,ct[4],matrix[4][9],ev[9],saturation[9],checksum;};
struct qc5_ccm_result {int32_t matrix[9];uint32_t registers[5],region,saturation;};
static uint32_t qc5_ccm_reg_hash(uint32_t hash,uint32_t off,uint32_t value)
{
    for(unsigned n=0;n<8;++n)hash=(hash^(((n<4?off:value)>>(8*(n%4)))&255u))*16777619u;
    return hash;
}
static uint32_t qc5_ccm_hash(const struct qc5_ccm_profile *p)
{
    const unsigned char *raw=(const unsigned char *)p;uint32_t h=2166136261u;
    for(unsigned n=0;n<offsetof(struct qc5_ccm_profile,checksum);++n)h=(h^raw[n])*16777619u;
    return h;
}
static int qc5_ccm_profile_valid(const struct qc5_ccm_profile *p)
{
    if(!p||p->version!=1||p->checksum!=qc5_ccm_hash(p))return 0;
    for(unsigned n=0;n<4;++n){if(p->ct[n]<1000||p->ct[n]>10000||(n&&p->ct[n]<=p->ct[n-1]+400u))return 0;
        for(unsigned k=0;k<9;++k)if(p->matrix[n][k]>0x3fff)return 0;}
    for(unsigned n=0;n<9;++n)if(p->saturation[n]>512||(n&&p->ev[n]<p->ev[n-1]))return 0;
    return 1;
}
static int32_t qc5_ccm_signed14(uint32_t v){return v>=8192?(int32_t)v-16384:(int32_t)v;}
static int32_t qc5_ccm_blend(int32_t lo,int32_t hi,uint32_t position,uint32_t span)
{
    int64_t product=(int64_t)(hi-lo)*position;
    /* The original interpolates the magnitude and then applies the sign.
     * A signed arithmetic shift would be one too low on falling segments. */
    return lo+(int32_t)(product/span);
}
static uint32_t qc5_ccm_saturation(const struct qc5_ccm_profile *p,uint32_t ev_q10)
{
    uint32_t ev=ev_q10>>10;
    for(unsigned n=0;n<9;++n){
        if(ev>p->ev[n])continue;
        if(!n||p->ev[n]==p->ev[n-1])return p->saturation[n];
        uint32_t lo=p->saturation[n-1],hi=p->saturation[n];
        uint32_t delta=(uint32_t)((uint64_t)(ev-p->ev[n-1])*(hi>lo?hi-lo:lo-hi)/(p->ev[n]-p->ev[n-1]));
        return hi>=lo?lo+delta:lo-delta;
    }
    return p->saturation[8];
}
static int qc5_ccm_calculate(const struct qc5_ccm_profile *p,uint32_t ct,uint32_t ev_q10,struct qc5_ccm_result *out)
{
    if(!out||!qc5_ccm_profile_valid(p)||ct<1000||ct>30000)return 0;
    unsigned low=3,high=3,region=6;uint32_t position=0,span=1;
    if(ct<=p->ct[0]+200){low=high=0;region=0;}
    else for(unsigned n=1;n<4;++n){
        if(ct<p->ct[n]-200){low=n-1;high=n;region=2*n-1;
            position=ct-p->ct[n-1]-200;span=p->ct[n]-p->ct[n-1]-400;break;}
        if(ct<=p->ct[n]+200){low=high=n;region=2*n;break;}
    }
    int32_t matrix[9],sat[9];uint32_t saturation=qc5_ccm_saturation(p,ev_q10);
    static const uint32_t luma[3]={0x4c8b,0x9646,0x1d2f};
    for(unsigned n=0;n<9;++n)matrix[n]=qc5_ccm_blend(qc5_ccm_signed14(p->matrix[low][n]),qc5_ccm_signed14(p->matrix[high][n]),position,span);
    for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column)
        sat[row*3+column]=row==column?(int32_t)((saturation*(65536-luma[column])>>8)+luma[column]):
            (int32_t)luma[column]-(int32_t)(saturation*luma[column]>>8);
    for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column){
        int32_t sum=0;
        for(unsigned k=0;k<3;++k){int32_t a=matrix[row*3+k],b=sat[k*3+column];
            uint32_t am=(uint32_t)(a<0?-a:a),bm=(uint32_t)(b<0?-b:b);
            int32_t term=(int32_t)(((uint64_t)(am<<6)*bm)>>16);
            sum+=(a<0)!=(b<0)?-term:term;
        }
        out->matrix[row*3+column]=sum<0?-((-sum)>>6):sum>>6;
    }
    for(unsigned n=0;n<5;++n){out->registers[n]=(uint32_t)out->matrix[2*n]&0x3fff;
        if(n<4)out->registers[n]|=((uint32_t)out->matrix[2*n+1]&0x3fff)<<16;}
    out->region=region;out->saturation=saturation;return 1;
}
static uint32_t qc5_ccm_calculated_hash(const struct qc5_ccm_profile *p,uint32_t ct,uint32_t ev)
{
    struct qc5_ccm_result result;if(!qc5_ccm_calculate(p,ct,ev,&result))return 0;
    uint32_t hash=2166136261u;for(unsigned n=0;n<5;++n)hash=qc5_ccm_reg_hash(hash,0x5004+4*n,result.registers[n]);
    return hash;
}
#endif

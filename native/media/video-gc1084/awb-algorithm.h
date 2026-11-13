/* SPDX-License-Identifier: GPL-3.0-only
 * AWB math adapted from opensensor/open-tx-isp, commit
 * 2fb6534b9ecdc8d39a6041debee6455ec2da512f. See awb-math.inc.
 * Pure userspace math: no kernel API, DMA, MMIO, allocation or I/O.
 */
#ifndef QC5_AWB_ALGORITHM_H
#define QC5_AWB_ALGORITHM_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include "awb-cluster.h"
#define QC5_AWB_ZONES 225u
struct qc5_awb_profile {
    uint32_t version,point,pixel_threshold,history,temporal_threshold,outdoor_ev,indoor_ev,startup_ev,startup_ct;
    uint32_t mf[6],wb_static[2],lights[20],light_count;
    uint32_t ct_in_parameters[2],ct_out_parameters[2],ct_indoor[4],ct_out_high[4],ct_out_low[4];
    uint32_t rg_pos[15],bg_pos[15],distance[3];
    uint32_t indoor_mesh[225],ct_mesh[225],zone_mesh[225],outdoor_mesh[225],light_lut[514];
    uint32_t cluster[10],trend[7],checksum;
};
struct qc5_awb_snapshot {uint32_t r[225],g[225],b[225],pixels[225];};
struct qc5_awb_calculation {uint32_t rg,bg,ct,active_zones,light_source_zones;};
struct qc5_awb_result {uint32_t r,b,ct,rg,bg,active_zones,light_zones,updates;};
struct qc5_awb_state {
    const struct qc5_awb_profile *profile;
    struct t23x_awb_cluster_state clusters;
    uint32_t raw_rg[225],raw_bg[225],ratio[450],pixel_weight[225],mesh_weight[225];
    uint32_t distance[225],distance_weight[225],cluster_r[225],cluster_b[225],cluster_w[225];
    uint32_t history_rg[15],history_bg[15],history_ct[15],history_count;
    uint32_t ev,previous_r,previous_b,have_target,target_r,target_b,current_r,current_b,origin_r,origin_b,smooth_step;
    uint32_t runs,updates;
    uint32_t ct_thresholds[4],ct_enabled,ct_base;
};
static uint32_t qc5_awb_profile_hash(const struct qc5_awb_profile *p)
{
    const uint8_t *raw=(const uint8_t *)p;uint32_t h=2166136261u;
    for(unsigned n=0;n<offsetof(struct qc5_awb_profile,checksum);++n)h=(h^raw[n])*16777619u;
    return h;
}
static int qc5_awb_profile_valid(const struct qc5_awb_profile *p)
{
    if(!p||p->version!=1||p->point!=10||!p->pixel_threshold||p->pixel_threshold>8191||
       !p->history||p->history>15||p->temporal_threshold>65535||!p->outdoor_ev||p->outdoor_ev>=p->indoor_ev||
       p->indoor_ev>=p->startup_ev||p->indoor_ev-p->outdoor_ev<3||p->startup_ev>(UINT32_MAX>>10)||
       p->startup_ct<1000||p->startup_ct>10000||!p->light_count||p->light_count>10||
       !p->wb_static[0]||p->wb_static[0]>4096||!p->wb_static[1]||p->wb_static[1]>4096||
       p->cluster[5]>255||p->checksum!=qc5_awb_profile_hash(p))return 0;
    for(unsigned n=0;n<15;++n)
        if(!p->rg_pos[n]||p->rg_pos[n]>1023||!p->bg_pos[n]||p->bg_pos[n]>1023||
           (n&&(p->rg_pos[n]<=p->rg_pos[n-1]||p->bg_pos[n]<=p->bg_pos[n-1])))return 0;
    for(unsigned n=0;n<225;++n)
        if(p->indoor_mesh[n]>65535||p->outdoor_mesh[n]>65535||p->zone_mesh[n]>4096||p->ct_mesh[n]>65535)return 0;
    for(unsigned n=0;n<514;++n)if(p->light_lut[n]>65535)return 0;
    for(unsigned n=0;n<20;++n)if(p->lights[n]>1023)return 0;
    if(p->ct_in_parameters[0]>1||p->ct_out_parameters[0]>1||p->ct_in_parameters[1]>256||p->ct_out_parameters[1]>256)return 0;
    const uint32_t *curves[3]={p->ct_indoor,p->ct_out_high,p->ct_out_low};
    for(unsigned n=0;n<3;++n){const uint32_t *c=curves[n];unsigned enabled=n?p->ct_out_parameters[0]:p->ct_in_parameters[0];
        if(c[1]<1000||c[3]>30000||c[1]>c[0]||c[0]>=c[2]||c[2]>c[3]||
           (enabled&&(c[1]==c[0]||c[2]==c[3])))return 0;
    }
    if(p->distance[0]==1&&p->distance[1]<=p->distance[2])return 0;
    return 1;
}
static int qc5_awb_init(struct qc5_awb_state *s,const struct qc5_awb_profile *p)
{
    if(!s)return 0;memset(s,0,sizeof(*s));
    if(!qc5_awb_profile_valid(p))return 0;
    s->profile=p;s->ev=p->startup_ev<<10;return 1;
}
#define AWB_MIN(a,b) ((a)<(b)?(a):(b))
#define AWB_MAX(a,b) ((a)>(b)?(a):(b))
static uint32_t awb_mul_q(struct qc5_awb_state *,uint32_t,uint32_t);
static uint32_t awb_div_q(struct qc5_awb_state *,uint64_t,uint64_t);
static void qc5_awb_ct_prepare(struct qc5_awb_state *s)
{
    const struct qc5_awb_profile *p=s->profile;
    uint32_t ev=s->ev>>10,span=(p->indoor_ev-p->outdoor_ev)/3,end=p->indoor_ev+span;
    const uint32_t *selected=ev<p->indoor_ev?p->ct_out_parameters:p->ct_in_parameters;
    s->ct_enabled=selected[0];s->ct_base=selected[1];
    const uint32_t *a=p->ct_indoor,*b=a;uint32_t position=0;
    if(p->ct_out_parameters[0]){
        if(ev<p->outdoor_ev)a=b=p->ct_out_high;
        else if(ev<p->outdoor_ev+span){a=p->ct_out_high;b=p->ct_out_low;position=ev-p->outdoor_ev;}
        else if(ev<p->indoor_ev)a=b=p->ct_out_low;
        else if(ev<end){a=p->ct_out_low;b=p->ct_indoor;position=ev-p->indoor_ev;}
        if(!p->ct_in_parameters[0]&&ev>=p->indoor_ev){
            a=b=p->ct_out_low;s->ct_enabled=ev>=end?0:p->ct_out_parameters[0];
            s->ct_base=ev>=end?256:p->ct_out_parameters[1]+(256-p->ct_out_parameters[1])*(ev-p->indoor_ev)/span;
        }else if(ev>=p->indoor_ev&&ev<end){
            s->ct_base=(uint32_t)(((int64_t)p->ct_out_parameters[1]*span+
                ((int64_t)p->ct_in_parameters[1]-p->ct_out_parameters[1])*(ev-p->indoor_ev))/span);
        }
    }else{
        s->ct_base=256;
        if(p->ct_in_parameters[0]&&ev>=p->indoor_ev)
            s->ct_base=ev>=end?p->ct_in_parameters[1]:256-(256-p->ct_in_parameters[1])*(ev-p->indoor_ev)/span;
    }
    for(unsigned n=0;n<4;++n){
        int64_t value=(int64_t)a[n]*span+((int64_t)b[n]-a[n])*position;
        s->ct_thresholds[n]=(uint32_t)(value/span);
    }
}
static uint32_t qc5_awb_ct_weight(struct qc5_awb_state *s,uint32_t rg,uint32_t bg,
                                   uint32_t mesh,uint32_t inverse_ct_q)
{
    (void)rg;(void)bg;if(!s->ct_enabled)return mesh;
    uint32_t inverse=inverse_ct_q>>s->profile->point;
    if(!inverse)return mesh; /* IQ validation admits zero CT cells; no divide by zero. */
    uint32_t ct=1000000u/inverse,*c=s->ct_thresholds,q=s->profile->point;
    uint32_t factor=s->ct_base<<q;
    if(ct>c[1]&&ct<c[3]){
        if(ct>=c[0]&&ct<=c[2])factor=256u<<q;
        else{
            uint32_t delta=ct>c[2]?c[3]-ct:ct-c[1];
            uint32_t span=ct>c[2]?c[3]-c[2]:c[0]-c[1];
            uint32_t slope=awb_div_q(s,(256u-s->ct_base)<<q,span<<q);
            factor+=awb_mul_q(s,delta<<q,slope);
        }
    }
    return awb_div_q(s,awb_mul_q(s,mesh,factor),256u<<q);
}
#include "awb-math.inc"
static uint32_t qc5_awb_absdiff(uint32_t a,uint32_t b){return a>b?a-b:b-a;}
static int qc5_awb_run(struct qc5_awb_state *s,const struct qc5_awb_snapshot *snapshot,
                         uint32_t ev_q10,struct qc5_awb_result *out)
{
    if(!s||!s->profile||!snapshot||!out)return -EINVAL;
    for(unsigned n=0;n<225;++n)
        if(snapshot->r[n]>0x1fffff||snapshot->g[n]>0x1fffff||snapshot->b[n]>0x1fffff||snapshot->pixels[n]>8191)return -ERANGE;
    s->ev=ev_q10;qc5_awb_ct_prepare(s);struct qc5_awb_calculation result={0};
    int ret=awb_calculate(s,snapshot,&result);++s->runs;
    if(ret)return ret;
    if(result.rg||result.bg)awb_history_update(s,&result.rg,&result.bg,&result.ct);
    uint32_t r=awb_ratio_to_mf(s,result.rg),b=awb_ratio_to_mf(s,result.bg);
    uint32_t threshold=s->profile->cluster[8]?s->profile->cluster[9]:s->profile->temporal_threshold;
    if(!s->have_target){
        s->target_r=s->current_r=s->origin_r=r;s->target_b=s->current_b=s->origin_b=b;
        s->have_target=1;s->smooth_step=s->profile->history;
    }else if(qc5_awb_absdiff(r,s->target_r)+qc5_awb_absdiff(b,s->target_b)>threshold){
        s->origin_r=s->current_r;s->origin_b=s->current_b;
        s->target_r=r;s->target_b=b;s->smooth_step=0;
    }
    if(s->smooth_step<s->profile->history){
        ++s->smooth_step;
        uint32_t dr=qc5_awb_absdiff(s->target_r,s->origin_r)*s->smooth_step/s->profile->history;
        uint32_t db=qc5_awb_absdiff(s->target_b,s->origin_b)*s->smooth_step/s->profile->history;
        s->current_r=s->target_r>=s->origin_r?s->origin_r+dr:s->origin_r-dr;
        s->current_b=s->target_b>=s->origin_b?s->origin_b+db:s->origin_b-db;
    }
    r=s->current_r;b=s->current_b;
    r=(r*s->profile->wb_static[0]+512u)>>10;
    b=(b*s->profile->wb_static[1]+512u)>>10;
    r=t23x_awb_gain_trend(s->profile->trend,result.ct,r,0);
    b=t23x_awb_gain_trend(s->profile->trend,result.ct,b,1);
    if(qc5_awb_absdiff(r,s->previous_r)>4||qc5_awb_absdiff(b,s->previous_b)>4){
        s->previous_r=r;s->previous_b=b;++s->updates;
    }
    *out=(struct qc5_awb_result){s->previous_r,s->previous_b,result.ct,result.rg,result.bg,result.active_zones,result.light_source_zones,s->updates};
    return 0;
}
#undef AWB_MIN
#undef AWB_MAX
#endif

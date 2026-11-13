/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_HELIX_LAYOUT_H
#define QC5_HELIX_LAYOUT_H
#include <stdint.h>
#include "qc5-nv12-layout.h"

enum qc5_helix_area {H_REF0,H_REF1,H_BITSTREAM,H_SCRATCH,H_DESCRIPTOR,H_ACCESS_UNIT,H_AREA_COUNT};
struct qc5_helix_region {uint32_t start,bytes;};
struct qc5_helix_layout {
    struct qc5_helix_region region[H_AREA_COUNT];
    uint32_t scratch_offsets[4],end;
};
/* Caller owns a layout already checked with qc5_helix_layout_valid. */
static int qc5_helix_owns(const struct qc5_helix_layout *p,uint32_t address)
{
    for(unsigned int n=0;n<H_AREA_COUNT;++n)
        if(address>=p->region[n].start&&address-p->region[n].start<p->region[n].bytes)return 1;
    return 0;
}
#define QC5_HELIX_MAX_CABAC (512u*1024u)
#define QC5_HELIX_AU_BYTES (QC5_HELIX_MAX_CABAC+QC5_HELIX_MAX_CABAC/2u+512u)
#define QC5_HELIX_AU_PHYS UINT32_C(0x01c0e000)
#define QC5_HELIX_CP0_LOG (QC5_PREVIEW_PHYS+512u)

static int qc5_helix_layout(unsigned int width,unsigned int height,struct qc5_helix_layout *out)
{
    if(!out||width!=1280||height!=720)return 0;
    struct qc5_nv12_bank input;
    if(!qc5_nv12_plan(1,width,height,&input))return 0;
    const uint32_t sizes[H_AREA_COUNT]={1382400,1382400,1048576+4096,958464,16384,QC5_HELIX_AU_BYTES};
    /* Preserve the FULL existing NV12 tail guard, not only page padding. */
    uint32_t cursor=(input.end+4096u+4095u)&~4095u;
    for(unsigned int n=0;n<H_AREA_COUNT;++n){
        if(n==H_ACCESS_UNIT){if(cursor>QC5_HELIX_AU_PHYS)return 0;cursor=QC5_HELIX_AU_PHYS;}
        uint64_t end=(uint64_t)cursor+sizes[n];
        if(end>QC5_STATS_GUARD_BASE)return 0;
        out->region[n]=(struct qc5_helix_region){cursor,sizes[n]};
        cursor=((uint32_t)end+4095u)&~4095u;
        if(cursor>QC5_STATS_GUARD_BASE-4096u)return 0;
        cursor+=4096u;
    }
    out->scratch_offsets[0]=0x17000;out->scratch_offsets[1]=0x51000;
    out->scratch_offsets[2]=0x61000;out->scratch_offsets[3]=0x9b000;
    out->end=cursor;
    return out->region[H_ACCESS_UNIT].start==QC5_HELIX_AU_PHYS&&cursor<=QC5_STATS_GUARD_BASE;
}

static int qc5_helix_layout_valid(const struct qc5_helix_layout *plan)
{
    struct qc5_helix_layout expected;
    if(!plan||!qc5_helix_layout(1280,720,&expected))return 0;
    for(unsigned int n=0;n<H_AREA_COUNT;++n)
        if(plan->region[n].start!=expected.region[n].start||plan->region[n].bytes!=expected.region[n].bytes)return 0;
    for(unsigned int n=0;n<4;++n)if(plan->scratch_offsets[n]!=expected.scratch_offsets[n])return 0;
    return plan->end==expected.end;
}
#endif

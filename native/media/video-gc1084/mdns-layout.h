/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native-calibrated MDNS layout used by the temporal capture adapter.
 * The adapter requires a verified no-map DT reservation and checks every
 * destination before enabling hardware access to this span.
 */
#ifndef QC5_MDNS_LAYOUT_H
#define QC5_MDNS_LAYOUT_H
#include "qc5-frame-layout.h"
#define QC5_MDNS_RESERVE_START 0x01280000u
#define QC5_MDNS_RESERVE_END QC5_CAPTURE_RAM_BASE
#define QC5_MDNS_BASE (QC5_MDNS_RESERVE_START+4096u)
#define QC5_MDNS_ALLOCATED 0x152000u
#define QC5_MDNS_USED 1384240u
struct qc5_mdns_pair {uint32_t offset,value;};
struct qc5_mdns_layout {uint32_t base,allocated,used,width,height,memopt,mode;struct qc5_mdns_pair registers[20];};
_Static_assert(QC5_MDNS_BASE+QC5_MDNS_ALLOCATED+4096u<=QC5_MDNS_RESERVE_END,"MDNS keeps both guard pages below the existing capture reservation");
static int qc5_mdns_plan(uint32_t base,unsigned width,unsigned height,unsigned mode,unsigned memopt,struct qc5_mdns_layout *out)
{
 if(!out||!base||(base&4095u)||width!=1280||height!=720||mode>1||memopt!=1||
    (uint64_t)base+QC5_MDNS_ALLOCATED>33554432u)return 0;
 *out=(struct qc5_mdns_layout){.base=base,.allocated=QC5_MDNS_ALLOCATED,.used=QC5_MDNS_USED,
    .width=width,.height=height,.mode=mode,.memopt=memopt};
 const uint32_t offsets[]={0x7820,0x7824,0x7828,0x782c,0x7830,0x7834,0x7840,0x7844,0x7848,0x784c,
    0x7850,0x7854,0x7838,0x783c,0x7858,0x785c,0x7860,0x7864,0x7868,0x786c};
 const uint32_t metadata=base+1280u*720u*3u/2u;
 const uint32_t values[]={base,1280,base+1280u*720u,1280,metadata,40,metadata,0,metadata,0,
    metadata,0,0,1,base,0,base,0,base,0};
 for(unsigned n=0;n<20;n++)out->registers[n]=(struct qc5_mdns_pair){offsets[n],values[n]};
 return 1;
}
static int qc5_mdns_reserved(const struct qc5_mdns_layout *p)
{
 struct qc5_mdns_layout expected;
 if(!p||p->base!=QC5_MDNS_BASE||!qc5_mdns_plan(p->base,p->width,p->height,p->mode,p->memopt,&expected)||
    p->allocated!=expected.allocated||p->used!=expected.used)return 0;
 for(unsigned n=0;n<20;n++)if(p->registers[n].offset!=expected.registers[n].offset||p->registers[n].value!=expected.registers[n].value)return 0;
 return 1;
}
static int qc5_mdns_owns(const struct qc5_mdns_layout *p,uint32_t physical)
{return qc5_mdns_reserved(p)&&physical>=p->base&&physical-p->base<p->allocated;}
#endif

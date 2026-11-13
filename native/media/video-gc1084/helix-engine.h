/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_HELIX_ENGINE_H
#define QC5_HELIX_ENGINE_H
#include "helix-plan.h"
#define HELIX_ERRORS ((1u<<2)|(1u<<7)|(1u<<10)|(1u<<20))
struct helix_io {
    void *opaque;
    uint32_t (*read)(void *,unsigned int,unsigned int);
    void (*write)(void *,unsigned int,unsigned int,uint32_t);
    void (*pause)(void *,unsigned int);
};
static const uint32_t helix_failure_registers[]={0x34,0x84,0x70,0x74,0x7c,0x90038,0x40004,0x60000,0x90000,0x30000,0x50000,0x80000};
struct helix_result {uint32_t status,polls,bytes,reset_mask,fault_register,fault_value;uint32_t before_reset[12];};
/* Block 0=CPM, 1=Helix. Do not restore a reset/stop state someone else owns. */
static int helix_reset(const struct helix_io *io,struct helix_result *r)
{
    uint32_t old=io->read(io->opaque,0,0xc4);
    if(old&0xc0000000u){r->fault_register=0x100000c4;r->fault_value=old;return 0;}
    io->write(io->opaque,0,0xc4,old|(1u<<30));
    unsigned int n;
    for(n=0;n<25000;++n){if(io->read(io->opaque,0,0xc4)&(1u<<29))break;io->pause(io->opaque,20);}
    if(n==25000){io->write(io->opaque,0,0xc4,old);r->fault_register=0x100000c4;r->fault_value=old|(1u<<30);return 0;}
    io->write(io->opaque,0,0xc4,old|(1u<<31));
    io->write(io->opaque,0,0xc4,old);
    uint32_t now=io->read(io->opaque,0,0xc4);
    if((now&~(1u<<29))!=(old&~(1u<<29))){r->fault_register=0x100000c4;r->fault_value=now;return 0;}
    return 1;
}
static int helix_encode(const struct helix_io *io,const struct qc5_helix_plan *p,
                         const uint32_t *commands,struct helix_result *r)
{
    if(!io||!io->read||!io->write||!io->pause||!r||!qc5_helix_commands_valid(commands,p))return 0;
    if(!helix_reset(io,r))return 0;r->reset_mask=1;
    if(io->read(io->opaque,1,0x34)&(HELIX_ERRORS|1u)){
        r->fault_register=0x13100034;r->fault_value=io->read(io->opaque,1,0x34);return 0;}
    /* Same IRQ request bits as the reference driver; the SoC IRQ stays masked.
     * TLB is off, physical command/input/output addresses only. */
    io->write(io->opaque,1,0,(1u<<29)|(1u<<28)|(1u<<20)|(1u<<17)|(1u<<16)|(1u<<9));
    io->write(io->opaque,1,0x84,p->layout.region[H_DESCRIPTOR].start|1u);
    for(r->polls=1;r->polls<=2000;++r->polls){
        r->status=io->read(io->opaque,1,0x34);
        if(r->status&(HELIX_ERRORS|1u))break;
        io->pause(io->opaque,1000);
    }
    if((r->status&1u)&&!(r->status&HELIX_ERRORS))r->bytes=io->read(io->opaque,1,0x90038);
    int ok=r->polls<=2000&&(r->status&1u)&&!(r->status&HELIX_ERRORS)&&
        r->bytes>0&&r->bytes<=QC5_HELIX_MAX_CABAC;
    if(!ok)for(unsigned int n=0;n<12;++n)r->before_reset[n]=io->read(io->opaque,1,helix_failure_registers[n]);
    /* Reset even on timeout/error so CPU never reads a live DMA output. */
    if(helix_reset(io,r))r->reset_mask|=2;else ok=0;
    if(!ok&&!r->fault_register){r->fault_register=0x13100034;r->fault_value=r->status;}
    return ok;
}
#endif

/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_CCM_CAPTURE_H
#define QC5_CCM_CAPTURE_H
#include <string.h>
#include "ccm-algorithm.h"
#include "gc1084-ccm.private.h"
#ifdef QC5_IMAGE_CONTINUOUS
#include "image-continuous.h"
#endif
struct qc5_ccm_io {void *opaque;uint32_t (*read)(void *,uint32_t);int (*write)(void *,uint32_t,uint32_t);};
struct qc5_ccm_capture {
    const struct qc5_ccm_profile *profile;struct qc5_ccm_io io;
    uint32_t expected[5],ct,ev,ready,failed,fault_reg,fault_value,samples,updates,read_mask,read_hash;
};
static uint32_t qc5_ccm_note_meter(const struct qc5_ccm_capture *c)
{
 if(!c||c->updates>c->samples)return 0;
#ifdef QC5_IMAGE_CONTINUOUS
 return (qc5_image_note_count(c->samples)<<24)|(qc5_image_note_count(c->updates)<<16)|
  ((c->samples>248||c->updates>248)?512u:0u);
#else
 return (c->samples<<24)|(c->updates<<16);
#endif
}
static int qc5_ccm_fail(struct qc5_ccm_capture *c,uint32_t off,uint32_t value)
{c->failed=1;c->fault_reg=off;c->fault_value=value;return 0;}
static int qc5_ccm_prepare(struct qc5_ccm_capture *c,const struct qc5_ccm_io *io,unsigned bank)
{
    if(!c)return 0;memset(c,0,sizeof(*c));
    if(!io||!io->read||!io->write||bank>1)return 0;c->io=*io;
    c->profile=&qc5_ccm_profiles[bank];if(!qc5_ccm_profile_valid(c->profile))return qc5_ccm_fail(c,0x76000,bank);
    for(unsigned n=0;n<5;++n){uint32_t value=io->read(io->opaque,0x5004+4*n);
        if(value& (n<4?0xc000c000u:0xffffc000u))return qc5_ccm_fail(c,0x5004+4*n,value);
        c->expected[n]=value;}
    c->ready=1;return 1;
}
static int qc5_ccm_tick(struct qc5_ccm_capture *c,uint32_t ct,uint32_t ev)
{
    if(!c||!c->ready||c->failed)return 0;
#ifndef QC5_IMAGE_CONTINUOUS
    if(c->samples>=32)return 0;
#endif
    struct qc5_ccm_result result;
    if(!qc5_ccm_calculate(c->profile,ct,ev,&result))return qc5_ccm_fail(c,0x76004,ct);
    if(memcmp(result.registers,c->expected,sizeof(c->expected))){
        for(unsigned n=0;n<5;++n)
            if(!c->io.write(c->io.opaque,0x5000,1)||!c->io.write(c->io.opaque,0x5004+4*n,result.registers[n]))
                return qc5_ccm_fail(c,0x5004+4*n,result.registers[n]);
        memcpy(c->expected,result.registers,sizeof(c->expected));
#ifdef QC5_IMAGE_CONTINUOUS
        c->updates=qc5_image_inc(c->updates);
#else
        ++c->updates;
#endif
    }
#ifdef QC5_IMAGE_CONTINUOUS
    c->samples=qc5_image_inc(c->samples);
#else
    ++c->samples;
#endif
    c->ct=ct;c->ev=ev;return 1;
}
static int qc5_ccm_finish(struct qc5_ccm_capture *c)
{
    if(!c||!c->ready||c->failed||c->samples<16||c->updates>c->samples)return 0;
    c->read_mask=0;c->read_hash=0;uint32_t hash=2166136261u;
    for(unsigned n=0;n<5;++n){uint32_t off=0x5004+4*n,value=c->io.read(c->io.opaque,off);
        if(value!=c->expected[n])return qc5_ccm_fail(c,off,value);
        c->read_mask|=1u<<n;hash=qc5_ccm_reg_hash(hash,off,value);}
    c->read_hash=hash;return 1;
}
#endif

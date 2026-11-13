/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_HELIX_RUNTIME_H
#define QC5_HELIX_RUNTIME_H
#include "helix-engine.h"
#include "helix-record.h"
#include "helix-guards.h"
struct hw_io {volatile uint32_t *clock,*vpu;};
extern unsigned int qc5_helix_cpu_request(void);
static uint32_t hw_read(void *p,unsigned int b,unsigned int r){struct hw_io *io=p;return (b?io->vpu:io->clock)[r/4];}
static void hw_write(void *p,unsigned int b,unsigned int r,uint32_t v){struct hw_io *io=p;mmio(b?io->vpu:io->clock,r,v);}
static void hw_pause(void *p,unsigned int us){(void)p;delay_us(us);}
static int prepare_helix_cpu(volatile uint8_t *ram,struct qc5_helix_record *evidence)
{
    uint32_t *m=evidence->meta;
    unsigned int setup_status=qc5_helix_cpu_request();
    const volatile uint32_t *cp0=(const volatile uint32_t *)(ram+(QC5_HELIX_CP0_LOG-QC5_CAPTURE_RAM_BASE));
    m[H_CP0_BEFORE]=cp0[1];m[H_CP0_AFTER]=cp0[3];m[H_CP0_REQUESTED]=cp0[2];
    for(unsigned int n=0;n<4;++n)memcpy(evidence->prefix+n*4,(const void *)(cp0+n),4);
    if(cp0[0]!=0x37335043u){m[H_FAULT_REG]=QC5_HELIX_CP0_LOG;m[H_FAULT_VALUE]=cp0[0];return 0;}
    if(setup_status||cp0[2]!=((cp0[1]|0x340u)&0x3ffu)){
        m[H_FAULT_REG]=0xc0160007u;m[H_FAULT_VALUE]=cp0[3];return 0;
    }
    return 1;
}
static int cpu_self_test(void)
{
    int fd=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);if(fd<0)return CAPTURE_MAP;
    volatile uint8_t *ram=map_phys(fd,QC5_CAPTURE_RAM_BASE,QC5_CAPTURE_RAM_END-QC5_CAPTURE_RAM_BASE);
    close(fd);if(ram==MAP_FAILED)return CAPTURE_MAP;
    struct qc5_preview record={0};struct qc5_helix_record proof={0};
    proof.meta[H_MAGIC]=QC5_H264_MAGIC;proof.meta[H_VERSION]=2;proof.meta[H_STAGE]=2;
    int ok=prepare_helix_cpu(ram,&proof);
    memcpy(record.pixels,&proof,sizeof(proof));publish_record(ram,&record,CAPTURE_FRAME);
    printf("QC5_CPU_SELFTEST %d\n",ok);
    return CAPTURE_FRAME; /* Partial CPU evidence only, never a captured frame. */
}
#endif

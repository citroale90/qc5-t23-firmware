/* SPDX-License-Identifier: GPL-2.0-only
 * T23 native 32-bit controller protocol; card control and data READ only.
 * No interrupts, PDMA, flash, GPIO, PLL changes, erase or data writes.
 * Reset/PIO reference: jzmmc_v12, Copyright (C) 2012 Ingenic Semiconductor
 * Co., Ltd., written by Large Dipper <ykli@ingenic.com> (GPL-2.0-only).
 */
#ifndef QC5_MSC_RO_PIO_H
#define QC5_MSC_RO_PIO_H
struct qmr_io {
    void *opaque;
    unsigned (*read)(void *,unsigned);
    void (*write)(void *,unsigned,unsigned);
    unsigned long long (*now_us)(void *);
    void (*pause)(void *);
};
enum {QMR_OK=0,QMR_TIMEOUT=-1,QMR_IO=-2,QMR_DENIED=-3};
static int qmr_allowed(unsigned cmd)
{
    switch(cmd){case 0:case 2:case 3:case 6:case 7:case 8:case 9:case 10:
    case 12:case 13:case 16:case 17:case 18:case 23:case 41:case 42:case 51:case 55:case 58:case 59:return 1;}
    return 0;
}
/* Linux requires a full 4 KiB page per block request. Keep the hardware
 * protocol to single-block CMD17 and use the same 512-byte staging buffer. */
static int qmr_chunk_plan(unsigned cmd,unsigned arg,unsigned blksz,unsigned blocks,int blockaddr,unsigned index,unsigned *opcode,unsigned *address,unsigned *offset)
{
    if(!qmr_allowed(cmd)||!blksz||blksz>512||!blocks||blocks>8||index>=blocks)return QMR_DENIED;
    if(blocks>1&&((cmd!=17&&cmd!=18)||blksz!=512))return QMR_DENIED;
    unsigned stride=blockaddr?1:512;
    if(cmd==17||cmd==18){
        if(!blockaddr&&(arg&511))return QMR_DENIED;
        if(arg>~0u-(blocks-1)*stride)return QMR_DENIED;
    }else stride=0;
    *opcode=cmd==18?17:cmd;*address=arg+index*stride;*offset=index*blksz;return 0;
}
static int qmr_wait(struct qmr_io *io,unsigned off,unsigned mask,int set,unsigned timeout_us,int errors)
{
    unsigned long long start=io->now_us(io->opaque);
    for(unsigned tries=0;tries<1000000;++tries){
        unsigned status=io->read(io->opaque,4);
        if(errors&&(status&0x3f))return QMR_IO;
        if(!!(io->read(io->opaque,off)&mask)==!!set)return QMR_OK;
        if(io->now_us(io->opaque)-start>=timeout_us)return QMR_TIMEOUT;
        io->pause(io->opaque);
    }
    return QMR_TIMEOUT;
}
static int qmr_reset(struct qmr_io *io)
{
    unsigned divider=io->read(io->opaque,8);
    io->write(io->opaque,0,8);
    io->write(io->opaque,0,io->read(io->opaque,0)&~8u);
    int status=qmr_wait(io,4,1u<<15,0,10000,0);
    io->write(io->opaque,0x24,~0u);io->write(io->opaque,0x28,~0u);
    io->write(io->opaque,0x44,0);io->write(io->opaque,0x40,0);
    io->write(io->opaque,8,divider);
    io->write(io->opaque,0x10,255);io->write(io->opaque,0x14,0xffffff);
    return status;
}
static int qmr_command(struct qmr_io *io,unsigned cmd,unsigned arg,unsigned format,unsigned bytes,unsigned resp[4])
{
    if(!qmr_allowed(cmd)||bytes>512||format&~(128u|64u|7u)||(format&7)>3)return QMR_DENIED;
    io->write(io->opaque,0x24,~0u);io->write(io->opaque,0x28,~0u);io->write(io->opaque,0x44,0);
    io->write(io->opaque,0x18,bytes);io->write(io->opaque,0x1c,bytes?1:0);
    io->write(io->opaque,0x10,255);io->write(io->opaque,0x14,0xffffff);
    io->write(io->opaque,0x2c,cmd);io->write(io->opaque,0x30,arg);
    io->write(io->opaque,0x0c,format|(bytes?8u:0));io->write(io->opaque,0,6);
    int status=qmr_wait(io,0x28,4,1,500000,1);if(status)return status;
    if((format&7)==2){
        unsigned value=io->read(io->opaque,0x34)&65535u;
        for(unsigned i=0;i<4;++i){resp[i]=value<<24;value=io->read(io->opaque,0x34)&65535u;resp[i]|=value<<8;value=io->read(io->opaque,0x34)&65535u;resp[i]|=value>>8;}
    }else if(format&7){
        unsigned a=io->read(io->opaque,0x34)&65535u,b=io->read(io->opaque,0x34)&65535u,c=io->read(io->opaque,0x34)&65535u;
        resp[0]=(a<<24)|(b<<8)|(c&255);
    }
    io->write(io->opaque,0x28,4);return QMR_OK;
}
static int qmr_read(struct qmr_io *io,void *output,unsigned bytes)
{
    unsigned char *p=output;if(!bytes||bytes>512||!output)return QMR_DENIED;
    unsigned long long start=io->now_us(io->opaque);
    for(unsigned n=0;n<bytes;){
        unsigned long long elapsed=io->now_us(io->opaque)-start;
        if(elapsed>=1000000)return QMR_TIMEOUT;
        unsigned left=(unsigned)(1000000-elapsed);
        int status=qmr_wait(io,4,64,0,left<500000?left:500000,1);if(status)return status;
        unsigned word=io->read(io->opaque,0x38);
        for(unsigned b=0;b<4&&n<bytes;++b,++n)p[n]=(unsigned char)(word>>(8*b));
    }
    unsigned long long elapsed=io->now_us(io->opaque)-start;
    if(elapsed>=1000000)return QMR_TIMEOUT;
    unsigned left=(unsigned)(1000000-elapsed);
    int status=qmr_wait(io,0x28,1,1,left<500000?left:500000,1);
    if(!status)io->write(io->opaque,0x28,1);
    return status;
}
#endif

/* SPDX-License-Identifier: GPL-2.0-only
 * T23 capture-only diagnostic, derived from the pinned Ingenic GPL SDK.
 * No boot/flash/network operations. Only an exact 16 kHz S16 mono transfer.
 */
#ifndef QC5_AUDIO_PLAN_H
#define QC5_AUDIO_PLAN_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "audio-layout.h"
/* Used only after the video/ISP writers and all video guards have stopped.
 * This tail was unused, remains inside the existing 9 MiB reservation and
 * does not overlap statistics, encoded data, CP0 or the SD preview record.
 */
#define QA_CH (7u*32u)
#define QA_GUARD 0xa5u
enum qa_region {QA_CPM,QA_AIC,QA_CODEC,QA_PDMA,QA_REGIONS};
enum qa_fault {QA_OK,QA_ARGUMENT,QA_PARENT,QA_MMIO,QA_BUSY,QA_CLOCK,
    QA_RESET,QA_DESCRIPTOR_ERROR,QA_DMA_ERROR,QA_TIMEOUT,QA_GUARD_ERROR,QA_SILENT,QA_TIMING,QA_OVERRUN};
struct qa_io {
    void *opaque;
    uint32_t (*read)(void *,unsigned,uint32_t);
    int (*write)(void *,unsigned,uint32_t,uint32_t);
    uint64_t (*time_us)(void *);
    void (*delay_us)(void *,uint32_t);
    volatile uint8_t *ram;
};
struct qa_result {
    uint32_t magic,version,status,stage,rate,samples,bytes,hash;
    uint32_t elapsed_us,polls,clock,clock_read,dcs,remaining,guards,stopped;
    uint32_t minimum,maximum,nonzero,transitions,mean_abs,clips,fault_region,fault_offset;
};
struct qa_state {struct qa_io io;struct qa_result note;unsigned fault,owned;uint64_t capture_first,capture_last;
    uint32_t dma_bytes,captured_bytes,fifo_status,watermark_stopped;};
_Static_assert(sizeof(struct qa_result)==96,"Bounded audio note");
_Static_assert(QA_BYTES%16==0&&QA_PCM+QA_BYTES+512<=QA_END,"PCM and tail guard fit");
static uint32_t qa_read(struct qa_state *s,unsigned region,uint32_t off)
{return s->io.read(s->io.opaque,region,off);}
static int qa_fail(struct qa_state *s,unsigned fault,unsigned region,uint32_t off)
{if(!s->fault){s->fault=fault;s->note.status=fault;s->note.fault_region=region;s->note.fault_offset=off;}return 0;}
static int qa_write(struct qa_state *s,unsigned region,uint32_t off,uint32_t val)
{
    if(s->fault)return 0;
    if(!s->io.write(s->io.opaque,region,off,val))return qa_fail(s,QA_MMIO,region,off);
    return 1;
}
static int qa_update(struct qa_state *s,unsigned region,uint32_t off,uint32_t mask,uint32_t val)
{return !(val&~mask)&&qa_write(s,region,off,(qa_read(s,region,off)&~mask)|val);}
static void qa_pause(struct qa_state *s,unsigned us)
{if(!s->fault)s->io.delay_us(s->io.opaque,us);}
static uint32_t qa_pll(uint32_t v)
{
    unsigned m=v>>20,n=v>>14&63,od1=v>>11&7,od0=v>>8&7;
    if((v&9)!=9||!m||!n||!od1||!od0)return 0;
    uint64_t rate=UINT64_C(24000000)*m/(n*od1*od0);
    return rate<=2400000000u?(uint32_t)rate:0;
}
static uint32_t qa_gcd(uint32_t a,uint32_t b){while(b){uint32_t r=a%b;a=b;b=r;}return a;}
static int qa_clock_plan(uint32_t cpccr,uint32_t apll,uint32_t mpll,uint32_t speaker,uint32_t *out)
{
    if(!out)return 0;uint32_t parent=0;
    unsigned src=speaker>>30;
    if(src==0){unsigned sclka=cpccr>>30;parent=sclka==2?qa_pll(apll):sclka==1?24000000u:0;}
    else if(src==1)parent=qa_pll(mpll);
    if(!parent)return 0;
    uint32_t target=QA_RATE*256u,gcd=qa_gcd(parent,target),m=target/gcd,n=parent/gcd;
    if(!m||m>511||!n||n>0xfffff)return 0;
    *out=(speaker&0xc0000000u)|0x20000000u|(m<<20)|n;
    return (uint64_t)parent*m==(uint64_t)target*n;
}
static int qa_codec_set(struct qa_state *s,unsigned reg,int first,int last,int val)
{
    if(reg>0x130||(reg&3)||first<0||last<first||last>7||val<0||((unsigned)val>>(last-first+1)))
        return qa_fail(s,QA_ARGUMENT,QA_CODEC,reg),-1;
    uint32_t mask=((1u<<(last-first+1))-1u)<<first;
    if(!qa_update(s,QA_CODEC,reg,mask,(unsigned)val<<first))return -1;
    /* Compare the requested field. Other bits can be changed by the codec. */
    if((qa_read(s,QA_CODEC,reg)&mask)!=((unsigned)val<<first))return qa_fail(s,QA_MMIO,QA_CODEC,reg),-1;
    return 0;
}
static int qa_wait(struct qa_state *s,unsigned r,uint32_t off,uint32_t mask,uint32_t value,unsigned ticks,unsigned fault)
{
    for(unsigned n=0;n<ticks&&!s->fault;n++){
        if((qa_read(s,r,off)&mask)==value)return 1;qa_pause(s,1000);
    }
    return qa_fail(s,fault,r,off);
}
static int qa_guards(struct qa_state *s)
{
    for(unsigned n=32;n<4096;n++)if(s->io.ram[n]!=QA_GUARD)return 0;
    for(unsigned n=4096+QA_DMA_BYTES;n<QA_SIZE;n++)if(s->io.ram[n]!=QA_GUARD)return 0;
    return 1;
}
static void qa_analyze(struct qa_state *s)
{
    uint32_t hash=2166136261u,nz=0,changes=0,clips=0;int min=32767,max=-32768,prev=0;uint64_t absolute=0;
    for(unsigned n=0;n<QA_SAMPLES;n++){
        unsigned lo=s->io.ram[4096+2*n],hi=s->io.ram[4097+2*n];
        hash=(hash^lo)*16777619u;hash=(hash^hi)*16777619u;
        int v=(int)(int16_t)(lo|hi<<8);if(v<min)min=v;if(v>max)max=v;
        nz+=v!=0;changes+=n&&prev!=v;clips+=v<=-32760||v>=32760;prev=v;absolute+=v<0?-v:v;
    }
    s->note.hash=hash;s->note.minimum=(uint32_t)(int32_t)min;s->note.maximum=(uint32_t)(int32_t)max;
    s->note.nonzero=nz;s->note.transitions=changes;s->note.mean_abs=(uint32_t)(absolute/QA_SAMPLES);s->note.clips=clips;
    if(!nz||!changes)qa_fail(s,QA_SILENT,QA_AIC,0x34);
}
static void qa_descriptor(struct qa_state *s)
{
    /* Eight words, as used by the original QC5, finite transfer, no link or
     * peripheral interrupt. 16-byte units, 16-bit source/destination port.
     */
    const uint32_t words[]={0x0040a300u,0x10020034u,QA_PCM,QA_DMA_BYTES/16u,0,7,0,0};
    for(unsigned n=0;n<8;n++)for(unsigned b=0;b<4;b++)s->io.ram[n*4+b]=(uint8_t)(words[n]>>(8*b));
}
static int qa_descriptor_valid(struct qa_state *s)
{
    const uint32_t words[]={0x0040a300u,0x10020034u,QA_PCM,QA_DMA_BYTES/16u,0,7,0,0};
    for(unsigned n=0;n<8;n++){uint32_t v=0;for(unsigned b=0;b<4;b++)v|=(uint32_t)s->io.ram[n*4+b]<<(8*b);if(v!=words[n])return 0;}
    return 1;
}
#include "codec-capture.generated.h"
/* Cleanup must run even after a failed write/readback. No codec/DAC enable. */
static void qa_stop(struct qa_state *s)
{
    if(!s->owned)return;
    for(unsigned n=0;n<3;n++){
        int a=s->io.write(s->io.opaque,QA_AIC,4,qa_read(s,QA_AIC,4)&~0x8001u);
        int b=1;for(unsigned ch=5;ch<=7;ch++)b&=s->io.write(s->io.opaque,QA_PDMA,ch*32+0x10,0);
        if(!a||!b)qa_fail(s,QA_MMIO,!a?QA_AIC:QA_PDMA,!a?4:QA_CH+0x10);
        s->note.stopped=a&&b&&!(qa_read(s,QA_AIC,4)&0x8001u);
        for(unsigned ch=5;ch<=7;ch++)s->note.stopped&=!(qa_read(s,QA_PDMA,ch*32+0x10)&1u);
        if(s->note.stopped)break;
    }
}
static int qa_capture(struct qa_state *s)
{
    if(!s||!s->io.read||!s->io.write||!s->io.delay_us||!s->io.time_us||!s->io.ram)return 0;
    memset(&s->note,0,sizeof s->note);s->fault=0;s->owned=0;s->capture_first=s->capture_last=0;
    s->dma_bytes=QA_DMA_BYTES;s->captured_bytes=s->fifo_status=s->watermark_stopped=0;
    s->note.magic=0x37335241u;s->note.version=1;s->note.rate=QA_RATE;
    s->note.samples=QA_SAMPLES;s->note.bytes=QA_BYTES;s->note.stage=1;
    if(!qa_clock_plan(qa_read(s,QA_CPM,0),qa_read(s,QA_CPM,0x10),qa_read(s,QA_CPM,0x14),
                      qa_read(s,QA_CPM,0x70),&s->note.clock))return qa_fail(s,QA_PARENT,QA_CPM,0x70);
    /* Warm handoff: nobody else may own PDMA. Check only after opening its
     * gate. The new kernel diagnostic has CONFIG_DMADEVICES disabled.
     */
    if(!qa_update(s,QA_CPM,0x20,(1u<<11)|(1u<<21),0))return 0;
    for(unsigned n=0;n<32;n++)if((n<5||n>7)&&(qa_read(s,QA_PDMA,n*32+0x10)&1u))
        return qa_fail(s,QA_BUSY,QA_PDMA,n*32+0x10);
    for(unsigned n=5;n<=7;n++)if((qa_read(s,QA_PDMA,n*32+0x10)&1u)&&qa_read(s,QA_PDMA,n*32+0xc)!=n)
        return qa_fail(s,QA_BUSY,QA_PDMA,n*32+0xc);
    s->owned=1;
    /* Stop the inherited microphone and speaker channels before touching the
     * AIC or codec; channel 6 is left stopped for this capture-only test.
     */
    qa_write(s,QA_PDMA,5*32+0x10,0);qa_write(s,QA_PDMA,6*32+0x10,0);qa_write(s,QA_PDMA,QA_CH+0x10,0);
    qa_write(s,QA_AIC,4,0);qa_update(s,QA_AIC,0,1,0);
    qa_write(s,QA_CPM,0x70,s->note.clock);qa_write(s,QA_CPM,0x84,s->note.clock);
    s->note.clock_read=qa_read(s,QA_CPM,0x84);
    if((qa_read(s,QA_CPM,0x70)&0x3fffffffu)!=(s->note.clock&0x3fffffffu)||
       (s->note.clock_read&0x3fffffffu)!=(s->note.clock&0x3fffffffu))qa_fail(s,QA_CLOCK,QA_CPM,0x84);
    qa_write(s,QA_AIC,0,6);qa_write(s,QA_AIC,0,14);
    if(!qa_wait(s,QA_AIC,0,(3u<<13)|8u,0,50,QA_RESET))goto done;
    qa_codec_disable_playback(s);qa_codec_init(s);qa_codec_set_sample_rate(s,QA_RATE);
    qa_codec_enable_record(s);qa_codec_record_set_again(s,0,29);
    qa_codec_set(s,8,5,6,0);qa_codec_set(s,8,0,0,1);
    if(s->fault)goto done;
    s->note.stage=2;
    qa_write(s,QA_AIC,0,0x08100071u); /* Internal codec master, AIC slave. */
    qa_write(s,QA_AIC,0x10,0x00020000u); /* Native right-first I2S. */
    qa_write(s,QA_AIC,0x30,0x00040004u);
    qa_write(s,QA_AIC,4,0x00011080u); /* 16-bit, right mono, flush RX. */
    if(!qa_wait(s,QA_AIC,4,0x80,0,20,QA_RESET))goto done;
    qa_write(s,QA_AIC,0x14,~0x60u); /* Native clear-by-zero over/underrun bits. */
    for(unsigned n=0;n<QA_SIZE;n++)s->io.ram[n]=QA_GUARD;
    qa_descriptor(s);
    if(!qa_descriptor_valid(s)||!qa_guards(s)){qa_fail(s,QA_DESCRIPTOR_ERROR,QA_PDMA,0);goto done;}
    s->note.stage=3;
    qa_write(s,QA_PDMA,0x1000,0x003f0001u); /* Exact QC5 master mode. */
    qa_write(s,QA_PDMA,QA_CH+0xc,7);
    qa_write(s,QA_PDMA,QA_CH+8,QA_DMA_BYTES/16u);
    qa_write(s,QA_PDMA,QA_CH+0x18,QA_DESCRIPTOR);
    qa_write(s,QA_PDMA,0x100c,1u<<7);
    qa_write(s,QA_PDMA,QA_CH+0x10,0x40000001u);
    uint64_t first=s->io.time_us(s->io.opaque);
    s->capture_first=first;
    qa_write(s,QA_AIC,4,0x00019001u);
    if(!first){qa_fail(s,QA_TIMING,QA_AIC,4);goto done;}
    s->note.stage=4;
    for(unsigned n=0;n<2600&&!s->fault;n++){
        s->note.polls=n+1;s->note.dcs=qa_read(s,QA_PDMA,QA_CH+0x10);
        s->note.remaining=qa_read(s,QA_PDMA,QA_CH+8);
#ifdef QC5_AUDIO_TAIL
        /* The DMA keeps draining into the private margin until RX is stopped.
         * Never accept an exhausted margin, a DMA error or a FIFO overrun.
         * Stopping RX before DMA is essential: it closes the finite-DMA race.
         */
        s->fifo_status=qa_read(s,QA_AIC,0x14);
        if(s->fifo_status&0x40u){qa_fail(s,QA_OVERRUN,QA_AIC,0x14);break;}
        if(s->note.remaining>QA_DMA_BYTES/16u||s->note.dcs&0x1cu){qa_fail(s,QA_DMA_ERROR,QA_PDMA,QA_CH+0x10);break;}
        if(s->note.remaining<=QA_RX_MARGIN/16u){
            qa_stop(s);
            s->note.remaining=qa_read(s,QA_PDMA,QA_CH+8);
            s->fifo_status=qa_read(s,QA_AIC,0x14);
            uint64_t last=s->io.time_us(s->io.opaque);
            if(!last||last<first||last-first>2032000u||last-first<1800000u)
                qa_fail(s,QA_TIMING,QA_PDMA,QA_CH+0x10);
            else{s->note.elapsed_us=(uint32_t)(last-first);s->capture_last=last;}
            if(!s->note.remaining||s->note.remaining>QA_RX_MARGIN/16u)
                qa_fail(s,QA_DMA_ERROR,QA_PDMA,QA_CH+8);
            else s->captured_bytes=QA_DMA_BYTES-s->note.remaining*16u;
            if(s->fifo_status&0x40u)qa_fail(s,QA_OVERRUN,QA_AIC,0x14);
            s->watermark_stopped=!s->fault&&s->note.stopped;
            break;
        }
#else
        if(qa_read(s,QA_AIC,0x14)&0x40u){qa_fail(s,QA_OVERRUN,QA_AIC,0x14);break;}
#endif
        uint64_t now=s->io.time_us(s->io.opaque);
        if(!now||now<first||now-first>2600000){qa_fail(s,QA_TIMING,QA_PDMA,QA_CH+0x10);break;}
        s->note.elapsed_us=(uint32_t)(now-first);
        s->capture_last=now;
        if(s->note.dcs&0x14){qa_fail(s,QA_DMA_ERROR,QA_PDMA,QA_CH+0x10);break;}
        if(s->note.dcs&8u){
            if(s->note.remaining)qa_fail(s,QA_DMA_ERROR,QA_PDMA,QA_CH+8);
            if(s->note.elapsed_us<1800000||s->note.elapsed_us>2500000)qa_fail(s,QA_TIMING,QA_PDMA,QA_CH+0x10);
            break;
        }
        qa_pause(s,1000);
    }
#ifdef QC5_AUDIO_TAIL
    if(!s->fault&&!s->watermark_stopped)qa_fail(s,QA_TIMEOUT,QA_PDMA,QA_CH+0x10);
#else
    if(!s->fault&&!(s->note.dcs&8u))qa_fail(s,QA_TIMEOUT,QA_PDMA,QA_CH+0x10);
#endif
done:
    qa_stop(s);
    if(!s->note.stopped)qa_fail(s,QA_DMA_ERROR,QA_PDMA,QA_CH+0x10);
    if(s->note.stage>=3){s->note.guards=qa_guards(s);if(!s->note.guards)qa_fail(s,QA_GUARD_ERROR,QA_PDMA,0);}
    if(!s->fault){qa_analyze(s);if(!s->fault)s->note.stage=5;}
    return !s->fault;
}
#endif

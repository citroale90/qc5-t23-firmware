/* SPDX-License-Identifier: GPL-3.0-only
 * Four bounded AWB statistics pages inside the existing reservation.
 * No new video buffers or heap allocation. Only the audited AWB writer runs.
 */
#ifndef QC5_AWB_CAPTURE_H
#define QC5_AWB_CAPTURE_H
#include "qc5-frame-layout.h"
#include "awb-algorithm.h"
#include "awb-note.h"
#include "gc1084-awb.private.h"
struct qc5_awb_reg {uint32_t offset,value;};
#include "gc1084-awb-stats.private.h"
#include "exposure-ev.h"
#ifdef QC5_IMAGE_CONTINUOUS
#include "image-continuous.h"
#endif
#define QC5_AWB_STATS_BASE (QC5_STATS_BASE+0xc000u)
#define QC5_AWB_STATS_BYTES 0x4000u
#define QC5_AWB_READ_MASK ((1u<<18)-1u)
#define QC5_AWB_MAX_SAMPLES 32u
#define QC5_AWB_MAX_STATS_FRAMES 4096u
static uint32_t qc5_awb_ev_q10(unsigned lines,unsigned gain)
{
    return qc5_exposure_ev_q10(lines,gain);
}
_Static_assert(!(QC5_AWB_STATS_BASE&4095u),"AWB banks are page aligned");
_Static_assert(QC5_AWB_STATS_BASE>=QC5_STATS_BASE&&QC5_AWB_STATS_BASE+QC5_AWB_STATS_BYTES<=QC5_STATS_END,"AWB ring stays in its reservation");
struct qc5_awb_io {
    void *opaque;uint32_t (*read)(void *,uint32_t);int (*write)(void *,uint32_t,uint32_t);
    int (*time_us)(void *,uint64_t *);
};
#ifdef QC5_CAPTURE_SERVICE
static uint32_t qc5_awb_timing_inc(uint32_t value){return value==UINT32_MAX?value:value+1;}
#endif
struct qc5_awb_capture {
    struct qc5_awb_state algorithm;struct qc5_awb_snapshot snapshot;
    struct qc5_awb_io io;volatile uint8_t *ram;
    uint32_t expected[4],bank,ready,running,failed,fault_reg,fault_value;
    uint32_t frames,samples,updates,drops,last_bank,sample_bank,stats_hash,max_us;
    uint32_t populated,read_mask,stop_mask,started,ct,configured,latch_wait;
#ifdef QC5_IMAGE_CONTINUOUS
    uint32_t sample_phase,sample_generation;
#endif
#ifdef QC5_CAPTURE_SERVICE
    uint32_t copy_max_us,copy_drops,work_overruns,quantized_work;
    uint64_t last_start_us,last_copy_us,last_end_us;
#endif
#if defined(QC5_AWB_DARK_SAFE)||defined(QC5_AWB_NO_ZONES_SAFE)
    uint32_t empty_samples;
#endif
};
static int qc5_awb_capture_fail(struct qc5_awb_capture *c,uint32_t reg,uint32_t value)
{c->failed=1;c->fault_reg=reg;c->fault_value=value;return 0;}
static void qc5_awb_capture_drop(struct qc5_awb_capture *c)
{
#ifdef QC5_IMAGE_CONTINUOUS
 c->drops=qc5_image_inc(c->drops);
#else
 ++c->drops;
#endif
}
static uint32_t qc5_awb_pair_hash(uint32_t hash,uint32_t offset,uint32_t value)
{
    for(unsigned n=0;n<8;++n)hash=(hash^(((n<4?offset:value)>>(8*(n%4)))&255u))*16777619u;
    return hash;
}
static int qc5_awb_owns(const struct qc5_awb_capture *c,uint32_t address)
{return c&&c->ready&&address>=QC5_AWB_STATS_BASE&&address-QC5_AWB_STATS_BASE<QC5_AWB_STATS_BYTES;}
static int qc5_awb_read_config(struct qc5_awb_capture *c,int include_thresholds)
{
    c->read_mask=0;
    for(unsigned n=0;n<14;++n){
        if(n>=9&&!include_thresholds)continue;
        uint32_t off=0xb004+4*n,value=c->io.read(c->io.opaque,off);
        if(value!=qc5_awb_config_expected[c->bank][n])return qc5_awb_capture_fail(c,off,value);
        c->read_mask|=1u<<n;
    }
    for(unsigned n=0;n<4;++n){
        uint32_t off=0xb03c+4*n,value=c->io.read(c->io.opaque,off);
        if(value!=QC5_AWB_STATS_BASE+4096*n)return qc5_awb_capture_fail(c,off,value);
        c->read_mask|=1u<<(14+n);
    }
    return c->read_mask==(include_thresholds?QC5_AWB_READ_MASK:(0x1ffu|(15u<<14)));
}
static int qc5_awb_capture_prepare(struct qc5_awb_capture *c,const struct qc5_awb_io *io,
                                    volatile uint8_t *ram,unsigned bank)
{
    if(!c)return 0;memset(c,0,sizeof(*c));
    if(!io||!io->read||!io->write||!io->time_us||!ram||bank>1)return 0;
    c->io=*io;c->ram=ram;c->bank=bank;
    if(!qc5_awb_init(&c->algorithm,&qc5_awb_profiles[bank]))return qc5_awb_capture_fail(c,0x75000,bank);
    const uint32_t idle[]={0x800,0x804,0xd040,0xb000,0xa04c,0xa84c,0xb04c,0x4490,0x5b80,0xb8b8};
    for(unsigned n=0;n<sizeof(idle)/sizeof(idle[0]);++n){uint32_t value=io->read(io->opaque,idle[n]);
        if(value)return qc5_awb_capture_fail(c,idle[n],value);}
    uint32_t hash=2166136261u;
    for(unsigned n=0;n<19;++n){struct qc5_awb_reg r=qc5_awb_config[bank][n];
        if(!((r.offset==0xb000&&r.value==1)||(r.offset>=0xb004&&r.offset<=0xb038&&! (r.offset&3))))
            return qc5_awb_capture_fail(c,r.offset,r.value);
        hash=qc5_awb_pair_hash(hash,r.offset,r.value);
    }
    if(hash!=qc5_awb_config_hashes[bank])return qc5_awb_capture_fail(c,0x75004,hash);
    for(unsigned n=0;n<4;++n){uint32_t off=0xb03c+4*n,value=io->read(io->opaque,off);
        if(value!=QC5_AWB_STATS_BASE+4096*n)return qc5_awb_capture_fail(c,off,value);}
    for(unsigned n=0;n<QC5_AWB_STATS_BYTES;++n)ram[QC5_AWB_STATS_BASE-QC5_CAPTURE_RAM_BASE+n]=0;
    for(unsigned n=0;n<19;++n){struct qc5_awb_reg r=qc5_awb_config[bank][n];
        if(!io->write(io->opaque,r.offset,r.value))return qc5_awb_capture_fail(c,r.offset,r.value);}
    if(!io->write(io->opaque,0xb000,0))return qc5_awb_capture_fail(c,0xb000,0);
    if(io->read(io->opaque,0xb000)||io->read(io->opaque,0xb04c))return qc5_awb_capture_fail(c,0xb000,1);
    const struct qc5_awb_profile *p=&qc5_awb_profiles[bank];
    uint32_t r=t23x_awb_gain_trend(p->trend,p->startup_ct,(p->mf[4]*p->wb_static[0]+512u)>>10,0)|0x04000000u;
    uint32_t b=t23x_awb_gain_trend(p->trend,p->startup_ct,(p->mf[5]*p->wb_static[1]+512u)>>10,1)|0x04000000u;
    c->expected[0]=c->expected[2]=r;c->expected[1]=c->expected[3]=b;c->ct=p->startup_ct;c->ready=1;
    return 1;
}
static int qc5_awb_capture_start(struct qc5_awb_capture *c)
{
    if(!c||!c->ready||c->running||c->failed)return 0;
    if(c->io.read(c->io.opaque,0xd040)!=1)return qc5_awb_capture_fail(c,0xd040,0);
    /* Grid and all four DMA bases are read back BEFORE enabling the writer.
     * Colour/luma thresholds are shadowed until the first frame latch. */
    if(!qc5_awb_read_config(c,0))return 0;
    if(c->io.read(c->io.opaque,0xb000)||c->io.read(c->io.opaque,0xb04c))return qc5_awb_capture_fail(c,0xb04c,1);
    if(!c->io.write(c->io.opaque,0xb000,1)||!c->io.write(c->io.opaque,0xb04c,3))return qc5_awb_capture_fail(c,0xb04c,3);
    uint32_t latch=c->io.read(c->io.opaque,0xb000),writer=c->io.read(c->io.opaque,0xb04c);
    if(latch>1||writer!=3)return qc5_awb_capture_fail(c,0xb04c,writer);
    c->running=c->started=1;return 1;
}
static int qc5_awb_capture_snapshot(struct qc5_awb_capture *c,uint32_t irq,uint32_t ev_q10,int allow_gain)
{
    if(!c||!c->running||c->failed)return 0;
    if(!(irq&(1u<<30)))return 1;
    if(!c->configured){
        ++c->latch_wait;
        for(unsigned n=9;n<14;++n){uint32_t off=0xb004+4*n,value=c->io.read(c->io.opaque,off);
            if(value!=qc5_awb_config_expected[c->bank][n]){
                if(c->latch_wait>=8)return qc5_awb_capture_fail(c,off,value);
                return 1;
            }
        }
        if(!qc5_awb_read_config(c,1))return 0;c->configured=1;
    }
    /* Statistics follow sensor frames, not delivered NV12 frames. A busy
     * encoder or Wi-Fi sender can make those counters diverge. Keep the
     * finite software meter bounded without aborting video at frame 249;
     * tick() still rearms the hardware latch after the sample limit. */
#ifndef QC5_IMAGE_CONTINUOUS
    if(c->samples>=QC5_AWB_MAX_SAMPLES)return 1;
#endif
    uint32_t bank=c->io.read(c->io.opaque,0xb050)&3u;
    if(c->frames&&bank==c->last_bank){qc5_awb_capture_drop(c);return 1;}
    c->last_bank=bank;
#ifdef QC5_IMAGE_CONTINUOUS
    unsigned first=c->frames==0;
    c->frames=qc5_image_inc(c->frames);c->sample_phase=(c->sample_phase+1)&7u;
    if(!first&&c->sample_phase)return 1;
#else
    ++c->frames;
    if(c->frames>QC5_AWB_MAX_STATS_FRAMES)return qc5_awb_capture_fail(c,0xb050,c->frames);
    if(c->frames!=1&&c->frames%8)return 1;
#endif
    uint64_t start,end;if(!c->io.time_us(c->io.opaque,&start))return qc5_awb_capture_fail(c,0x75008,0);
#ifdef QC5_CAPTURE_SERVICE
    c->last_start_us=start;c->last_copy_us=c->last_end_us=0;
#endif
    const volatile uint32_t *words=(const volatile uint32_t *)(c->ram+(QC5_AWB_STATS_BASE-QC5_CAPTURE_RAM_BASE)+bank*4096u);
    uint32_t hash=2166136261u,populated=0;
    for(unsigned n=0;n<225;++n){
        uint32_t w[4];for(unsigned k=0;k<4;++k){w[k]=words[4*n+k];
            for(unsigned b=0;b<4;++b)hash=(hash^((w[k]>>(8*b))&255u))*16777619u;}
        c->snapshot.r[n]=w[0]&0x1fffffu;c->snapshot.g[n]=((w[1]&0x3ffu)<<11)|(w[0]>>21);
        c->snapshot.b[n]=(w[1]&0x7ffffc00u)>>10;c->snapshot.pixels[n]=((w[3]&1u)<<12)|(w[2]>>20);
        populated+=c->snapshot.pixels[n]>0;
    }
    if((c->io.read(c->io.opaque,0xb050)&3u)!=bank||(c->frames==1&&populated<112)){qc5_awb_capture_drop(c);return 1;}
#ifdef QC5_CAPTURE_SERVICE
    /* The DMA pages rotate independently of this userspace task. Validate
     * the copy age BEFORE advancing the algorithm or writing any WB gain.
     * A delayed copy is discarded and tick() rearms the next statistics
     * event. It must not tear down the video producer during Linux startup.
     * The copied snapshot is private CPU state after this point. */
    uint64_t copied;
    if(!c->io.time_us(c->io.opaque,&copied))return qc5_awb_capture_fail(c,0x75018,1);
    c->last_copy_us=copied;
    if(copied<start)return qc5_awb_capture_fail(c,0x75018,2);
    uint64_t copy_us=copied-start;
    if(copy_us>40000){c->copy_drops=qc5_awb_timing_inc(c->copy_drops);qc5_awb_capture_drop(c);return 1;}
    if(copy_us>c->copy_max_us)c->copy_max_us=(uint32_t)copy_us;
#endif
    struct qc5_awb_result result={0};int rc=qc5_awb_run(&c->algorithm,&c->snapshot,ev_q10,&result);
    if(rc)return qc5_awb_capture_fail(c,0x7500c,(uint32_t)rc);
#ifdef QC5_IMAGE_CONTINUOUS
    c->samples=qc5_image_inc(c->samples);
#else
    ++c->samples;
#endif
    c->stats_hash=hash;c->sample_bank=bank;c->ct=result.ct;c->populated|=populated>0;
#ifdef QC5_AWB_NO_ZONES_SAFE
    if(!populated)c->empty_samples=qc5_image_inc(c->empty_samples);
#elif defined(QC5_AWB_DARK_SAFE)
    if(!populated&&hash==QC5_AWB_EMPTY_STATS_HASH)c->empty_samples=qc5_image_inc(c->empty_samples);
#endif
    uint32_t expected[4]={result.r|0x04000000u,result.b|0x04000000u,result.r|0x04000000u,result.b|0x04000000u};
    if(allow_gain&&memcmp(expected,c->expected,sizeof(expected))){
        if(!result.r||result.r>0x3fff||!result.b||result.b>0x3fff)return qc5_awb_capture_fail(c,0x1804,result.r);
        for(unsigned n=0;n<4;++n)
            if(!c->io.write(c->io.opaque,0x1800,1)||!c->io.write(c->io.opaque,0x1804+n*4,expected[n]))
                return qc5_awb_capture_fail(c,0x1804+n*4,expected[n]);
        memcpy(c->expected,expected,sizeof(expected));
#ifdef QC5_IMAGE_CONTINUOUS
        c->updates=qc5_image_inc(c->updates);
#else
        ++c->updates;
#endif
    }
#ifdef QC5_CAPTURE_SERVICE
    if(!c->io.time_us(c->io.opaque,&end))return qc5_awb_capture_fail(c,0x7501c,1);
    c->last_end_us=end;
    if(end<copied)return qc5_awb_capture_fail(c,0x7501c,2);
    /* Processing the immutable CPU snapshot can be preempted. Wall time
     * above one sensor period is a performance observation, not evidence
     * that the snapshot is torn. Equal clock readings are also valid at
     * the clock's resolution; a failed/backward clock still fails closed.
     * The service frame watchdog and independent health owner stay intact. */
    uint64_t elapsed=end-start;
    if(!elapsed)c->quantized_work=qc5_awb_timing_inc(c->quantized_work);
    if(elapsed>40000)c->work_overruns=qc5_awb_timing_inc(c->work_overruns);
    if(elapsed>c->max_us)c->max_us=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
#else
    if(!c->io.time_us(c->io.opaque,&end)||end<=start||end-start>40000)return qc5_awb_capture_fail(c,0x75010,0);
    if(end-start>c->max_us)c->max_us=(uint32_t)(end-start);
#endif
#ifdef QC5_IMAGE_CONTINUOUS
    c->sample_generation^=1u;
#endif
    return 1;
}
static int qc5_awb_capture_tick(struct qc5_awb_capture *c,uint32_t irq,uint32_t ev_q10,int allow_gain)
{
    if(!qc5_awb_capture_snapshot(c,irq,ev_q10,allow_gain))return 0;
    /* Exact native awb_interrupt_static rearms this latch on every done IRQ,
     * including frames deliberately skipped by the bounded software meter. */
    if((irq&(1u<<30))&&!c->io.write(c->io.opaque,0xb000,1))
        return qc5_awb_capture_fail(c,0xb000,1);
    return 1;
}
static int qc5_awb_capture_stop(struct qc5_awb_capture *c)
{
    if(!c||!c->io.write)return 0;c->stop_mask=0;
    if(!c->io.write(c->io.opaque,0xb04c,0)||!c->io.write(c->io.opaque,0xb000,0))return qc5_awb_capture_fail(c,0xb04c,0);
    if(!c->io.read(c->io.opaque,0xb04c))c->stop_mask|=1;
    if(!c->io.read(c->io.opaque,0xb000))c->stop_mask|=2;
    c->running=0;return c->stop_mask==3?1:qc5_awb_capture_fail(c,0xb04c,c->stop_mask);
}
static int qc5_awb_capture_finish(struct qc5_awb_capture *c)
{
    if(!c||!c->ready||c->failed||c->running||c->stop_mask!=3||c->samples<16||!c->updates)return 0;
    if(!c->populated){
#ifdef QC5_AWB_NO_ZONES_SAFE
        if(c->empty_samples!=c->samples||c->ct!=5000)return 0;
#elif defined(QC5_AWB_DARK_SAFE)
        if(c->empty_samples!=c->samples||c->stats_hash!=QC5_AWB_EMPTY_STATS_HASH||c->ct!=5000)return 0;
#else
        return 0;
#endif
    }
    if(!c->configured||!qc5_awb_read_config(c,1))return 0;
    for(unsigned n=0;n<4;++n){uint32_t off=0x1804+n*4,value=c->io.read(c->io.opaque,off);
        if(value!=c->expected[n])return qc5_awb_capture_fail(c,off,value);}
    return 1;
}
static struct qc5_awb_note qc5_awb_capture_note(const struct qc5_awb_capture *c)
{
    unsigned samples=c->samples,updates=c->updates,version=1,saturated=0;
#ifdef QC5_IMAGE_CONTINUOUS
    if(updates>samples)return (struct qc5_awb_note){0};
    version=3;saturated=(samples>248||updates>248)?1u<<24:0;
    samples=qc5_image_note_count(samples);updates=qc5_image_note_count(updates);
#endif
#ifdef QC5_AWB_NO_ZONES_SAFE
    version=5;
    if(!c->populated&&c->samples&&c->empty_samples==c->samples&&c->ct==5000)saturated|=QC5_AWB_NO_ZONES_FLAG;
#elif defined(QC5_AWB_DARK_SAFE)
    version=4;
    if(!c->populated&&c->samples&&c->empty_samples==c->samples&&c->stats_hash==QC5_AWB_EMPTY_STATS_HASH&&c->ct==5000)
        saturated|=QC5_AWB_EMPTY_STATS_FLAG;
#endif
    unsigned ms=c->max_us/1000u+!!(c->max_us%1000u);if(ms>255)ms=255;
    return (struct qc5_awb_note){QC5_AWB_NOTE_MAGIC,
        version|(samples<<8)|(updates<<16)|(ms<<24),
        c->algorithm.profile?c->algorithm.profile->checksum:0,c->ct,c->stats_hash,
        c->read_mask|(c->stop_mask<<18)|(c->sample_bank<<20)|(c->started<<22)|(c->populated<<23)|saturated};
}
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native 720p/QP30 live video, fixed RAM ownership and a single bounded reader.
 */
#define QC5_PREVIEW_VERSION 23u
#define QC5_NV12_WB
#define QC5_NV12_AE
#ifndef QC5_NV12_TARGET
#define QC5_NV12_TARGET 248u
#endif
#define QC5_NV12_INITIAL_GAIN 8u
#define QC5_NV12_ENCODER_HOOK
#define main nv12_capture_main
#include "capture-msca.c"
#undef main
#include "helix-runtime.h"
#include "helix-sequence.h"
#include "helix-live-net.h"
#include "../media-handoff.h"
#include "../media-status.h"
static struct qmi_writer integrated_media={.directory=-1};
#include "helix-live-record.h"
#ifdef QC5_STREAM_RUNTIME
#include "../video-runtime/producer-helix.h"
_Static_assert(QC5_LIVE_RECORD_VERSION==19&&QC5_LIVE_FRAMES==512&&QC5_NV12_TARGET==632,"Pinned runtime trial contract");
#endif
#ifdef QC5_AUDIO_CONCURRENT
#include "../audio-t23/audio-layout.h"
#ifdef QC5_AUDIO_STREAM
#include "../audio-continuous/stream-descriptors.h"
#ifdef QC5_AUDIO_DUPLEX
#include "../audio-duplex/duplex-layout.h"
#endif
#else
#include "audio-child.h"
static struct qc5_audio_child live_audio;
#endif
_Static_assert(QA_BASE>=QC5_STATS_END+4096u&&QA_END==QC5_PREVIEW_PHYS,"Audio stays in the unused reservation tail");
#endif
#ifdef QC5_LIVE_BOOTLOG
#define QC5_NETBOOT_PLATFORM
#include "live-net-beacon.h"
#include QC5_LIVE_NONCE_HEADER
#endif
static struct {
    struct qc5_helix_layout layout;struct hw_io hw;struct qc5_helix_sequence sequence;
    struct qc5_helix_live_record proof;struct qc5_live_net net;
    unsigned int ready;uint64_t first,last;
#ifdef QC5_STREAM_RUNTIME
    struct qc5_producer producer;struct qp_helix engine;
    struct qc5_preview *current_record;struct msca_record *current_data;struct msca_state *current_state;
    uint64_t client_epoch,current_start,cabac_total;
#ifdef QC5_CAPTURE_SERVICE
    struct qc5_helix_picture service_last_picture;
#endif
#endif
#ifdef QC5_LIVE_BOOTLOG
    struct qc5_netboot boot;struct qc5_live_beacon beacon;
#endif
} live;
#ifdef QC5_MEMORY_PROOF
static struct qc5_memory_note memory_proof;
static int memory_ok;
#endif
static void live_progress(const char *stage,uint64_t completed)
{
#ifdef QC5_LIVE_PROGRESS
    int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);if(fd<0)return;
    uint64_t encoded=live.proof.meta[H_ENCODED];
#ifdef QC5_STREAM_RUNTIME
    encoded=live.producer.input.encoded;
#endif
    char line[192];int n=snprintf(line,sizeof line,"<6>QC5_VIDEO_PROGRESS stage=%s completed=%llu encoded=%llu fault=%08x value=%08x\n",stage,(unsigned long long)completed,(unsigned long long)encoded,live.proof.meta[H_FAULT_REG],live.proof.meta[H_FAULT_VALUE]);
    if(n>0&&n<(int)sizeof line)(void)write(fd,line,(unsigned)n);close(fd);
#else
    (void)stage;(void)completed;
#endif
}
static int encoder_owns_address(uint32_t address)
{
    if(!live.ready)return 0;
#ifdef QC5_AUDIO_CONCURRENT
#ifdef QC5_AUDIO_STREAM
    if(qas_owns_address(address))return 1;
#ifdef QC5_AUDIO_DUPLEX
    /* The separate duplex owner checks its finite descriptor, immutable
     * PCM batch and the remaining384-byte guard before releasing TX. */
    if(qad_owns_address(address))return 1;
#endif
#else
    if(qa_owns_address(address))return 1;
#endif
#endif
    return qc5_helix_owns(&live.layout,address);
}
static int live_failure(struct qc5_preview *r,uint32_t address,uint32_t value)
{live.proof.meta[H_FAULT_REG]=address;live.proof.meta[H_FAULT_VALUE]=value;live_progress("failure",0);return core_refuse(r,address,value);}
static int encoder_prepare(volatile uint8_t *ram,struct qc5_preview *r)
{
    live_progress("encoder_prepare",0);
    uint32_t *m=live.proof.meta;struct qc5_helix_record cpu={0};
#ifdef QC5_MEMORY_PROOF
    if(!memory_ok)return live_failure(r,0x77000,memory_proof.flags);
#endif
    m[H_STAGE]=2;int ok=prepare_helix_cpu(ram,&cpu);
    m[H_CP0_BEFORE]=cpu.meta[H_CP0_BEFORE];m[H_CP0_AFTER]=cpu.meta[H_CP0_AFTER];m[H_CP0_REQUESTED]=cpu.meta[H_CP0_REQUESTED];
    if(!ok){memcpy(live.proof.prefix,cpu.prefix,16);return live_failure(r,cpu.meta[H_FAULT_REG],cpu.meta[H_FAULT_VALUE]);}
    if(!qc5_helix_layout(1280,720,&live.layout))return 0;
    int fd=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);if(fd<0)return 0;
    live.hw.clock=regs[CPM];live.hw.vpu=map_phys(fd,0x13100000,0x100000);close(fd);
    if(live.hw.vpu==MAP_FAILED)return 0;
#ifdef QC5_STREAM_RUNTIME
    const struct helix_io native={&live.hw,hw_read,hw_write,hw_pause};
    if(!qp_init(&live.producer,(uint8_t*)ram,QC5_CAPTURE_RAM_END-QC5_CAPTURE_RAM_BASE)||
       !qp_helix_prepare(&live.engine,(uint8_t*)ram,QC5_CAPTURE_RAM_END-QC5_CAPTURE_RAM_BASE,&native))return 0;
#else
    for(unsigned int n=0;n<H_AREA_COUNT;++n){const struct qc5_helix_region *a=&live.layout.region[n];
        memset((void *)(ram+(a->start-QC5_CAPTURE_RAM_BASE)),0,a->bytes);}
    encoder_guards(ram,&live.layout,1);
#endif
    live.ready=1;
    live.sequence=(struct qc5_helix_sequence){.gop=32,.qp=30};
    m[H_DESCRIPTOR_PHYS]=live.layout.region[H_DESCRIPTOR].start;m[H_STAGE]=3;
    live_progress("encoder_ready",0);
    /* Wi-Fi interrupts must remain available. Only the two video IRQs are polled. */
    return (r->intc_mask&(BIT(29)|BIT(30)))==(BIT(29)|BIT(30));
}
#ifdef QC5_STREAM_RUNTIME
#ifdef QC5_CAPTURE_SERVICE
#include "../video-runtime/native-service-web.h"
#include "../native-integrated/native-completion-r4/bridge.h"
#include "../native-integrated/native-completion-r4/qr-capture.h"
#ifdef QC5_AUDIO_STREAM
#include "../video-runtime/native-service-audio.h"
#endif
#endif
static unsigned integrated_media_flags(void)
{
 unsigned flags=QMI_VIDEO;
 if(service_audio.child.started&&!service_audio.child.receiver.finished&&service_audio.periods){
  flags|=QMI_AUDIO;if(service_talk.initialized&&service_talk.child_ready)flags|=QMI_TALK;
 }
 return flags;
}
#include "../video-runtime/native-bridge.h"
#ifdef QC5_CAPTURE_SERVICE
#include "../video-runtime/native-service-loop.h"
#endif
#else
static int encoder_completed(volatile uint8_t *ram,struct qc5_preview *r,unsigned int completed)
{
    if(completed==1||completed==120||completed==121||completed>=224||!(completed%32))live_progress("frame",completed);
#ifdef QC5_LIVE_BOOTLOG
    qc5_beacon_tick(&live.beacon);
#endif
    if(qc5_net_tick(&live.net)){
        if(live.sequence.pending)return live_failure(r,0x72000,completed);
        live.sequence.have_reference=0;live.sequence.next_frame=0;live.sequence.since_idr=0;
    }
    if(completed<=QC5_LIVE_WARMUP)return 1;
    uint32_t *m=live.proof.meta,*s=live.proof.net;unsigned int n=completed-121,bank=(completed-1)%2;
    if(!live.ready||n>=128||m[H_ENCODED]!=n)return live_failure(r,0x72004,completed);
    uint64_t start,encoded,end;
    struct qc5_helix_picture picture;struct qc5_helix_plan plan;
    uint32_t *desc=(uint32_t *)(ram+(live.layout.region[H_DESCRIPTOR].start-QC5_CAPTURE_RAM_BASE));
    uint8_t *bs=(uint8_t *)(ram+(live.layout.region[H_BITSTREAM].start-QC5_CAPTURE_RAM_BASE));
    uint8_t *au=(uint8_t *)(ram+(QC5_HELIX_AU_PHYS-QC5_CAPTURE_RAM_BASE));
    if(!ae_time_us(r,&start)||!qc5_helix_sequence_next(&live.sequence,bank,&picture)||
        !qc5_helix_build_picture(&plan,&picture,desc,4096,bs))return live_failure(r,0x72008,n);
    m[H_PAIRS]=plan.pairs;m[H_DESC_HASH]=plan.descriptor_hash;m[H_ORDER_HASH]=plan.order_hash;m[H_INPUT_BANK]=bank;
    __asm__ volatile("sync" ::: "memory");
    struct helix_io io={&live.hw,hw_read,hw_write,hw_pause};struct helix_result result={0};
    if(completed>=224)live_progress("before_encode",completed);
    int ok=helix_encode(&io,&plan,desc,&result);
    if(completed>=224)live_progress("after_encode",completed);
    m[H_STATUS]=result.status;m[H_ERRORS]|=result.status&HELIX_ERRORS;m[H_RESET_MASK]=result.reset_mask;
    if(result.polls>m[H_POLLS])m[H_POLLS]=result.polls;
    if(!ok){memcpy(live.proof.prefix,result.before_reset,sizeof(result.before_reset));return live_failure(r,result.fault_register,result.fault_value);}
    if(!encoder_guards(ram,&live.layout,0))return live_failure(r,0x7200c,n);
    int bytes=qc5_helix_annexb(&plan,bs,bs+256,result.bytes,au,QC5_HELIX_AU_BYTES);
    if(!bytes||!ae_time_us(r,&encoded)||encoded<=start||encoded-start>3000000)return live_failure(r,0x72010,n);
    if(!n){memcpy(live.proof.prefix,au,(unsigned int)bytes<sizeof(live.proof.prefix)?(unsigned int)bytes:sizeof(live.proof.prefix));m[H_AU_HASH]=2166136261u;live.first=start;}
    for(unsigned int i=0;i<(unsigned int)bytes;++i)m[H_AU_HASH]=(m[H_AU_HASH]^au[i])*16777619u;
    m[H_AU_BYTES]+=(unsigned int)bytes;m[H_CABAC_BYTES]+=result.bytes;m[H_ENCODED]=n+1;
    s[L_IDRS]+=picture.idr;
    s[L_LAST_BYTES]=(unsigned int)bytes;
    if(encoded-start>s[L_ENCODE_MAX_US])s[L_ENCODE_MAX_US]=(uint32_t)(encoded-start);
    if(live.net.stream&&!qc5_net_write(&live.net,au,(unsigned int)bytes,1))live_progress("send_timeout",completed);
    if(completed>=224)live_progress("after_send",completed);
    if(!qc5_helix_sequence_finish(&live.sequence,&picture,1)||!ae_time_us(r,&end)||end<=start||end-start>3200000)
        return live_failure(r,0x72014,n);
    if(end-start>s[L_FRAME_MAX_US])s[L_FRAME_MAX_US]=(uint32_t)(end-start);live.last=end;
#ifdef QC5_AUDIO_CONCURRENT
    if(!n&&!qc5_audio_child_start(&live_audio,"/bin/qc5-audio"))return live_failure(r,0x78000,errno);
#endif
    return 1;
}
#endif
#ifndef QC5_EXTERNAL_LIVE_MAIN
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--cpu-self-test")){
#ifdef QC5_MEMORY_PROOF
        if(!qc5_memory_test(&memory_proof))return CAPTURE_MAP;
        printf("QC5_MEMORY_SELFTEST bytes=%u hash=%08x total_kb=%u flags=%u\n",memory_proof.heap_bytes,memory_proof.heap_hash,memory_proof.total_kb,memory_proof.flags);
#endif
        return cpu_self_test();
    }
    memset(&live,0,sizeof(live));
    live_progress("producer_start",0);
    uint32_t *m=live.proof.meta,*s=live.proof.net;
    m[H_MAGIC]=QC5_H264_MAGIC;m[H_VERSION]=QC5_LIVE_RECORD_VERSION;m[H_STAGE]=1;m[H_WIDTH]=1280;m[H_HEIGHT]=720;m[H_QP]=30;m[H_WARMUP]=120;
    s[L_TARGET]=QC5_LIVE_FRAMES;s[L_PORT]=QC5_LIVE_PORT;
    if(!qc5_net_open(&live.net,0,QC5_LIVE_PORT)){m[H_FAULT_REG]=0x72018;m[H_FAULT_VALUE]=errno;}
#ifdef QC5_LIVE_BOOTLOG
    qc5_netboot_sample(&live.boot);(void)qc5_beacon_open(&live.beacon,&live.boot,qc5_live_nonce);
    char *at=live.net.extra_header;
    at+=sprintf(at,"X-QC5-Nonce: ");
    for(unsigned int n=0;n<16;++n)at+=sprintf(at,"%02x",qc5_live_nonce[n]);
    memcpy(at,"\r\n",3);
#endif
#ifdef QC5_MEMORY_PROOF
    live_progress("before_heap_test",0);
    memory_ok=qc5_memory_test(&memory_proof);
    live_progress(memory_ok?"heap_verified":"heap_failed",0);
#endif
    live_progress("before_capture",0);
    int status=nv12_capture_main();
    live_progress("after_capture",0);
    int fd=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);if(fd<0){qc5_net_finish(&live.net);return CAPTURE_MAP;}
    volatile uint8_t *ram=map_phys(fd,QC5_CAPTURE_RAM_BASE,QC5_CAPTURE_RAM_END-QC5_CAPTURE_RAM_BASE);close(fd);
    if(ram==MAP_FAILED){qc5_net_finish(&live.net);return CAPTURE_MAP;}
    struct qc5_preview record;memcpy(&record,(const void *)(ram+(QC5_PREVIEW_PHYS-QC5_CAPTURE_RAM_BASE)),512);
    const uint32_t *words=(const uint32_t *)&record;
    if(words[0]!=QC5_PREVIEW_MAGIC||words[1]!=23||words[127]!=qc5_frame_hash((const uint8_t *)&record,508))memset(&record,0,sizeof(record));
    struct msca_record nv12;memcpy(&nv12,record.pixels,sizeof(nv12));
    m[H_CAPTURE_STATUS]=(uint32_t)status;m[H_COMPLETED]=nv12.meta[M_COMPLETED];m[H_CAPTURE_STOP]=nv12.meta[M_STOP];
    m[H_GAIN]=nv12.meta[M_GAIN];m[H_EXPOSURE]=nv12.meta[M_EXPOSURE];
#ifdef QC5_SENSOR_ORIENTATION
    if(!m[H_FAULT_REG]&&nv12.meta[M_FAULT_ADDRESS]){
        m[H_FAULT_REG]=nv12.meta[M_FAULT_ADDRESS];m[H_FAULT_VALUE]=nv12.meta[M_FAULT_VALUE];
    }
#endif
    struct msca_state stopped={0};uint32_t bad=0;
    for(unsigned int n=0;n<2;++n)(void)qc5_nv12_plan(n,1280,720,&stopped.planes[n]);
    m[H_GUARDS]=live.ready&&m[H_CAPTURE_STOP]==31&&!record.csi_err1&&!record.csi_err2&&
        encoder_guards(ram,&live.layout,0)&&nv12_guard(ram,&stopped,&bad);
    uint32_t net_counts[6]={0};
    if(!qc5_net_note32(&live.net,net_counts)){
        if(!m[H_FAULT_REG])(void)live_failure(&record,0x7d034,0);
        status=CAPTURE_FRAME;m[H_STAGE]=3;m[H_CAPTURE_STATUS]=CAPTURE_FRAME;
    }
    s[L_ACCEPTED]=net_counts[0];s[L_REQUESTS]=net_counts[1];s[L_DISCONNECTS]=net_counts[2];
    s[L_TIMEOUTS]=net_counts[3];s[L_SENT_FRAMES]=net_counts[4];s[L_SENT_BYTES]=net_counts[5];s[L_SEND_MAX_US]=live.net.max_send_us;
    s[L_FIRST_US]=(uint32_t)live.first;s[L_LAST_US]=(uint32_t)live.last;s[L_ELAPSED_US]=(uint32_t)(live.last-live.first);
    if(!status)m[H_STAGE]=8;
#ifdef QC5_LIVE_BOOTLOG
    qc5_netboot_sample(&live.boot);
#ifdef QC5_MEMORY_PROOF
    struct qc5_netboot extended=live.boot;extended.words[NB_VERSION]=2;
    memcpy(&extended.words[NB_UNUSED0],&memory_proof,sizeof(memory_proof));extended.words[NB_HASH]=qc5_netboot_hash(&extended,60);
    memcpy(live.proof.prefix+128,&extended,64);
#else
    memcpy(live.proof.prefix+128,&live.boot,64);
#endif
    qc5_beacon_close(&live.beacon);
#endif
#ifdef QC5_NV12_SPATIAL
    uint32_t spatial_writes=0,spatial_updates=0;
    if(!qc5_filter_u32_pair(active_spatial.writes,active_spatial.updates,&spatial_writes,&spatial_updates)){
        if(!m[H_FAULT_REG])(void)live_failure(&record,0x7d030,0);
        status=CAPTURE_FRAME;m[H_STAGE]=3;m[H_CAPTURE_STATUS]=CAPTURE_FRAME;
    }
    struct qc5_spatial_note spatial={.magic=QC5_SPATIAL_NOTE_MAGIC,.version=1,
        .profile_hash=gc1084_spatial.checksum,.writes=spatial_writes,
        .updates=spatial_updates,.gain=active_spatial.gain_index,
        .read_hash=spatial_readback_hash,.bypass=active_spatial.ready?msca_read(0x0c):0};
#ifdef QC5_NV12_DETAIL
    spatial.version=2;spatial.profile_hash=qc5_spatial_pair_digest(gc1084_spatial.checksum,gc1084_detail.checksum);
    spatial.read_hash=qc5_spatial_pair_digest(spatial_readback_hash,active_detail.read_hash);
#endif
#ifdef QC5_AF_STATS
    spatial.version=3;spatial.profile_hash=qc5_spatial_pair_digest(spatial.profile_hash,qc5_af_profile_hash());
    spatial.read_hash=qc5_spatial_pair_digest(spatial.read_hash,active_af_stats.read_hash);
#endif
#ifdef QC5_MDNS_GATE
    spatial.version=4;
#endif
#ifdef QC5_MDNS_DEFERRED
    spatial.version=5;
#endif
    memcpy(live.proof.prefix+96,&spatial,sizeof(spatial));
#endif
#ifdef QC5_LIVE_WB_NOTE
    struct qc5_wb_note white={.magic=QC5_WB_NOTE_MAGIC,.version=1,.plan_hash=wb_active_hash(),
        .requested_r=wb_active_expected()[0],.requested_b=wb_active_expected()[1],
        .read_r=status?0:nv12.data.success.wb.values[0],.read_b=status?0:nv12.data.success.wb.values[1],
        .read_mask=0};
    if(!status&&nv12.data.success.wb.read_count<=4)
        for(unsigned int n=0;n<nv12.data.success.wb.read_count;++n)
            if(nv12.data.success.wb.values[n]==wb_active_expected()[n])white.read_mask|=1u<<n;
#ifdef QC5_DYNAMIC_AWB
    white.version=2;
    struct qc5_awb_note auto_white=qc5_awb_capture_note(&active_awb);
#ifdef QC5_DYNAMIC_CCM
    auto_white.progress=(auto_white.progress&~255u)|(QC5_LIVE_RECORD_VERSION>=18?3u:2u);auto_white.ct=active_ccm.ct;
#endif
    memcpy(live.proof.prefix+8,&auto_white,sizeof(auto_white));
#endif
    memcpy(live.proof.prefix+64,&white,sizeof(white));
#ifdef QC5_DYNAMIC_CCM
    unsigned ccm_version=3;
#ifdef QC5_IMAGE_CONTINUOUS
    ccm_version=4;
#endif
    struct qc5_ccm_wb_note colour={QC5_WB_NOTE_MAGIC,ccm_version,wb_active_hash(),
        active_ccm.profile?active_ccm.profile->checksum:0,active_ccm.read_hash,white.read_r,white.read_b,
        white.read_mask|(active_ccm.read_mask<<4)|qc5_ccm_note_meter(&active_ccm)};
    memcpy(live.proof.prefix+64,&colour,sizeof(colour));
#endif
#endif
#ifdef QC5_SENSOR_ORIENTATION
    memcpy(live.proof.prefix+32,&active_orientation,sizeof(active_orientation));
#endif
    int valid=qc5_helix_live_success(&live.proof);
#ifdef QC5_AUDIO_CONCURRENT
    if(!qc5_audio_child_finish(&live_audio))valid=0;
#endif
    memcpy(record.pixels,&live.proof,sizeof(live.proof));publish_record(ram,&record,valid?CAPTURE_OK:CAPTURE_FRAME);
    printf("QC5_H264_LIVE valid=%d captured=%u encoded=%u sent=%u bytes=%u\n",valid,m[H_COMPLETED],m[H_ENCODED],s[L_SENT_FRAMES],s[L_SENT_BYTES]);
    qc5_net_finish(&live.net);return valid?CAPTURE_OK:CAPTURE_FRAME;
}
#endif

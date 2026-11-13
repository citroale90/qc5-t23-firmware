/* SPDX-License-Identifier: GPL-3.0-only
 * Separate service proof schema. Never relax the old fixed632/512 schema19.
 * All lifetime counts/time fields are64-bit; legacy meta counts saturate.
 */
#ifndef QC5_NATIVE_SERVICE_NOTE_H
#define QC5_NATIVE_SERVICE_NOTE_H
#include <stdint.h>
#include <string.h>
#include "../video-gc1084/helix-record.h"
#include "../video-gc1084/helix-plan.h"
#include "../video-gc1084/awb-note.h"
#ifndef H_WARMUP
#define H_WARMUP H_Y_HASH
#define H_ENCODED H_UV_HASH
#endif
enum qsn_count {
 SN_CAPTURED,SN_QUEUED,SN_ENCODED,SN_PUBLISHED,SN_AU_BYTES,SN_CABAC_BYTES,SN_MUX_BYTES,
 SN_POLLS,SN_FIRST_US,SN_LAST_US,SN_FIRST_CAPTURE_US,SN_LAST_CAPTURE_US,SN_HOLD_US,SN_DURATION_US,
 SN_EPOCH,SN_ACCEPTED,SN_REQUESTS,SN_DISCONNECTS,SN_TIMEOUTS,SN_SENT_FRAMES,SN_SENT_BYTES,
 SN_WS_MESSAGES,SN_SPATIAL_UPDATES,SN_BCSH_FRAMES,SN_COUNT
};
enum qsn_extra {
 SN_WB_PROFILE,SN_CCM_PROFILE,SN_CCM_READ,SN_WB_R,SN_WB_B,SN_AWB_READ,SN_CT,
 SN_SPATIAL_PROFILE,SN_SPATIAL_READ,SN_BCSH_READ,SN_MDNS_READ,SN_FLAGS,SN_EXTRA
};
/* H_SAVED holds the last actual picture's compact syntax ONLY in schema20. */
static uint32_t qsn_picture(const struct qc5_helix_picture *p)
{return p->idr|(p->idr_id<<1)|(p->output_reference<<2)|(p->input_bank<<3)|(p->frame_number<<4);}
struct qc5_service_note {uint32_t meta[32];uint64_t count[SN_COUNT];uint8_t nonce[16];uint32_t extra[SN_EXTRA];};
_Static_assert(SN_COUNT==24&&SN_EXTRA==12&&sizeof(struct qc5_service_note)==384,"Service proof fits unchanged SD envelope");
static int qsn_awb_readback_valid(uint32_t value,uint32_t ct,unsigned version)
{
 uint32_t empty=version==21?value&QC5_AWB_EMPTY_STATS_FLAG:version==22?value&QC5_AWB_NO_ZONES_FLAG:0;
 if(empty&&((value&(1u<<23))||ct!=5000))return 0;
 return ((value&~((1u<<24)|empty))&0xffcfffffu)==(empty?0x004fffffu:0x00cfffffu);
}
static uint32_t qsn_u32(uint64_t n){return n>UINT32_MAX?UINT32_MAX:(uint32_t)n;}
static int qsn_descriptor_valid(const struct qc5_service_note *p)
{
 uint32_t v=p->meta[H_SAVED];
 if(v&~UINT32_C(0x3fff))return 0;
 struct qc5_helix_picture pic={v&1,(v>>4)&1023,(v>>1)&1,30,(v>>3)&1,(v>>2)&1};
 if(pic.input_bank!=p->meta[H_INPUT_BANK]||pic.input_bank!=((p->count[SN_CAPTURED]-1)&1)||
    pic.frame_number>31||(pic.idr?pic.frame_number!=0:pic.frame_number==0))return 0;
 /* A small descriptor/syntax scratch is used after DMA is quiescent. It is
  * not a pixel/AU queue or a second encoder and touches no device registers. */
 uint32_t desc[4096];uint8_t syntax[256];struct qc5_helix_plan plan;
 return qc5_helix_build_picture(&plan,&pic,desc,4096,syntax)&&plan.pairs==p->meta[H_PAIRS]&&
  plan.descriptor_hash==p->meta[H_DESC_HASH]&&plan.order_hash==p->meta[H_ORDER_HASH];
}
static int qc5_service_note_valid(const struct qc5_service_note *p,const uint8_t nonce[16])
{
 if(!p||!nonce||memcmp(p->nonce,nonce,16))return 0;
 const uint32_t *m=p->meta,*e=p->extra;const uint64_t *n=p->count;
 if(m[H_MAGIC]!=QC5_H264_MAGIC||(m[H_VERSION]!=20&&m[H_VERSION]!=21&&m[H_VERSION]!=22)||m[H_STAGE]!=8||m[H_CAPTURE_STATUS]||
    m[H_CAPTURE_STOP]!=31||m[H_GUARDS]!=1||m[H_RESET_MASK]!=3||m[H_FAULT_REG]||m[H_FAULT_VALUE]||
    m[H_WIDTH]!=1280||m[H_HEIGHT]!=720||m[H_QP]!=30||m[H_WARMUP]!=120||
    m[H_DESCRIPTOR_PHYS]!=0x1baa000||m[H_CP0_REQUESTED]!=((m[H_CP0_BEFORE]|0x340)&0x3ff)||
    !(m[H_STATUS]&1)||(m[H_STATUS]&0x100484)||m[H_ERRORS]||!m[H_POLLS]||m[H_POLLS]>2000||
    m[H_GAIN]>24||m[H_EXPOSURE]<4||m[H_EXPOSURE]>896||!m[H_AU_HASH])return 0;
 if(n[SN_CAPTURED]<632||n[SN_CAPTURED]!=n[SN_QUEUED]||n[SN_CAPTURED]-120!=n[SN_ENCODED]||
    n[SN_PUBLISHED]!=n[SN_ENCODED]||n[SN_SENT_FRAMES]!=n[SN_ENCODED]||
    !n[SN_CABAC_BYTES]||n[SN_AU_BYTES]<=n[SN_CABAC_BYTES]||n[SN_SENT_BYTES]!=n[SN_AU_BYTES]||
    n[SN_ENCODED]>UINT64_MAX/786944||n[SN_AU_BYTES]>n[SN_ENCODED]*786944||
    n[SN_MUX_BYTES]<=n[SN_AU_BYTES]||n[SN_WS_MESSAGES]<=n[SN_ENCODED]||
    n[SN_ACCEPTED]!=1||n[SN_REQUESTS]!=1||n[SN_DISCONNECTS]||n[SN_TIMEOUTS]||n[SN_EPOCH]!=1||
    n[SN_POLLS]<n[SN_CAPTURED]||n[SN_BCSH_FRAMES]!=n[SN_CAPTURED]||!n[SN_SPATIAL_UPDATES]||
    n[SN_SPATIAL_UPDATES]>n[SN_CAPTURED])return 0;
 if(n[SN_DURATION_US]!=25000000||!n[SN_FIRST_US]||n[SN_FIRST_US]>=n[SN_LAST_US]||
    n[SN_LAST_US]-n[SN_FIRST_US]<n[SN_DURATION_US]||n[SN_LAST_US]-n[SN_FIRST_US]>30000000||
    !n[SN_FIRST_CAPTURE_US]||n[SN_FIRST_CAPTURE_US]>=n[SN_FIRST_US]||
    n[SN_LAST_CAPTURE_US]<n[SN_LAST_US]||n[SN_HOLD_US]<=n[SN_FIRST_US]||
    n[SN_HOLD_US]>n[SN_LAST_CAPTURE_US])return 0;
 if(m[H_COMPLETED]!=qsn_u32(n[SN_CAPTURED])||m[H_ENCODED]!=qsn_u32(n[SN_ENCODED])||
    m[H_AU_BYTES]!=qsn_u32(n[SN_AU_BYTES])||m[H_CABAC_BYTES]!=qsn_u32(n[SN_CABAC_BYTES]))return 0;
 if(!e[SN_WB_PROFILE]||!e[SN_CCM_PROFILE]||!e[SN_CCM_READ]||!e[SN_SPATIAL_PROFILE]||
    !e[SN_SPATIAL_READ]||!e[SN_BCSH_READ]||!e[SN_MDNS_READ]||e[SN_FLAGS]!=31||
    e[SN_CT]<1000||e[SN_CT]>30000||
    (e[SN_WB_R]&0xffffc000)!=0x04000000||!(e[SN_WB_R]&0x3fff)||
    (e[SN_WB_B]&0xffffc000)!=0x04000000||!(e[SN_WB_B]&0x3fff)||
    !qsn_awb_readback_valid(e[SN_AWB_READ],e[SN_CT],m[H_VERSION]))return 0;
 return qsn_descriptor_valid(p);
}
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_HELIX_LIVE_RECORD_H
#define QC5_HELIX_LIVE_RECORD_H
#include "helix-record.h"
#include "live-net-boot.h"
#include "spatial-note.h"
#include "wb-note.h"
#include "sensor-orientation.h"
#include "awb-note.h"
#include "ccm-note.h"
#ifndef QC5_EXPECT_CCM_HASH
#define QC5_EXPECT_CCM_HASH 0u
#endif
#ifndef QC5_EXPECT_WB_HASH
#define QC5_EXPECT_WB_HASH 0u
#endif
#ifndef QC5_EXPECT_SPATIAL_HASH
#define QC5_EXPECT_SPATIAL_HASH 0u
#endif
#ifndef QC5_LIVE_RECORD_VERSION
#define QC5_LIVE_RECORD_VERSION 4u
#endif
#ifndef QC5_LIVE_FRAMES
#define QC5_LIVE_FRAMES 128u
#endif
#define QC5_LIVE_WARMUP 120u
#define QC5_LIVE_PORT 18073u
#define H_WARMUP H_Y_HASH
#define H_ENCODED H_UV_HASH
enum qc5_live_field {
    L_TARGET,L_ACCEPTED,L_REQUESTS,L_DISCONNECTS,L_TIMEOUTS,L_SENT_FRAMES,L_SENT_BYTES,
    L_SEND_MAX_US,L_ENCODE_MAX_US,L_FRAME_MAX_US,L_IDRS,L_ELAPSED_US,L_FIRST_US,L_LAST_US,L_LAST_BYTES,L_PORT
};
struct qc5_helix_live_record {uint32_t meta[32],net[16];uint8_t prefix[192];};
_Static_assert(sizeof(struct qc5_helix_live_record)==384,"Live proof fits existing single SD checkpoint");
static int qc5_helix_live_success(const struct qc5_helix_live_record *p)
{
    if(!p)return 0;const uint32_t *m=p->meta,*s=p->net;
    unsigned target=m[H_VERSION]==19?512u:128u;
    if(m[H_MAGIC]!=QC5_H264_MAGIC||(m[H_VERSION]<4||m[H_VERSION]>19)||m[H_STAGE]!=8||m[H_CAPTURE_STATUS]||
        m[H_COMPLETED]!=target+120||m[H_CAPTURE_STOP]!=31||m[H_ENCODED]!=target||m[H_WARMUP]!=120||
        m[H_INPUT_BANK]!=1||m[H_PAIRS]!=1034||m[H_ORDER_HASH]!=0x042e72a7u||m[H_DESC_HASH]!=673378052u||
        m[H_CP0_REQUESTED]!=((m[H_CP0_BEFORE]|0x340)&0x3ff)||
        !(m[H_STATUS]&1)||m[H_STATUS]&0x100484u||m[H_ERRORS]||m[H_RESET_MASK]!=3||
        !m[H_POLLS]||m[H_POLLS]>2000||m[H_GUARDS]!=1||m[H_FAULT_REG]||m[H_FAULT_VALUE]||
        m[H_SAVED]||m[H_WIDTH]!=1280||m[H_HEIGHT]!=720||m[H_QP]!=30||m[H_DESCRIPTOR_PHYS]!=0x1baa000||
        !m[H_AU_HASH]||!m[H_CABAC_BYTES]||m[H_AU_BYTES]<=m[H_CABAC_BYTES]||m[H_AU_BYTES]>target*786944u||
        m[H_GAIN]>24||m[H_EXPOSURE]<4||m[H_EXPOSURE]>896)return 0;
    if(s[L_TARGET]!=target||s[L_ACCEPTED]!=1||s[L_REQUESTS]!=1||s[L_DISCONNECTS]||s[L_TIMEOUTS]||
        s[L_SENT_FRAMES]!=target||s[L_SENT_BYTES]!=m[H_AU_BYTES]||s[L_PORT]!=18073||s[L_IDRS]!=target/32||
        !s[L_SEND_MAX_US]||s[L_SEND_MAX_US]>200000||!s[L_ENCODE_MAX_US]||s[L_ENCODE_MAX_US]>3000000||
        s[L_FRAME_MAX_US]<s[L_ENCODE_MAX_US]||s[L_FRAME_MAX_US]>3200000||
        s[L_ELAPSED_US]<(target==512?16000000u:4000000u)||s[L_ELAPSED_US]>(target==512?35000000u:20000000u)||s[L_FIRST_US]>=s[L_LAST_US]||
        s[L_LAST_US]-s[L_FIRST_US]!=s[L_ELAPSED_US]||s[L_LAST_BYTES]<6||s[L_LAST_BYTES]>786944)return 0;
    if(m[H_VERSION]>=5){
        struct qc5_netboot boot;memcpy(&boot,p->prefix+128,64);const uint32_t *b=boot.words;
        if(!qc5_netboot_valid(&boot)||b[NB_VERSION]!=(m[H_VERSION]>=13?2u:1u)||b[NB_STAGES]!=4095||b[NB_FAILED]||
            (b[NB_IP]&0xffffff00u)!=0xc0a80400u||!(b[NB_IP]&255u)||(b[NB_IP]&255u)==255u||
            !b[NB_IFINDEX]||!b[NB_MAC0]||!b[NB_AVAILABLE_KB])return 0;
    }
    if(m[H_VERSION]>=6){
        struct qc5_spatial_note spatial;memcpy(&spatial,p->prefix+96,sizeof(spatial));
        if(!qc5_spatial_note_version_valid(&spatial,m[H_GAIN],QC5_EXPECT_SPATIAL_HASH,m[H_VERSION]>=17?5:m[H_VERSION]>=16?4:m[H_VERSION]>=15?3:m[H_VERSION]>=14?2:1))return 0;
    }
    if(m[H_VERSION]>=7&&m[H_VERSION]<12){
        struct qc5_wb_note white;memcpy(&white,p->prefix+64,sizeof(white));
        if(!qc5_wb_note_version_valid(&white,QC5_EXPECT_WB_HASH,m[H_VERSION]>=10?2:1))return 0;
    }
    if(m[H_VERSION]>=8){
        struct qc5_orientation_note orientation;memcpy(&orientation,p->prefix+32,sizeof(orientation));
        if(!qc5_orientation_note_valid(&orientation))return 0;
    }
    if(m[H_VERSION]>=10){
        struct qc5_awb_note awb;memcpy(&awb,p->prefix+8,sizeof(awb));
        if(!qc5_awb_note_version_valid(&awb,QC5_EXPECT_WB_HASH,m[H_VERSION]>=18?3:m[H_VERSION]>=12?2:1))return 0;
        if(m[H_VERSION]>=12){
            struct qc5_ccm_wb_note colour;memcpy(&colour,p->prefix+64,sizeof(colour));
            if(colour.version!=(m[H_VERSION]>=18?4u:3u)||!qc5_ccm_wb_note_valid(&colour,QC5_EXPECT_WB_HASH,QC5_EXPECT_CCM_HASH)||
                colour.ccm_read_hash!=qc5_ccm_calculated_hash(&qc5_ccm_profiles[0],awb.ct,qc5_exposure_ev_q10(m[H_EXPOSURE],m[H_GAIN])))return 0;
        }
    }
    return !p->prefix[0]&&!p->prefix[1]&&!p->prefix[2]&&p->prefix[3]==1&&p->prefix[4]==0x67;
}
#endif

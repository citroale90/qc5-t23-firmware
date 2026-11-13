/* SPDX-License-Identifier: GPL-2.0-or-later
 * Uses OpenIMP 493aa97445fabcf78717a8c1b00a3bb280073577 and its x264-derived
 * syntax writer. The fixed PPS must advertise the builder's quant matrices.
 * Addresses are exclusively from the locally checked 9 MiB reservation.
 */
#include <string.h>
#include "helix-plan.h"
#include "t21/t21_h264_descriptor.h"
#include "t30/h264enc/common.h"
#include "t30/t30_annexb.h"
static const uint8_t native_pps[]={
    0,0,0,1,0x68,0xee,0x3c,0xe1,0,0x42,0x42,0,0x84,0x84,4,0x4c,
    0x52,0x1b,0x93,0xc5,0x7c,0x9f,0x93,0xf9,0x3f,0x27,0xc9,0xe6,0xe4,0xc9,0x24,0x2c,
    0x22,0x42,0x90,0x9c,0x9e,0x4f,0xaf,0xc9,0xfd,0x7e,0x4f,0xaf,0x27,0x26,0xa4,0xc0};
static uint32_t hash_bytes(const void *source,unsigned int length)
{
    const uint8_t *p=source;uint32_t h=2166136261u;
    for(unsigned int n=0;n<length;++n)h=(h^p[n])*16777619u;
    return h;
}
static int picture_valid(const struct qc5_helix_picture *p)
{
    return p&&p->idr<=1&&p->frame_number<=1023&&p->idr_id<=1&&p->qp<=51&&
        p->input_bank<=1&&p->output_reference<=1&&((p->idr&&p->frame_number==0)||(!p->idr&&p->frame_number>0));
}
int qc5_helix_commands_valid(const uint32_t *d,const struct qc5_helix_plan *p)
{
    if(!d||!p||!picture_valid(&p->picture)||!qc5_helix_layout_valid(&p->layout)||p->pairs!=(p->picture.idr?1015u:1034u)||
        p->slice_bytes<1||p->slice_bytes>256||(p->picture.idr?p->parameter_bytes<sizeof(native_pps):p->parameter_bytes!=0)||
        p->parameter_bytes>sizeof(p->parameters))return 0;
    uint32_t order=2166136261u;
    for(unsigned int n=0;n<p->pairs;++n){
        uint32_t command=d[2*n+1],reg=command&0xffffcu;
        /* All command addresses are offsets into this one Helix window;
         * no privileged CP0/TLB/other-peripheral writes are permitted. */
        if(!(command&0x80000000u)||(command&0x3ff00003u)||
            !!(command&0x40000000u)!=(n+1==p->pairs)||reg>=0xc0000u)return 0;
        uint32_t key=reg|(command&0x40000000u);
        for(unsigned int b=0;b<4;++b)order=(order^((key>>(b*8))&255))*16777619u;
    }
    return order==(p->picture.idr?0xe734d389u:0x042e72a7u)&&p->order_hash==order&&
        hash_bytes(d,p->pairs*8)==p->descriptor_hash;
}
int qc5_helix_build_picture(struct qc5_helix_plan *out,const struct qc5_helix_picture *picture,uint32_t *d,
                    unsigned int words,uint8_t *slice)
{
    if(!out||!d||!slice||!picture_valid(picture)||words<4096)return 0;
    struct qc5_helix_plan p={0};struct qc5_nv12_bank input;
    p.picture=*picture;
    if(!qc5_helix_layout(1280,720,&p.layout)||!qc5_nv12_plan(picture->input_bank,1280,720,&input))return 0;
    h264_sps_t sps={0};h264_pps_t pps={0};h264_slice_header_t header={0};h264_cabac_t cabac={0};
    sps.i_profile_idc=PROFILE_HIGH;sps.i_level_idc=31;sps.i_log2_max_frame_num=10;
    sps.i_poc_type=2;sps.i_num_ref_frames=2;sps.b_gaps_in_frame_num_value_allowed=1;
    sps.i_mb_width=80;sps.i_mb_height=45;sps.b_frame_mbs_only=1;
    sps.b_direct8x8_inference=1;sps.i_chroma_format_idc=CHROMA_420;
    sps.b_vui=1;sps.vui.b_timing_info_present=1;sps.vui.i_num_units_in_tick=1;
    sps.vui.i_time_scale=50;sps.vui.b_fixed_frame_rate=1;
    pps.b_cabac=1;pps.i_num_slice_groups=1;pps.i_num_ref_idx_l0_default_active=1;
    pps.i_num_ref_idx_l1_default_active=1;pps.i_pic_init_qp=26;pps.i_pic_init_qs=26;
    pps.b_deblocking_filter_control=1;pps.b_transform_8x8_mode=1;
    uint8_t syntax[256]={0};bs_t bits;
    if(picture->idr){
        bs_init(&bits,syntax,sizeof(syntax));h264e_sps_write(&bits,&sps);
        unsigned int n=(unsigned int)bs_pos(&bits)/8u;if(n>sizeof(syntax))return 0;
        T30AnnexBWriter writer;
        if(t30_annexb_begin(&writer,p.parameters,sizeof(p.parameters),NAL_SPS,NAL_PRIORITY_HIGHEST)||
            t30_annexb_append(&writer,syntax,n))return 0;
        p.parameter_bytes=(unsigned int)(writer.output-p.parameters);
        if(p.parameter_bytes+sizeof(native_pps)>sizeof(p.parameters))return 0;
        memcpy(p.parameters+p.parameter_bytes,native_pps,sizeof(native_pps));p.parameter_bytes+=sizeof(native_pps);
    }
    memset(slice,0,256);bs_init(&bits,slice,256);
    int type=picture->idr?SLICE_TYPE_I:SLICE_TYPE_P;
    int priority=picture->idr?NAL_PRIORITY_HIGHEST:NAL_PRIORITY_HIGH;
    h264e_slice_header_init(&header,&sps,&pps,picture->idr?(int)picture->idr_id:-1,(int)picture->frame_number,(int)picture->qp);header.i_type=type;
    header.i_disable_deblocking_filter_idc=0;
    h264e_slice_header_write(&bits,&header,priority);bs_align_1(&bits);
    if(bits.i_left!=32)return 0;
    p.slice_bytes=(unsigned int)(bits.p-bits.p_start);
    h264_cabac_init();h264_cabac_context_init(&cabac,type,(int)picture->qp,header.i_cabac_init_idc);
    T21H264SliceConfig c={0};c.mb_width=80;c.mb_height=45;c.last_mby=44;c.qp=(uint8_t)picture->qp;c.slice_type=picture->idr?0:1;
    c.width=1280;c.height=720;c.raw_format=8;c.cabac_state=cabac.state;
    c.raw[0]=input.y;c.raw[1]=input.uv;c.stride[0]=1280;c.stride[1]=1280;
    c.reference_y=p.layout.region[picture->output_reference?H_REF0:H_REF1].start;c.reference_c=c.reference_y+921600;
    c.output_y=p.layout.region[picture->output_reference?H_REF1:H_REF0].start;c.output_c=c.output_y+921600;
    c.bitstream=p.layout.region[H_BITSTREAM].start+256;c.bitstream_kib=1024;
    c.scratch_base=p.layout.region[H_SCRATCH].start;
    memcpy(c.scratch_offset,p.layout.scratch_offsets,sizeof(c.scratch_offset));c.bsf_stop=1;
    c.descriptor=d;c.descriptor_words=words;
    size_t pairs=0;if(T21_H264_BuildDescriptor(&c,&pairs)||pairs!=(picture->idr?1015u:1034u))return 0;
    p.pairs=(uint32_t)pairs;p.descriptor_hash=hash_bytes(d,p.pairs*8);p.order_hash=picture->idr?0xe734d389u:0x042e72a7u;
    if(!qc5_helix_commands_valid(d,&p))return 0;
    *out=p;return 1;
}
int qc5_helix_build(struct qc5_helix_plan *out,unsigned int bank,uint32_t *d,unsigned int words,uint8_t *slice)
{
    const struct qc5_helix_picture picture={1,0,0,30,bank,0};
    return qc5_helix_build_picture(out,&picture,d,words,slice);
}
int qc5_helix_annexb(const struct qc5_helix_plan *p,const uint8_t *slice,
                      const uint8_t *cabac,unsigned int bytes,uint8_t *out,unsigned int cap)
{
    if(!p||!slice||!cabac||!out||!bytes||bytes>QC5_HELIX_MAX_CABAC||
        !p->slice_bytes||p->slice_bytes>256||p->parameter_bytes>256||
        cap<p->parameter_bytes)return 0;
    memcpy(out,p->parameters,p->parameter_bytes);T30AnnexBWriter w;
    if(t30_annexb_begin(&w,out+p->parameter_bytes,cap-p->parameter_bytes,p->picture.idr?NAL_SLICE_IDR:NAL_SLICE,p->picture.idr?NAL_PRIORITY_HIGHEST:NAL_PRIORITY_HIGH)||
        t30_annexb_append(&w,slice,p->slice_bytes)||t30_annexb_append(&w,cabac,bytes))return 0;
    return (int)(w.output-out);
}

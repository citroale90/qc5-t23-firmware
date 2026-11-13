/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_HELIX_PLAN_H
#define QC5_HELIX_PLAN_H
#include "helix-layout.h"
struct qc5_helix_picture {uint32_t idr,frame_number,idr_id,qp,input_bank,output_reference;};
struct qc5_helix_plan {
    struct qc5_helix_layout layout;
    struct qc5_helix_picture picture;
    uint32_t pairs,descriptor_hash,order_hash,slice_bytes,parameter_bytes;
    uint8_t parameters[256];
};
int qc5_helix_build(struct qc5_helix_plan *,unsigned int bank,
                    uint32_t *descriptor,unsigned int descriptor_words,uint8_t *slice);
int qc5_helix_build_picture(struct qc5_helix_plan *,const struct qc5_helix_picture *,
                    uint32_t *descriptor,unsigned int descriptor_words,uint8_t *slice);
int qc5_helix_commands_valid(const uint32_t *,const struct qc5_helix_plan *);
int qc5_helix_annexb(const struct qc5_helix_plan *,const uint8_t *slice,
                      const uint8_t *cabac,unsigned int bytes,uint8_t *output,unsigned int capacity);
#endif

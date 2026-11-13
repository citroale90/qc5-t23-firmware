/* SPDX-License-Identifier: GPL-2.0-or-later
 * Persistent producer adapter for the same Helix transaction engine used
 * by the verified finite capture. OS code still owns CP0/clocks/MMIO mapping.
 */
#ifndef QC5_PRODUCER_HELIX_H
#define QC5_PRODUCER_HELIX_H
#include "producer.h"
#include "../video-gc1084/helix-engine.h"
#include "../video-gc1084/helix-guards.h"
struct qp_helix {
 struct qc5_helix_layout layout;
 struct helix_io io;
 struct helix_result result;
 uint8_t *arena;
 uint32_t pairs,descriptor_hash,order_hash,input_bank;
 unsigned initialized,failed;
};
/* Caller supplies a zeroed object and has already authorized the 9MiB
 * reservation and completed the native CPU/clock preparation. */
static int qp_helix_prepare(struct qp_helix *s,uint8_t *arena,size_t bytes,const struct helix_io *io)
{
 if(!s||s->initialized||!arena||((uintptr_t)arena&4095)||bytes!=QC5_CAPTURE_RAM_END-QC5_CAPTURE_RAM_BASE||
    !io||!io->read||!io->write||!io->pause)return 0;
 struct qc5_helix_layout layout;if(!qc5_helix_layout(1280,720,&layout))return 0;
 for(unsigned a=0;a<H_AREA_COUNT;a++){
  const struct qc5_helix_region *p=&layout.region[a];
  memset(arena+(p->start-QC5_CAPTURE_RAM_BASE),0,p->bytes);
 }
 if(!encoder_guards(arena,&layout,1))return 0;
 *s=(struct qp_helix){.layout=layout,.io=*io,.arena=arena,.initialized=1};return 1;
}
static int qp_helix_fail(struct qp_helix *s)
{if(s)s->failed=1;return 0;}
static int qp_helix_encode(struct qp_helix *s,const struct qc5_helix_picture *picture,struct qc5_encoded_output *out)
{
 if(!s||!out)return 0;memset(out,0,sizeof *out);
 if(!s->initialized||s->failed||!encoder_guards(s->arena,&s->layout,0))return qp_helix_fail(s);
 uint32_t *desc=(uint32_t *)(s->arena+(s->layout.region[H_DESCRIPTOR].start-QC5_CAPTURE_RAM_BASE));
 uint8_t *bs=s->arena+(s->layout.region[H_BITSTREAM].start-QC5_CAPTURE_RAM_BASE);
 uint8_t *au=s->arena+(QC5_HELIX_AU_PHYS-QC5_CAPTURE_RAM_BASE);
 struct qc5_helix_plan plan;
 if(!qc5_helix_build_picture(&plan,picture,desc,4096,bs))return qp_helix_fail(s);
 s->pairs=plan.pairs;s->descriptor_hash=plan.descriptor_hash;s->order_hash=plan.order_hash;s->input_bank=picture->input_bank;
 memset(&s->result,0,sizeof s->result);__sync_synchronize();
 int ok=helix_encode(&s->io,&plan,desc,&s->result);__sync_synchronize();
 if(!ok||s->result.reset_mask!=3||!encoder_guards(s->arena,&s->layout,0))return qp_helix_fail(s);
 int bytes=qc5_helix_annexb(&plan,bs,bs+256,s->result.bytes,au,QC5_HELIX_AU_BYTES);
 if(bytes<=0)return qp_helix_fail(s);
 *out=(struct qc5_encoded_output){au,(unsigned)bytes,s->result.reset_mask};return 1;
}
/* Never permit a retry over an active or unconfirmed failed DMA engine. */
static int qp_helix_stopped(struct qp_helix *s,unsigned verified_idle)
{
 if(!s||!s->initialized||verified_idle!=1)return 0;
 memset(s,0,sizeof *s);return 1;
}
#endif

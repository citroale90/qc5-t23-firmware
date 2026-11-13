/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_HELIX_RECORD_H
#define QC5_HELIX_RECORD_H
#include <stdint.h>
#define QC5_H264_MAGIC UINT32_C(0x37334348) /* HC37 */
enum {H_MAGIC,H_VERSION,H_STAGE,H_CAPTURE_STATUS,H_COMPLETED,H_CAPTURE_STOP,H_INPUT_BANK,
 H_PAIRS,H_DESC_HASH,H_ORDER_HASH,H_CP0_BEFORE,H_CP0_AFTER,H_STATUS,H_ERRORS,H_POLLS,H_CABAC_BYTES,
 H_AU_BYTES,H_AU_HASH,H_GUARDS,H_RESET_MASK,H_FAULT_REG,H_FAULT_VALUE,H_SAVED,H_WIDTH,H_HEIGHT,H_QP,
 H_Y_HASH,H_UV_HASH,H_GAIN,H_EXPOSURE,H_DESCRIPTOR_PHYS,H_CP0_REQUESTED,H_META_COUNT};
struct qc5_helix_record {uint32_t meta[32];uint8_t prefix[256];};
_Static_assert(sizeof(struct qc5_helix_record)==384,"Encoder proof fits existing checkpoint");
static int qc5_helix_success(const struct qc5_helix_record *p)
{
 const uint32_t *m=p->meta;
 return m[H_MAGIC]==QC5_H264_MAGIC&&m[H_VERSION]==2&&m[H_STAGE]==8&&
  !m[H_CAPTURE_STATUS]&&m[H_COMPLETED]==120&&m[H_CAPTURE_STOP]==31&&m[H_INPUT_BANK]<2&&
  m[H_PAIRS]==1015&&m[H_ORDER_HASH]==0xe734d389u&&m[H_DESC_HASH]==(m[H_INPUT_BANK]?0xf6deb587u:0xfb46393fu)&&
  m[H_CP0_REQUESTED]==((m[H_CP0_BEFORE]|0x340u)&0x3ffu)&&!(m[H_STATUS]&((1u<<2)|(1u<<7)|(1u<<10)|(1u<<20)))&&
  (m[H_STATUS]&1)&&!m[H_ERRORS]&&m[H_POLLS]>0&&m[H_POLLS]<=2000&&
  m[H_CABAC_BYTES]>0&&m[H_CABAC_BYTES]<=512u*1024u&&
  m[H_AU_BYTES]>m[H_CABAC_BYTES]&&m[H_AU_BYTES]<=786944&&m[H_AU_HASH]&&
  m[H_GUARDS]==1&&m[H_RESET_MASK]==3&&!m[H_FAULT_REG]&&!m[H_FAULT_VALUE]&&
  m[H_WIDTH]==1280&&m[H_HEIGHT]==720&&m[H_QP]==30&&m[H_DESCRIPTOR_PHYS]==0x1baa000&&
  p->prefix[0]==0&&p->prefix[1]==0&&p->prefix[2]==0&&p->prefix[3]==1&&p->prefix[4]==0x67;
}
#endif

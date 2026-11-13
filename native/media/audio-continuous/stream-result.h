/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit process boundary, no device implementation. Padding-independent
 *80-byte final record, decoded identically by the separately licensed peer.
 */
#ifndef QC5_AUDIO_STREAM_RESULT_H
#define QC5_AUDIO_STREAM_RESULT_H
#include "stream-ipc.h"
struct qas_stream_result {
 uint64_t periods,delivered,discarded,samples,first_us,last_us,polls;
 uint32_t pcm_hash,guards,stopped,hardware_fault,owner_fault,safe_to_release;
};
static void qasi_result_pack(uint8_t out[80],const struct qas_stream_result *r)
{
 const uint64_t wide[7]={r->periods,r->delivered,r->discarded,r->samples,r->first_us,r->last_us,r->polls};
 const uint32_t word[6]={r->pcm_hash,r->guards,r->stopped,r->hardware_fault,r->owner_fault,r->safe_to_release};
 for(unsigned i=0;i<7;i++)qasi_le64(out+i*8,wide[i]);for(unsigned i=0;i<6;i++)qasi_le32(out+56+i*4,word[i]);
}
static void qasi_result_decode(struct qas_stream_result *r,const uint8_t in[80])
{
 *r=(struct qas_stream_result){qasi_u64(in),qasi_u64(in+8),qasi_u64(in+16),qasi_u64(in+24),qasi_u64(in+32),qasi_u64(in+40),qasi_u64(in+48),
  qasi_u32(in+56),qasi_u32(in+60),qasi_u32(in+64),qasi_u32(in+68),qasi_u32(in+72),qasi_u32(in+76)};
}
static int qasi_result_valid(const struct qas_stream_result *r,uint64_t periods,uint32_t pcm_hash)
{
 if(!r||!periods||periods>UINT64_MAX/1000||r->periods!=periods||r->samples!=periods*1000||
    r->delivered!=periods||r->discarded||r->pcm_hash!=pcm_hash||
    r->guards!=1||r->stopped!=1||r->hardware_fault||r->owner_fault||r->safe_to_release!=1||
    !r->first_us||r->last_us<=r->first_us||r->polls<periods||periods>UINT64_MAX/62500)return 0;
 uint64_t nominal=periods*62500,elapsed=r->last_us-r->first_us,slack=300000+nominal/1000;
 return nominal>elapsed?nominal-elapsed<=slack:elapsed-nominal<=slack;
}
#endif

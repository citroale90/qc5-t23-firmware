/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded peer of the separate capture process. No codec or device code;
 * consume synchronously before returning. Reported PCM hash is independently
 * accumulated from every received packet, not trusted from the final note.
 */
#ifndef QC5_AUDIO_STREAM_RECEIVER_H
#define QC5_AUDIO_STREAM_RECEIVER_H
#include "stream-result.h"
struct qas_receiver {
 uint8_t nonce[16];uint64_t periods,last_us,target_periods;
 uint32_t pcm_hash,finished,failed;
 struct qas_stream_result final;
};
typedef int (*qasi_pcm_sink)(void *,const uint8_t *,size_t,uint64_t,uint64_t);
static int qasi_receiver_start(struct qas_receiver *s,const uint8_t nonce[16],uint64_t target_periods)
{
 if(!s||!nonce||target_periods>UINT64_MAX/62500)return 0;
 memset(s,0,sizeof *s);memcpy(s->nonce,nonce,16);s->pcm_hash=2166136261u;s->target_periods=target_periods;return 1;
}
static int qasi_receiver_record(struct qas_receiver *s,const uint8_t *raw,size_t bytes,qasi_pcm_sink sink,void *opaque)
{
 if(!s||s->failed||s->finished)return 0;
 struct qas_ipc_record m;
 if(!sink||!qasi_decode(raw,bytes,s->nonce,&m)||s->periods==UINT64_MAX||
    m.serial!=s->periods+1||m.stamp<s->last_us)goto failed;
 if(m.kind==QAS_IPC_FINAL){
  qasi_result_decode(&s->final,m.payload);
  if((s->target_periods&&s->periods!=s->target_periods)||m.stamp!=s->final.last_us||
     !qasi_result_valid(&s->final,s->periods,s->pcm_hash))goto failed;
  s->finished=1;return 1;
 }
 if(s->target_periods&&s->periods>=s->target_periods)goto failed;
 if(sink(opaque,m.payload,m.bytes,m.serial,m.stamp)!=1)goto failed;
 for(unsigned i=0;i<m.bytes;i++)s->pcm_hash=(s->pcm_hash^m.payload[i])*16777619u;
 s->periods++;s->last_us=m.stamp;return 1;
failed:
 s->failed=1;return 0;
}
#endif

/* SPDX-License-Identifier: GPL-3.0-only
 * Connect the separate native microphone process to the existing WebUI
 * listen protocol. One small packet, no extra DMA ring or video buffer.
 */
#ifndef QC5_NATIVE_SERVICE_AUDIO_H
#define QC5_NATIVE_SERVICE_AUDIO_H
#include "../native-integrated/native-features/feature-owner.h"
static struct qfs_owner service_features;
static unsigned service_features_attempted;
#ifdef QC5_AUDIO_DUPLEX
/* ACKs can be produced after the enclosing video tick sampled its clock.
 * Sample after recv(), as the child already does for incoming requests.
 * Keep strict packet-age/replay checks; never accept future timestamps. */
#define QASI_EXTRA_RECORD(raw,bytes,now) qtp_record(&service_talk,raw,bytes,qc5_net_us())
#endif
#include "../audio-continuous/stream-child.h"
#ifdef QC5_AUDIO_DUPLEX
#undef QASI_EXTRA_RECORD
#endif
#include "../audio-continuous/stream-alaw.h"
/* Zero means keep the single ADC process until an explicit service stop.
 * Bounded diagnostics retain their original default. */
#ifndef QC5_NATIVE_AUDIO_TARGET_PERIODS
#define QC5_NATIVE_AUDIO_TARGET_PERIODS UINT64_C(320)
#endif
struct qc5_service_audio {
 struct qasi_child child;
 struct qc5_live_net *net;
 uint64_t epoch,deadline,periods,bytes,sent_periods,sent_bytes,no_reader,disconnects,budget_dropped;
 uint32_t alaw_hash,sent_hash,failure_stage;
 uint8_t packet[QAS_PERIOD_SAMPLES+2];
};
static struct qc5_service_audio service_audio;
static int qsa_receive(void *opaque,const uint8_t *pcm,size_t bytes,uint64_t serial,uint64_t stamp)
{
 struct qc5_service_audio *s=opaque;
 if(!s||serial!=s->periods+1||s->bytes>UINT64_MAX-QAS_PERIOD_SAMPLES||
    qas_alaw_packet(pcm,bytes,s->packet+1,sizeof s->packet-1)!=QAS_PERIOD_SAMPLES+1)return 0;
 s->packet[0]=1;
 (void)qfs_audio(&service_features,s->packet+2,QAS_PERIOD_SAMPLES,stamp);
 if(!s->periods)s->alaw_hash=s->sent_hash=2166136261u;
 for(unsigned i=2;i<sizeof s->packet;i++)s->alaw_hash=(s->alaw_hash^s->packet[i])*16777619u;
 s->periods++;s->bytes+=QAS_PERIOD_SAMPLES;
#ifdef QC5_AUDIO_DUPLEX
 if(service_talk.initialized)service_talk.child_ready=1;
 /* Preserve recording and raw ADC diagnostics above. Reuse the single
  * packet for live PCMA silence; RTP/WS sizes and timestamps never change. */
 if(qtp_live_muted(&service_talk,stamp))memset(s->packet+2,0xd5,QAS_PERIOD_SAMPLES);
#endif
 qrt_audio(s->packet+2,QAS_PERIOD_SAMPLES,stamp);
 struct qc5_live_net *net=s->net;
 /* MIME and the first video init segment must precede audio. A raw H.264
  * reader is also permitted; it must never receive WebSocket audio bytes. */
 if(!net||net->client<0||net->stream!=2||!s->epoch||service_web.net!=net||
    service_web.epoch!=s->epoch||!service_web.frames){s->no_reader++;return 1;}
 uint64_t now=qc5_net_us();
 /* Processing/recording or preemption can exhaust the group's budget
  * before this message starts. No WebSocket byte has been emitted yet:
  * omit this listen packet without truncating or closing the video stream.
  * A partial header/body still requires the existing consumer close below.
  * The original absolute ten-ms group deadline is never renewed. */
 if(now&&now>=s->deadline){
  if(s->budget_dropped==UINT64_MAX)return 0;
  s->budget_dropped++;return 1;
 }
 uint8_t header[4]={0x82,126,(uint8_t)(sizeof s->packet>>8),(uint8_t)sizeof s->packet};
 if(!now||now>=s->deadline||
    !qc5_net_write_pair_until(net,header,sizeof header,s->packet,sizeof s->packet,0,s->deadline)){
  /* A partially sent message invalidates only this consumer. Never leave
   * a truncated audio frame before the next video frame on the socket. */
  s->disconnects++;qc5_net_close(net);return 1;
 }
 s->sent_periods++;s->sent_bytes+=QAS_PERIOD_SAMPLES;
 for(unsigned i=2;i<sizeof s->packet;i++)s->sent_hash=(s->sent_hash^s->packet[i])*16777619u;
 return 1;
}
static int qsa_start(struct qc5_service_audio *s,const uint8_t nonce[16],uint64_t now)
{return s&&qasi_child_start(&s->child,nonce,QC5_NATIVE_AUDIO_TARGET_PERIODS,now,"/bin/qc5-audio-stream");}
static int qsa_tick(struct qc5_service_audio *s,struct qc5_live_net *net,uint64_t epoch,uint64_t now)
{
 if(!s)return 0;s->failure_stage=0;if(!s->child.started)return 1;
 if(!now||now>UINT64_MAX-10000){s->failure_stage=1;return 0;}
 s->net=net;s->epoch=epoch;s->deadline=now+10000;
#ifdef QC5_AUDIO_DUPLEX
 if(!service_talk.initialized){
  int capacity=4096;
  if(s->child.fd<0||setsockopt(s->child.fd,SOL_SOCKET,SO_SNDBUF,&capacity,sizeof capacity)||
     !qtp_init(&service_talk,s->child.fd,s->child.receiver.nonce,30,qsw_talk_reply,NULL)){s->failure_stage=2;return 0;}
 }
 if(!qtp_observe(&service_talk,epoch,net&&net->client>=0&&net->stream==2,now)){s->failure_stage=3;return 0;}
#endif
 /* The entire group of at most eight audio records shares ten ms, so a
  * stalled browser cannot consume a fresh deadline for each packet. */
 int ok=qasi_child_tick(&s->child,now,qsa_receive,s);if(!ok)s->failure_stage=4;
#ifdef QC5_AUDIO_DUPLEX
 if(s->child.receiver.finished)service_talk.child_ready=0;
#endif
 return ok;
}
#ifdef QC5_AUDIO_DUPLEX
static void qsa_failure_note(const struct qc5_service_audio *s)
{
 char text[512];
 int n=snprintf(text,sizeof text,"<6>QC5_AUDIO_FAILURE stage=%u child=%u receiver=%u reaped=%u result=%d periods=%llu talk_phase=%u talk_error=%u ipc_fault=%u kind=%u id=%u serial=%llu packet_error=%u stamp=%llu received=%llu origin=%u write_errno=%u bad_now=%llu previous=%llu\n",
 s->failure_stage,s->child.failed,s->child.receiver.failed,s->child.reaped,s->child.result,(unsigned long long)s->child.receiver.periods,
 service_talk.phase,service_talk.error,service_talk.inbox.fault,service_talk.last_record_kind,service_talk.last_record_id,
 (unsigned long long)service_talk.last_record_serial,service_talk.last_record_error,(unsigned long long)service_talk.last_record_stamp,(unsigned long long)service_talk.last_record_now,service_talk.fault_reason,service_talk.last_write_errno,(unsigned long long)service_talk.bad_clock_now,(unsigned long long)service_talk.bad_clock_previous);
 /* PID1 persists this inherited pipe; /dev/kmsg alone is not tailed live. */
 if(n>0&&n<(int)sizeof text)(void)write(STDERR_FILENO,text,(unsigned)n);
 const struct qas_stream_result *r=&s->child.receiver.final;
 n=snprintf(text,sizeof text,"QC5_MIC_PEER_FINAL periods=%llu delivered=%llu discarded=%llu fault=%u owner=%u guards=%u stopped=%u release=%u\n",
  (unsigned long long)r->periods,(unsigned long long)r->delivered,(unsigned long long)r->discarded,
  r->hardware_fault,r->owner_fault,r->guards,r->stopped,r->safe_to_release);
 if(n>0&&n<(int)sizeof text)(void)write(STDERR_FILENO,text,(unsigned)n);
}
#define QSA_FAILURE_NOTE(s) qsa_failure_note(s)
#endif
static int qsa_finish(struct qc5_service_audio *s,struct qc5_live_net *net,uint64_t epoch)
{
 uint64_t start=qc5_net_us();
 if(!s||!s->child.started||!start)return 0;
 if(!s->child.receiver.finished)qasi_child_stop(&s->child);
 for(unsigned n=0;n<2000&&!qasi_child_finished(&s->child);n++){
  uint64_t now=qc5_net_us();if(!now||now<start||now-start>2000000||!qsa_tick(s,net,epoch,now))break;
  usleep(1000);
 }
 return qasi_child_finished(&s->child)&&s->periods&&s->periods<=UINT64_MAX/QAS_PERIOD_SAMPLES&&
  s->periods==s->child.receiver.periods&&s->bytes==s->periods*QAS_PERIOD_SAMPLES&&
  (!QC5_NATIVE_AUDIO_TARGET_PERIODS||s->periods==QC5_NATIVE_AUDIO_TARGET_PERIODS);
}
#endif

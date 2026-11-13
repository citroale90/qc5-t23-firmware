/* SPDX-License-Identifier: GPL-2.0-only
 * Opt-in capture-process adapter: private IPC and strictly bounded writes.
 * The caller supplies raw mapped writes, which are reachable only through
 * these DAC/TX whitelists. No clocks, GPIO, global DMA or RX writes here.
 */
#ifndef QC5_DUPLEX_SIDECAR_H
#define QC5_DUPLEX_SIDECAR_H
#include <fcntl.h>
#include <unistd.h>
#include "duplex-capture.generated.h"
#include "talk-requests.h"
struct qds_sidecar {
 uint64_t halt_reported;
 struct qdu_control control;struct qtr_bridge bridge;struct qas_sidecar hooks;
 struct qas_capture *capture;uint8_t nonce[16],request[QTIP_MAX];
 int fd;unsigned gain,seconds,initialized,closing,error_logged;
 int (*raw_write)(void *,unsigned,uint32_t,uint32_t);
};
static uint32_t qds_tx_read(void *opaque,unsigned region,unsigned off)
{
 struct qds_sidecar *s=opaque;
 int valid=region==QDT_AIC?(off==0||off==4||off==0x14):region==QDT_PDMA?
  (off==0x1000||(off<32u*32u&&(off%32u)==0x10)||(off>=QDT_CH&&off<=QDT_CH+0x18&&!(off&3))):0;
 if(!valid)return UINT32_MAX;
 return s->capture->hw.io.read(s->capture->hw.io.opaque,region==QDT_AIC?QA_AIC:QA_PDMA,off);
}
static int qds_tx_write(void *opaque,unsigned region,unsigned off,uint32_t value)
{
 struct qds_sidecar *s=opaque;int valid=0;
 if(region==QDT_AIC){
  valid=off==0?value==0x08100031u:off==4?
   value==0x19001u||value==0x99801u||value==0x99901u||value==0x9d803u:off==0x14?value==~0x20u:0;
 }else if(region==QDT_PDMA){
  if(off==0x1000){if(value!=0x003f0001u||!s->control.tx.TX_start_attempted||!s->control.tx.stopped||qds_tx_read(s,QDT_PDMA,0x1000)!=0x003f0009u)return 0;return s->raw_write(s->capture->hw.io.opaque,QA_PDMA,off,value);}
  valid=off==QDT_CH+0xc?value==6:off==QDT_CH+8?value>=40&&value<=160&&!(value%40):
   off==QDT_CH+0x10?value==0||value==0x40000000u||value==0x40000001u:
   off==QDT_CH+0x18?value==QAD_DESCRIPTOR:off==0x100c?value==1u<<6:0;
 }
 if(!valid)return 0;
 return s->raw_write(s->capture->hw.io.opaque,region==QDT_AIC?QA_AIC:QA_PDMA,off,value);
}
static uint32_t qds_codec_read(void *opaque,uint32_t off)
{
 struct qds_sidecar *s=opaque;
 if(off!=0x80&&off!=0x84&&off!=0x8c&&off!=0x98&&off!=0x9c&&off!=0xa0)return UINT32_MAX;
 return s->capture->hw.io.read(s->capture->hw.io.opaque,QA_CODEC,off);
}
static int qds_codec_write(void *opaque,uint32_t off,uint32_t value)
{
 struct qds_sidecar *s=opaque;uint32_t mask=off==0x80?7:off==0x84?127:off==0x8c?15:off==0x98?15:off==0x9c?0xf0:off==0xa0?255:0;
 if(!mask||((qds_codec_read(s,off)^value)&~mask))return 0;
 return s->raw_write(s->capture->hw.io.opaque,QA_CODEC,off,value);
}
static uint64_t qds_time(void *opaque)
{struct qds_sidecar *s=opaque;return s->capture->hw.io.time_us(s->capture->hw.io.opaque);}
static void qds_pause(void *opaque,unsigned us)
{struct qds_sidecar *s=opaque;s->capture->hw.io.delay_us(s->capture->hw.io.opaque,us);}
static int qds_start(void *opaque,struct qas_capture *capture)
{
 struct qds_sidecar *s=opaque;if(s->initialized||!capture)return 0;s->capture=capture;
 struct qdt_io tx={s,qds_tx_read,qds_tx_write,qds_time,qds_pause};
 struct qac_io codec={s,qds_codec_read,qds_codec_write};
 if(!qdu_init(&s->control,&tx,&codec,capture->hw.io.ram,4096,s->seconds,s->gain)||!qtr_init(&s->bridge,&s->control,s->nonce))return 0;
 s->initialized=1;return 1;
}
static int qds_send_pending(struct qds_sidecar *s)
{
 if(!s->bridge.pending_bytes)return 1;
 ssize_t sent=send(s->fd,s->bridge.pending,s->bridge.pending_bytes,MSG_DONTWAIT|MSG_NOSIGNAL);
 if(sent<0&&(errno==EAGAIN||errno==EWOULDBLOCK))return 1;
 return sent>0&&qtr_sent(&s->bridge,(size_t)sent);
}
static int qds_poll(void *opaque,uint64_t observed_us)
{
 struct qds_sidecar *s=opaque;(void)observed_us;if(!s->initialized||s->closing)return 0;
 /* Four bounded packets per poll leave the125ms microphone consumer and
  * its20ms copy lease independent of input bursts or a stalled browser. */
 for(unsigned n=0;n<4&&!s->control.error;n++){
  size_t bytes;int got=qtip_receive(s->fd,s->request,&bytes);if(!got)break;
  uint64_t now=qds_time(s);
  if(got<0||!qtr_request(&s->bridge,s->request,bytes,now)){
   if(!s->control.error)(void)qdu_fail(&s->control,QDU_QUEUE,now);break;
  }
 }
 int active=qtr_poll(&s->bridge,qds_time(s));
 if(s->control.tx.halt_acks!=s->halt_reported&&(s->control.tx.halt_acks<=3||s->control.phase==QDU_OFF)){
  char text[240];int n=snprintf(text,sizeof text,"QC5_TALK_DMA_HALT_ACK count=%llu TX_stopped=%u validated_RX_at_ack=1 global_reset=0 completion_reobserved=%llu\n",(unsigned long long)s->control.tx.halt_acks,s->control.tx.stopped,(unsigned long long)s->control.tx.completion_reobserved);
  if(n>0&&n<(int)sizeof text)(void)write(STDERR_FILENO,text,(unsigned)n);s->halt_reported=s->control.tx.halt_acks;
 }

 if(s->control.error&&!s->error_logged){s->error_logged=1;
  {char detail[420];
   int n=snprintf(detail,sizeof detail,"QC5_TALK_TX_SNAPSHOT error=%u primed=%u dcs=%x source=%x again=%x count=%x target=%x command=%x fifo=%x started=%llu observed=%llu nominal=%llu\n",
    s->control.tx.error,s->control.tx.primed,s->control.tx.last_dcs,s->control.tx.last_source,s->control.tx.last_again,s->control.tx.last_count,s->control.tx.last_target,s->control.tx.last_command,s->control.tx.last_fifo,
    (unsigned long long)s->control.tx.started_us,(unsigned long long)s->control.tx.last_us,(unsigned long long)s->control.tx.nominal_us);
   if(n>0&&n<(int)sizeof detail)(void)write(STDERR_FILENO,detail,(unsigned)n);
  }
  {char text[768];
   int n=snprintf(text,sizeof text,"<6>QC5_TALK_CHILD_FAULT error=%u phase=%u tx_error=%u codec_error=%u tx_stopped=%u codec_phase=%u queue_clock=%u ipc_fault=%u received=%llu dropped=%llu batches=%llu initial_tur=%llu halt_acks=%llu rx_fault=%x rx_control=%x rx_dcs=%x rx_status=%x ack_phase=%u ack_before=%x ack_after=%x ack_dcs=%x setup=%x global_dma=%x tx_dcs=%x frame=%x control=%x now=%llu tx_last=%llu codec_last=%llu\n",
    s->control.error,s->control.phase,s->control.tx.error,s->control.codec.error,s->control.tx.stopped,s->control.codec.phase,
    s->control.queue.clock_failed,s->bridge.inbox.fault,(unsigned long long)s->bridge.voice_received,(unsigned long long)s->bridge.voice_dropped,
    (unsigned long long)s->control.tx.completed_batches,(unsigned long long)s->control.tx.startup_underruns,(unsigned long long)s->control.tx.halt_acks,s->control.tx.rx_fault,s->control.tx.rx_control,s->control.tx.rx_dcs,s->control.tx.rx_status,s->control.tx.ack_phase,s->control.tx.ack_before,s->control.tx.ack_after,s->control.tx.ack_dcs,s->control.tx.setup_fault,s->control.tx.setup_dmac,s->control.tx.setup_dcs,s->control.tx.setup_frame,s->control.tx.setup_control,(unsigned long long)qds_time(s),(unsigned long long)s->control.tx.last_us,(unsigned long long)s->control.codec.last_us);
   /* Inherited stderr is captured and synced by the RAM trial logger. */
   if(n>0&&n<(int)sizeof text)(void)write(STDERR_FILENO,text,(unsigned)n);}
 }
 if(!qds_send_pending(s)){
  if(!s->control.error)(void)qdu_fail(&s->control,QDU_QUEUE,qds_time(s));
  return s->control.phase==QDU_STOPPING;
 }
 return active!=0;
}
static int qds_stop(void *opaque,uint64_t now)
{
 struct qds_sidecar *s=opaque;
 if(!s->initialized){
  return (!s->control.tx.owned||s->control.tx.stopped)&&(!s->control.codec.owned||s->control.codec.phase==QAC_OFF)?1:0;
 }
 if(s->control.tx.stopped&&s->control.codec.phase==QAC_OFF&&!s->control.queue.held)return 1;
 if(!s->closing){s->closing=1;if(!qdu_shutdown(&s->control,now))return 0;}
 (void)qtr_poll(&s->bridge,now);(void)qds_send_pending(s);
 if(s->control.tx.stopped&&s->control.codec.phase==QAC_OFF&&!s->control.queue.held)return 1;
 return s->control.phase==QDU_STOPPING?2:0;
}
static int qds_owns_guard(void *opaque,uint32_t address)
{(void)opaque;return qad_owns_address(address);}
static int qds_guards(void *opaque)
{
 struct qds_sidecar *s=opaque;if(!s->capture)return 0;
 if(s->control.tx.frames)return qdt_memory(&s->control.tx)&&qdt_hash(&s->control.tx)==s->control.tx.hash;
 for(unsigned off=QAD_DESCRIPTOR-QA_BASE;off<QAD_END-QA_BASE;off++)if(s->capture->hw.io.ram[off]!=QA_GUARD)return 0;
 return 1;
}
static int qds_attach(struct qds_sidecar *s,struct qas_capture *capture,int fd,const uint8_t nonce[16],
                      int (*raw_write)(void *,unsigned,uint32_t,uint32_t),unsigned seconds,unsigned gain)
{
 if(!s||!capture||capture->sidecar||s->capture||s->hooks.start||!nonce||fd<0||!raw_write||seconds<1||seconds>30||gain>31)return 0;
 s->fd=fd;s->raw_write=raw_write;s->gain=gain;s->seconds=seconds;memcpy(s->nonce,nonce,16);
 s->hooks=(struct qas_sidecar){s,qds_start,qds_poll,qds_stop,qds_owns_guard,qds_guards};
 capture->sidecar=&s->hooks;return 1;
}
#endif

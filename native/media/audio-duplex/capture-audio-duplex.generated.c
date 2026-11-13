/* SPDX-License-Identifier: GPL-2.0-only
 * Opt-in native microphone/intercom process. Private inherited IPC;
 * no LAN listener, flash, boot settings or watchdog refresh. DAC output requires a valid private START and settled power state.
 * RAM validation lasts twenty seconds inside the existing video watchdog.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <time.h>
#include "duplex-sidecar.h"
#include "../audio-continuous/stream-process.h"
#include "../audio-continuous/stream-result.h"
#include "../audio-continuous/stream-hardware.generated.h"
#include QC5_LIVE_NONCE_HEADER
#ifndef QC5_AUDIO_CAPTURE_PERIODS
#define QC5_AUDIO_CAPTURE_PERIODS UINT64_C(320)
#endif
static struct qas_capture capture;
static struct qds_sidecar intercom;
/* Raw stores are not exposed to the network/parent. Only the sidecar's
 * field/value whitelist can call this second offset-level fence. */
static int duplex_raw_store(void *opaque,unsigned region,uint32_t off,uint32_t value)
{
 struct hardware *h=opaque;
 int valid=region==QA_AIC?(off==0||off==4||off==0x14):region==QA_PDMA?
  (off==QDT_CH+8||off==QDT_CH+0xc||off==QDT_CH+0x10||off==QDT_CH+0x18||off==0x100c):
  region==QA_CODEC?(off==0x80||off==0x84||off==0x8c||off==0x98||off==0x9c||off==0xa0):0;
 if(region==QA_PDMA&&off==0x1000){
  if(value!=0x003f0001u||!intercom.control.tx.owned||!intercom.control.tx.stopped||
     !intercom.control.tx.TX_start_attempted||h->reg[QA_PDMA][off/4]!=0x003f0009u)return 0;
 }else if(!valid)return 0;
 __sync_synchronize();h->reg[region][off/4]=value;__sync_synchronize();return 1;
}
static struct qasi_sender sender;
static void capture_signal(int signum){(void)signum;capture.stop_requested=1;}
static int stream_self_test(void)
{
 struct qas_descriptor desc[QAS_PERIODS];uint8_t payload[QAS_IPC_FINAL_BYTES]={0},wire[QAS_IPC_HEADER+QAS_IPC_FINAL_BYTES];
 struct qas_ipc_record record;
 return qas_descriptors(desc,QAS_PERIODS)&&qas_descriptors_valid(desc,QAS_PERIODS)&&
  qasi_pack(wire,sizeof wire,QAS_IPC_FINAL,payload,sizeof payload,1,1,qc5_live_nonce)==sizeof wire&&
  qasi_decode(wire,sizeof wire,qc5_live_nonce,&record)&&record.kind==QAS_IPC_FINAL;
}
int main(int argc,char **argv)
{
 if(argc==2&&!strcmp(argv[1],"--cpu-self-test")){
  if(!stream_self_test())return 1;
  printf("QC5_AUDIO_STREAM_SELFTEST periods=%u pcm_bytes=%u descriptor_bytes=%u packet_bytes=%u capture_state_bytes=%zu sender_bytes=%zu\n",
   QAS_PERIODS,QAS_PCM_BYTES,QAS_DESC_BYTES,QAS_IPC_MAX,sizeof capture,sizeof sender);
  uint8_t raw[QTIP_HEADER+4],seconds[4];struct qtip_record command;
  qasi_le32(seconds,30);
  int bytes=qtip_pack(raw,sizeof raw,QTIP_START,1,1,1,qc5_live_nonce,seconds,4);
  if(bytes!=sizeof raw||!qtip_decode(raw,bytes,qc5_live_nonce,&command)||command.kind!=QTIP_START||!qad_layout_valid())return 1;
  printf("QC5_AUDIO_DUPLEX_SELFTEST sidecar_bytes=%zu ipc_max=%u reserved_extra=0 ready_for_hardware=0\n",sizeof intercom,QTIP_MAX);
  return 0;
 }
 struct utsname u;
 if(argc!=2||strcmp(argv[1],"--capture-during-video")||geteuid()!=0||uname(&u)||
    strncmp(u.release,"7.3.1-",6)||!qasi_private_socket(3))return 77;
 sender.fd=3;memcpy(sender.nonce,qc5_live_nonce,16);
 struct sigaction sa={.sa_handler=capture_signal};sigemptyset(&sa.sa_mask);
 if(sigaction(SIGTERM,&sa,NULL)||sigaction(SIGINT,&sa,NULL))return 78;
 signal(SIGPIPE,SIG_IGN);
 struct hardware h={0};capture.hw.io=(struct qa_io){&h,read_hw,write_hw,time_us,pause_us,NULL};
 const uint32_t base[]={0x10000000,0x10020000,0x10021000,0x13420000};
 const unsigned size[]={4096,4096,4096,8192};int fd=-1,mapped=0,ok=0,sent=0;
 fd=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);if(fd<0)goto done;
 if(!video_mode(fd,1)){qa_fail(&capture.hw,QA_BUSY,QA_AIC,0x133e0300);goto done;}
 for(unsigned n=0;n<4;n++){
  h.reg[n]=mmap(NULL,size[n],PROT_READ|PROT_WRITE,MAP_SHARED,fd,base[n]);
  if(h.reg[n]==MAP_FAILED){qa_fail(&capture.hw,QA_MMIO,n,base[n]);goto done;}mapped++;
 }
 capture.hw.io.ram=mmap(NULL,QA_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,fd,QA_BASE);
 if(capture.hw.io.ram==MAP_FAILED){capture.hw.io.ram=NULL;goto done;}
 for(unsigned n=0;n<QA_SIZE;n++)if(capture.hw.io.ram[n]!=QA_GUARD){
  qa_fail(&capture.hw,QA_GUARD_ERROR,QA_PDMA,QA_BASE+n);goto done;
 }
 if(!qds_attach(&intercom,&capture,sender.fd,qc5_live_nonce,duplex_raw_store,30,24))goto done;
 ok=qas_stream_capture(&capture,QC5_AUDIO_CAPTURE_PERIODS,qasi_process_pcm,&sender);
done:
 /* The core normally stops itself; partial mapping never owns the device.
  * Never advertise a successful release while PDMA or RX is still active. */
 if(capture.hw.owned&&!capture.result.stopped)qa_stop(&capture.hw);
 struct qas_stream_result result={capture.result.periods,capture.result.delivered,capture.result.discarded,
  capture.result.samples,capture.result.first_us,capture.result.last_us,capture.result.polls,
  capture.result.pcm_hash,capture.result.guards,capture.result.stopped,capture.hw.fault,
  capture.owner.error,capture.result.safe_to_release};
 uint8_t payload[QAS_IPC_FINAL_BYTES];qasi_result_pack(payload,&result);
 uint64_t now=time_us(NULL);
 int bytes=qasi_pack(sender.record,sizeof sender.record,QAS_IPC_FINAL,payload,sizeof payload,
  result.periods+1,result.last_us?result.last_us:now,qc5_live_nonce);
 if(bytes>0)sent=qasi_send(sender.fd,sender.record,(size_t)bytes);
 char line[896];int len=snprintf(line,sizeof line,"<6>QC5_AUDIO_STREAM_RESULT ok=%d sent=%d periods=%llu delivered=%llu discarded=%llu samples=%llu hash=%08x first_us=%llu last_us=%llu guards=%u stopped=%u fault=%u owner_fault=%u release=%u min=%d max=%d nonzero=%llu transitions=%llu clips=%llu configured_dcs=%08x first_dcs=%08x last_dcs=%08x last_target=%08x last_count=%08x last_again=%08x last_fifo=%08x last_command=%08x last_dda=%08x last_dmac=%08x\n",
  ok,sent,(unsigned long long)result.periods,(unsigned long long)result.delivered,(unsigned long long)result.discarded,
  (unsigned long long)result.samples,result.pcm_hash,(unsigned long long)result.first_us,(unsigned long long)result.last_us,
  result.guards,result.stopped,result.hardware_fault,result.owner_fault,result.safe_to_release,capture.minimum,capture.maximum,
  (unsigned long long)capture.nonzero,(unsigned long long)capture.transitions,(unsigned long long)capture.clips,
  capture.configured_dcs,capture.first_dcs,capture.last_dcs,capture.last_target,capture.last_count,capture.last_again,
  capture.last_fifo,capture.last_command,capture.last_dda,capture.last_dmac);
 int log=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
 if(log>=0){if(len>0&&(size_t)len<sizeof line)(void)write(log,line,(size_t)len);close(log);}
 len=snprintf(line,sizeof line,"QC5_MIC_FINAL ok=%d sent=%d periods=%llu delivered=%llu discarded=%llu fault=%u owner=%u guards=%u stopped=%u release=%u copy_retries=%llu dcs=%08x target=%08x count=%08x fifo=%08x\n",
  ok,sent,(unsigned long long)result.periods,(unsigned long long)result.delivered,(unsigned long long)result.discarded,
  result.hardware_fault,result.owner_fault,result.guards,result.stopped,result.safe_to_release,(unsigned long long)capture.copy_retries,
  capture.last_dcs,capture.last_target,capture.last_count,capture.last_fifo);
 if(len>0&&(size_t)len<sizeof line)(void)write(STDERR_FILENO,line,(size_t)len);
 char talk_line[896];int talk_len=snprintf(talk_line,sizeof talk_line,
  "<6>QC5_INTERCOM_RESULT initialized=%u phase=%u error=%u tx_error=%u codec_error=%u tx_stopped=%u codec_phase=%u codec_muted=%u sidecar_safe=%u voice_received=%llu voice_dropped=%llu batches=%llu acknowledged=%llu tx_dcs=%08x tx_source=%08x tx_again=%08x tx_count=%08x tx_fifo=%08x tx_command=%08x tx_target=%08x tx_dda=%08x codec_step=%u tx_started=%llu tx_last=%llu\n",
  intercom.initialized,intercom.control.phase,intercom.control.error,intercom.control.tx.error,intercom.control.codec.error,
  intercom.control.tx.stopped,intercom.control.codec.phase,intercom.control.codec.muted,capture.sidecar_safe,
  (unsigned long long)intercom.bridge.voice_received,(unsigned long long)intercom.bridge.voice_dropped,
  (unsigned long long)intercom.control.tx.completed_batches,(unsigned long long)intercom.bridge.last_sent,
  intercom.control.tx.last_dcs,intercom.control.tx.last_source,intercom.control.tx.last_again,intercom.control.tx.last_count,
  intercom.control.tx.last_fifo,intercom.control.tx.last_command,intercom.control.tx.last_target,intercom.control.tx.last_dda,
  intercom.control.codec.index,(unsigned long long)intercom.control.tx.started_us,(unsigned long long)intercom.control.tx.last_us);
 log=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
 if(log>=0){if(talk_len>0&&(size_t)talk_len<sizeof talk_line)(void)write(log,talk_line,(size_t)talk_len);close(log);}
 if(capture.hw.io.ram)munmap((void*)capture.hw.io.ram,QA_SIZE);
 for(int n=0;n<mapped;n++)munmap((void*)h.reg[n],size[n]);if(fd>=0)close(fd);close(sender.fd);
 return ok&&sent&&(!QC5_AUDIO_CAPTURE_PERIODS||result.periods==QC5_AUDIO_CAPTURE_PERIODS)&&
  qasi_result_valid(&result,result.periods,result.pcm_hash)?0:1;
}

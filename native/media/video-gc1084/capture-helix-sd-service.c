/* SPDX-License-Identifier: GPL-3.0-only
 * Native service RAM validation. Uses a time/stop lifecycle, not a raised
 * finite frame count. Fixed external60s watchdog; no flash/env writes.
 * The distinct schema20 preserves all attempted schema19 images unchanged.
 */
#define QC5_CAPTURE_SERVICE
#define QC5_EXTERNAL_LIVE_MAIN
#include "capture-helix-sd-runtime.c"
#include "../video-runtime/native-service-note.h"
#ifndef QC5_SERVICE_SCHEMA
#define QC5_SERVICE_SCHEMA 20
#endif
static void service_signal(int signum){(void)signum;service_capture.stop_requested=1;}
int main(int argc,char **argv)
{
 if(argc==2&&!strcmp(argv[1],"--cpu-self-test")){
  if(!qc5_memory_test(&memory_proof))return CAPTURE_MAP;
  printf("QC5_MEMORY_SELFTEST bytes=%u hash=%08x total_kb=%u flags=%u\n",memory_proof.heap_bytes,memory_proof.heap_hash,memory_proof.total_kb,memory_proof.flags);
  return cpu_self_test();
 }
 memset(&live,0,sizeof live);memset(&service_capture,0,sizeof service_capture);
 service_capture.duration_us=0; /* Production: explicit stop/failure, no diagnostic timer. */
 signal(SIGTERM,service_signal);signal(SIGINT,service_signal);signal(SIGPIPE,SIG_IGN);
 uint32_t *m=live.proof.meta;
 m[H_MAGIC]=QC5_H264_MAGIC;m[H_VERSION]=QC5_SERVICE_SCHEMA;m[H_STAGE]=1;m[H_WIDTH]=1280;m[H_HEIGHT]=720;m[H_QP]=30;m[H_WARMUP]=120;
 /* Failure to expose the single local endpoint remains visible in proof;
  * it cannot turn a capture-only run into a passed service validation. */
 if(!qc5_net_open(&live.net,0,QC5_LIVE_PORT))return CAPTURE_MAP;
 integrated_media.directory=qnf_directory(QCI_DIRECTORY);
 if(integrated_media.directory<0||!qmi_replace(integrated_media.directory,"QCM7 0 0 0\n",11)){qq_capture_stop();qrt_end();qmi_stop(&integrated_media);qc5_net_finish(&live.net);return CAPTURE_MAP;}
 qc5_netboot_sample(&live.boot);(void)qc5_beacon_open(&live.beacon,&live.boot,qc5_live_nonce);
 char *at=live.net.extra_header;at+=sprintf(at,"X-QC5-Nonce: ");
 for(unsigned n=0;n<16;n++)at+=sprintf(at,"%02x",qc5_live_nonce[n]);memcpy(at,"\r\n",3);
 memory_ok=qc5_memory_test(&memory_proof);
 (void)qrt_begin(integrated_media.directory,INADDR_ANY,8554);
 int status=nv12_capture_main();
 int fd=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);
 if(fd<0){qc5_beacon_close(&live.beacon);qq_capture_stop();qrt_end();qmi_stop(&integrated_media);qc5_net_finish(&live.net);return CAPTURE_MAP;}
 volatile uint8_t *ram=map_phys(fd,QC5_CAPTURE_RAM_BASE,QC5_CAPTURE_RAM_END-QC5_CAPTURE_RAM_BASE);close(fd);
 if(ram==MAP_FAILED){qc5_beacon_close(&live.beacon);qq_capture_stop();qrt_end();qmi_stop(&integrated_media);qc5_net_finish(&live.net);return CAPTURE_MAP;}
 struct qc5_preview record={0};memcpy(&record,(const void*)(ram+QC5_PREVIEW_PHYS-QC5_CAPTURE_RAM_BASE),512);
 const uint32_t *words=(const uint32_t*)&record;
 int inner_valid=words[0]==QC5_PREVIEW_MAGIC&&words[1]==23&&words[127]==qc5_frame_hash((const uint8_t*)&record,508);
 if(!inner_valid){memset(&record,0,sizeof record);if(!status)status=CAPTURE_FRAME;}
 struct msca_record nv12;memcpy(&nv12,record.pixels,sizeof nv12);
 m[H_CAPTURE_STATUS]=(uint32_t)status;m[H_COMPLETED]=qsn_u32(service_capture.completed);m[H_CAPTURE_STOP]=service_capture.stopped;
 m[H_GAIN]=nv12.meta[M_GAIN];m[H_EXPOSURE]=nv12.meta[M_EXPOSURE];
 m[H_SAVED]=qsn_picture(&live.service_last_picture);
 if(!m[H_FAULT_REG]&&nv12.meta[M_FAULT_ADDRESS]){m[H_FAULT_REG]=nv12.meta[M_FAULT_ADDRESS];m[H_FAULT_VALUE]=nv12.meta[M_FAULT_VALUE];}
 struct msca_state stopped={0};uint32_t bad=0;
 for(unsigned n=0;n<2;n++)(void)qc5_nv12_plan(n,1280,720,&stopped.planes[n]);
 m[H_GUARDS]=live.ready&&service_capture.finished&&m[H_CAPTURE_STOP]==31&&!record.csi_err1&&!record.csi_err2&&
  encoder_guards(ram,&live.layout,0)&&nv12_guard(ram,&stopped,&bad);
 struct qc5_service_note note={0};
 memcpy(note.meta,m,sizeof note.meta);memcpy(note.nonce,qc5_live_nonce,16);
 uint64_t *c=note.count;
 c[SN_CAPTURED]=service_capture.completed;c[SN_QUEUED]=service_capture.queued;c[SN_ENCODED]=live.producer.input.encoded;
 c[SN_PUBLISHED]=live.producer.published;c[SN_AU_BYTES]=live.producer.bytes;c[SN_CABAC_BYTES]=live.cabac_total;
 c[SN_MUX_BYTES]=service_web.mux_bytes;c[SN_POLLS]=service_capture.polls;c[SN_FIRST_US]=live.first;c[SN_LAST_US]=live.last;
 c[SN_FIRST_CAPTURE_US]=service_capture.first_frame_us;c[SN_LAST_CAPTURE_US]=service_capture.last_frame_us;
 c[SN_HOLD_US]=service_capture.hold_us;c[SN_DURATION_US]=service_capture.duration_us;c[SN_EPOCH]=live.client_epoch;
 c[SN_ACCEPTED]=live.net.accepted;c[SN_REQUESTS]=live.net.requests;c[SN_DISCONNECTS]=live.net.disconnects;
 c[SN_TIMEOUTS]=live.net.timeouts;c[SN_SENT_FRAMES]=live.net.frames;c[SN_SENT_BYTES]=live.net.bytes;
 c[SN_WS_MESSAGES]=service_web.messages;c[SN_SPATIAL_UPDATES]=active_spatial.updates;c[SN_BCSH_FRAMES]=active_bcsh.frames;
 uint32_t *e=note.extra;struct qc5_awb_note awb=qc5_awb_capture_note(&active_awb);
 e[SN_WB_PROFILE]=awb.profile_hash;e[SN_CCM_PROFILE]=active_ccm.profile?active_ccm.profile->checksum:0;
 e[SN_CCM_READ]=active_ccm.read_hash;e[SN_WB_R]=nv12.data.success.wb.values[0];e[SN_WB_B]=nv12.data.success.wb.values[1];
 /* Statistics continue while controls are held for drain. Use the CT that
  * was actually committed to CCM, not a later unapplied AWB suggestion. */
 e[SN_AWB_READ]=awb.readback;e[SN_CT]=active_ccm.ct;e[SN_SPATIAL_PROFILE]=QC5_EXPECT_SPATIAL_HASH;
 e[SN_SPATIAL_READ]=qc5_spatial_pair_digest(qc5_spatial_pair_digest(spatial_readback_hash,active_detail.read_hash),active_af_stats.read_hash);
 e[SN_BCSH_READ]=active_bcsh.read_hash;e[SN_MDNS_READ]=active_mdns.read_hash;
 unsigned sps=!live.proof.prefix[0]&&!live.proof.prefix[1]&&!live.proof.prefix[2]&&live.proof.prefix[3]==1&&live.proof.prefix[4]==0x67;
#if defined(QC5_AWB_DARK_SAFE)||defined(QC5_AWB_NO_ZONES_SAFE)
 char awb_line[360];int awb_len=snprintf(awb_line,sizeof awb_line,
  "<6>QC5_AWB_END samples=%u empty=%u frames=%u updates=%u populated=%u hash=%08x stop=%u ct=%u max_us=%u fault=%08x\n",
  active_awb.samples,active_awb.empty_samples,active_awb.frames,active_awb.updates,active_awb.populated,
  active_awb.stats_hash,active_awb.stop_mask,active_awb.ct,active_awb.max_us,active_awb.fault_reg);
 int awb_log=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
 if(awb_log>=0){if(awb_len>0&&(size_t)awb_len<sizeof awb_line)(void)write(awb_log,awb_line,(size_t)awb_len);close(awb_log);}
#endif
 unsigned audio_ok;
#ifdef QC5_AUDIO_STREAM
 audio_ok=qsa_finish(&service_audio,&live.net,live.client_epoch);
 char audio_line[480];int audio_len=snprintf(audio_line,sizeof audio_line,"<6>QC5_NATIVE_AUDIO valid=%u received=%llu encoded_bytes=%llu encoded_hash=%08x sent=%llu sent_bytes=%llu sent_hash=%08x no_reader=%llu disconnects=%llu pcm_hash=%08x child_result=%d\n",
  audio_ok,(unsigned long long)service_audio.periods,(unsigned long long)service_audio.bytes,service_audio.alaw_hash,
  (unsigned long long)service_audio.sent_periods,(unsigned long long)service_audio.sent_bytes,service_audio.sent_hash,
  (unsigned long long)service_audio.no_reader,(unsigned long long)service_audio.disconnects,
  service_audio.child.receiver.pcm_hash,service_audio.child.result);
 int audio_log=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
 if(audio_log>=0){if(audio_len>0&&(size_t)audio_len<sizeof audio_line)(void)write(audio_log,audio_line,(size_t)audio_len);close(audio_log);}
#else
 audio_ok=qc5_audio_child_finish(&live_audio);
#endif
 e[SN_FLAGS]=(sps?1:0)|(service_capture.finished?2:0)|(qc5_orientation_note_valid(&active_orientation)?4:0)|
  (audio_ok?8:0)|(memory_ok&&qc5_memory_note_valid(&memory_proof)?16:0);
 if(!status&&service_capture.finished&&service_capture.reason==QSC_REQUEST)note.meta[H_STAGE]=8;
 int valid=!status&&service_capture.finished&&service_capture.reason==QSC_REQUEST&&m[H_GUARDS]&&m[H_CAPTURE_STOP]==31&&audio_ok;
 /* The mux hash is independent of native AU hash: the mux adds containers,
  * while the observer separately decodes and verifies every actual frame. */
 int log=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
 if(log>=0){char line[320];int n=snprintf(line,sizeof line,
  "<6>QC5_NATIVE_SERVICE valid=%d reason=%u captured=%llu encoded=%llu published=%llu mux_bytes=%llu mux_hash=%08x messages=%llu stop=%u flags=%u\n",
  valid,service_capture.reason,(unsigned long long)c[SN_CAPTURED],(unsigned long long)c[SN_ENCODED],
  (unsigned long long)c[SN_PUBLISHED],(unsigned long long)c[SN_MUX_BYTES],service_web.mux_hash,
  (unsigned long long)c[SN_WS_MESSAGES],service_capture.stopped,e[SN_FLAGS]);
  if(n>0&&(size_t)n<sizeof line)(void)write(log,line,(unsigned)n);close(log);}
 memcpy(record.pixels,&note,sizeof note);publish_record(ram,&record,valid?CAPTURE_OK:CAPTURE_FRAME);
 qc5_beacon_close(&live.beacon);qq_capture_stop();qrt_end();qmi_stop(&integrated_media);qc5_net_finish(&live.net);
 printf("QC5_NATIVE_SERVICE_RESULT %d captured=%llu encoded=%llu published=%llu\n",valid,
  (unsigned long long)c[SN_CAPTURED],(unsigned long long)c[SN_ENCODED],(unsigned long long)c[SN_PUBLISHED]);
 return valid?CAPTURE_OK:CAPTURE_FRAME;
}

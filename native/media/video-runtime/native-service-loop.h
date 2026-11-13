/* SPDX-License-Identifier: GPL-3.0-only
 * Native capture lifetime, independent of reader connections and of the old
 * finite frame/poll target. RAM validation still has an external fixed WDT;
 * this code neither refreshes it nor accesses flash or boot settings.
 */
#ifndef QC5_NATIVE_SERVICE_LOOP_H
#define QC5_NATIVE_SERVICE_LOOP_H
#include <signal.h>
enum qsc_reason { QSC_NONE, QSC_DURATION, QSC_REQUEST, QSC_FAILURE };
struct qc5_service_capture {
 uint64_t polls,started_us,last_us,first_frame_us,last_frame_us,hold_us;
 uint64_t first_encoded_us,duration_us,completed,queued;
 unsigned reason,stopped,finished;
 volatile sig_atomic_t stop_requested;
};
static struct qc5_service_capture service_capture;
#ifndef QC5_SERVICE_START_TIMEOUT_US
#define QC5_SERVICE_START_TIMEOUT_US UINT64_C(5000000)
#endif
#ifndef QC5_SERVICE_FRAME_TIMEOUT_US
#define QC5_SERVICE_FRAME_TIMEOUT_US UINT64_C(4000000)
#endif
#ifndef QC5_SERVICE_DRAIN_TIMEOUT_US
#define QC5_SERVICE_DRAIN_TIMEOUT_US UINT64_C(2000000)
#endif
static unsigned service_controls_pending(const struct qc5_ae_controller *ae)
{
 unsigned pending=ae->pending;
#ifdef QC5_MDNS_DEFERRED
 pending|=active_mdns_deferred.pending;
#endif
 return pending;
}
static int service_frame_invariants(volatile uint8_t *ram,struct qc5_preview *r,
                                    struct msca_record *d,const struct msca_state *s,
                                    const struct qc5_ae_controller *ae)
{
 uint32_t *m=d->meta;
 if(!s->service||!s->control_hold||!s->target||s->target!=s->queued||s->queued!=s->completed||
    s->completed<=120||m[M_COMPLETED]!=msca_note_count(s->completed)||m[M_STOP]!=31||
    !m[M_LATCH_POLLS]||m[M_LATCH_POLLS]>=m[M_FIRST]||m[M_SHAPE_MASK]!=MSCA_SHAPE_MASK||
    !s->last_poll||s->last_poll>service_capture.polls||m[M_CSC_MATCH]!=1||m[M_SENSOR_MASK]!=127||
    (m[M_ISP_IRQ]&0x3fe)||!(m[M_ISP_IRQ]&1)||!(r->vic_status&1)||
    r->csi_err1||r->csi_err2||(r->vic_status&VIC_ERRORS)||
    service_controls_pending(ae)||ae->samples!=msca_note_count(s->completed)||
    m[M_AE_SAMPLES]!=ae->samples||m[M_AE_UPDATES]!=ae->updates||
    m[M_GAIN]>24||m[M_EXPOSURE]<4||m[M_EXPOSURE]>896)
  return core_refuse(r,0x7e020,msca_note_count(s->completed));
 for(unsigned n=0;n<2;n++){
  struct qc5_nv12_bank canonical;
  if(!qc5_nv12_plan(n,1280,720,&canonical)||memcmp(&canonical,&s->planes[n],sizeof canonical))
   return core_refuse(r,0x7e024,n);
 }
 uint32_t bad=0;
 if(!nv12_guard(ram,s,&bad))return core_refuse(r,bad,ram[bad-QC5_CAPTURE_RAM_BASE]);
#ifdef QC5_NV12_WB
 if(!wb_record_valid(d))return core_refuse(r,0x7e028,0);
#endif
 m[M_GUARDS]=1;m[M_STAGE]=8;return 1;
}
static int encoder_service_capture(volatile uint8_t *ram,struct qc5_preview *record,struct msca_record *data)
{
 struct msca_state state={0};
 struct qc5_ae_controller ae={.selected={720,QC5_NV12_INITIAL_GAIN},.target=QC5_AE_TARGET};
 uint32_t *m=data->meta;struct qc5_service_capture *p=&service_capture;
 /* A process owns exactly one pipeline. Starting it again would conceal a
  * failed stop or reset the encoder's reference ownership. */
 if(p->started_us||p->polls||p->stopped||p->finished)return CAPTURE_FRAME;
 int ok=msca_service_begin(&state)&&ae_time_us(record,&p->started_us)&&p->started_us;
 p->last_us=p->started_us;
 if(ok)ok=msca_capture_start(ram,record,data,&state);
 while(ok){
  uint64_t now;
  if(p->polls==UINT64_MAX||!ae_time_us(record,&now)||now<p->last_us){ok=core_refuse(record,0x7e000,0);break;}
  p->last_us=now;m[M_POLLS]=msca_note_count(++p->polls);
  qrt_poll();
#ifdef QC5_AUDIO_STREAM
  if(!qsa_tick(&service_audio,&live.net,live.client_epoch,now)){
#ifdef QSA_FAILURE_NOTE
   QSA_FAILURE_NOTE(&service_audio);
#endif
   ok=core_refuse(record,0x78004,service_audio.child.receiver.failed);break;}
#endif
  if(!p->first_frame_us&&now-p->started_us>=QC5_SERVICE_START_TIMEOUT_US){ok=core_refuse(record,0x7e004,0);break;}
  if(p->last_frame_us&&now-p->last_frame_us>=QC5_SERVICE_FRAME_TIMEOUT_US){ok=core_refuse(record,0x7e008,msca_note_count(state.completed));break;}
  if(!p->reason&&(p->stop_requested||(p->duration_us&&p->first_encoded_us&&now-p->first_encoded_us>=p->duration_us))){
   p->reason=p->stop_requested?QSC_REQUEST:QSC_DURATION;p->hold_us=now;
   if(!msca_service_hold_controls(&state)){ok=core_refuse(record,0x7e00c,0);break;}
  }
  if(p->reason&&now-p->hold_us>=QC5_SERVICE_DRAIN_TIMEOUT_US){ok=core_refuse(record,0x7e010,service_controls_pending(&ae));break;}
  if(!msca_poll_events(record,data,&state)){ok=0;break;}
  if(!state.queued){
   if(p->polls>=QC5_NV12_POLLS){ok=core_refuse(record,0x7e014,0);break;}
   if(msca_wait_shape(record,data,&state,(unsigned)p->polls)<0){ok=0;break;}
   delay_us(1000);continue;
  }
  /* Existing delayed sensor/MDNS changes must be confirmed while frames
   * continue. Only then freeze the queue at the two destinations already
   * owned by DMA; no new destination may enter after the drain request. */
  if(p->reason&&!state.target&&!service_controls_pending(&ae)&&!msca_service_drain(&state,0)){
   ok=core_refuse(record,0x7e018,0);break;
  }
  if(msca_shape_mask()!=MSCA_SHAPE_MASK){ok=core_refuse(record,0x70030,msca_shape_mask());break;}
  uint32_t y,uv;
  if(msca_completed_pair(data,&state,&y,&uv)){
   if(!msca_complete(record,data,&state,y,uv,p->polls)||
      !ae_completed_frame(ram,record,data,&state,&ae)||
      !encoder_runtime_frame(ram,record,data,&state)){ok=0;break;}
   if(!ae_time_us(record,&now)||now<p->last_us){ok=core_refuse(record,0x7e000,1);break;}
   p->last_us=p->last_frame_us=now;
   if(!p->first_frame_us)p->first_frame_us=now;
   if(state.completed>120&&!p->first_encoded_us)p->first_encoded_us=now;
   if(state.target&&state.completed==state.target)break;
  }
  delay_us(1000);
 }
 p->completed=state.completed;p->queued=state.queued;
 if(!ok){
  p->reason=QSC_FAILURE;
  if(record->capture_flags&QC5_CAPTURE_VARIANT_REFUSED){
   m[M_FAULT_ADDRESS]=record->isp_route_blocker;m[M_FAULT_VALUE]=record->dma_addr_before;
  }
 }
 msca_snapshot(data);m[M_STOP]=msca_stop();p->stopped=m[M_STOP];
 if(!encoder_runtime_stopped(record,m[M_STOP]))ok=0;
 m[M_CORE_RUN]=msca_read(0x800);m[M_OUTPUT_ENABLE]=msca_read(0xd040);m[M_VIC_RUN]=frame_read_vic(0);
 record->csi_err1=csi[0x20/4];record->csi_err2=csi[0x24/4];
 if(!ok||m[M_STOP]!=31||!state.target||state.completed!=state.target){p->reason=QSC_FAILURE;return CAPTURE_FRAME;}
 delay_us(250000);
 if(!color_finish(core,record))ok=0;else m[M_CSC_MATCH]=color_csc_mode()==0;
#ifdef QC5_DYNAMIC_AWB
 if(ok&&!qc5_awb_capture_finish(&active_awb))ok=awb_failed(record);
#ifdef QC5_DYNAMIC_CCM
 if(ok&&!qc5_ccm_finish(&active_ccm))ok=ccm_failed(record);
#endif
#endif
#ifdef QC5_NV12_WB
 if(ok&&!wb_finish(core,record,data))ok=0;
#endif
#ifdef QC5_NV12_SPATIAL
 if(ok&&!spatial_finish_frames(core,record,data,state.completed))ok=0;
#endif
#ifdef QC5_AF_STATS
 if(ok&&!af_stats_finish(ram,record,m[M_STOP]))ok=0;
#endif
#ifdef QC5_SENSOR_ORIENTATION
 if(ok&&!sensor_orientation_finish(record))ok=0;
#endif
 if(ok&&!service_frame_invariants(ram,record,data,&state,&ae))ok=0;
 if(!ok){p->reason=QSC_FAILURE;return CAPTURE_FRAME;}
 p->finished=1;return CAPTURE_OK;
}
#endif

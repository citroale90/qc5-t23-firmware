/* SPDX-License-Identifier: GPL-3.0-only
 * Native adapter for the persistent producer. Legacy diagnostics keep their
 * bounded512-frame proof; service mode keeps64-bit lifetime accounting.
 * No frame copies or new queues. The existing sensor/ISP preparation, clock
 * authority and MSCA FIFO checks stay in force. This is not a flash service.
 */
#ifndef QC5_NATIVE_PRODUCER_BRIDGE_H
#define QC5_NATIVE_PRODUCER_BRIDGE_H
static int runtime_meter_checked(void *opaque,const struct qc5_frame_ticket *ticket,uint64_t us)
{
 (void)opaque;
 if(!service_features_attempted){
  service_features_attempted=1;uint64_t epoch=qfs_epoch();
  if(!qfs_init(&service_features,"/run/qc5-native-network/features.sock",epoch,"/mnt/sda0/qc5-recordings","/bin/qc5-record-sink"))service_features.failed=1;
 }
 if(live.current_state&&live.current_data&&ticket&&ticket->bank<2&&
    live.producer.input.owner[ticket->bank]==QO_CPU&&live.producer.arena){
  const struct qc5_nv12_bank *plane=&live.current_state->planes[ticket->bank];
  if(plane->y>=QC5_CAPTURE_RAM_BASE&&plane->y<=QC5_CAPTURE_RAM_END-1280u*720u){
   qq_capture_luma(live.producer.arena+(plane->y-QC5_CAPTURE_RAM_BASE),1280,720,1280,us);
   (void)qfs_luma(&service_features,live.producer.arena+(plane->y-QC5_CAPTURE_RAM_BASE),1280,720,1280,
    live.current_data->meta[M_GAIN],live.current_data->meta[M_EXPOSURE],0,us);
  }
 }
 if(service_features.initialized&&ticket->serial>120){
  const struct qc5_nv12_bank *plane=&live.current_state->planes[ticket->bank];
  if(plane->uv<QC5_CAPTURE_RAM_BASE||plane->uv_bytes!=1280u*720u/2||
     plane->uv>QC5_CAPTURE_RAM_END-plane->uv_bytes||
     !qdi_frame(&service_features.image,live.producer.arena+(plane->uv-QC5_CAPTURE_RAM_BASE),plane->uv_bytes,ticket->serial,us))return 0;
 }
 return live.current_record&&live.current_state&&live.current_data&&us&&
  !(live.current_record->capture_flags&QC5_CAPTURE_VARIANT_REFUSED)&&
  live.current_state->completed==ticket->serial&&
  live.current_data->meta[M_AE_SAMPLES]==msca_note_count(ticket->serial);
}
static int runtime_encode_native(void *opaque,const struct qc5_helix_picture *picture,struct qc5_encoded_output *out)
{
 (void)opaque;struct qc5_preview *r=live.current_record;
 uint32_t *m=live.proof.meta,*s=live.proof.net;
 uint64_t serial=live.producer.input.completed,start,end;
 if(serial<=120||(!live.current_state->service&&serial>QC5_NV12_TARGET)||
    live.producer.input.encoded!=serial-121||!ae_time_us(r,&start))return 0;
 uint64_t n=serial-121;live.current_start=start;
 int ok=qp_helix_encode(&live.engine,picture,out);
#ifdef QC5_CAPTURE_SERVICE
 if(ok)live.service_last_picture=*picture;
#endif
 const struct helix_result *result=&live.engine.result;
 m[H_PAIRS]=live.engine.pairs;m[H_DESC_HASH]=live.engine.descriptor_hash;
 m[H_ORDER_HASH]=live.engine.order_hash;m[H_INPUT_BANK]=live.engine.input_bank;
 m[H_STATUS]=result->status;m[H_ERRORS]|=result->status&HELIX_ERRORS;m[H_RESET_MASK]=result->reset_mask;
 if(result->polls>m[H_POLLS])m[H_POLLS]=result->polls;
 if(!ok){memcpy(live.proof.prefix,result->before_reset,sizeof(result->before_reset));
  return live_failure(r,result->fault_register?result->fault_register:0x7d010,result->fault_value);}
 if(!ae_time_us(r,&end)||end<=start||end-start>3000000)return live_failure(r,0x7d014,n);
 if(!n){
  unsigned take=out->bytes<sizeof live.proof.prefix?out->bytes:sizeof live.proof.prefix;
  memcpy(live.proof.prefix,out->data,take);m[H_AU_HASH]=2166136261u;live.first=start;
 }
 for(unsigned i=0;i<out->bytes;i++)m[H_AU_HASH]=(m[H_AU_HASH]^out->data[i])*16777619u;
 if(live.producer.bytes>UINT64_MAX-out->bytes||live.cabac_total>UINT64_MAX-result->bytes)
  return live_failure(r,0x7d038,0);
 live.cabac_total+=result->bytes;
 m[H_AU_BYTES]=msca_note_count(live.producer.bytes+out->bytes);
 m[H_CABAC_BYTES]=msca_note_count(live.cabac_total);m[H_ENCODED]=msca_note_count(n+1);
 if(picture->idr&&s[L_IDRS]!=UINT32_MAX)s[L_IDRS]++;
 s[L_LAST_BYTES]=out->bytes;
 if(end-start>s[L_ENCODE_MAX_US])s[L_ENCODE_MAX_US]=(uint32_t)(end-start);
#ifdef QC5_AUDIO_CONCURRENT
#ifdef QC5_AUDIO_STREAM
 if(!n&&!qsa_start(&service_audio,qc5_live_nonce,end))return live_failure(r,0x78000,errno);
#else
 if(!n&&!qc5_audio_child_start(&live_audio,"/bin/qc5-audio"))return live_failure(r,0x78000,errno);
#endif
#endif
 return 1;
}
static uint64_t runtime_clock(void *opaque)
{(void)opaque;uint64_t us=0;return ae_time_us(live.current_record,&us)?us:0;}
static int runtime_publish(void *opaque,const struct qc5_frame_output *frame,uint64_t deadline)
{
 (void)opaque;
 uint64_t now=runtime_clock(NULL);
 if(!now||!deadline)return 0;
#ifdef QC5_CAPTURE_SERVICE
 return qc5_service_publish(&live.net,live.client_epoch,frame,deadline);
#else
 return live.net.stream&&qc5_net_write(&live.net,frame->data,frame->bytes,1);
#endif
}
static int runtime_record(void *opaque,const struct qc5_frame_output *frame,uint64_t deadline)
{
 (void)opaque;uint64_t now=runtime_clock(NULL);if(!now||now>=deadline)return 0;
#ifndef QC5_CAPTURE_SERVICE
 qrt_video(frame->data,frame->bytes,frame->captured_us,frame->idr);
#endif
 int ok=qfs_video(&service_features,frame->data,frame->bytes,frame->captured_us,frame->idr);
 uint64_t end=runtime_clock(NULL);
 if(!end||end>deadline){qrr_end(&service_features.record,1,end?end:now);return 0;}return ok;
}
#ifdef QC5_CAPTURE_SERVICE
/* Both native sinks own their bounded I/O and recovery. In particular the
 * recorder already enforces eight ms per AU in qrc_begin/send and retries
 * a failed writer after closing it. The legacy producer's permanent record
 * disable must not prevent those retries after a scheduling delay.
 * This AU remains valid until the NEXT encode; no frame is retained/copied. */
static int runtime_output_frame(uint64_t captured_us)
{
 unsigned bytes=live.proof.net[L_LAST_BYTES];
 if(!captured_us||!live.producer.arena||!bytes||bytes>QC5_HELIX_AU_BYTES)return 0;
 if(!qfs_video(&service_features,live.producer.arena+(QC5_HELIX_AU_PHYS-QC5_CAPTURE_RAM_BASE),bytes,captured_us,live.service_last_picture.idr)){
  uint64_t now=runtime_clock(NULL);qrr_end(&service_features.record,1,now?now:captured_us);
 }
 qrt_video(live.producer.arena+(QC5_HELIX_AU_PHYS-QC5_CAPTURE_RAM_BASE),bytes,captured_us,live.service_last_picture.idr);
 return 1;
}
#endif
static int runtime_drop_reader(void *opaque,uint64_t epoch)
{
 (void)opaque;if(!epoch||epoch!=live.client_epoch)return 0;
 if(live.net.client>=0)qc5_net_close(&live.net);
 return 1;
}
static int runtime_requeue_native(void *opaque,uint32_t y,uint32_t uv)
{
 (void)opaque;
 return live.current_state&&live.current_record&&
  live.current_state->queued==live.producer.input.queued&&
  msca_requeue(live.current_record,live.current_state,y,uv);
}
static int encoder_runtime_frame(volatile uint8_t *ram,struct qc5_preview *r,struct msca_record *data,struct msca_state *state)
{
 if(!live.ready||!ram||!state||!data||state->completed!=live.producer.input.completed+1||
    state->queued!=live.producer.input.queued)return live_failure(r,0x7d000,state?state->completed:0);
 live.current_record=r;live.current_data=data;live.current_state=state;
#ifdef QC5_LIVE_BOOTLOG
 qc5_beacon_tick(&live.beacon);
#endif
 if(qc5_net_tick(&live.net)){
  if(live.client_epoch==UINT64_MAX)return live_failure(r,0x7d004,0);
  live.client_epoch++;
 }
 unsigned bank=(state->completed-1)%2;
 uint64_t limit=msca_limit(state);unsigned retire=limit&&state->queued==limit;
 uint64_t us;if(!ae_time_us(r,&us))return 0;
 const struct qc5_producer_io io={NULL,runtime_meter_checked,runtime_encode_native,runtime_clock,
  runtime_publish,runtime_drop_reader,runtime_requeue_native,
#ifdef QC5_CAPTURE_SERVICE
  NULL
#else
  runtime_record
#endif
 };
 uint64_t epoch=live.net.stream?live.client_epoch:0;
 if(!qp_frame_mode(&live.producer,&io,state->planes[bank].y,state->planes[bank].uv,us,epoch,retire)){
  if(live.proof.meta[H_FAULT_REG])return 0;
  if((r->capture_flags&QC5_CAPTURE_VARIANT_REFUSED)&&r->isp_route_blocker)
   return live_failure(r,r->isp_route_blocker,r->dma_addr_before);
  return live_failure(r,0x7d008,live.producer.error);
 }
 if(state->queued!=live.producer.input.queued||state->completed!=live.producer.input.completed)
  return live_failure(r,0x7d00c,state->queued);
 if(state->completed>120){
#ifdef QC5_CAPTURE_SERVICE
  if(!runtime_output_frame(us))return live_failure(r,0x7d03c,0);
#endif
  uint64_t end;if(!ae_time_us(r,&end)||end<=live.current_start||end-live.current_start>3200000)
   return live_failure(r,0x7d018,state->completed);
  if(end-live.current_start>live.proof.net[L_FRAME_MAX_US])live.proof.net[L_FRAME_MAX_US]=(uint32_t)(end-live.current_start);
  live.last=end;
  (void)qmi_publish(&integrated_media,live.producer.input.encoded,integrated_media_flags());
 }
 if(state->completed==1||state->completed==120||state->completed==121||(limit&&state->completed==limit)||
    !(state->completed%(state->service?1024:32)))
  live_progress("runtime_frame",state->completed);
 return 1;
}
static int encoder_runtime_stopped(struct qc5_preview *r,unsigned stop)
{
 qq_capture_stop();uint64_t feature_now=qc5_net_us();qfs_close(&service_features,feature_now);
 live.current_record=NULL;live.current_data=NULL;live.current_state=NULL;
 if(!live.ready)return 1;
 unsigned idle=live.engine.result.reset_mask==3;
 unsigned retired=(live.producer.input.owner[0]==QO_STOPPED)+(live.producer.input.owner[1]==QO_STOPPED);
 if(!idle){struct helix_result reset={0};idle=helix_reset(&live.engine.io,&reset);}
 if(stop!=31||!idle){qp_fail(&live.producer,QP_CAPTURE);return live_failure(r,0x7d01c,stop);}
 if(live.producer.input.phase==QO_RUN&&!qp_request_stop(&live.producer))return live_failure(r,0x7d020,0);
 if(!qp_stopped(&live.producer,stop,idle)||!qp_helix_stopped(&live.engine,idle))return live_failure(r,0x7d024,0);
#ifdef QC5_AUDIO_STREAM
 /* Both image destinations and the encoder are now idle. Stop and drain
  * the still-running microphone BEFORE post-capture memory/profile checks,
  * which can occupy the parent long enough to fill the bounded IPC queue. */
 if(service_audio.child.started&&!qsa_finish(&service_audio,&live.net,live.client_epoch))return live_failure(r,0x78008,service_audio.child.result);
#endif
 int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
 if(fd<0)return live_failure(r,0x7d028,errno);
 char line[256];int n=snprintf(line,sizeof line,
  "<6>QC5_RUNTIME_PIPELINE completed=%llu queued=%llu encoded=%llu retired=%u phase=%u engine_idle=1 error=%u\n",
  (unsigned long long)live.producer.input.completed,(unsigned long long)live.producer.input.queued,
  (unsigned long long)live.producer.input.encoded,retired,live.producer.input.phase,live.producer.error);
 int logged=n>0&&(size_t)n<sizeof line&&write(fd,line,(size_t)n)==n;close(fd);
 return logged?1:live_failure(r,0x7d02c,0);
}
#endif

#ifndef QC5_MEDIA_H
#define QC5_MEDIA_H
#include "qc5_mp4.h"
#include "qc5_video_clock.h"
#include "qc5_media_talk.h"
#ifdef QC5_HOST_TEST
#define QC5_MEDIA_REFRESH_MS 500
#else
#define QC5_MEDIA_REFRESH_MS 30000
#endif
/* One native session supplies both H264 and microphone audio. The WebUI
 * receives video-only fMP4 plus small tagged PCMA packets, without transcoding. */
struct video_parser {
 unsigned char header[32],audio[4098],video[512*1024],output[4096];
 size_t head,remaining,used,pending,output_used,video_committed;unsigned kind;uint64_t stamp,pending_stamp;
 struct qc5_video_clock clock;
 struct qc5_mp4 mux;int fd,ready,mime;uint16_t expected;
 int (*frame_handler)(void*,const unsigned char*,size_t,uint64_t,unsigned);
 int (*audio_handler)(void*,const unsigned char*,size_t,unsigned,uint64_t);
 void*handler_context;
 long long last_video,send_deadline;unsigned frames,audios,kinds[16],ws_fragments;int error;long long clock_delta;
};
static struct video_parser video_stream;
static int video_flush(struct video_parser*s){
 if(!s->output_used)return 0;
 unsigned char part[4097];part[0]=0;memcpy(part+1,s->output,s->output_used);
 if(talk_ws_send_until(s->fd,2,part,s->output_used+1,s->send_deadline)){s->error=-20;return -1;}
 s->output_used=0;s->ws_fragments++;return 0;
}
static int video_sink(void*opaque,const void*data,size_t size){
 struct video_parser*s=opaque;
 if(!s->mime){char info[110];int n=snprintf(info,sizeof(info),"{\"mime\":\"video/mp4; codecs=\\\"avc1.%02x%02x%02x\\\"\"}",s->mux.sps[1],s->mux.sps[2],s->mux.sps[3]);if(talk_ws_send_until(s->fd,1,info,(size_t)n,s->send_deadline)){s->error=-20;return -1;}s->mime=1;}
 /* Coalesce the muxer's small box/length/NAL callbacks within this sample.
  * The resulting byte stream is identical, with fewer tiny WS/TCP packets. */
 const unsigned char*p=data;
 while(size){size_t take=sizeof(s->output)-s->output_used;if(take>size)take=size;
  memcpy(s->output+s->output_used,p,take);s->output_used+=take;p+=take;size-=take;
  if(s->output_used==sizeof(s->output)&&video_flush(s))return -1;
 }
 return 0;
}

static size_t video_payload_pages(struct video_parser*s,size_t n){
 if(!n)return 0;
 return (((uintptr_t)s->video&4095)+n+4095)&~(size_t)4095;
}
static void video_payload_trim(struct video_parser*s,size_t next_size){
#ifdef __linux__
 /* A large keyframe must not pin its entire high-water allocation for the
  * rest of the session. The preceding frame has already been delivered.
  * Keep room for the next complete frame; discard only whole, now unused
  * payload pages. The first/last shared pages (audio/output/metadata) stay.
  * No frame is shortened, re-encoded, skipped or cleared before delivery. */
 size_t keep=video_payload_pages(s,next_size);
 if(keep>=s->video_committed||s->video_committed-keep<64*1024)return;
 uintptr_t base=(uintptr_t)s->video&~(uintptr_t)4095;
 uintptr_t end=((uintptr_t)s->video+sizeof(s->video))&~(uintptr_t)4095;
 if(end>base+s->video_committed)end=base+s->video_committed;
 uintptr_t first=base+keep;
 if(first<end&&!madvise((void*)first,end-first,MADV_DONTNEED))s->video_committed=keep;
#else
 (void)s;(void)next_size;
#endif
}
static int video_bytes(struct video_parser*s,const unsigned char*p,size_t n){
 while(n){
  if(s->head<32){size_t take=32-s->head;if(take>n)take=n;memcpy(s->header+s->head,p,take);s->head+=take;p+=take;n-=take;if(s->head<32)continue;
   if(memcmp(s->header,"\x55\xaa\x15\xa8",4)){s->error=-10;return -1;}
   s->remaining=le32(s->header+16);s->kind=s->header[4];s->used=0;s->stamp=(uint64_t)le32(s->header+8)*1000+(unsigned)s->header[6]+((unsigned)s->header[7]<<8);
   if(!s->remaining||s->remaining>2*1024*1024||((s->kind==0||s->kind==1)&&s->remaining>sizeof(s->video))||((s->kind==7||s->kind==12)&&s->remaining>4096)){s->error=-11;return -1;}
   if((s->kind==0||s->kind==1)&&s->pending){
    if(s->ready){s->clock_delta=(long long)s->stamp-(long long)s->pending_stamp;
     uint64_t presentation=s->clock.presentation_ms;unsigned duration=qc5_video_clock_step(&s->clock,s->pending_stamp,s->stamp);
     s->send_deadline=now_ms()+QC5_WS_WRITE_TIMEOUT_MS;
     int r=s->frame_handler?s->frame_handler(s->handler_context,s->video,s->pending,presentation,duration):qc5_mp4_video_timed(&s->mux,s->video,s->pending,presentation,duration);
     if(!r&&!s->frame_handler)r=video_flush(s);s->send_deadline=0;
     if(r){if(!s->error)s->error=r;return -1;}}
    s->pending=0;
   }
   if(s->kind==0||s->kind==1){
    video_payload_trim(s,s->remaining);
    /* Reject an unaffordable frame before accepting its payload. Do not
     * count these pages as committed until they are actually written. */
    size_t needed=video_payload_pages(s,s->remaining);
    if(!qc5_memory_video_growth_ok(s->video_committed,needed)){s->error=QC5_VIDEO_MEMORY_LIMIT;return -1;}
   }
  }
  size_t take=s->remaining;if(take>n)take=n;
  if(s->kind==0||s->kind==1){
   /* A fragmented frame can span a drop in available memory. Recheck each
    * newly touched target page instead of treating its header as a reserve. */
   size_t needed=video_payload_pages(s,s->used+take);
   if(!qc5_memory_video_growth_ok(s->video_committed,needed)){s->error=QC5_VIDEO_MEMORY_LIMIT;return -1;}
   memcpy(s->video+s->used,p,take);
   if(needed>s->video_committed)s->video_committed=needed;
  }
  else if(s->kind==7||s->kind==12)memcpy(s->audio+2+s->used,p,take);
  s->used+=take;s->remaining-=take;p+=take;n-=take;
  if(!s->remaining){
   if(s->kind<16)s->kinds[s->kind]++;
   if(s->kind==0||s->kind==1){s->pending=s->used;s->pending_stamp=s->stamp;s->last_video=now_ms();s->frames++;}
   else if((s->kind==7||s->kind==12)&&s->ready){s->audio[0]=1;s->audio[1]=(unsigned char)s->kind;int r=s->audio_handler?s->audio_handler(s->handler_context,s->audio+2,s->used,s->kind,s->stamp):talk_ws_send(s->fd,2,s->audio,s->used+2);if(r){s->error=-20;return -1;}s->audios++;}
   s->head=0;
  }
 }
 return 0;
}
static int video_packet(int n,const struct sockaddr_in*source){
 struct video_parser*s=&video_stream;
 if(n<8||!connected||source->sin_port!=peer.sin_port||wire[1]!=0xd0||wire[5]!=1)return 0;
 uint16_t seq=(uint16_t)((unsigned)wire[6]*256+wire[7]);if((int16_t)(seq-s->expected)>0)return 0;
 unsigned char ack[6]={0xd1,1,0,1,wire[6],wire[7]};packet(0xd1,ack,6,&peer);
 if(seq==s->expected){s->expected++;return video_bytes(s,wire+8,(size_t)n-8);}return 0;
}
static void video_parser_init(struct video_parser*s,int fd){
 /* The header and payload buffers are filled before use. Clearing them after
  * fork would turn all 512 KiB of video capacity into private dirty pages,
  * even when the camera produces much smaller frames. Reset only metadata. */
 memset((unsigned char*)s+offsetof(struct video_parser,head),0,sizeof(*s)-offsetof(struct video_parser,head));
 s->fd=fd;s->last_video=now_ms();qc5_mp4_init(&s->mux,0,15,video_sink,s);s->mux.live_avc1=1;
}
static void video_worker(int fd){
 child_session(fd);memset(&media_talk,0,sizeof(media_talk));media_talk.speaker_fd=-1;struct video_parser*s=&video_stream;video_parser_init(s,fd);media_packet=video_packet;
 const char*reason="setup";unsigned close_code=1011;int live=0;long long memory_checked=-1;
 /* Recheck in the child: memory may have changed since the parent's fork. */
 if(!qc5_memory_stream_ok(now_ms(),&memory_checked)){reason="memory_pressure";close_code=1013;goto done;}
 if(get_status(1)||camera_rpc("livestream","streamid=80",4000))goto done;
 live=1;int accepted=-1;if(field_int((char*)response+8,"result",&accepted)||accepted)goto done;
 if(camera_rpc("audiostream","streamid=7",4000))goto done;
 accepted=-1;if(field_int((char*)response+8,"result",&accepted)||accepted)goto done;
 const char*ready="{\"ready\":true,\"audio\":\"PCMA\",\"duplex\":true}";if(talk_ws_send(fd,1,ready,strlen(ready)))goto done;
 s->ready=1;reason="video_idle";s->last_video=now_ms();unsigned char input[512],decoded[320];size_t used=0;long long refreshed=now_ms(),reported=refreshed;
 while(!stopping&&now_ms()-s->last_video<8000){
  if(!qc5_memory_stream_ok(now_ms(),&memory_checked)){reason="memory_pressure";close_code=1013;break;}
  if(now_ms()-reported>=60000){fprintf(stderr,"QC5_MEDIA_PROGRESS video=%u audio=%u kind7=%u kind12=%u clock_adjustments=%u presentation_ms=%llu\n",s->frames,s->audios,s->kinds[7],s->kinds[12],s->clock.adjustments,(unsigned long long)s->clock.presentation_ms);reported=now_ms();}
  if(!media_talk.active&&now_ms()-refreshed>=QC5_MEDIA_REFRESH_MS){if(get_status(1)){reason="session_refresh";break;}refreshed=now_ms();}
  struct pollfd p[2]={{fd,POLLIN,0},{udp,POLLIN,0}};if(poll(p,2,5)<0&&errno!=EINTR)break;
  if(p[0].revents&(POLLERR|POLLHUP|POLLNVAL)){reason="client_closed";close_code=1000;break;}
  if(p[0].revents&POLLIN){ssize_t n=recv(fd,input+used,sizeof(input)-used,MSG_DONTWAIT);if(!n){reason="client_closed";close_code=1000;break;}if(n<0&&errno!=EAGAIN&&errno!=EINTR)break;if(n>0)used+=(size_t)n;
   while(used){unsigned op;size_t size;int bytes=qc5_ws_decode(input,used,&op,decoded,sizeof(decoded),&size);if(bytes<0){close_code=1002;goto done;}if(!bytes)break;memmove(input,input+bytes,used-(size_t)bytes);used-=(size_t)bytes;
    if(op==8){reason="client_release";close_code=1000;goto done;}if(op==9){if(talk_ws_send(fd,10,decoded,size))goto done;}else if(op!=10&&media_talk_input(fd,op,decoded,size)){close_code=1003;goto done;}
   }if(used==sizeof(input)){close_code=1009;break;}
  }
  if(p[1].revents&POLLIN){struct sockaddr_in source;int n=receive_packet(0,&source);if(n<0){reason=s->error?"media_error":wire[1]==0xf0?"camera_closed":"receive_error";break;}if(n>=10)media_talk_ack(n,&source);if(n>=8&&source.sin_port==peer.sin_port&&wire[1]==0xd0&&wire[5]==0){if(control_packet(n,&source)<0||complete_frame(0)<0){reason="control_notification";break;}}else if(n>=8&&source.sin_port==peer.sin_port&&wire[1]==0xd0&&wire[5]!=1){unsigned char ack[6]={0xd1,wire[5],0,1,wire[6],wire[7]};packet(0xd1,ack,6,&peer);}}
  if(media_talk_tick(fd)){reason="speaker_transport";break;}
 }
 if(stopping)close_code=1000;
 done:if(s->error==QC5_VIDEO_MEMORY_LIMIT){reason="memory_pressure";close_code=1013;}fprintf(stderr,"QC5_MEDIA_END code=%u reason=%s error=%d delta_ms=%lld sequence=%u video=%u audio=%u clock_adjustments=%u\n",close_code,reason,s->error,s->clock_delta,s->expected,s->frames,s->audios,s->clock.adjustments);talk_ws_finish(fd,close_code);native_stream_release(live);close(fd);memset(password,0,sizeof(password));_exit(0);
}
#endif

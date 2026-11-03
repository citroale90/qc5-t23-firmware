#ifndef QC5_MEDIA_TALK_H
#define QC5_MEDIA_TALK_H
/* The existing live subscription already accepts speaker channel 3. Reuse
 * it rather than opening a second native session and a second gateway child.
 * All added storage is bounded: eight 20 ms PCMA frames and one reliable packet.
 * Standalone intercom admission and active-stream reserve remain independent. */
struct media_talk_session {
 unsigned char queue[8][320],flight[356];
 unsigned count,head,received,dropped,retries;
 uint16_t sequence;uint32_t press_id;
 int active,inflight,disabled,speaker_fd;
 long long started,last_audio,next_frame,last_sent,first_sent;
 unsigned max_seconds;
};
static struct media_talk_session media_talk;
static int media_talk_reply_stopped(int fd,uint32_t id,const char*reason){
 char text[170];int n=snprintf(text,sizeof(text),"{\"talk\":false,\"talkId\":%u,\"reason\":\"%s\"}",(unsigned)id,reason);
 return n<0||(size_t)n>=sizeof(text)?-1:talk_ws_send(fd,1,text,(size_t)n);
}
static void media_talk_unlock_idle(void){
 if(!media_talk.active&&!media_talk.inflight&&media_talk.speaker_fd>=0){close(media_talk.speaker_fd);media_talk.speaker_fd=-1;}
}
static int media_talk_end(int fd,const char*reason){
 struct media_talk_session*t=&media_talk;int active=t->active;
 t->active=0;t->count=0;t->head=0;media_talk_unlock_idle();
 if(!active)return 0;
 fprintf(stderr,"QC5_MEDIA_TALK_END reason=%s frames=%u dropped=%u retries=%u\n",reason,t->received,t->dropped,t->retries);
 return media_talk_reply_stopped(fd,t->press_id,reason);
}
static int media_talk_start(int fd,uint32_t id){
 struct media_talk_session*t=&media_talk;
 if(t->active)return id==t->press_id?0:media_talk_reply_stopped(fd,id,"busy"); /* Repeated start cannot extend the 30 second limit. */
 if(t->disabled||qc5_memory_available_kb()<QC5_STREAM_RESERVE_KB+64){
  return media_talk_reply_stopped(fd,id,"unavailable");
 }
 char value[16];cfg("TALK_MAX_SECONDS",value,sizeof(value));long limit=30;
 if(value[0]){char*end;limit=strtol(value,&end,10);if(*end||limit<1||limit>30)return -1;}
 if(t->speaker_fd<0){t->speaker_fd=speaker_acquire();if(t->speaker_fd<0)return media_talk_reply_stopped(fd,id,"busy");}
 t->max_seconds=(unsigned)limit;t->press_id=id;t->active=1;t->count=t->head=0;t->received=t->dropped=t->retries=0;
 t->started=t->last_audio=t->next_frame=now_ms();
 char ready[190];int n=snprintf(ready,sizeof(ready),"{\"talk\":true,\"talkId\":%u,\"ready\":true,\"codec\":\"PCMA\",\"sampleRate\":16000,\"frameSamples\":320,\"maxSeconds\":%u}",(unsigned)t->press_id,t->max_seconds);
 return n<0||(size_t)n>=sizeof(ready)?-1:talk_ws_send(fd,1,ready,(size_t)n);
}
static int media_talk_input(int fd,unsigned op,const unsigned char*data,size_t size){
 struct media_talk_session*t=&media_talk;
 if(op==2&&size==12&&!memcmp(data,"QCT1",4)&&!data[5]&&!data[6]&&!data[7]){
  uint32_t id=(uint32_t)data[8]|(uint32_t)data[9]<<8|(uint32_t)data[10]<<16|(uint32_t)data[11]<<24;
  if(!id||data[4]>1)return -1;
  if(data[4])return media_talk_start(fd,id);
  if(t->active&&t->press_id==id)return media_talk_end(fd,"client_release");
  return media_talk_reply_stopped(fd,id,"client_release");
 }
 if(op!=2||size!=320)return -1;
 if(!t->active)return 0; /* Discard late buffered audio after stop/limit. */
 if(t->count==8){t->head=(t->head+1)%8;t->count--;t->dropped++;}
 memcpy(t->queue[(t->head+t->count)%8],data,320);t->count++;t->received++;t->last_audio=now_ms();return 0;
}
static void media_talk_ack(int n,const struct sockaddr_in*source){
 struct media_talk_session*t=&media_talk;
 if(t->inflight&&source->sin_port==peer.sin_port&&talk_acknowledged(n,t->sequence)){t->inflight=0;t->sequence++;}
}
static int media_talk_tick(int fd){
 struct media_talk_session*t=&media_talk;long long now=now_ms();
 if(t->active&&(now-t->started>=(long long)t->max_seconds*1000||now-t->last_audio>(t->received?700:2000))){
  if(media_talk_end(fd,now-t->started>=(long long)t->max_seconds*1000?"limit":"audio_idle"))return -1;
 }
 if(t->inflight){
  if(now-t->first_sent>500){t->inflight=0;t->disabled=1;media_talk_unlock_idle();return media_talk_end(fd,"native_ack_timeout");}
  if(now-t->last_sent>=50){t->retries++;if(packet(0xd0,t->flight,sizeof(t->flight),&peer))return -1;t->last_sent=now;}
 }else if(t->active&&t->count&&now>=t->next_frame){
  talk_frame_encode(t->flight,t->sequence,t->queue[t->head],now);t->head=(t->head+1)%8;t->count--;
  if(packet(0xd0,t->flight,sizeof(t->flight),&peer))return -1;
  t->inflight=1;t->last_sent=t->first_sent=now;t->next_frame+=20;if(t->next_frame<now-40)t->next_frame=now;
 }
 media_talk_unlock_idle();return 0;
}
#endif

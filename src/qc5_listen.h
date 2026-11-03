#ifndef QC5_LISTEN_H
#define QC5_LISTEN_H
/* Skip H264 payload without allocating it; forward only native G711 A-law.
 * Native frame metadata is 32 bytes; kinds 7/12 identify 8/16 kHz. */
struct listen_parser{unsigned char header[32],audio[4097];size_t head,remaining,audio_used;int audio_kind;};
static int listen_ready;
static int listen_bytes(int fd,struct listen_parser*s,const unsigned char*p,size_t n,long long*last_audio){
 while(n){
  if(s->head<32){size_t take=32-s->head;if(take>n)take=n;memcpy(s->header+s->head,p,take);s->head+=take;p+=take;n-=take;if(s->head<32)continue;
   if(memcmp(s->header,"\x55\xaa\x15\xa8",4))return -1;
   s->remaining=le32(s->header+16);s->audio_kind=s->header[4]==7||s->header[4]==12?s->header[4]:0;s->audio_used=1;s->audio[0]=(unsigned char)s->audio_kind;
   if(!s->remaining||s->remaining>2*1024*1024||(s->audio_kind&&s->remaining>4096))return -1;
  }
  size_t take=s->remaining;if(take>n)take=n;
  if(s->audio_kind){memcpy(s->audio+s->audio_used,p,take);s->audio_used+=take;}
  p+=take;n-=take;s->remaining-=take;
  if(!s->remaining){if(s->audio_kind){if(listen_ready&&talk_ws_send(fd,2,s->audio,s->audio_used))return -1;*last_audio=now_ms();}s->head=0;}
 }
 return 0;
}
static struct listen_parser listen_stream;
static uint16_t listen_expected;
static int listen_fd;
static long long listen_last_audio;
static int listen_packet(int n,const struct sockaddr_in*source){
 if(n<8||!connected||source->sin_port!=peer.sin_port||wire[1]!=0xd0||wire[5]!=1)return 0;
 uint16_t seq=(uint16_t)((unsigned)wire[6]*256+wire[7]);if((int16_t)(seq-listen_expected)>0)return 0;
 unsigned char ack[6]={0xd1,1,0,1,wire[6],wire[7]};packet(0xd1,ack,6,&peer);
 if(seq==listen_expected){listen_expected++;return listen_bytes(listen_fd,&listen_stream,wire+8,(size_t)n-8,&listen_last_audio);}
 return 0;
}
static void listen_worker(int fd){
 child_session(fd);listen_ready=0;listen_fd=fd;listen_expected=0;memset(&listen_stream,0,sizeof(listen_stream));listen_last_audio=now_ms();media_packet=listen_packet;int live=0;unsigned close_code=1011;
 if(get_status(1)||camera_rpc("livestream","streamid=80",4000))goto done;
 live=1;if(camera_rpc("audiostream","streamid=7",4000))goto done;
 int accepted=-1;if(field_int((char*)response+8,"result",&accepted)||accepted)goto done;
 const char*ready="{\"ready\":true,\"codec\":\"PCMA\",\"sampleRates\":[8000,16000]}";
 if(talk_ws_send(fd,1,ready,strlen(ready)))goto done;
 listen_ready=1;listen_last_audio=now_ms();unsigned char input[512],decoded[320];size_t used=0;long long refreshed=now_ms();
 while(!stopping&&now_ms()-listen_last_audio<4000){
  if(now_ms()-refreshed>=30000){if(get_status(1))break;refreshed=now_ms();}
  struct pollfd p[2]={{fd,POLLIN,0},{udp,POLLIN,0}};if(poll(p,2,20)<0&&errno!=EINTR)break;
  if(p[0].revents&(POLLERR|POLLHUP|POLLNVAL)){close_code=1000;break;}
  if(p[0].revents&POLLIN){ssize_t n=recv(fd,input+used,sizeof(input)-used,MSG_DONTWAIT);if(!n){close_code=1000;break;}if(n<0&&errno!=EAGAIN&&errno!=EINTR)break;if(n>0)used+=(size_t)n;
   while(used){unsigned op;size_t size;int bytes=qc5_ws_decode(input,used,&op,decoded,sizeof(decoded),&size);if(bytes<0){close_code=1002;goto done;}if(!bytes)break;memmove(input,input+bytes,used-(size_t)bytes);used-=(size_t)bytes;if(op==8){close_code=1000;goto done;}if(op==9){if(talk_ws_send(fd,10,decoded,size))goto done;}else if(op!=10){close_code=1003;goto done;}}
   if(used==sizeof(input)){close_code=1009;break;}
  }
  if(p[1].revents&POLLIN){struct sockaddr_in source;int n=receive_packet(0,&source);if(n<0)break;
   if(n<8||source.sin_port!=peer.sin_port||wire[1]!=0xd0)continue;
   if(wire[5]==0){if(control_packet(n,&source)<0||complete_frame(0)<0)break;}else if(wire[5]!=1){unsigned char ack[6]={0xd1,wire[5],0,1,wire[6],wire[7]};packet(0xd1,ack,6,&peer);}
  }
 }
 if(stopping)close_code=1000;
 done:talk_ws_finish(fd,close_code);native_stream_release(live);close(fd);memset(password,0,sizeof(password));_exit(0);
}
#endif

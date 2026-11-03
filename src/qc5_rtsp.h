#ifndef QC5_RTSP_H
#define QC5_RTSP_H
/* RTSP/1.0 + H264 (RFC6184) and PCMA over the existing TCP connection.
 * No transcoding, UDP destination chosen by a client, resolver or WAN client.
 * VLC retries TCP after 461 Unsupported Transport. One bounded reader. */
struct qc5_rtsp {
 int fd,live,playing,wait_key,setup[2];unsigned channel[2];
 unsigned char sps[256],pps[256];size_t sps_n,pps_n;
 uint32_t ssrc[2],packets[2],octets[2],audio_next;uint16_t sequence[2];
 uint64_t origin_ms,video_elapsed_ms;int video_clock_set;long long started;time_t wall;
 char session[17],base[600];
};
static void rtsp_be32(unsigned char*p,uint32_t x){p[0]=(unsigned char)(x>>24);p[1]=(unsigned char)(x>>16);p[2]=(unsigned char)(x>>8);p[3]=(unsigned char)x;}
static int rtsp_interleaved(struct qc5_rtsp*r,unsigned channel,const void*data,size_t n){
 if(n>1500)return -1;unsigned char b[1504];b[0]='$';b[1]=(unsigned char)channel;b[2]=(unsigned char)(n>>8);b[3]=(unsigned char)n;memcpy(b+4,data,n);return send_all(r->fd,b,n+4);
}
static int rtsp_rtp(struct qc5_rtsp*r,unsigned track,const void*data,size_t n,uint32_t stamp,int marker){
 if(n>1400)return -1;unsigned char b[1412];b[0]=0x80;b[1]=(unsigned char)((track?97:96)|(marker?128:0));uint16_t seq=r->sequence[track]++;b[2]=(unsigned char)(seq>>8);b[3]=(unsigned char)seq;rtsp_be32(b+4,stamp);rtsp_be32(b+8,r->ssrc[track]);memcpy(b+12,data,n);
 if(rtsp_interleaved(r,r->channel[track],b,n+12))return -1;r->packets[track]++;r->octets[track]+=(uint32_t)n;return 0;
}
static unsigned rtsp_prefix(const unsigned char*p,size_t n,size_t at){
 if(at+3<=n&&!p[at]&&!p[at+1]&&p[at+2]==1)return 3;
 return at+4<=n&&!p[at]&&!p[at+1]&&!p[at+2]&&p[at+3]==1?4:0;
}
static int rtsp_nal(struct qc5_rtsp*r,const unsigned char*p,size_t n,uint32_t stamp,int marker){
 if(!n||(p[0]&128))return -1;
 if(n<=1200)return rtsp_rtp(r,0,p,n,stamp,marker);
 unsigned char b[1200];unsigned char header=p[0];p++;n--;int first=1;
 long long deadline=now_ms()+2500;
 while(n){if(stopping||now_ms()>=deadline)return -1;size_t take=n>1198?1198:n;b[0]=(unsigned char)((header&0xe0)|28);b[1]=(unsigned char)((header&31)|(first?128:0)|(take==n?64:0));memcpy(b+2,p,take);if(rtsp_rtp(r,0,b,take+2,stamp,marker&&take==n))return -1;first=0;p+=take;n-=take;}
 return 0;
}
static uint64_t rtsp_elapsed_ms(struct qc5_rtsp*r){long long n=now_ms()-r->started;return n>0?(uint64_t)n:0;}
static int rtsp_video(void*context,const unsigned char*p,size_t n,uint64_t ms,unsigned duration){
 struct qc5_rtsp*r=context;size_t at=0;unsigned prefix;(void)duration;(void)ms;
 /* The vendor frame clock can advance twice as fast as real time. Browser
  * mux timestamps are not the RTP clock: SR, audio and video must share the
  * monotonic PLAY reference. Give batched access units distinct timestamps. */
 uint64_t elapsed=r->playing?rtsp_elapsed_ms(r):0;
 if(r->playing){if(r->video_clock_set&&elapsed<=r->video_elapsed_ms)elapsed=r->video_elapsed_ms+1;r->video_elapsed_ms=elapsed;r->video_clock_set=1;}
 while(at<n){while(at<n&&!(prefix=rtsp_prefix(p,n,at)))at++;if(at==n)break;size_t start=at+prefix;at=start;while(at<n&&!rtsp_prefix(p,n,at))at++;size_t end=at;while(end>start&&!p[end-1])end--;if(end==start)return -1;
  unsigned type=p[start]&31;size_t length=end-start;if(type==7||type==8){if(length>256||(type==7&&length<4))return -1;unsigned char*dst=type==7?r->sps:r->pps;memcpy(dst,p+start,length);if(type==7)r->sps_n=length;else r->pps_n=length;}
  if(type==5)r->wait_key=0;
  if(r->playing&&!r->wait_key&&r->setup[0]&&rtsp_nal(r,p+start,length,(uint32_t)(elapsed*90),at==n))return -1;
 }
 return 0;
}
static int rtsp_audio(void*context,const unsigned char*p,size_t n,unsigned kind,uint64_t ms){
 struct qc5_rtsp*r=context;(void)ms;if(!r->playing||!r->setup[1]||kind!=12)return 0;
 uint32_t stamp=r->audio_next;
 while(n){size_t take=n>1200?1200:n;if(rtsp_rtp(r,1,p,take,stamp,!r->packets[1]))return -1;stamp+=(uint32_t)take;p+=take;n-=take;}
 r->audio_next=stamp;return 0;
}
static int rtsp_sender_reports(struct qc5_rtsp*r){
 /* Both tracks use the same clock reference. Correlating with each packet's
  * arrival time instead would turn network jitter into backwards audio DTS. */
 long long elapsed=now_ms()-r->started;if(elapsed<0)elapsed=0;
 for(unsigned t=0;t<2;t++)if(r->setup[t]&&r->packets[t]){
  unsigned char b[48]={0x80,200,0,6};rtsp_be32(b+4,r->ssrc[t]);
  rtsp_be32(b+8,(uint32_t)((uint64_t)r->wall+2208988800ULL+(uint64_t)elapsed/1000));rtsp_be32(b+12,(uint32_t)((((uint64_t)elapsed%1000)<<32)/1000));
  rtsp_be32(b+16,(uint32_t)((uint64_t)elapsed*(t?16000:90000)/1000));rtsp_be32(b+20,r->packets[t]);rtsp_be32(b+24,r->octets[t]);b[28]=0x81;b[29]=202;b[31]=4;rtsp_be32(b+32,r->ssrc[t]);b[36]=1;b[37]=9;memcpy(b+38,"qc5-local",9);
  if(rtsp_interleaved(r,r->channel[t]+1,b,sizeof(b)))return -1;
 }
 return 0;
}
static void rtsp_b64(const unsigned char*p,size_t n,char*out){
 static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t k=0;
 for(size_t i=0;i<n;i+=3){size_t left=n-i;uint32_t v=(uint32_t)p[i]<<16;if(left>1)v|=(uint32_t)p[i+1]<<8;if(left>2)v|=p[i+2];out[k++]=table[v>>18];out[k++]=table[(v>>12)&63];out[k++]=left>1?table[(v>>6)&63]:'=';out[k++]=left>2?table[v&63]:'=';}out[k]=0;
}
static int rtsp_reply(struct qc5_rtsp*r,unsigned seq,int code,const char*extra,const char*body){
 char b[2600];const char*reason=code==200?"OK":code==400?"Bad Request":code==404?"Not Found":code==454?"Session Not Found":code==455?"Method Not Valid in This State":code==461?"Unsupported Transport":code==503?"Service Unavailable":"Not Implemented";
 int n=snprintf(b,sizeof(b),"RTSP/1.0 %d %s\r\nCSeq: %u\r\nServer: QC5-local\r\n%sContent-Length: %zu\r\n\r\n%s",code,reason,seq,extra?extra:"",body?strlen(body):0,body?body:"");if(n<0||(size_t)n>=sizeof(b))return -1;return send_all(r->fd,b,(size_t)n);
}
static int rtsp_pump(void){
 struct sockaddr_in source;int n=receive_packet(20,&source);if(n<0)return -1;
 if(n>=8&&source.sin_port==peer.sin_port&&wire[1]==0xd0&&wire[5]==0){if(control_packet(n,&source)<0||complete_frame(0)<0)return -1;}
 else if(n>=8&&source.sin_port==peer.sin_port&&wire[1]==0xd0&&wire[5]!=1){unsigned char ack[6]={0xd1,wire[5],0,1,wire[6],wire[7]};packet(0xd1,ack,6,&peer);}return 0;
}
static int rtsp_start(struct qc5_rtsp*r){
 if(qc5_memory_available_kb()<QC5_STREAM_RESERVE_KB)return -1;
 if(r->live)return r->sps_n&&r->pps_n?0:-1;struct video_parser*s=&video_stream;video_parser_init(s,r->fd);s->frame_handler=rtsp_video;s->audio_handler=rtsp_audio;s->handler_context=r;s->ready=1;media_packet=video_packet;
 if(get_status(1)||camera_rpc("livestream","streamid=80",4000))return -1;r->live=1;int accepted=-1;if(field_int((char*)response+8,"result",&accepted)||accepted)return -1;
 if(camera_rpc("audiostream","streamid=7",4000)||field_int((char*)response+8,"result",&accepted)||accepted)return -1;
 long long end=now_ms()+5000;while(!stopping&&now_ms()<end&&(!r->sps_n||!r->pps_n))if(rtsp_pump())return -1;
 return r->sps_n&&r->pps_n?0:-1;
}
static int rtsp_request(struct qc5_rtsp*r,char*request){
 char method[24],url[600],version[16],tail;unsigned seq;char value[256],headers[1000];
 char*end=strstr(request,"\r\n");if(!end)return -1;*end=0;
 if(sscanf(request,"%23s %599s %15s %c",method,url,version,&tail)!=3||strcmp(version,"RTSP/1.0"))return -1;
 const char*h=end+2;if(header_value(h,"CSeq",value,sizeof(value))!=1)return -1;char*number;unsigned long v=strtoul(value,&number,10);if(!*value||*number||v>2147483647)return -1;seq=(unsigned)v;
 int session=header_value(h,"Session",value,sizeof(value));if(session<0)return -1;if(session){char*semi=strchr(value,';');if(semi)*semi=0;if(strcmp(value,r->session))return rtsp_reply(r,seq,454,NULL,NULL);}
 if(!strcmp(method,"OPTIONS"))return rtsp_reply(r,seq,200,"Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, GET_PARAMETER, TEARDOWN\r\n",NULL);
 if(strncmp(url,"rtsp://",7))return rtsp_reply(r,seq,404,NULL,NULL);char*path=strchr(url+7,'/');if(!path)return rtsp_reply(r,seq,404,NULL,NULL);
 int track=!strcmp(path,"/live/track0")?0:!strcmp(path,"/live/track1")?1:-1;
 int aggregate=!strcmp(path,"/live")||!strcmp(path,"/live/");if(!aggregate&&track<0)return rtsp_reply(r,seq,404,NULL,NULL);
 if(!strcmp(method,"DESCRIBE")){
  if(!aggregate)return rtsp_reply(r,seq,404,NULL,NULL);if(rtsp_start(r))return rtsp_reply(r,seq,503,NULL,NULL);
  size_t length=(size_t)(path-url);if(length+7>=sizeof(r->base))return -1;memcpy(r->base,url,length);memcpy(r->base+length,"/live/",7);
  char sps[345],pps[345],sdp[1300];rtsp_b64(r->sps,r->sps_n,sps);rtsp_b64(r->pps,r->pps_n,pps);
  int n=snprintf(sdp,sizeof(sdp),"v=0\r\no=- 1 1 IN IP4 0.0.0.0\r\ns=QC5 local\r\nc=IN IP4 0.0.0.0\r\nt=0 0\r\na=control:*\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1;profile-level-id=%02x%02x%02x;sprop-parameter-sets=%s,%s\r\na=control:track0\r\nm=audio 0 RTP/AVP 97\r\na=rtpmap:97 PCMA/16000/1\r\na=control:track1\r\n",r->sps[1],r->sps[2],r->sps[3],sps,pps);
  if(n<0||(size_t)n>=sizeof(sdp))return -1;snprintf(headers,sizeof(headers),"Content-Type: application/sdp\r\nContent-Base: %s\r\n",r->base);return rtsp_reply(r,seq,200,headers,sdp);
 }
 if(!strcmp(method,"SETUP")){
  if(track<0||!r->base[0]||r->playing)return rtsp_reply(r,seq,455,NULL,NULL);
  if((r->setup[0]||r->setup[1])&&!session)return rtsp_reply(r,seq,454,NULL,NULL);
  if(header_value(h,"Transport",value,sizeof(value))!=1)return rtsp_reply(r,seq,461,NULL,NULL);
  unsigned first,last;int consumed=0;
  if(sscanf(value,"RTP/AVP/TCP;unicast;interleaved=%u-%u%n",&first,&last,&consumed)!=2||value[consumed]||first>254||last!=first+1||(r->setup[1-track]&&(first==r->channel[1-track]||first==r->channel[1-track]+1||last==r->channel[1-track])))return rtsp_reply(r,seq,461,NULL,NULL);
  r->setup[track]=1;r->channel[track]=first;
  snprintf(headers,sizeof(headers),"Session: %s;timeout=60\r\nTransport: RTP/AVP/TCP;unicast;interleaved=%u-%u;ssrc=%08X\r\n",r->session,first,last,r->ssrc[track]);return rtsp_reply(r,seq,200,headers,NULL);
 }
 if(!r->setup[0]&&!r->setup[1])return rtsp_reply(r,seq,455,NULL,NULL);
 if(!session)return rtsp_reply(r,seq,454,NULL,NULL);
 snprintf(headers,sizeof(headers),"Session: %s;timeout=60\r\n",r->session);
 if(!strcmp(method,"PLAY")){
  if(!aggregate)return rtsp_reply(r,seq,455,NULL,NULL);
  if(!r->playing)r->wait_key=1;
  if(!r->started){r->origin_ms=video_stream.clock.presentation_ms;r->started=now_ms();r->wall=time(NULL);r->audio_next=0;}
  else if(!r->playing)r->audio_next=(uint32_t)(rtsp_elapsed_ms(r)*16);
  r->playing=1;return rtsp_reply(r,seq,200,headers,NULL);
 }
 if(!strcmp(method,"PAUSE")){r->playing=0;return rtsp_reply(r,seq,200,headers,NULL);}
 if(!strcmp(method,"GET_PARAMETER"))return rtsp_reply(r,seq,200,headers,NULL);
 if(!strcmp(method,"TEARDOWN")){rtsp_reply(r,seq,200,headers,NULL);return 1;}
 return rtsp_reply(r,seq,501,NULL,NULL);
}
static void rtsp_worker(int fd){
 child_session(fd);struct qc5_rtsp r;memset(&r,0,sizeof(r));r.fd=fd;
 if(qc5_random_hex(r.session))goto done;r.ssrc[0]=(uint32_t)strtoul(r.session+8,NULL,16);char first[9];memcpy(first,r.session,8);first[8]=0;r.ssrc[1]=(uint32_t)strtoul(first,NULL,16);r.sequence[0]=(uint16_t)r.ssrc[0];r.sequence[1]=(uint16_t)r.ssrc[1];
 char input[8193];size_t used=0;long long last_request=now_ms(),refreshed=last_request,report_at=last_request+5000,memory_checked=-1;
 while(!stopping){
  if(r.live&&!qc5_memory_stream_ok(now_ms(),&memory_checked)){fprintf(stderr,"QC5_RTSP_MEMORY_PRESSURE\n");break;}
  if((!r.playing&&now_ms()-last_request>15000)||(r.live&&now_ms()-video_stream.last_video>8000))break;
  if(r.live&&now_ms()-refreshed>=QC5_MEDIA_REFRESH_MS){if(get_status(1))break;refreshed=now_ms();}
  if(r.playing&&now_ms()>=report_at){if(rtsp_sender_reports(&r))break;report_at=now_ms()+5000;}
  struct pollfd p[2]={{fd,POLLIN,0},{udp,POLLIN,0}};if(poll(p,2,20)<0&&errno!=EINTR)break;
  if(p[0].revents&(POLLERR|POLLHUP|POLLNVAL))break;
  if(p[0].revents&POLLIN){ssize_t n=recv(fd,input+used,sizeof(input)-1-used,MSG_DONTWAIT);if(n==0)break;if(n<0&&errno!=EAGAIN&&errno!=EINTR)break;if(n>0)used+=(size_t)n;input[used]=0;
   while(used){
    if(input[0]=='$'){if(used<4)break;size_t length=(unsigned char)input[2]*256u+(unsigned char)input[3];if(length>4096)goto done;if(used<length+4)break;memmove(input,input+length+4,used-length-4);used-=length+4;input[used]=0;continue;}
    char*end=strstr(input,"\r\n\r\n");if(!end)break;size_t bytes=(size_t)(end-input)+4;if(bytes>4096||memchr(input,0,bytes))goto done;
    char*start=strstr(input,"\r\n");if(!start)goto done;char value[24];int has=header_value(start+2,"Content-Length",value,sizeof(value));if(has<0)goto done;unsigned long body=0;if(has){char*tail;body=strtoul(value,&tail,10);if(!*value||*tail||body>512)goto done;}if(used<bytes+body)break;
    char request[4097];memcpy(request,input,bytes);request[bytes]=0;int result=rtsp_request(&r,request);if(result)goto done;bytes+=body;memmove(input,input+bytes,used-bytes);used-=bytes;input[used]=0;last_request=now_ms();
   }if(used==sizeof(input)-1)break;
  }
  if(r.live&&p[1].revents&POLLIN)if(rtsp_pump())break;
 }
 done:fprintf(stderr,"QC5_RTSP_END video=%u audio=%u kind7=%u kind12=%u rtp_video=%u rtp_audio=%u setup_video=%d setup_audio=%d\n",video_stream.frames,video_stream.audios,video_stream.kinds[7],video_stream.kinds[12],r.packets[0],r.packets[1],r.setup[0],r.setup[1]);native_stream_release(r.live);close(fd);memset(password,0,sizeof(password));_exit(0);
}
static void rtsp_accept(void){
 reap_one(&rtsp_pid);int fd=accept(rtsp_listener,NULL,NULL);if(fd<0)return;
 release_idle_buffers(1);
 if(rtsp_pid||qc5_memory_available_kb()<QC5_VIDEO_ADMISSION_KB){const char*b="RTSP/1.0 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n";send_all(fd,b,strlen(b));close(fd);return;}
 pid_t pid=fork();if(!pid)rtsp_worker(fd);if(pid>0)rtsp_pid=pid;close(fd);
}
static int rtsp_open(const char*bind_ip){
 if(!rtsp_port)return 0;rtsp_listener=socket(AF_INET,SOCK_STREAM,0);if(rtsp_listener<0)return -1;int one=1;setsockopt(rtsp_listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons((uint16_t)rtsp_port);
 if(fcntl(rtsp_listener,F_SETFL,O_NONBLOCK)||inet_pton(AF_INET,bind_ip,&a.sin_addr)!=1||bind(rtsp_listener,(struct sockaddr*)&a,sizeof(a))||listen(rtsp_listener,2)){close(rtsp_listener);rtsp_listener=-1;return -1;}return 0;
}
#endif

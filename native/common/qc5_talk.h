/* Dedicated child and UDP session: microphone traffic never reuses the
 * control session, changes codec configuration, or restarts the vendor app. */
#ifndef QC5_TALK_H
#define QC5_TALK_H
#include <sys/wait.h>
#include <netinet/tcp.h>
#include "qc5_websocket.h"
#include "qc5_memory.h"
#include "qc5_child.h"
#include "qc5_stream_close.h"
static pid_t talk_pid,listen_pid,video_pid;
static void listen_worker(int fd);
static void video_worker(int fd);
static void reap_one(pid_t*p){(void)qc5_child_reap(p,NULL);}
static void talk_reap(void){reap_one(&talk_pid);reap_one(&listen_pid);reap_one(&video_pid);}
static void stop_one(pid_t*p){
 if(*p<=0)return;
 if(qc5_child_stop(p,NULL,900,100)&&*p>0)fprintf(stderr,"QC5_CHILD_STOP_PENDING pid=%ld\n",(long)*p);
}
static void talk_shutdown(void){stop_one(&talk_pid);stop_one(&listen_pid);stop_one(&video_pid);}
static int header_value(const char *headers,const char *name,char *out,size_t cap){
 size_t key=strlen(name);int found=0;out[0]=0;
 for(const char*p=headers;*p&&strncmp(p,"\r\n",2);){
  const char*end=strstr(p,"\r\n");if(!end)return -1;
  const char*colon=memchr(p,':',(size_t)(end-p));if(!colon)return -1;
  if((size_t)(colon-p)==key&&!strncasecmp(p,name,key)){
   if(found++)return -1;
   const char*a=colon+1,*z=end;while(a<z&&(*a==' '||*a=='\t'))a++;while(z>a&&(z[-1]==' '||z[-1]=='\t'))z--;
   if((size_t)(z-a)>=cap)return -1;
   memcpy(out,a,(size_t)(z-a));out[z-a]=0;
  }
  p=end+2;
 }
 return found;
}
#define QC5_WS_WRITE_TIMEOUT_MS 8000
static int talk_ws_send_until(int fd,unsigned opcode,const void*data,size_t size,long long end){
 unsigned char bytes[4200];if(size>4196)return -1;size_t used=2;bytes[0]=(unsigned char)(128|opcode);
 if(size<126)bytes[1]=(unsigned char)size;else{bytes[1]=126;bytes[2]=(unsigned char)(size>>8);bytes[3]=(unsigned char)size;used=4;}
 memcpy(bytes+used,data,size);size+=used;used=0;int error=0;
 /* Real camera TCP_INFO showed RTO=2.41 s on an established socket.
  * Allow bounded retransmission recovery, keeping the local PPPP lease
  * alive. Never drop a partial WebSocket frame or extend a sample forever. */
 while(used<size&&now_ms()<end){if(stopping&&opcode!=8){error=EINTR;break;}camera_keepalive();ssize_t n=send(fd,bytes+used,size-used,MSG_DONTWAIT|MSG_NOSIGNAL);if(n>0){used+=(size_t)n;continue;}if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR){error=errno;break;}struct pollfd p={fd,POLLOUT,0};poll(&p,1,5);}
 if(used==size)return 0;
 if(!error&&now_ms()>=end)error=ETIMEDOUT;
 if(!(opcode==8&&(error==EPIPE||error==ECONNRESET))){
  fprintf(stderr,"QC5_WS_SEND_FAILED opcode=%u sent=%zu total=%zu errno=%d\n",opcode,used,size,error);
#ifdef __linux__
  /* Failure-only bounded socket metadata: no media bytes or credentials. */
  struct tcp_info info;memset(&info,0,sizeof(info));socklen_t length=sizeof(info);
  int outq=-1,sndbuf=-1; socklen_t option_length=sizeof(sndbuf);
  (void)ioctl(fd,TIOCOUTQ,&outq);(void)getsockopt(fd,SOL_SOCKET,SO_SNDBUF,&sndbuf,&option_length);
  if(!getsockopt(fd,IPPROTO_TCP,TCP_INFO,&info,&length))
   fprintf(stderr,"QC5_WS_TCP state=%u unacked=%u lost=%u retrans=%u rto_us=%u rtt_us=%u cwnd=%u mss=%u outq=%d sndbuf=%d\n",
    info.tcpi_state,info.tcpi_unacked,info.tcpi_lost,info.tcpi_total_retrans,info.tcpi_rto,info.tcpi_rtt,info.tcpi_snd_cwnd,info.tcpi_snd_mss,outq,sndbuf);
#endif
 }
 return -1;
}
static int talk_ws_send(int fd,unsigned opcode,const void*data,size_t size){
 return talk_ws_send_until(fd,opcode,data,size,now_ms()+QC5_WS_WRITE_TIMEOUT_MS);
}
static void talk_ws_finish(int fd,unsigned close_code){
 unsigned char byte;ssize_t n=recv(fd,&byte,1,MSG_PEEK|MSG_DONTWAIT);
 if(!n||(n<0&&(errno==ECONNRESET||errno==ENOTCONN||errno==EBADF)))return;
 unsigned char code[2]={(unsigned char)(close_code>>8),(unsigned char)close_code};
 /* A congested browser must not postpone the native stop/ACK exchange. */
 (void)talk_ws_send_until(fd,8,code,sizeof(code),now_ms()+150);
}
static int talk_acknowledged(int n,uint16_t sequence){
 return packet_acknowledged(n,3,sequence);
}
static void child_session(int fd){
 /* Close copied parent sockets without sending the parent's disconnect. */
 for(int i=3;i<4096;i++)if(i!=fd)close(i);
 int flags=fcntl(fd,F_GETFL,0);
 if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)){close(fd);_exit(1);}
 int no_delay=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&no_delay,sizeof(no_delay));
 /* Keep live-client TCP queues bounded on the 20 MiB vendor kernel. A
  * temporary wireless pause may wait for bounded TCP retransmission recovery,
  * but must not grow a large kernel send queue or truncate a WS frame. */
 int send_buffer=32768,receive_buffer=4096;
 if(setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&send_buffer,sizeof(send_buffer))||setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&receive_buffer,sizeof(receive_buffer)))
  fprintf(stderr,"QC5_MEDIA_SOCKET_LIMIT_FAILED errno=%d\n",errno);
 udp=-1;connected=authenticated=reconnect_needed=0;txseq=rxseq=0;pending_len=0;stopping=0;status_cache[0]=0;media_packet=NULL;
}
/* A shared-live speaker and a standalone speaker must never overlap. Kernel
 * locks release on exit, without stale PID files or a second native session. */
static int speaker_acquire(void){
 int directory=open(provision_run,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(directory<0)return -1;
 struct stat st;if(fstat(directory,&st)||st.st_uid!=geteuid()||(st.st_mode&0022)){close(directory);return -1;}
 int fd=openat(directory,"speaker.lock",O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);close(directory);if(fd<0)return -1;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_uid!=geteuid()||(st.st_mode&0077)){close(fd);return -1;}
 struct flock lock;memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
 if(fcntl(fd,F_SETLK,&lock)){close(fd);return -1;}return fd;
}
/* Shared wire format for standalone and live-stream speaker transport. */
static void talk_frame_encode(unsigned char flight[356],uint16_t sequence,const unsigned char audio[320],long long now){
 memset(flight,0,356);flight[0]=0xd1;flight[1]=3;flight[2]=(unsigned char)(sequence>>8);flight[3]=(unsigned char)sequence;
 unsigned char *frame=flight+4;frame[0]=0x55;frame[1]=0xaa;frame[2]=0x15;frame[3]=0xa8;frame[4]=12;
 uint32_t seconds=(uint32_t)(now/1000);unsigned ms=(unsigned)(now%1000);frame[6]=(unsigned char)ms;frame[7]=(unsigned char)(ms>>8);
 for(unsigned i=0;i<4;i++)frame[8+i]=(unsigned char)(seconds>>(i*8));
 frame[16]=64;frame[17]=1;memcpy(frame+32,audio,320);
}
static void talk_worker(int fd){
 child_session(fd);
 int speaker=-1;
 unsigned received=0,dropped=0,retries=0;const char*reason="setup";int live=0;unsigned close_code=1011;char limit_value[16];cfg("TALK_MAX_SECONDS",limit_value,sizeof(limit_value));long max_seconds=30;
 if(limit_value[0]){char*end;max_seconds=strtol(limit_value,&end,10);if(*end||max_seconds<1||max_seconds>30)goto done;}
 speaker=speaker_acquire();if(speaker<0){reason="speaker_busy";close_code=1013;goto done;}
 if(get_status(1)||camera_rpc("livestream","streamid=80",4000))goto done;
 int accepted=-1;if(field_int((char*)response+8,"result",&accepted)||accepted)goto done;
 live=1;reason="client_release";
 char ready[160];snprintf(ready,sizeof(ready),"{\"ready\":true,\"codec\":\"PCMA\",\"sampleRate\":16000,\"frameSamples\":320,\"maxSeconds\":%ld}",max_seconds);
 if(talk_ws_send(fd,1,ready,strlen(ready)))goto done;
 unsigned char input[2048],decoded[320],queue[8][320],flight[356];
 size_t used=0;unsigned count=0,head=0;uint16_t sequence=0;int inflight=0;
 long long started=now_ms(),last_audio=started,next_frame=started,last_sent=0,first_sent=0;
 while(!stopping&&now_ms()-started<max_seconds*1000){
  long long now=now_ms();if(now-last_audio>(received?700:2000)){reason="audio_idle";close_code=1000;break;}
  struct pollfd p[2]={{fd,POLLIN,0},{udp,POLLIN,0}};
  int polled=poll(p,2,5);if(polled<0&&errno!=EINTR)break;
  if(p[0].revents&(POLLERR|POLLHUP|POLLNVAL)){close_code=1000;break;}
  if(p[0].revents&POLLIN){
   if(used==sizeof(input)){close_code=1009;break;}
   ssize_t n=recv(fd,input+used,sizeof(input)-used,MSG_DONTWAIT);
   if(n==0){close_code=1000;break;}if(n<0&&errno!=EINTR&&errno!=EAGAIN)break;
   if(n>0)used+=(size_t)n;
   while(used){
    unsigned op=0;size_t length=0;int bytes=qc5_ws_decode(input,used,&op,decoded,sizeof(decoded),&length);
    if(bytes<0){close_code=1002;goto done;}if(!bytes)break;
    memmove(input,input+bytes,used-(size_t)bytes);used-=(size_t)bytes;
    if(op==8){close_code=1000;goto done;}
    if(op==9){if(talk_ws_send(fd,10,decoded,length))goto done;continue;}
    if(op==10)continue;
    if(length!=320){close_code=1003;goto done;}
    if(count==8){head=(head+1)%8;count--;dropped++;}
    memcpy(queue[(head+count)%8],decoded,320);count++;received++;last_audio=now_ms();
   }
  }
  if(p[1].revents&POLLIN){
   struct sockaddr_in source;int n=receive_packet(0,&source);
   if(n<0)break;
   if(n>=8&&source.sin_port==peer.sin_port&&wire[1]==0xd0){
    if(wire[5]==0){if(control_packet(n,&source)<0||complete_frame(0)<0){reason="control_notification";break;}}
    else{unsigned char ack[6]={0xd1,wire[5],0,1,wire[6],wire[7]};packet(0xd1,ack,6,&peer);}
   }
   if(n>=10&&inflight&&source.sin_port==peer.sin_port&&talk_acknowledged(n,sequence)){inflight=0;sequence++;}
  }
  now=now_ms();
  if(inflight){
   if(now-first_sent>500){reason="native_ack_timeout";close_code=1011;break;}
   if(now-last_sent>=50){retries++;if(packet(0xd0,flight,sizeof(flight),&peer))break;last_sent=now;}
  }else if(count&&now>=next_frame){
   talk_frame_encode(flight,sequence,queue[head],now);head=(head+1)%8;count--;
   if(packet(0xd0,flight,sizeof(flight),&peer))break;
   inflight=1;last_sent=first_sent=now;next_frame+=20;if(next_frame<now-40)next_frame=now;
  }
 }
 if(stopping||now_ms()-started>=max_seconds*1000){reason="limit_or_shutdown";close_code=1000;}
 done:
 fprintf(stderr,"QC5_TALK_END code=%u reason=%s frames=%u dropped=%u retries=%u\n",close_code,reason,received,dropped,retries);
 /* Confirm the browser close promptly; native teardown remains bounded and
  * acknowledges pending media without sending it back to the browser. */
 talk_ws_finish(fd,close_code);native_stream_release(live);
 if(speaker>=0)close(speaker);
 close(fd);memset(password,0,sizeof(password));_exit(0);
}
static int has_upgrade_token(const char*value){
 for(const char*p=value;*p;){const char*end=strchr(p,',');if(!end)end=p+strlen(p);const char*a=p,*z=end;while(a<z&&(*a==' '||*a=='\t'))a++;while(z>a&&(z[-1]==' '||z[-1]=='\t'))z--;if(z-a==7&&!strncasecmp(a,"upgrade",7))return 1;p=*end?end+1:end;}return 0;
}
static void talk_error(int fd,int status,const char*message){http_reply(fd,status,"text/plain",message,strlen(message));}
static void websocket_upgrade(int fd,const char*headers,int mode){
 char host_value[256],origin[280],key[80],version[20],upgrade[40],connection[100],accept[29],expected[280];
 if(header_value(headers,"Host",host_value,sizeof(host_value))!=1||header_value(headers,"Origin",origin,sizeof(origin))!=1||
    header_value(headers,"Sec-WebSocket-Key",key,sizeof(key))!=1||header_value(headers,"Sec-WebSocket-Version",version,sizeof(version))!=1||
    header_value(headers,"Upgrade",upgrade,sizeof(upgrade))!=1||header_value(headers,"Connection",connection,sizeof(connection))!=1){talk_error(fd,400,"Intestazioni interfono non valide");return;}
 snprintf(expected,sizeof(expected),"https://%s",host_value);int allowed=!strcmp(origin,expected);
 snprintf(expected,sizeof(expected),"http://%s",host_value);allowed|=!strcmp(origin,expected);
 if(!allowed||!host_value[0]){talk_error(fd,403,"Origine non ammessa");return;}
 if(strcmp(version,"13")||strcasecmp(upgrade,"websocket")||!has_upgrade_token(connection)||qc5_ws_accept(key,accept)){talk_error(fd,400,"WebSocket non valido");return;}
 talk_reap();pid_t*owner=mode==2?&video_pid:mode?&listen_pid:&talk_pid;if(*owner>0){talk_error(fd,409,"Interfono gia in uso");return;}
 release_idle_buffers(mode==2);
 if(qc5_memory_available_kb()<(mode==2?QC5_VIDEO_ADMISSION_KB:QC5_AUDIO_ADMISSION_KB)){talk_error(fd,503,"Memoria occupata: riprova tra poco");return;}
 char response_header[220];int n=snprintf(response_header,sizeof(response_header),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept);
 if(send_all(fd,response_header,(size_t)n))return;
 pid_t pid=fork();if(pid<0){talk_ws_send(fd,8,"\x03\xf3",2);return;}
 if(!pid){if(mode==2)video_worker(fd);else if(mode)listen_worker(fd);else talk_worker(fd);}
 *owner=pid;
}
#include "qc5_listen.h"
#include "qc5_media.h"
static void talk_upgrade(int fd,const char*headers){websocket_upgrade(fd,headers,0);}
static void listen_upgrade(int fd,const char*headers){websocket_upgrade(fd,headers,1);}
static void video_upgrade(int fd,const char*headers){websocket_upgrade(fd,headers,2);}
#endif

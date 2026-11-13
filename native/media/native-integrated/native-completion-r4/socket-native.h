/* SPDX-License-Identifier: GPL-3.0-only
 * One optional RTSP reader of already encoded native AUs. No encoder, frame
 * queue, resolver, subprocess or vendor IPC. A slow peer loses only RTSP.
 */
#ifndef QC5_NATIVE_RTSP_SOCKET_H
#define QC5_NATIVE_RTSP_SOCKET_H
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <time.h>
#include <unistd.h>
#include "server-info.h"
struct qrl_server {
 int listener,peer;struct qrs_config config;struct qrs_client client;
 uint64_t origin_us,deadline_us,last_report_us;
 char input[2048];unsigned used,rtcp_left,closed;
 unsigned emit_fault,emit_errno,emit_remaining,emit_drop_logs;
 uint64_t last_emit_us;unsigned late_calls,late_bytes,late_logs;
 unsigned interleaved_remaining;
};
static uint64_t qrl_us(clockid_t clock)
{struct timespec t;if(clock_gettime(clock,&t)||t.tv_sec<0)return 0;return (uint64_t)t.tv_sec*1000000u+(unsigned long)t.tv_nsec/1000u;}
static void qrl_drop(struct qrl_server *s)
{
 if(s->peer>=0&&s->emit_fault&&s->emit_drop_logs<3){
  char text[320];int n=snprintf(text,sizeof text,"QC5_RTSP_EMIT_FAILURE cause=%u errno=%u remaining=%u playing=%u video_packets=%u audio_packets=%u late_calls=%u late_bytes=%u\n",s->emit_fault,s->emit_errno,s->emit_remaining,s->client.playing,s->client.rtp.packets[0],s->client.rtp.packets[1],s->late_calls,s->late_bytes);
  if(n>0&&n<(int)sizeof text)(void)write(STDERR_FILENO,text,(unsigned)n);s->emit_drop_logs++;
 }
 if(s->peer>=0){close(s->peer);s->closed++;}s->peer=-1;s->used=s->rtcp_left=s->interleaved_remaining=0;
 memset(&s->client,0,sizeof s->client);
}
static int qrl_emit_failed(struct qrl_server *s,unsigned cause,unsigned error,size_t remaining)
{if(s&&!s->emit_fault){s->emit_fault=cause;s->emit_errno=error;s->emit_remaining=remaining>UINT32_MAX?UINT32_MAX:(unsigned)remaining;}return 0;}
static void qrl_begin_budget(struct qrl_server *s)
{
 uint64_t now=qrl_us(CLOCK_MONOTONIC);s->deadline_us=now&&now<=UINT64_MAX-10000?now+10000:0;
 s->last_emit_us=now;s->late_calls=s->late_bytes=0;
}
static int qrl_emit(void *opaque,const uint8_t *p,size_t n)
{
 struct qrl_server *s=opaque;if(!s)return 0;if(s->peer<0||!p||!n)return qrl_emit_failed(s,1,0,n);
 /* Tiny RTP/STAP headers and length fields share the SAME TCP record
  * with their existing capture bytes. MSG_MORE avoids one radio packet per
  * tiny emit; the final fragment flushes without a copy or extra queue. */
 if(!s->interleaved_remaining&&n>=4&&p[0]=='$'){
  unsigned total=4+((unsigned)p[2]<<8)+p[3];
  if(n>total)return qrl_emit_failed(s,1,0,n);
  s->interleaved_remaining=total;
 }
 if(s->interleaved_remaining&&n>s->interleaved_remaining)return qrl_emit_failed(s,1,0,n);
 while(n){
  uint64_t now=qrl_us(CLOCK_MONOTONIC);if(!now||now<s->last_emit_us||!s->deadline_us)return qrl_emit_failed(s,2,0,n);s->last_emit_us=now;
  unsigned late=now>=s->deadline_us;
  if(late){
   /* Complete the bounded, immediately writable encoded unit after scheduling
    * delay. The original waiting deadline never moves; no late poll is
    * permitted. Slow peers still disconnect without affecting capture. */
   if(s->late_calls>=4096||s->late_bytes>1048576||n>1048576-s->late_bytes)return qrl_emit_failed(s,2,0,n);
   s->late_calls++;
  }
  ssize_t got=send(s->peer,p,n,MSG_NOSIGNAL|MSG_DONTWAIT|(s->interleaved_remaining>n?MSG_MORE:0));
  if(got>0){
   if(s->interleaved_remaining)s->interleaved_remaining-=(unsigned)got;
   if(late){s->late_bytes+=(unsigned)got;if(s->late_calls==1&&s->late_logs<3){char text[180];int bytes=snprintf(text,sizeof text,"QC5_RTSP_LATE_WRITE bytes=%u overdue_us=%llu\n",(unsigned)got,(unsigned long long)(now-s->deadline_us));if(bytes>0&&bytes<(int)sizeof text)(void)write(STDERR_FILENO,text,(unsigned)bytes);s->late_logs++;}}
   p+=got;n-=(size_t)got;continue;
  }
  if(got<0&&errno==EINTR){if(late)return qrl_emit_failed(s,2,0,n);continue;}
  if(got>=0||(errno!=EAGAIN&&errno!=EWOULDBLOCK))return qrl_emit_failed(s,3,got<0?(unsigned)errno:0,n);
  if(late)return qrl_emit_failed(s,2,0,n);
  /* Backpressure is temporary, not a disconnect. All fragments/headers
   * retain the caller's ONE absolute ten-ms budget. Never renew it here,
   * allocate a queue, or leave a partial RTP record in a surviving client. */
  now=qrl_us(CLOCK_MONOTONIC);if(!now||now<s->last_emit_us)return qrl_emit_failed(s,2,0,n);s->last_emit_us=now;if(now>=s->deadline_us)continue;
  struct pollfd ready={.fd=s->peer,.events=POLLOUT};
  int ms=(int)((s->deadline_us-now+999)/1000),result=poll(&ready,1,ms);
  if(result<0&&errno==EINTR)continue;
  if(!result)continue;
  if(result>0&&(ready.revents&POLLOUT)&&!(ready.revents&(POLLERR|POLLHUP|POLLNVAL)))continue;
  return qrl_emit_failed(s,result==0?2:4,result<0?(unsigned)errno:0,n);
 }
 return 1;
}
static int qrl_open(struct qrl_server *s,uint32_t address,unsigned port,uint64_t origin_us)
{
 if(!s||port>65535||!origin_us)return 0;
 memset(s,0,sizeof *s);s->listener=s->peer=-1;s->origin_us=origin_us;
 int fd=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return 0;
 int one=1;struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((uint16_t)port),.sin_addr.s_addr=htonl(address)};
 if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one)||bind(fd,(void*)&a,sizeof a)||listen(fd,2)){close(fd);return 0;}
 s->listener=fd;return 1;
}
static void qrl_close(struct qrl_server *s)
{if(!s)return;qrl_drop(s);if(s->listener>=0)close(s->listener);s->listener=-1;}
static int qrl_accept(struct qrl_server *s)
{
 int fd=accept4(s->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);if(fd<0)return errno==EAGAIN||errno==EINTR;
 if(s->peer>=0){static const char busy[]="RTSP/1.0 503 Service Unavailable\r\nCSeq: 0\r\nContent-Length: 0\r\n\r\n";
  (void)send(fd,busy,sizeof busy-1,MSG_NOSIGNAL|MSG_DONTWAIT);close(fd);return 1;}
 int send_capacity=16384,receive_capacity=4096,nodelay=1;
 if(setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&send_capacity,sizeof send_capacity)||
    setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&receive_capacity,sizeof receive_capacity)||
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&nodelay,sizeof nodelay)){close(fd);return 1;}
 uint8_t seed[16];if(getrandom(seed,sizeof seed,GRND_NONBLOCK)!=(ssize_t)sizeof seed){close(fd);return 1;}
 char session[17];for(unsigned i=0;i<8;i++){session[2*i]="0123456789abcdef"[seed[i]>>4];session[2*i+1]="0123456789abcdef"[seed[i]&15];}session[16]=0;
 uint32_t video,audio;memcpy(&video,seed+8,4);memcpy(&audio,seed+12,4);if(!video)video=1;if(!audio||audio==video)audio=video^0x80000000u;if(!audio)audio=2;
 s->peer=fd;s->used=s->rtcp_left=s->interleaved_remaining=0;s->last_report_us=0;s->emit_fault=s->emit_errno=s->emit_remaining=0;
 if(!qrs_begin(&s->client,&s->config,s,qrl_emit,s->origin_us,video,audio,session))qrl_drop(s);
 return 1;
}
static int qrl_rtcp_channel(struct qrl_server *s,unsigned channel)
{for(unsigned track=0;track<2;track++)if(s->client.setup[track]&&channel==s->client.rtp.channel[track]+1)return 1;return 0;}
static void qrl_consume(struct qrl_server *s,unsigned *requests)
{
  while(s->used&&s->peer>=0&&*requests<4){
   if(s->rtcp_left){unsigned n=s->used<s->rtcp_left?s->used:s->rtcp_left;
    memmove(s->input,s->input+n,s->used-n);s->used-=n;s->rtcp_left-=n;continue;}
   if(s->input[0]=='$'){
    if(s->used<4)break;unsigned n=((uint8_t)s->input[2]<<8)|(uint8_t)s->input[3];
    if(!qrl_rtcp_channel(s,(uint8_t)s->input[1])||n<4||n>1500){qrl_drop(s);break;}
    memmove(s->input,s->input+4,s->used-4);s->used-=4;s->rtcp_left=n;continue;
   }
   unsigned n=0;for(unsigned i=3;i<s->used;i++)if(!memcmp(s->input+i-3,"\r\n\r\n",4)){n=i+1;break;}
   if(!n)break;qrl_begin_budget(s);
   s->client.play_us=qrl_us(CLOCK_MONOTONIC);
   int result=qrs_request(&s->client,s->input,n);(*requests)++;
   if(result){qrl_drop(s);break;}memmove(s->input,s->input+n,s->used-n);s->used-=n;
  }
}
static void qrl_poll(struct qrl_server *s)
{
 if(!s||s->listener<0)return;(void)qrl_accept(s);if(s->peer<0)return;
 uint64_t end=qrl_us(CLOCK_MONOTONIC)+1000;unsigned requests=0;
 qrl_consume(s,&requests);
 while(s->peer>=0&&requests<4&&qrl_us(CLOCK_MONOTONIC)<end){
  if(s->used==sizeof s->input){qrl_drop(s);break;}
  ssize_t got=recv(s->peer,s->input+s->used,sizeof s->input-s->used,MSG_DONTWAIT);
  if(got<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;if(got<0&&errno==EINTR)continue;
  if(got<=0){qrl_drop(s);break;}s->used+=(unsigned)got;qrl_consume(s,&requests);
 }
}
static int qrl_config(struct qrl_server *s,const uint8_t *p,size_t n)
{
 if(!p||!n||n>QNR_AU_MAX||!qnr_prefix(p,n,0))return 0;size_t at=0;
 while(at<n){unsigned prefix=qnr_prefix(p,n,at);if(!prefix)return 0;size_t first=at+prefix;at=first;
  while(at<n&&!qnr_prefix(p,n,at))at++;size_t end=at;while(end>first&&!p[end-1])end--;if(end==first)return 0;
  unsigned type=p[first]&31;if(type!=7&&type!=8)continue;size_t len=end-first;if(len>256)return 0;
  uint8_t *dest=type==7?s->config.sps:s->config.pps;unsigned *size=type==7?&s->config.sps_n:&s->config.pps_n;
  if(*size&&(*size!=len||memcmp(dest,p+first,len)))qrl_drop(s);
  memcpy(dest,p+first,len);*size=(unsigned)len;
 }
 return 1;
}
/* Derive wall time afresh for every sender report. An NTP step must not
 * leave SRs anchored to the pre-synchronization boot clock. RTP itself keeps
 * its monotonic epoch, so a wall-clock correction cannot rewind media. */
static int qrl_wall_at(uint64_t media_us,uint64_t *wall)
{
 uint64_t mono=qrl_us(CLOCK_MONOTONIC),real=qrl_us(CLOCK_REALTIME);
 if(!mono||!real)return 0;
 if(media_us<=mono){uint64_t age=mono-media_us;if(age>=real)return 0;*wall=real-age;}
 else{uint64_t lead=media_us-mono;if(lead>1000000u||lead>UINT64_MAX-real)return 0;*wall=real+lead;}
 return 1;
}
static int qrl_report(struct qrl_server *s,unsigned track,uint64_t media_us)
{
 struct qnr_session *r=&s->client.rtp;if(media_us<s->origin_us)return 0;
 uint64_t delta=media_us-s->origin_us,unix_us;if(!qrl_wall_at(media_us,&unix_us))return 0;
 uint32_t rate=track?16000:90000,stamp=(uint32_t)((delta/1000000u)*rate+(delta%1000000u)*rate/1000000u);
 /* Compound SR + SDES(CNAME), RFC3550. Common real/monotonic epoch for both
  * tracks, with actual encoded packet/octet counters and no extra queue. */
 uint8_t b[56]={ '$',(uint8_t)(r->channel[track]+1),0,52,0x80,200,0,6 };
 qnr_be32(b+8,r->ssrc[track]);qnr_be32(b+12,(uint32_t)(unix_us/1000000u+2208988800u));
 qnr_be32(b+16,(uint32_t)(((unix_us%1000000u)<<32)/1000000u));qnr_be32(b+20,stamp);
 qnr_be32(b+24,r->packets[track]);qnr_be32(b+28,r->octets[track]);
 b[32]=0x81;b[33]=202;b[35]=5;qnr_be32(b+36,r->ssrc[track]);b[40]=1;b[41]=10;memcpy(b+42,"qc5-native",10);
 return qrl_emit(s,b,sizeof b);
}
static void qrl_reports(struct qrl_server *s,uint64_t media_us)
{
 if(s->peer<0||!s->client.playing||media_us<s->origin_us||
  (s->last_report_us&&(media_us<=s->last_report_us||media_us-s->last_report_us<2000000u)))return;
 for(unsigned track=0;track<2;track++)if(s->client.setup[track]&&s->client.rtp.seen[track]&&!qrl_report(s,track,media_us)){qrl_drop(s);return;}
 s->last_report_us=media_us;
}
static void qrl_video(struct qrl_server *s,const uint8_t *p,size_t n,uint64_t capture_us,int keyframe)
{
 if(!s||!qrl_config(s,p,n)){if(s)qrl_drop(s);return;}
 if(s->peer<0||!s->client.playing||!s->client.setup[0])return;
 if(s->client.wait_key&&!keyframe)return;s->client.wait_key=0;qrl_begin_budget(s);
 if(!s->client.rtp.seen[0]&&!qrl_report(s,0,capture_us)){qrl_drop(s);return;}
 if(!qnr_video(&s->client.rtp,p,n,capture_us))qrl_drop(s);else qrl_reports(s,capture_us);
}
static void qrl_audio(struct qrl_server *s,const uint8_t *p,size_t n,uint64_t first_sample_us)
{
 if(!s||s->peer<0||!s->client.playing||!s->client.setup[1]||(s->client.setup[0]&&s->client.wait_key))return;
 qrl_begin_budget(s);
 if(!s->client.rtp.seen[1]&&!qrl_report(s,1,first_sample_us)){qrl_drop(s);return;}
 if(!qnr_audio(&s->client.rtp,p,n,first_sample_us))qrl_drop(s);else qrl_reports(s,first_sample_us);
}
#endif

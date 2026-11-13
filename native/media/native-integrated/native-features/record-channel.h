/* SPDX-License-Identifier: GPL-3.0-only
 * One local socketpair to the file sink;8ms per encoded unit,4KiB chunks.
 * Linux copies each chunk before sendmsg returns. The next encode may reuse
 * its arena even when the writer is blocked. No whole-AU queue exists.
 */
#ifndef QC5_RECORD_CHANNEL_H
#define QC5_RECORD_CHANNEL_H
#include <errno.h>
#include <stdio.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "mkv-native.h"
enum {QRC_READY=1,QRC_BEGIN_VIDEO=2,QRC_BEGIN_AUDIO=3,QRC_DATA=4,QRC_COMMIT=5,QRC_DONE=6,QRC_ERROR=7,QRC_DATA_COMMIT=8,QRC_FIRST_VIDEO=9,QRC_FIRST_AUDIO=10,QRC_WHOLE_VIDEO=11,QRC_WHOLE_AUDIO=12};
#define QRC_MAGIC 0x37335251u
struct qrc_header {uint32_t magic,type,serial,bytes;};
_Static_assert(sizeof(struct qrc_header)==16,"Local fixed wire header");
/* Combine small Matroska headers before passing them to SOCK_SEQPACKET.
 * Each datagram also consumes kernel bookkeeping: sending a separate packet
 * for every EBML integer can fill the socket while carrying very few bytes.
 * This scratch area never retains an AU beyond its synchronous transaction. */
struct qrc_client {int fd;uint32_t serial;unsigned active,failed;uint64_t deadline_us,last_clock_us;unsigned buffered,late_calls,late_bytes,late_reports,audio,started;uint8_t pending[512];};
static uint64_t qrc_clock(void)
{struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000u+t.tv_nsec/1000u;}
static int qrc_send(struct qrc_client *c,unsigned type,const void *data,size_t n)
{
 if(!c||c->fd<0||c->failed||n>4096||(!data&&n))return 0;
 unsigned wire_type=type;
 if(!c->started&&(type==QRC_DATA||type==QRC_DATA_COMMIT))
  wire_type=type==QRC_DATA?(c->audio?QRC_FIRST_AUDIO:QRC_FIRST_VIDEO):(c->audio?QRC_WHOLE_AUDIO:QRC_WHOLE_VIDEO);
 struct qrc_header h={QRC_MAGIC,wire_type,c->serial,(uint32_t)n};struct iovec v[2]={{&h,sizeof h},{(void*)data,n}};
 struct msghdr msg={.msg_iov=v,.msg_iovlen=n?2:1};
 unsigned cause=0;int failure_errno=0;
 for(;;){
  uint64_t now=qrc_clock();if(!now||now<c->last_clock_us||!c->deadline_us){cause=1;break;}c->last_clock_us=now;
  unsigned late=now>=c->deadline_us;
  if(late){
   /* A descheduled producer may resume after its waiting budget. Try
    * the current packet without waiting; successful sends consume a fixed
    * per-unit byte/call allowance. No AU queue or renewed deadline exists.
    * A blocked socket still fails immediately after the original8ms. */
   if(c->late_calls>=512||c->late_bytes>791040||n>791040-c->late_bytes||
      !((!c->active&&!n&&(type==QRC_BEGIN_VIDEO||type==QRC_BEGIN_AUDIO))||
        (c->active&&(((type==QRC_DATA||type==QRC_DATA_COMMIT)&&n)||(type==QRC_COMMIT&&!n))))){cause=1;break;}
   c->late_calls++;
  }
  ssize_t got=sendmsg(c->fd,&msg,MSG_DONTWAIT|MSG_NOSIGNAL);
  if(got==(ssize_t)(sizeof h+n)){if(late)c->late_bytes+=(unsigned)n;if(type==QRC_DATA||type==QRC_DATA_COMMIT||type==QRC_BEGIN_VIDEO||type==QRC_BEGIN_AUDIO)c->started=1;return 1;}
  if(got<0&&errno==EINTR){if(late){cause=1;break;}continue;}
  if(got>=0||(errno!=EAGAIN&&errno!=EWOULDBLOCK)){cause=2;failure_errno=got<0?errno:0;break;}
  if(late){cause=3;break;}
  now=qrc_clock();if(!now||now<c->last_clock_us){cause=1;break;}c->last_clock_us=now;
  if(now>=c->deadline_us)continue;
  struct pollfd f={c->fd,POLLOUT,0};int wait=(int)((c->deadline_us-now+999)/1000);
  int ready=poll(&f,1,wait);
  if(ready<0&&errno==EINTR)continue;
  if(!ready)continue;
  if(ready<=0||!(f.revents&POLLOUT)){cause=3;failure_errno=ready<0?errno:0;break;}
 }
 fprintf(stderr,"QC5_RECORD_CHANNEL_FAILURE cause=%u type=%u serial=%u bytes=%u errno=%d\n",cause,type,c->serial,(unsigned)n,failure_errno);
 c->failed=1;(void)shutdown(c->fd,SHUT_WR);return 0;
}
static int qrc_begin(struct qrc_client *c,unsigned audio)
{
 if(!c||audio>1||c->active||c->failed||c->buffered||c->serial==UINT32_MAX)return 0;
 uint64_t now=qrc_clock();if(!now||now<c->last_clock_us||now>UINT64_MAX-8000)return 0;
 c->serial++;c->deadline_us=now+8000;c->last_clock_us=now;c->late_calls=c->late_bytes=0;
 /* First payload also carries BEGIN. Do not spend a datagram on an
  * empty control record before every video or microphone packet. */
 c->audio=audio;c->started=0;c->active=1;return 1;
}
static int qrc_sink(void *opaque,const void *data,size_t n)
{
 struct qrc_client *c=opaque;if(!c||!c->active||!data||!n)return 0;
 if(c->failed||c->buffered>sizeof c->pending)return 0;
 const uint8_t *p=data;
 while(n){
  /* Retain only the existing scratch tail so the final payload and its
   * commit share one atomic datagram. No additional AU/PCM queue exists. */
  if(c->buffered==sizeof c->pending){if(!qrc_send(c,QRC_DATA,c->pending,c->buffered))return 0;c->buffered=0;}
  if(!c->buffered&&n>sizeof c->pending){
   size_t chunk=n>4096+sizeof c->pending?4096:n-sizeof c->pending;
   if(!qrc_send(c,QRC_DATA,p,chunk))return 0;p+=chunk;n-=chunk;
  }else{
   size_t chunk=sizeof c->pending-c->buffered;if(chunk>n)chunk=n;
   memcpy(c->pending+c->buffered,p,chunk);c->buffered+=(unsigned)chunk;p+=chunk;n-=chunk;
  }
 }
 return 1;
}
static int qrc_commit(struct qrc_client *c)
{
 if(!c||!c->active||c->buffered>sizeof c->pending)return 0;
 int ok=1;
 if(!c->buffered&&!c->started)ok=qrc_send(c,c->audio?QRC_BEGIN_AUDIO:QRC_BEGIN_VIDEO,NULL,0);
 if(ok)ok=c->buffered?qrc_send(c,QRC_DATA_COMMIT,c->pending,c->buffered):qrc_send(c,QRC_COMMIT,NULL,0);
 if(ok)c->buffered=0;c->active=0;
 if(ok&&c->late_calls&&c->late_reports<3){fprintf(stderr,"QC5_RECORD_LATE_TAIL serial=%u calls=%u bytes=%u\n",c->serial,c->late_calls,c->late_bytes);c->late_reports++;}
 return ok;
}
static int qrc_receive(int fd,struct qrc_header *h,void *data,size_t capacity,int flags)
{
 if(!h||capacity>4096||(!data&&capacity))return -1;
 struct iovec v[2]={{h,sizeof *h},{data,capacity}};struct msghdr m={.msg_iov=v,.msg_iovlen=capacity?2:1};
 ssize_t n=recvmsg(fd,&m,flags);
 if(n==0)return 0;if(n<0)return errno==EAGAIN||errno==EWOULDBLOCK?2:-1;
 if(m.msg_flags&(MSG_TRUNC|MSG_CTRUNC)||n<(ssize_t)sizeof *h||h->magic!=QRC_MAGIC||h->bytes>capacity||n!=(ssize_t)(sizeof *h+h->bytes))return -1;
 return 1;
}
#endif

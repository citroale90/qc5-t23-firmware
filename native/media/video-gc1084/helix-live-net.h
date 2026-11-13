/* SPDX-License-Identifier: GPL-2.0-or-later
 * Single LAN reader. No frame queues, heap, DNS, default route or Internet I/O.
 * A slow/disconnected reader loses the connection, never part of a reference
 * chain. The producer must begin a new IDR after every newly accepted stream.
 */
#ifndef QC5_HELIX_LIVE_NET_H
#define QC5_HELIX_LIVE_NET_H
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#ifdef QC5_AUDIO_DUPLEX
#include <netinet/tcp.h>
#endif
#include <time.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#ifdef QC5_IMAGE_CONTINUOUS
typedef uint64_t qc5_net_count;
#define QC5_NET_COUNT_MAX UINT64_MAX
#else
typedef uint32_t qc5_net_count;
#define QC5_NET_COUNT_MAX UINT32_MAX
#endif
static void qc5_net_inc(qc5_net_count *n){if(*n<QC5_NET_COUNT_MAX)++*n;}
struct qc5_live_net {
    int listener,client,stream;
    qc5_net_count accepted,requests,disconnects,timeouts,frames,bytes;
    uint32_t max_send_us;
    uint64_t request_deadline,send_deadline,send_last_us;
    unsigned send_late_calls,send_late_bytes,send_fault_logs;
    unsigned int used;
#ifdef QC5_CAPTURE_SERVICE
    char request[4096],release[80],extra_header[64];
#else
    char request[512],release[80],extra_header[64];
#endif
};
#ifdef QC5_CAPTURE_SERVICE
static int qc5_service_upgrade(struct qc5_live_net *);
#endif
#ifndef QC5_NET_SEND_BUDGET_US
#define QC5_NET_SEND_BUDGET_US 180000u
#endif
static uint64_t qc5_net_us(void)
{
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0)return 0;
    return (uint64_t)t.tv_sec*1000000u+(uint32_t)t.tv_nsec/1000u;
}
static void qc5_net_close(struct qc5_live_net *s)
{
    if(s->client>=0){close(s->client);qc5_net_inc(&s->disconnects);}
    s->client=-1;s->stream=0;s->used=0;s->request_deadline=0;
}
static int qc5_net_flags(int fd)
{
    int f=fcntl(fd,F_GETFL),d=fcntl(fd,F_GETFD);
    return f>=0&&d>=0&&!fcntl(fd,F_SETFL,f|O_NONBLOCK)&&!fcntl(fd,F_SETFD,d|FD_CLOEXEC);
}
static int qc5_net_open(struct qc5_live_net *s,uint32_t address,unsigned int port)
{
    if(!s||port>65535)return 0;
    memset(s,0,sizeof(*s));s->client=s->listener=-1;
    struct utsname u;if(uname(&u))return 0;
    snprintf(s->release,sizeof(s->release),"%s",u.release);
    s->listener=socket(AF_INET,SOCK_STREAM,0);if(s->listener<0)return 0;
    int one=1;struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((uint16_t)port),.sin_addr.s_addr=htonl(address)};
    if(!qc5_net_flags(s->listener)||setsockopt(s->listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one))||
        bind(s->listener,(struct sockaddr *)&a,sizeof(a))||listen(s->listener,1)){
        close(s->listener);s->listener=-1;return 0;}
    return 1;
}
/* One bounded 180ms budget for an entire access unit, not per chunk.
 * Real detailed 720p frames exceed 250 KiB; both 20ms and 100ms truncated
 * complete access units on Wi-Fi. A slow client is still disconnected.
 * No extra queue, buffer, resolution or QP change is involved. */
/* A WebSocket header and its existing payload are submitted together. A
 * preempted writer may finish immediately writable bytes, with one unchanged
 * deadline, no late poll and a bounded total per access unit. */
static int qc5_net_write_pair_until(struct qc5_live_net *s,const void *data,unsigned bytes,
                                   const void *body,unsigned body_bytes,int frame,uint64_t deadline)
{
 uint64_t start=qc5_net_us();unsigned total=bytes+body_bytes,at=0,cause=0;
 if(!s||s->client<0||!data||!bytes||(!body&&body_bytes)||bytes>786944||body_bytes>786944-bytes||
    !start||!deadline||frame<0||frame>1||(deadline>start&&deadline-start>QC5_NET_SEND_BUDGET_US))return 0;
 if(frame&&(s->frames==QC5_NET_COUNT_MAX||s->bytes>QC5_NET_COUNT_MAX-total)){qc5_net_close(s);return 0;}
 if(s->send_last_us&&start<s->send_last_us)return 0;
 if(s->send_deadline!=deadline){s->send_deadline=deadline;s->send_late_calls=s->send_late_bytes=0;}
 s->send_last_us=start;
 struct iovec vec[2]={{(void*)data,bytes},{(void*)body,body_bytes}};unsigned first=0;
 while(at<total){
  uint64_t now=qc5_net_us();if(!now||now<s->send_last_us){cause=1;break;}s->send_last_us=now;
  unsigned late=now>=deadline;
  if(late){if(s->send_late_calls>=4096||s->send_late_bytes>1048576||total-at>1048576-s->send_late_bytes){cause=2;break;}s->send_late_calls++;}
  struct msghdr msg={.msg_iov=vec+first,.msg_iovlen=(body_bytes?2:1)-first};
  ssize_t n=sendmsg(s->client,&msg,MSG_NOSIGNAL|MSG_DONTWAIT);
  if(n>0){
   if((unsigned)n>total-at){cause=3;break;}
   unsigned used=(unsigned)n;at+=used;if(late)s->send_late_bytes+=used;
   while(used&&first<(body_bytes?2u:1u)){
    if(used<vec[first].iov_len){vec[first].iov_base=(char*)vec[first].iov_base+used;vec[first].iov_len-=used;used=0;}
    else{used-=(unsigned)vec[first].iov_len;first++;}
   }
   continue;
  }
  if(n<0&&errno==EINTR){if(late){cause=2;break;}continue;}
  if(n>=0||(errno!=EAGAIN&&errno!=EWOULDBLOCK)){cause=3;break;}
  if(late){cause=2;break;}
  now=qc5_net_us();if(!now||now<s->send_last_us){cause=1;break;}s->send_last_us=now;if(now>=deadline)continue;
  struct pollfd ready={.fd=s->client,.events=POLLOUT};int rc=poll(&ready,1,(int)((deadline-now+999)/1000));
  if(rc<0&&errno==EINTR)continue;if(!rc)continue;
  if(rc>0&&(ready.revents&POLLOUT)&&!(ready.revents&(POLLERR|POLLHUP|POLLNVAL)))continue;
  cause=4;break;
 }
 uint64_t end=qc5_net_us();if(end>=start&&end-start>s->max_send_us)s->max_send_us=end-start>UINT32_MAX?UINT32_MAX:(uint32_t)(end-start);
 if(at!=total){
  if(s->send_fault_logs<3){fprintf(stderr,"QC5_WEB_SEND_FAILURE cause=%u sent=%u total=%u late_calls=%u late_bytes=%u\n",cause,at,total,s->send_late_calls,s->send_late_bytes);s->send_fault_logs++;}
  qc5_net_inc(&s->timeouts);qc5_net_close(s);return 0;
 }
 if(frame){++s->frames;s->bytes+=total;}return 1;
}
static int qc5_net_write_until(struct qc5_live_net *s,const void *data,unsigned bytes,int frame,uint64_t deadline)
{return qc5_net_write_pair_until(s,data,bytes,NULL,0,frame,deadline);}
static int qc5_net_write(struct qc5_live_net *s,const void *data,unsigned int bytes,int frame)
{
    uint64_t now=qc5_net_us();
    if(!now||now>UINT64_MAX-QC5_NET_SEND_BUDGET_US)return 0;
    return qc5_net_write_until(s,data,bytes,frame,now+QC5_NET_SEND_BUDGET_US);
}
/* The old512-byte proof must never silently truncate service counters. */
static int qc5_net_note32(const struct qc5_live_net *s,uint32_t words[6])
{
    if(!s||!words||s->accepted>UINT32_MAX||s->requests>UINT32_MAX||s->disconnects>UINT32_MAX||
       s->timeouts>UINT32_MAX||s->frames>UINT32_MAX||s->bytes>UINT32_MAX)return 0;
    const uint32_t value[6]={(uint32_t)s->accepted,(uint32_t)s->requests,(uint32_t)s->disconnects,
        (uint32_t)s->timeouts,(uint32_t)s->frames,(uint32_t)s->bytes};
    memcpy(words,value,sizeof value);return 1;
}
/* Return 1 only when a newly completed HTTP request starts a fresh stream. */
static int qc5_net_tick(struct qc5_live_net *s)
{
    if(!s||s->listener<0)return 0;
    int fd=accept(s->listener,NULL,NULL);
    if(fd>=0){
        if(s->client>=0||!qc5_net_flags(fd)){close(fd);}
        else{
            int bytes=65536;
            if(setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&bytes,sizeof(bytes))){close(fd);return 0;}
#ifdef QC5_AUDIO_DUPLEX
            int immediate=1;
            if(setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&immediate,sizeof immediate)){close(fd);return 0;}
#endif
            s->client=fd;qc5_net_inc(&s->accepted);s->request_deadline=qc5_net_us()+1000000;
        }
    }
    if(s->client<0||s->stream)return 0;
    uint64_t now=qc5_net_us();
    if(!now||now>=s->request_deadline||s->used>=sizeof(s->request)-1){qc5_net_close(s);return 0;}
    ssize_t n=recv(s->client,s->request+s->used,sizeof(s->request)-1-s->used,0);
    if(n==0||(n<0&&errno!=EINTR&&errno!=EAGAIN&&errno!=EWOULDBLOCK)){qc5_net_close(s);return 0;}
    if(n<=0)return 0;
    /* No embedded NUL: a truncated request must not acquire another meaning. */
    if(memchr(s->request+s->used,0,(size_t)n)){qc5_net_close(s);return 0;}
    s->used+=(unsigned int)n;s->request[s->used]=0;
    char *end=strstr(s->request,"\r\n\r\n");if(!end)return 0;
    if(end+4!=s->request+s->used){qc5_net_close(s);return 0;}
    qc5_net_inc(&s->requests);
#ifdef QC5_CAPTURE_SERVICE
    if(!strncmp(s->request,"GET /media HTTP/1.1\r\n",21))return qc5_service_upgrade(s);
#endif
    char reply[256];
    if(!strncmp(s->request,"GET /live.h264 HTTP/1.1\r\n",25)||
        !strncmp(s->request,"GET /live.h264 HTTP/1.0\r\n",25)){
        int len=snprintf(reply,sizeof(reply),"HTTP/1.1 200 OK\r\nContent-Type: video/h264\r\nCache-Control: no-store\r\nConnection: close\r\nX-QC5-Kernel: %s\r\n%s\r\n",s->release,s->extra_header);
        if(len<=0||(size_t)len>=sizeof(reply)||!qc5_net_write(s,reply,(unsigned int)len,0))return 0;
        s->stream=1;return 1;
    }
    static const char bad[]="HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    (void)qc5_net_write(s,bad,sizeof(bad)-1,0);qc5_net_close(s);return 0;
}
static void qc5_net_finish(struct qc5_live_net *s)
{
    qc5_net_close(s);if(s->listener>=0)close(s->listener);s->listener=-1;
}
#endif

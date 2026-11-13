/* SPDX-License-Identifier: GPL-3.0-only
 * One HTTP listener delegates an accepted browser socket to the media owner.
 * Only a bounded HTTP header is copied. Video/audio stay on the original TCP
 * socket: no second encoder, proxy stream, frame copy or buffering queue.
 * The connected AF_UNIX/SEQPACKET channel must belong to the same uid.
 */
#ifndef QC5_CLIENT_HANDOFF_H
#define QC5_CLIENT_HANDOFF_H
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#define QCF_HEADER_MAX 4095u
#define QCF_TTL_US UINT64_C(1000000)
struct qcf_client {int fd;unsigned length;char request[QCF_HEADER_MAX+1];};
static uint64_t qcf_now(void)
{struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?0:(uint64_t)t.tv_sec*1000000u+t.tv_nsec/1000u;}
static void qcf_put64(unsigned char *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(v>>(i*8));}
static uint64_t qcf_get64(const unsigned char *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static int qcf_channel(int fd)
{
 int type=0;socklen_t n=sizeof type;if(getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&n)||type!=SOCK_SEQPACKET)return 0;
#ifdef __linux__
 struct ucred peer;n=sizeof peer;
 return !getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&n)&&n==sizeof peer&&peer.uid==geteuid();
#else
 return 0; /* This transport targets the Linux camera and Linux test VM. */
#endif
}
static int qcf_request(const char *p,unsigned n)
{
 if(!p||n<25||n>QCF_HEADER_MAX||memchr(p,0,n)||memcmp(p+n-4,"\r\n\r\n",4))return 0;
 if(memcmp(p,"GET /media HTTP/1.1\r\n",21)&&
    (n<29||(memcmp(p,"GET /live.h264 HTTP/1.1\r\n",25)&&memcmp(p,"GET /live.h264 HTTP/1.0\r\n",25))))return 0;
 for(unsigned i=0;i+4<n;i++)if(!memcmp(p+i,"\r\n\r\n",4))return 0;
 return 1;
}
static int qcf_tcp(int fd)
{
 int type=0;socklen_t n=sizeof type;struct sockaddr_storage peer;socklen_t len=sizeof peer;
 return !getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&n)&&type==SOCK_STREAM&&!getpeername(fd,(struct sockaddr*)&peer,&len)&&
        (peer.ss_family==AF_INET||peer.ss_family==AF_INET6);
}
/* Success transfers a reference. The caller then closes its own client fd.
 * EAGAIN leaves ownership with the caller; there is no retry queue here. */
static int qcf_send(int channel,int client,const char *request,unsigned bytes,uint64_t deadline)
{
 uint64_t now=qcf_now();if(!qcf_channel(channel)||!qcf_tcp(client)||!qcf_request(request,bytes)||!now||deadline<=now||deadline-now>QCF_TTL_US)return -1;
 unsigned char header[16]={'Q','C','F','7',1,0,0,0};header[6]=bytes;header[7]=bytes>>8;qcf_put64(header+8,deadline);
 struct iovec iov[2]={{header,sizeof header},{(void*)request,bytes}};
 union {struct cmsghdr aligned;unsigned char bytes[CMSG_SPACE(sizeof(int))];} control;
 memset(&control,0,sizeof control);struct msghdr message={0};message.msg_iov=iov;message.msg_iovlen=2;message.msg_control=control.bytes;message.msg_controllen=sizeof control.bytes;
 struct cmsghdr *c=CMSG_FIRSTHDR(&message);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(c),&client,sizeof client);
 ssize_t n=sendmsg(channel,&message,MSG_DONTWAIT|MSG_NOSIGNAL);
 if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
 return n==(ssize_t)(sizeof header+bytes)?1:-1;
}
/* Every received descriptor is closed on malformed/truncated/expired input.
 * MSG_CMSG_CLOEXEC prevents a concurrent exec from leaking an unvalidated fd.
 */
static int qcf_receive(int channel,struct qcf_client *out)
{
 if(!out)return -1;out->fd=-1;out->length=0;out->request[0]=0;
 if(!qcf_channel(channel))return -1;
 unsigned char data[16+QCF_HEADER_MAX];struct iovec iov={data,sizeof data};
 /* Receive the Linux SCM_RIGHTS maximum, closing every surplus descriptor.
  * A tiny ancillary buffer delegates cleanup to truncation semantics, which
  * also differ in user-mode emulators. This bounded1KiB buffer avoids that
  * ambiguity and cannot create a persistent queue or image allocation. */
 union {struct cmsghdr aligned;unsigned char bytes[CMSG_SPACE(253*sizeof(int))];} control;
 memset(&control,0,sizeof control);struct msghdr message={0};message.msg_iov=&iov;message.msg_iovlen=1;message.msg_control=control.bytes;message.msg_controllen=sizeof control.bytes;
 ssize_t n=recvmsg(channel,&message,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
 if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;if(n<0)return -1;
 int fds[4],bad=0;unsigned count=0;
 for(struct cmsghdr *c=CMSG_FIRSTHDR(&message);c;c=CMSG_NXTHDR(&message,c)){
  if(c->cmsg_level!=SOL_SOCKET||c->cmsg_type!=SCM_RIGHTS||c->cmsg_len<CMSG_LEN(0)){bad=1;continue;}
  size_t bytes=c->cmsg_len-CMSG_LEN(0);if(bytes%sizeof(int)){bad=1;continue;}
  for(size_t i=0;i<bytes/sizeof(int);i++){int fd;memcpy(&fd,(char*)CMSG_DATA(c)+i*sizeof(int),sizeof fd);if(count<4)fds[count++]=fd;else{close(fd);bad=1;}}
 }
 if(message.msg_flags&(MSG_TRUNC|MSG_CTRUNC)||count!=1||n<16)bad=1;
 if(!bad){
  unsigned size=(unsigned)data[6]|(unsigned)data[7]<<8;uint64_t deadline=qcf_get64(data+8),now=qcf_now();
  if(memcmp(data,"QCF7\1\0",6)||n!=(ssize_t)(16+size)||!qcf_request((char*)data+16,size)||!now||deadline<=now||deadline-now>QCF_TTL_US||!qcf_tcp(fds[0]))bad=1;
  if(!bad){
   int flags=fcntl(fds[0],F_GETFL),descriptor=fcntl(fds[0],F_GETFD);
   if(flags<0||descriptor<0||fcntl(fds[0],F_SETFL,flags|O_NONBLOCK)||fcntl(fds[0],F_SETFD,descriptor|FD_CLOEXEC))bad=1;
   else{out->fd=fds[0];out->length=size;memcpy(out->request,data+16,size);out->request[size]=0;return 1;}
  }
 }
 for(unsigned i=0;i<count;i++)close(fds[i]);return -1;
}
#endif

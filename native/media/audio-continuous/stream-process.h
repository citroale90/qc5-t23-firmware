/* SPDX-License-Identifier: GPL-2.0-or-later
 * Small, bounded, connected local process interface. No device operations.
 */
#ifndef QC5_AUDIO_STREAM_PROCESS_H
#define QC5_AUDIO_STREAM_PROCESS_H
#include <fcntl.h>
#include <sys/un.h>
#include "stream-ipc.h"
static int qasi_private_socket(int fd)
{
 int type=0;socklen_t n=sizeof type;struct sockaddr_un local,peer;
 if(fd<0||getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&n)||n!=sizeof type||type!=SOCK_SEQPACKET)return 0;
 n=sizeof local;if(getsockname(fd,(void*)&local,&n)||n<sizeof local.sun_family||local.sun_family!=AF_UNIX)return 0;
 n=sizeof peer;if(getpeername(fd,(void*)&peer,&n)||n<sizeof peer.sun_family||peer.sun_family!=AF_UNIX)return 0;
 int flags=fcntl(fd,F_GETFL),clo=fcntl(fd,F_GETFD);
 return flags>=0&&clo>=0&&!fcntl(fd,F_SETFL,flags|O_NONBLOCK)&&!fcntl(fd,F_SETFD,clo|FD_CLOEXEC);
}
struct qasi_sender {int fd;uint8_t nonce[16],record[QAS_IPC_MAX];};
static int qasi_process_pcm(void *opaque,const uint8_t *pcm,size_t n,uint64_t serial,uint64_t us,uint64_t deadline)
{
 struct qasi_sender *s=opaque;(void)deadline;
 if(!s)return -1;
 int bytes=qasi_pack(s->record,sizeof s->record,QAS_IPC_PCM,pcm,n,serial,us,s->nonce);
 return bytes>0?qasi_send(s->fd,s->record,(size_t)bytes):-1;
}
#endif

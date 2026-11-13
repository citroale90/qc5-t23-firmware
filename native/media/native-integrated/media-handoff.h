/* SPDX-License-Identifier: GPL-3.0-only
 * Adapter for the unchanged native producer and WebSocket/mux/audio code.
 * Include after helix-live-net.h, before the service/producer headers.
 */
#ifndef QC5_MEDIA_HANDOFF_H
#define QC5_MEDIA_HANDOFF_H
#include <sys/stat.h>
#include <sys/file.h>
#include "client-handoff.h"
#ifndef QCI_DIRECTORY
#define QCI_DIRECTORY "/run/qc5-native-network"
#endif
static int qci_path(char out[108])
{int n=snprintf(out,108,"%s/media-control.sock",QCI_DIRECTORY);return n>0&&n<108;}
static int qci_directory(void)
{struct stat st;return !lstat(QCI_DIRECTORY,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&077);}
#ifndef QCI_FRONTEND_ONLY
static struct {struct qc5_live_net *owner;int peer,lock;uint64_t deadline;dev_t dev;ino_t ino;char path[108];}qci_control={.peer=-1,.lock=-1};
static void qci_net_finish(struct qc5_live_net *s)
{
 if(qci_control.owner!=s)return;
 qc5_net_finish(s);if(qci_control.peer>=0)close(qci_control.peer);
 struct stat st;if(!lstat(qci_control.path,&st)&&S_ISSOCK(st.st_mode)&&st.st_dev==qci_control.dev&&st.st_ino==qci_control.ino)unlink(qci_control.path);
 if(qci_control.lock>=0)close(qci_control.lock);
 memset(&qci_control,0,sizeof qci_control);qci_control.peer=qci_control.lock=-1;
}
static int qci_lock(void)
{
 char path[108];int n=snprintf(path,sizeof path,"%s/media-owner.lock",QCI_DIRECTORY);if(n<=0||(size_t)n>=sizeof path)return -1;
 int fd=open(path,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);if(fd<0)return -1;struct stat st;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&077)||flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}
 return fd; /* Keep the inode: unlinking a lock permits two owners after a race. */
}
/* Under the lifetime lock, remove only an owned, disconnected socket. A live
 * peer, symlink or foreign object is never replaced during service restart. */
static int qci_remove_stale(const struct sockaddr_un *a)
{
 struct stat before,after;if(lstat(a->sun_path,&before))return errno==ENOENT;
 if(!S_ISSOCK(before.st_mode)||before.st_uid!=geteuid()||(before.st_mode&077))return 0;
 int probe=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(probe<0)return 0;
 int result=connect(probe,(const void*)a,sizeof *a),error=errno;close(probe);
 return result<0&&error==ECONNREFUSED&&!lstat(a->sun_path,&after)&&after.st_dev==before.st_dev&&after.st_ino==before.st_ino&&!unlink(a->sun_path);
}
static int qci_net_open(struct qc5_live_net *s,uint32_t address,unsigned port)
{
 (void)address;(void)port;if(!s||qci_control.owner||!qci_directory())return 0;
 memset(s,0,sizeof *s);s->client=s->listener=-1;struct utsname u;if(uname(&u))return 0;snprintf(s->release,sizeof s->release,"%s",u.release);
 struct sockaddr_un a={.sun_family=AF_UNIX};if(!qci_path(a.sun_path))return 0;
 qci_control.lock=qci_lock();if(qci_control.lock<0)return 0;qci_control.owner=s;
 if(!qci_remove_stale(&a))goto failed;
 s->listener=socket(AF_UNIX,SOCK_SEQPACKET,0);if(s->listener<0)goto failed;
 if(!qc5_net_flags(s->listener)||bind(s->listener,(void*)&a,sizeof a))goto failed;
 struct stat st;if(lstat(a.sun_path,&st)||!S_ISSOCK(st.st_mode))goto failed;
 qci_control.dev=st.st_dev;qci_control.ino=st.st_ino;strcpy(qci_control.path,a.sun_path);
 if(chmod(a.sun_path,0600)||listen(s->listener,4)){qci_net_finish(s);return 0;}return 1;
failed:qci_net_finish(s);return 0;
}
static int qci_net_tick(struct qc5_live_net *s)
{
 if(!s||qci_control.owner!=s||s->listener<0)return 0;uint64_t now=qcf_now();if(!now)return 0;
 if(qci_control.peer>=0&&now>=qci_control.deadline){close(qci_control.peer);qci_control.peer=-1;}
 int incoming=accept(s->listener,0,0);
 if(incoming>=0){
  if(qci_control.peer>=0||!qc5_net_flags(incoming)||!qcf_channel(incoming))close(incoming);
  else{qci_control.peer=incoming;qci_control.deadline=now+QCF_TTL_US;}
 }
 if(qci_control.peer<0)return 0;struct qcf_client client;int got=qcf_receive(qci_control.peer,&client);
 if(!got)return 0;close(qci_control.peer);qci_control.peer=-1;if(got<0)return 0;
 if(s->client>=0){
  static const char busy[]="HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
  (void)send(client.fd,busy,sizeof busy-1,MSG_DONTWAIT|MSG_NOSIGNAL);close(client.fd);return 0;
 }
 int capacity=65536,immediate=1;
 if(client.length>=sizeof s->request||setsockopt(client.fd,SOL_SOCKET,SO_SNDBUF,&capacity,sizeof capacity)
#ifdef QC5_AUDIO_DUPLEX
    ||setsockopt(client.fd,IPPROTO_TCP,TCP_NODELAY,&immediate,sizeof immediate)
#endif
 ){close(client.fd);return 0;}
 (void)immediate;s->client=client.fd;s->used=client.length;memcpy(s->request,client.request,client.length+1);
 s->request_deadline=qc5_net_us()+1000000;qc5_net_inc(&s->accepted);qc5_net_inc(&s->requests);
 if(!strncmp(s->request,"GET /media HTTP/1.1\r\n",21))return qc5_service_upgrade(s);
 char reply[256];int n=snprintf(reply,sizeof reply,"HTTP/1.1 200 OK\r\nContent-Type: video/h264\r\nCache-Control: no-store\r\nConnection: close\r\nX-QC5-Kernel: %s\r\n%s\r\n",s->release,s->extra_header);
 if(n<=0||(size_t)n>=sizeof reply||!qc5_net_write(s,reply,(unsigned)n,0)){qc5_net_close(s);return 0;}s->stream=1;return 1;
}
#endif
/* Frontend retains its fd until this succeeds. Its normal handler closes
 * that reference afterwards, while the producer owns the received reference.
 */
static int qci_delegate(int client,const char *request,unsigned bytes)
{
 if(!qci_directory())return 0;struct sockaddr_un a={.sun_family=AF_UNIX};if(!qci_path(a.sun_path))return 0;
 int channel=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(channel<0)return 0;
 int ok=!connect(channel,(void*)&a,sizeof a)&&qcf_send(channel,client,request,bytes,qcf_now()+QCF_TTL_US)==1;close(channel);return ok;
}
#ifndef QCI_FRONTEND_ONLY
#define qc5_net_open qci_net_open
#define qc5_net_tick qci_net_tick
#define qc5_net_finish qci_net_finish
#endif
#endif

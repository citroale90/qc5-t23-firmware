/* SPDX-License-Identifier: GPL-2.0-or-later
 * One separately executable codec process, private bounded PCM messages.
 * A browser disconnect never restarts it. No device code or blocking wait.
 */
#ifndef QC5_AUDIO_STREAM_CHILD_H
#define QC5_AUDIO_STREAM_CHILD_H
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include "stream-process.h"
#include "stream-receiver.h"
struct qasi_child {
 struct qas_receiver receiver;
 pid_t pid;int fd,result;
 unsigned started,reaped,failed,stop_sent;
 uint64_t started_us,last_tick_us,last_record_us,records;
 uint8_t record[QAS_IPC_MAX];
};
typedef void (*qasi_child_entry)(void *);
static int qasi_child_launch(struct qasi_child *s,const uint8_t nonce[16],uint64_t periods,uint64_t now,
                             qasi_child_entry entry,void *opaque)
{
 if(!s||!nonce||!entry||!now||s->started)return 0;
 int pair[2],capacity=65536;
#ifdef QC5_AUDIO_TEST_DARWIN_DGRAM
 int type=SOCK_DGRAM;
#else
 int type=SOCK_SEQPACKET;
#endif
 if(socketpair(AF_UNIX,type,0,pair))return 0;
 for(unsigned i=0;i<2;i++){
  int fd=pair[i],flags=fcntl(fd,F_GETFL),clo=fcntl(fd,F_GETFD);
  if(flags<0||clo<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)||fcntl(fd,F_SETFD,clo|FD_CLOEXEC)||
     setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&capacity,sizeof capacity)||
     setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&capacity,sizeof capacity)){
   close(pair[0]);close(pair[1]);return 0;
  }
 }
 memset(s,0,sizeof *s);s->fd=-1;s->result=127;
 if(!qasi_receiver_start(&s->receiver,nonce,periods)){close(pair[0]);close(pair[1]);return 0;}
 pid_t child=fork();
 if(child<0){close(pair[0]);close(pair[1]);return 0;}
 if(!child){
  close(pair[0]);
  if(pair[1]!=3){if(dup2(pair[1],3)!=3)_exit(126);close(pair[1]);}
  int clo=fcntl(3,F_GETFD);if(clo<0||fcntl(3,F_SETFD,clo&~FD_CLOEXEC))_exit(126);
  entry(opaque);_exit(126);
 }
 close(pair[1]);s->pid=child;s->fd=pair[0];s->started=1;
 s->started_us=s->last_tick_us=s->last_record_us=now;return 1;
}
static void qasi_audio_exec(void *opaque)
{
 const char *program=opaque;
 if(program&&*program=='/')execl(program,program,"--capture-during-video",(char*)NULL);
 _exit(126);
}
static int qasi_child_start(struct qasi_child *s,const uint8_t nonce[16],uint64_t periods,uint64_t now,const char *program)
{
 return program&&program[0]=='/'&&qasi_child_launch(s,nonce,periods,now,qasi_audio_exec,(void*)program);
}
static void qasi_child_stop(struct qasi_child *s)
{
 if(s&&s->started&&!s->reaped&&!s->stop_sent){
  s->stop_sent=1;(void)kill(s->pid,SIGTERM);
 }
}
static int qasi_child_tick(struct qasi_child *s,uint64_t now,qasi_pcm_sink sink,void *opaque)
{
 if(!s||!s->started||!now||now<s->last_tick_us||!sink)return 0;
 s->last_tick_us=now;
 /* At most eight packets per video-loop visit. Socket capacity is bounded,
  * and every packet is a private copy. No public socket or extra PCM ring. */
 for(unsigned n=0;n<8&&!s->failed&&s->fd>=0&&!s->receiver.finished;n++){
  ssize_t bytes=recv(s->fd,s->record,sizeof s->record,MSG_DONTWAIT|MSG_TRUNC);
  if(bytes<0){if(errno==EINTR)continue;if(errno!=EAGAIN&&errno!=EWOULDBLOCK)s->failed=1;break;}
  if(!bytes||bytes>(ssize_t)sizeof s->record||s->records==UINT64_MAX){s->failed=1;break;}
#ifdef QASI_EXTRA_RECORD
  int extra=QASI_EXTRA_RECORD(s->record,(size_t)bytes,now);
  if(extra<0){s->failed=1;break;}
  if(extra){s->records++;continue;}
#endif
  if(!qasi_receiver_record(&s->receiver,s->record,(size_t)bytes,sink,opaque)){s->failed=1;break;}
  s->records++;s->last_record_us=now;
 }
 if(!s->reaped){
  int status=0;pid_t got=waitpid(s->pid,&status,WNOHANG);
  if(got==s->pid){s->reaped=1;s->result=WIFEXITED(status)?WEXITSTATUS(status):125;if(s->result)s->failed=1;}
  else if(got<0&&errno!=EINTR){s->reaped=1;s->failed=1;s->result=125;}
 }
 uint64_t timeout=s->receiver.periods?2000000u:5000000u;
 if(!s->receiver.finished&&now-s->last_record_us>timeout)s->failed=1;
 if(s->failed){qasi_child_stop(s);if(s->fd>=0){close(s->fd);s->fd=-1;}return 0;}
 if(s->receiver.finished&&s->fd>=0){close(s->fd);s->fd=-1;}
 return 1;
}
static int qasi_child_finished(const struct qasi_child *s)
{return s&&s->started&&s->receiver.finished&&!s->receiver.failed&&!s->failed&&s->reaped&&!s->result;}
#endif

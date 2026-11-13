/* SPDX-License-Identifier: GPL-3.0-only
 * Capture-side recording owner. File operations and fsync run in an exec'd
 * child. No file write or child wait may block the ISP/encoder owner.
 */
#ifndef QC5_RECORD_RUNTIME_H
#define QC5_RECORD_RUNTIME_H
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include "record-channel.h"
#include "qc5_record_policy_state.h"
enum {QRR_IDLE=0,QRR_STARTING,QRR_ACTIVE,QRR_CLOSING};
#define QRR_CLOSE_TIMEOUT_US UINT64_C(30000000)
struct qrr_recording {
 struct qrc_client channel;struct qkv_mux mux;struct qc5_rec_policy_state policy;
 pid_t child;unsigned phase,failed,moving;uint64_t epoch,deadline,start_us,last_us,clips,errors,clip_id,retry_after;
 const char *directory,*program;uint64_t post_us,segment_us;
};
static int qrr_init(struct qrr_recording *s,uint64_t epoch,const char *directory,const char *program)
{
 if(!s||!epoch||!directory||!program)return 0;
 *s=(struct qrr_recording){.channel={.fd=-1},.epoch=epoch,.directory=directory,.program=program,
  .post_us=10000000,.segment_us=60000000};return 1;
}
static void qrr_failure(struct qrr_recording *s,uint64_t now)
{
 if(!s->failed){s->errors++;fprintf(stderr,"QC5_RECORD_FAILURE phase=%u channel_failed=%u active=%u serial=%u frames=%llu samples=%llu bytes=%llu\n",s->phase,s->channel.failed,s->channel.active,s->channel.serial,(unsigned long long)s->mux.frames,(unsigned long long)s->mux.samples,(unsigned long long)s->mux.bytes);}
 s->failed=1;s->retry_after=now<=UINT64_MAX-10000000?now+10000000:UINT64_MAX;
}
static void qrr_end(struct qrr_recording *s,unsigned failed,uint64_t now)
{
 if(!s||s->phase==QRR_IDLE||s->phase==QRR_CLOSING)return;
 if(failed)qrr_failure(s,now);
 if(s->channel.fd>=0)(void)shutdown(s->channel.fd,SHUT_WR);
 /* File fsync/rename is asynchronous and may outlast two seconds on SD.
  * Keep one owned child while it drains; capture never waits for it. */
 s->phase=QRR_CLOSING;s->deadline=now<=UINT64_MAX-QRR_CLOSE_TIMEOUT_US?now+QRR_CLOSE_TIMEOUT_US:UINT64_MAX;
}
static int qrr_spawn(struct qrr_recording *s,uint64_t now)
{
 if(!s||s->phase!=QRR_IDLE||s->child||s->clip_id==UINT64_MAX||!now||now>UINT64_MAX-2000000)return 0;
 int pair[2];if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair))return 0;
 int capacity=8192; /* Linux doubles it; at most16KiB of encoded pieces. */
 if(setsockopt(pair[0],SOL_SOCKET,SO_SNDBUF,&capacity,sizeof capacity)){close(pair[0]);close(pair[1]);return 0;}
 char epoch[17];uint64_t id=s->epoch+(++s->clip_id);if(!id)id=1;
 snprintf(epoch,sizeof epoch,"%016llx",(unsigned long long)id);
 pid_t child=fork();
 if(child==0){
  close(pair[0]);if(pair[1]!=3){if(dup2(pair[1],3)<0)_exit(126);close(pair[1]);}
  if(fcntl(3,F_SETFD,0))_exit(126);
  /* Do not inherit DMA mappings through exec or any peripheral/socket fd. */
#ifdef SYS_close_range
  if(syscall(SYS_close_range,4u,~0u,0u))
#endif
  {struct rlimit limit;if(getrlimit(RLIMIT_NOFILE,&limit)||limit.rlim_cur>65536)_exit(126);for(int fd=4;fd<(int)limit.rlim_cur;fd++)close(fd);}
  execl(s->program,s->program,s->directory,epoch,(char*)NULL);_exit(127);
 }
 close(pair[1]);if(child<0){close(pair[0]);return 0;}
 s->child=child;s->channel=(struct qrc_client){.fd=pair[0]};s->phase=QRR_STARTING;
 s->failed=0;s->retry_after=0;s->deadline=now+2000000;s->start_us=0;
 return 1;
}
static int qrr_tick(struct qrr_recording *s,uint64_t now)
{
 if(!s||!now||now>UINT64_MAX-2000000||now<s->last_us)return 0;s->last_us=now;
 if(s->phase==QRR_IDLE)return 1;
 int status=0;pid_t done=waitpid(s->child,&status,WNOHANG);
 if(done==s->child){
  if(!WIFEXITED(status)||WEXITSTATUS(status)){fprintf(stderr,"QC5_RECORD_CHILD_EXIT phase=%u exited=%u code=%d signal=%d\n",s->phase,!!WIFEXITED(status),WIFEXITED(status)?WEXITSTATUS(status):-1,WIFSIGNALED(status)?WTERMSIG(status):0);fflush(stderr);}
  if(!s->failed&&s->phase==QRR_CLOSING&&WIFEXITED(status)&&WEXITSTATUS(status)==0)s->clips++;
  else if(!s->failed)qrr_failure(s,now);
  if(s->channel.fd>=0)close(s->channel.fd);s->channel.fd=-1;s->child=0;s->phase=QRR_IDLE;return 1;
 }
 if(done<0&&errno!=EINTR){qrr_failure(s,now);return 0;}
 struct qrc_header h;int got=qrc_receive(s->channel.fd,&h,NULL,0,MSG_DONTWAIT);
 if(got==1){
  if(s->phase==QRR_STARTING&&h.type==QRC_READY&&!h.serial&&!h.bytes){
   if(!qkv_init(&s->mux,qrc_sink,&s->channel,1280,720)){qrr_end(s,1,now);return 1;}
   s->phase=QRR_ACTIVE;s->deadline=0;
  }else if(s->phase==QRR_CLOSING&&(h.type==QRC_DONE||h.type==QRC_ERROR)&&!h.bytes){
   if(h.type==QRC_ERROR&&!s->failed)qrr_failure(s,now);
  }else qrr_end(s,1,now);
 }else if(got!=2&&s->phase!=QRR_CLOSING)qrr_end(s,1,now);
 if(s->phase!=QRR_ACTIVE&&now>=s->deadline){
  if(!s->failed)qrr_failure(s,now);
  /* An uninterruptible card write can delay exit. Keep exactly one child;
   * do not accumulate replacements or block capture in waitpid. */
  (void)kill(s->child,SIGKILL);s->deadline=now+2000000;
 }
 return 1;
}
static int qrr_configure(struct qrr_recording *s,unsigned continuous,unsigned motion,uint64_t now)
{
 if(!s||continuous>1||motion>1)return 0;s->policy.continuous=continuous;s->policy.motion=motion;
 if(!motion)s->policy.event_active=0;
 if(!qc5_rec_policy_wanted(&s->policy))qrr_end(s,0,now);return 1;
}
static int qrr_activity(struct qrr_recording *s,uint64_t now,unsigned moving)
{
 if(!s||moving>1||!now||now>INT64_MAX||now<s->last_us)return 0;s->moving=moving;
 if(moving){s->policy.last_motion=(long long)now;if(s->policy.motion&&!s->policy.inhibit)s->policy.event_active=1;}
 else if(s->policy.last_motion>=0&&now>=(uint64_t)s->policy.last_motion&&now-(uint64_t)s->policy.last_motion>=s->post_us){s->policy.event_active=0;s->policy.inhibit=0;}
 if(!qc5_rec_policy_wanted(&s->policy))qrr_end(s,0,now);return 1;
}
static int qrr_stop(struct qrr_recording *s,uint64_t now)
{if(!s||!now||now>INT64_MAX)return 0;qc5_rec_policy_stop(&s->policy,(long long)now);qrr_end(s,0,now);return 1;}
static int qrr_video(struct qrr_recording *s,const uint8_t *au,size_t n,uint64_t stamp,unsigned key)
{
 if(!s||!au||!n||!stamp||key>1)return 0;
 if(!qc5_rec_policy_wanted(&s->policy))return 1;
 if(s->phase==QRR_IDLE){if(key&&stamp>=s->retry_after&&!qrr_spawn(s,stamp))qrr_failure(s,stamp);return 1;}
 if(s->phase!=QRR_ACTIVE)return 1;
 if(s->start_us&&stamp<s->start_us){qrr_end(s,1,stamp);return 1;}
 if(s->start_us&&key&&(stamp-s->start_us>=s->segment_us||s->mux.bytes>63u*1024u*1024u)){qrr_end(s,0,stamp);return 1;}
 if(!s->mux.started&&!key)return 1;
 unsigned cause=0;
 if(!qrc_begin(&s->channel,0))cause=1;
 else if(!qkv_video(&s->mux,au,n,stamp,key))cause=2;
 else if(!qrc_commit(&s->channel))cause=3;
 if(cause){
  if(s->errors<3)fprintf(stderr,"QC5_RECORD_UNIT_FAILURE unit=0 cause=%u stamp=%llu previous_video_ms=%llu previous_audio_ms=%llu mux_failed=%u channel_failed=%u buffered=%u\n",cause,(unsigned long long)stamp,(unsigned long long)s->mux.video_ms,(unsigned long long)s->mux.audio_ms,s->mux.failed,s->channel.failed,s->channel.buffered);
  qrr_end(s,1,stamp);
 }
 else if(!s->start_us)s->start_us=stamp;
 return 1; /* Storage failure is visible in status, never a capture fault. */
}
static int qrr_audio(struct qrr_recording *s,const uint8_t *pcma,size_t n,uint64_t stamp)
{
 if(!s||!pcma||!n||!stamp)return 0;
 if(s->phase!=QRR_ACTIVE||!s->mux.started||stamp/1000<s->mux.origin_ms)return 1;
 unsigned cause=0;
 if(!qrc_begin(&s->channel,1))cause=1;
 else if(!qkv_audio(&s->mux,pcma,n,stamp))cause=2;
 else if(!qrc_commit(&s->channel))cause=3;
 if(cause){
  if(s->errors<3)fprintf(stderr,"QC5_RECORD_UNIT_FAILURE unit=1 cause=%u stamp=%llu previous_video_ms=%llu previous_audio_ms=%llu mux_failed=%u channel_failed=%u buffered=%u\n",cause,(unsigned long long)stamp,(unsigned long long)s->mux.video_ms,(unsigned long long)s->mux.audio_ms,s->mux.failed,s->channel.failed,s->channel.buffered);
  qrr_end(s,1,stamp);
 }
 return 1;
}
#endif

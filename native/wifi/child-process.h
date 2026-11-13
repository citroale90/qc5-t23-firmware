/* SPDX-License-Identifier: GPL-3.0-only
 * Only unreaped direct children are eligible for stop/restart. */
#ifndef QC5_NATIVE_CHILD_PROCESS_H
#define QC5_NATIVE_CHILD_PROCESS_H
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
static int qnp_running(pid_t *child)
{
 if(*child<=0)return 0;int status;pid_t found;
 do found=waitpid(*child,&status,WNOHANG);while(found<0&&errno==EINTR);
 if(found==0)return 1;if(found==*child)*child=0;return 0;
}
static uint64_t qnp_now(void)
{struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0?0:(uint64_t)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;}
/* SIGKILL does not make a task in uninterruptible kernel sleep reapable.
 * Keep its ownership if it cannot be reaped: no replacement may be started. */
static int qnp_stop_progress(pid_t *child,int(*progress)(void))
{
 if(*child<=0)return 1;int status;pid_t found;uint64_t start=qnp_now();unsigned forced=0;
 for(;;){
  found=waitpid(*child,&status,WNOHANG);
  if(found==*child){*child=0;return 1;}
  if(found<0&&errno!=EINTR)return 0;
  uint64_t now=qnp_now();if(!start||!now||now<start)return 0;
  if(now-start>=6000)return 0;
  if(!forced){if(kill(*child,SIGTERM)&&errno!=ESRCH)return 0;forced=1;}
  if(forced==1&&now-start>=5000){if(kill(*child,SIGKILL)&&errno!=ESRCH)return 0;forced=2;}
  if(progress&&!progress())return 0;
  usleep(10000);
 }
}
static int qnp_stop(pid_t *child){return qnp_stop_progress(child,NULL);}
static pid_t qnp_spawn_policy(const char *executable,char *const argv[],unsigned keep_stderr)
{
 pid_t parent=getpid(),child=fork();if(child!=0)return child;
 signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);
#ifdef __linux__
 if(prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()!=parent)_exit(126);
#else
 (void)parent;
#endif
 int input=open("/dev/null",O_RDONLY|O_CLOEXEC),log=open("/dev/null",O_WRONLY|O_CLOEXEC);
 if(input<0||log<0)_exit(126);
 if(dup2(input,STDIN_FILENO)<0||dup2((keep_stderr&2)?STDERR_FILENO:log,STDOUT_FILENO)<0||(!keep_stderr&&dup2(log,STDERR_FILENO)<0))_exit(126);
 if(fcntl(STDIN_FILENO,F_SETFD,0)||fcntl(STDOUT_FILENO,F_SETFD,0)||fcntl(STDERR_FILENO,F_SETFD,0))_exit(126);
 if(input>STDERR_FILENO)close(input);if(log>STDERR_FILENO)close(log);execv(executable,argv);_exit(127);
}
static pid_t qnp_spawn(const char *executable,char *const argv[])
{return qnp_spawn_policy(executable,argv,0);}
#endif

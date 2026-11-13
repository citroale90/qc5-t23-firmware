/* SPDX-License-Identifier: GPL-3.0-only
 * Own one AP-capable supplicant, controller and portal as a cohort. A dead
 * member causes a bounded stop and clean restart, preserving saved config.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include "child-process.h"
#include "network-files.h"
#include "heartbeat.h"
static volatile sig_atomic_t qns_quit;
static void qns_signal(int sig){(void)sig;qns_quit=1;}
static int qns_iface(const char *s){return s&&strlen(s)<16&&!strncmp(s,"wlan",4)&&s[4]&&strspn(s+4,"0123456789")==strlen(s+4);}
static int qns_health_directory=-1;
static uint64_t qns_health_sequence,qns_health_due;
static uint64_t qns_health_now(void){struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?0:(uint64_t)t.tv_sec*1000000+(unsigned)t.tv_nsec/1000;}
static int qns_health_tick(void)
{
 uint64_t now=qns_health_now();if(!now)return 0;
 if(now>=qns_health_due){if(!qhb_publish(qns_health_directory,"network-health",now,&qns_health_sequence))return 0;qns_health_due=now+1000000;}
 return 1;
}
static int qns_delay(unsigned ticks){
 for(unsigned i=0;i<ticks&&!qns_quit;i++){
  if(!qns_health_tick()){qns_quit=1;return 0;}
  usleep(100000);
 }return !qns_quit;
}
static int qns_ap_once(int directory)
{
 int fd=openat(directory,"ap-once",O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
 struct stat st;char magic[9];ssize_t n=read(fd,magic,sizeof magic);
 int valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&077)&&st.st_nlink==1&&st.st_size==8&&n==8&&!memcmp(magic,"QCAP001\n",8);close(fd);
 if(!valid){fprintf(stderr,"QC5_WIFI_AP_REQUEST rejected=1\n");return 0;}
 fd=openat(directory,"ap-once-used",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 int ok=write(fd,magic,8)==8;if(close(fd))ok=0;
 if(!ok||unlinkat(directory,"ap-once",0))return 0;
 return 1;
}
static int qns_running(pid_t *child,unsigned role)
{
 if(*child<=0)return 0;int status;pid_t found;
 do found=waitpid(*child,&status,WNOHANG);while(found<0&&errno==EINTR);
 if(!found)return 1;
 if(found==*child){fprintf(stderr,"QC5_WIFI_CHILD_EXIT role=%u pid=%ld exited=%u code=%d signal=%d\n",role,(long)*child,!!WIFEXITED(status),WIFEXITED(status)?WEXITSTATUS(status):-1,WIFSIGNALED(status)?WTERMSIG(status):0);*child=0;}
 else fprintf(stderr,"QC5_WIFI_CHILD_WAIT_FAILURE role=%u errno=%d\n",role,errno);
 return 0;
}
int main(int argc,char **argv)
{
 const char *run="/run/qc5-native-network",*wpa_dir="/run/wpa",*wpa="/bin/wpa_supplicant-ap",*network="/bin/qc5-network",*portal="/bin/qc5-native-portal";
 if(argc<2||!qns_iface(argv[1]))return 64;
#ifdef QC5_STACK_HOST_TEST
 if(argc!=7)return 64;run=argv[2];wpa_dir=argv[3];wpa=argv[4];network=argv[5];portal=argv[6];
#else
 struct utsname kernel;if(argc!=2||geteuid()||uname(&kernel)||strcmp(kernel.release,"7.3.1-qc5-sd-tiny"))return 65;
#endif
 umask(077);
 if((mkdir(run,0700)&&errno!=EEXIST)||(mkdir(wpa_dir,0700)&&errno!=EEXIST))return 66;
 int directory=qnf_directory(run),wpa_directory=qnf_directory(wpa_dir);if(directory<0||wpa_directory<0)return 66;
 qns_health_directory=directory;
 int lock=openat(directory,"stack.lock",O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 66;
 int controller_lock=openat(directory,"controller.lock",O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
 if(controller_lock<0||flock(controller_lock,LOCK_EX|LOCK_NB))return 66;close(controller_lock);
 char config[256],path[600],socket_name[32];
 int n=snprintf(config,sizeof config,"ctrl_interface=%s\nap_scan=1\n",wpa_dir);
 if(n<=0||(size_t)n>=sizeof config||!qnf_publish(directory,"wpa.conf",config,(size_t)n)||snprintf(path,sizeof path,"%s/wpa.conf",run)>=(int)sizeof path)return 67;
 snprintf(socket_name,sizeof socket_name,"%s",argv[1]);
 signal(SIGTERM,qns_signal);signal(SIGINT,qns_signal);signal(SIGPIPE,SIG_IGN);
 int good=1;
 while(!qns_quit){
  pid_t children[2]={0};unlinkat(directory,"wifi-controller",0);
  (void)qnf_text(directory,"wifi-phase","starting\n");
  /* Remove stale control endpoints only after the preceding owned cohort
   * is reaped. The directory is private and cannot be a symlink. */
  struct stat endpoint;if(!fstatat(wpa_directory,socket_name,&endpoint,AT_SYMLINK_NOFOLLOW)){
   if(!S_ISSOCK(endpoint.st_mode)||endpoint.st_uid!=geteuid()||unlinkat(wpa_directory,socket_name,0)){good=0;break;}
  }
  char *wa[]={(char*)wpa,"-q","-Dnl80211","-i",argv[1],"-c",path,NULL};children[0]=qnp_spawn_policy(wpa,wa,3);
  fprintf(stderr,"QC5_WIFI_WPA_START pid=%ld\n",(long)children[0]);
  int ready=0;
  uint64_t ready_began=qns_health_now();
  while(!qns_quit){
   if(!qns_running(&children[0],0))break;
   /* Probe first: a ready endpoint remains valid after scheduling delay. */
   if(!fstatat(wpa_directory,socket_name,&endpoint,AT_SYMLINK_NOFOLLOW)&&S_ISSOCK(endpoint.st_mode)&&endpoint.st_uid==geteuid()){ready=1;break;}
   uint64_t now=qns_health_now();
   if(!ready_began||!now||now<ready_began||now-ready_began>=30000000)break;
   if(!qns_delay(1))break;
  }
  fprintf(stderr,"QC5_WIFI_WPA_READY ready=%u quitting=%u pid=%ld\n",ready,!!qns_quit,(long)children[0]);
  unsigned soft_restarts=0;
  uint64_t restart_window=qns_health_now();
  while(ready&&!qns_quit){
   unsigned ap=qns_ap_once(directory);
   fprintf(stderr,"QC5_WIFI_CONTROLLER_START mode=%s\n",ap?"AP_ONCE":"AUTO");
   char *na[]={(char*)network,argv[1],ap?"none":"auto",NULL};
   children[1]=qnp_spawn_policy(network,na,1);
   while(!qns_quit&&qns_running(&children[0],0)&&qns_running(&children[1],1))qns_delay(1);
   if(qns_quit||!qns_running(&children[0],0))break;
   /* A reaped controller can be replaced while its private supplicant is
    * still alive. The controller rebuilds only its owned RAM profiles;
    * interface/driver teardown is not part of this recovery path. */
   if(!qnp_stop_progress(&children[1],qns_health_tick)){good=0;break;}
   uint64_t now=qns_health_now();if(!now||now<restart_window){good=0;break;}
   if(now-restart_window>=60000000){restart_window=now;soft_restarts=0;}
   if(soft_restarts>=3){
    fprintf(stderr,"QC5_WIFI_CONTROLLER_RECOVERY exhausted=1 full_cohort_fallback=1\n");break;
   }
   ++soft_restarts;unlinkat(directory,"wifi-controller",0);
   (void)qnf_text(directory,"wifi-phase","recovering\n");
   fprintf(stderr,"QC5_WIFI_CONTROLLER_RECOVERY supplicant_preserved=1 attempt=%u\n",soft_restarts);
   if(!qns_delay(50))break;
  }
  for(int i=1;i>=0;i--){
   fprintf(stderr,"QC5_WIFI_CHILD_STOP role=%u pid=%ld\n",i,(long)children[i]);
   int stopped=qnp_stop_progress(&children[i],qns_health_tick);
   fprintf(stderr,"QC5_WIFI_CHILD_STOPPED role=%u ok=%u remaining_pid=%ld\n",i,stopped,(long)children[i]);
   if(!stopped)good=0;
  }
  unlinkat(directory,"wifi-controller",0);(void)qnf_text(directory,"wifi-phase","stopped\n");
  if(!good||!qns_delay(50))break;
 }
 close(lock);close(wpa_directory);close(directory);return good?0:70;
}

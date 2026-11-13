/* SPDX-License-Identifier: GPL-3.0-only
 * Native7.3 network supervisor. Not activated by the retained H62 trial.
 * Radio control, portal mailbox and durable storage have one owner.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "wifi-transaction.h"
#include "wifi-flash-linux.h"
#include "owned-wifi-reset.h"
#include "network-files.h"
#include "heartbeat.h"
static uint64_t qnr_health_sequence;
#include "scan-results.h"
#ifdef QC5_NETWORK_AP_PROBE
#include "ap-ram-probe.h"
static void qnr_probe_log(const char *message)
{int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);if(fd>=0){(void)write(fd,message,strlen(message));close(fd);}}
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#ifndef QNR_RUN
#define QNR_RUN "/run/qc5-native-network"
#endif
#ifndef QNR_BB
#define QNR_BB "/rom/bin/busybox"
#endif
struct qnr_runtime {struct qc5_wifi_profile boot_profile;unsigned migrate_pending;uint64_t migrate_since;int control,directory;pid_t dhcp;char iface[16],local[108];const char *sd;unsigned scan_networks;};
static volatile sig_atomic_t quit;
static void qnr_signal(int sig){(void)sig;quit=1;}
static uint64_t qnr_now(void)
{struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?0:(uint64_t)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;}
static int qnr_interface(const char *s)
{return s&&strlen(s)<16&&!strncmp(s,"wlan",4)&&s[4]&&strspn(s+4,"0123456789")==strlen(s+4);}
static unsigned qnr_diag_count;
static int qnr_command_failure(const char *command,unsigned cause,uint64_t elapsed,int error)
{
 const char *tag=!strcmp(command,"STATUS")?"STATUS":!strcmp(command,"SCAN_RESULTS")?"SCAN_RESULTS":
  !strcmp(command,"SCAN")?"SCAN":!strcmp(command,"ADD_NETWORK")?"ADD_NETWORK":
  !strncmp(command,"SET_NETWORK ",12)?"SET_NETWORK":!strncmp(command,"DISABLE_NETWORK ",16)?"DISABLE_NETWORK":
  !strncmp(command,"SELECT_NETWORK ",15)?"SELECT_NETWORK":!strncmp(command,"REMOVE_NETWORK ",15)?"REMOVE_NETWORK":"CONTROL";
 if(qnr_diag_count++<4)fprintf(stderr,"QC5_WIFI_CONTROL_FAILURE command=%s cause=%u elapsed_ms=%llu errno=%d\n",tag,cause,(unsigned long long)elapsed,error);
 return -1;
}
static int qnr_command(void *opaque,const char *command,char *reply,size_t cap)
{
 struct qnr_runtime *r=opaque;size_t n=strlen(command);if(!n||n>256||!cap)return -1;
 uint64_t start=qnr_now();if(!start||send(r->control,command,n,MSG_DONTWAIT)!=(ssize_t)n)return qnr_command_failure(command,1,0,errno);
 for(;;){
  uint64_t now=qnr_now();if(!now||now<start)return qnr_command_failure(command,2,0,0);
  /* Consume one already queued reply before declaring a scheduling timeout.
   * Never renew the5000ms waiting budget and never submit the command twice. */
  struct iovec iov={reply,cap};struct msghdr message={0};message.msg_iov=&iov;message.msg_iovlen=1;
  ssize_t got=recvmsg(r->control,&message,MSG_DONTWAIT);
  if(got>0)return (size_t)got<=cap&&!(message.msg_flags&(MSG_TRUNC|MSG_CTRUNC))?(int)got:qnr_command_failure(command,3,now-start,0);
  if(got==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR))return qnr_command_failure(command,4,now-start,got<0?errno:0);
  now=qnr_now();if(!now||now<start)return qnr_command_failure(command,2,0,0);
  if(now-start>=5000)return qnr_command_failure(command,5,now-start,0);
  struct pollfd p={r->control,POLLIN,0};int ready=poll(&p,1,(int)(5000-(now-start)));
  if(ready<0&&errno==EINTR)continue;if(!ready)continue;
  if(ready!=1||!(p.revents&POLLIN))return qnr_command_failure(command,6,now-start,ready<0?errno:0);
 }
}
#include "child-process.h"
#define qnr_stop_child qnp_stop
static pid_t qnr_spawn(char *const argv[]){return qnp_spawn(QNR_BB,argv);}
static int qnr_exec(char *const argv[])
{
 pid_t child=qnr_spawn(argv);if(child<0)return 0;int status;uint64_t start=qnr_now();
 for(;;){
  /* A completed owned child is valid even if the parent resumed late. */
  pid_t got=waitpid(child,&status,WNOHANG);
  if(got==child)return WIFEXITED(status)&&!WEXITSTATUS(status);
  if(got<0&&errno!=EINTR)return 0;
  uint64_t now=qnr_now();if(!start||!now||now<start||now-start>=1000)break;
  usleep(10000);
 }
 (void)qnr_stop_child(&child);return 0;
}
static int qnr_role(void *opaque,enum qnw_role role)
{
 struct qnr_runtime *r=opaque;if(role>QNW_AP||!qnr_stop_child(&r->dhcp))return 0;
 char *flush[]={QNR_BB,"ip","-4","addr","flush","dev",r->iface,NULL};
 char *routes[]={QNR_BB,"ip","-4","route","flush","dev",r->iface,NULL};
 if(!qnr_exec(flush)||!qnr_exec(routes))return 0;if(role==QNW_NONE)return 1;
 char *up[]={QNR_BB,"ip","link","set","dev",r->iface,"up",NULL};if(!qnr_exec(up))return 0;
 if(role==QNW_STATION){
  char *args[]={QNR_BB,"udhcpc","-f","-i",r->iface,"-s","/bin/qc5-native-dhcp",NULL};
  r->dhcp=qnr_spawn(args);return r->dhcp>0;
 }
 char *address[]={QNR_BB,"ip","addr","add","192.168.168.1/24","dev",r->iface,NULL};
 if(!qnr_exec(address))return 0;
 char config[512];int n=snprintf(config,sizeof config,
  "start 192.168.168.20\nend 192.168.168.80\ninterface %s\noption subnet 255.255.255.0\n"
  "option router 192.168.168.1\noption dns 192.168.168.1\noption lease 120\n"
  "lease_file " QNR_RUN "/leases\npidfile " QNR_RUN "/dhcp.pid\nauto_time 30\n",r->iface);
 if(n<=0||(size_t)n>=sizeof config)return 0;
 int fd=open(QNR_RUN "/udhcpd.conf",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
 if(fd<0)return 0;int ok=write(fd,config,(size_t)n)==n;close(fd);if(!ok)return 0;
 char *args[]={QNR_BB,"udhcpd","-f",QNR_RUN "/udhcpd.conf",NULL};r->dhcp=qnr_spawn(args);return r->dhcp>0;
}
static int qnr_ap_once(int directory)
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
/* Root-only one-shot setup request. Keep the same supplicant and saved
 * station network; exercise the actual AP/STA policy rather than restarting
 * the entire radio cohort or deliberately providing wrong credentials. */
static int qnr_setup_once(struct qnr_runtime *r,struct qnt_transaction *t,uint64_t now)
{
 struct qnw_mode *m=t->mode;
 if(t->pending_id>=0||t->cleanup_id>=0||m->station_id<0||m->policy.phase!=QC5_NET_CONNECTED)return 1;
 if(!qnr_ap_once(r->directory))return 1;
 if(!now||now>UINT64_MAX-180000||!qnw_id_command(m,"DISABLE_NETWORK",m->station_id))return 0;
 memset(&m->policy,0,sizeof m->policy);m->policy.phase=QC5_NET_JOINING;m->policy.deadline=now;
 if(!qnt_tick(t,now,0,0)||m->policy.phase!=QC5_NET_SETUP_AP||m->desired!=QNW_AP)return 0;
 /* Leave three minutes to a person doing the explicit one-shot test.
  * Portal activity and credential submissions still use the native policy. */
 m->policy.retry_after=now+180000;
 fprintf(stderr,"QC5_WIFI_SETUP_REQUEST same_supplicant=1 saved_station_retained=1 idle_retry_ms=180000\n");
 return 1;
}
static unsigned qnr_leased(const char *iface)
{
 struct ifaddrs *all=NULL;if(getifaddrs(&all))return 0;unsigned leased=0;
 for(struct ifaddrs *p=all;p;p=p->ifa_next)if(p->ifa_addr&&p->ifa_addr->sa_family==AF_INET&&!strcmp(p->ifa_name,iface)){
  uint32_t a=ntohl(((struct sockaddr_in*)p->ifa_addr)->sin_addr.s_addr);
  if(a&&a!=0xc0a8a801u&&(a>>24)!=127&&(a>>16)!=0xa9feu)leased=1;
 }
 freeifaddrs(all);return leased;
}
static unsigned qnr_portal(uint64_t now)
{
 int fd=open(QNR_RUN "/wifi-portal-active",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 char text[32];ssize_t n=read(fd,text,sizeof text);close(fd);if(n<2||n>21||text[n-1]!='\n')return 0;
 uint64_t value=0;
 for(ssize_t i=0;i<n-1;i++){
  unsigned d=(unsigned char)text[i]-'0';if(d>9||value>(UINT64_MAX-d)/10)return 0;value=value*10+d;
 }
 return value<=now&&now-value<=15000;
}
static int qnr_mac_suffix(const char *iface,char suffix[7])
{
 char path[80],raw[19];int n=snprintf(path,sizeof path,"/sys/class/net/%s/address",iface);
 if(n<=0||(size_t)n>=sizeof path)return 0;int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
 ssize_t len=read(fd,raw,sizeof raw);close(fd);if(len!=18||raw[17]!='\n')return 0;
 for(unsigned i=0;i<17;i++)if(i%3==2?raw[i]!=':':!((raw[i]>='0'&&raw[i]<='9')||(raw[i]>='a'&&raw[i]<='f')))return 0;
 for(unsigned i=0;i<3;i++){suffix[i*2]=raw[9+i*3];suffix[i*2+1]=raw[10+i*3];}suffix[6]=0;return 1;
}
static int qnr_sd_profile(const char *sd,struct qc5_wifi_profile *profile)
{
 if(!qc5_sd_mounted(sd,0))return 0;
 int directory=open(sd,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(directory<0)return -1;
 int fd=openat(directory,"config.txt",O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);close(directory);
 if(fd<0)return errno==ENOENT?0:-1;
 struct stat info;char text[8193];long size=-1;int parsed=-1;
 if(!fstat(fd,&info)&&S_ISREG(info.st_mode)&&info.st_size>=0&&info.st_size<=8192)size=qc5_config_read_fd(fd,text,sizeof text);
 if(size>=0)parsed=qc5_wifi_config_profile(text,(unsigned long)size,profile);
 close(fd);memset(text,0,sizeof text);return parsed;
}
static int qnr_saved_profile(const char *sd,const char *iface,struct qc5_wifi_profile *p)
{
 int rc=qwf_linux_load(iface,p);
 if(rc==1){fprintf(stderr,"QC5_WIFI_PROFILE source=flash SD_required=0\n");return 1;}
 if(rc<0){fprintf(stderr,"QC5_WIFI_PROFILE read_failed=1 source=flash\n");return -1;}
 rc=qnr_sd_profile(sd,p);
 if(rc==1)fprintf(stderr,"QC5_WIFI_PROFILE source=SD migration_to_flash_after_connection=1\n");
 return rc;
}
static int qnr_persist(void *opaque,const struct qc5_wifi_profile *profile)
{
 struct qnr_runtime *r=opaque;
 if(qwf_linux_store(r->iface,profile)){puts("QC5_WIFI_SAVE failed durable_confirmation=0");return 0;}
 puts("QC5_WIFI_SAVE verified source=flash SD_required=0");
 /* The flash transaction is authoritative. Mirror a writable SD when it
  * exists, preserving other user settings; no card is needed for success. */
 if(qc5_sd_mounted(r->sd,1)) (void)qc5_wifi_store(r->sd,NULL,0,profile);
 return 1;
}
static void qnr_migration_tick(struct qnr_runtime *r,const struct qnt_transaction *t,uint64_t now,unsigned leased)
{
 if(!r->migrate_pending||t->phase!=QNT_IDLE)return;
 if(t->mode->policy.phase!=QC5_NET_CONNECTED||!leased){r->migrate_since=0;return;}
 if(!r->migrate_since)r->migrate_since=now;
 if(now>=r->migrate_since&&now-r->migrate_since>=5000){
  if(qnr_persist(r,&r->boot_profile)){r->migrate_pending=0;memset(&r->boot_profile,0,sizeof r->boot_profile);}
  else r->migrate_since=now;
 }
}
static int qnr_publish(struct qnr_runtime *r,const struct qnt_transaction *t)
{
 const struct qnw_mode *m=t->mode;const char *phase=m->active==QNW_AP?"hotspot\n":m->policy.phase==QC5_NET_CONNECTED?"connected\n":"connecting\n";
 const char *state=t->phase==QNT_SAVED?"wifi_salvato_flash\n":t->phase==QNT_JOIN_FAILED?"wifi_non_connesso\n":
  t->phase==QNT_STORAGE_FAILED?"wifi_connesso_salvataggio_non_confermato\n":t->phase==QNT_CONTROL_FAILED?"richiesta_non_accettata\n":
  t->pending_id>=0?"connessione_in_corso\n":"attesa_configurazione\n";
 return qnf_text(r->directory,"wifi-phase",phase)&&qnf_text(r->directory,"wifi-native-state",state);
}
static int qnr_request(struct qnr_runtime *r,struct qnt_transaction *t)
{
 if(t->pending_id>=0||t->cleanup_id>=0)return 1;
 struct qc5_wifi_profile profile;int found=qnf_request(r->directory,&profile);
 if(found==1){r->migrate_pending=0;memset(&r->boot_profile,0,sizeof r->boot_profile);}
 if(found==1&&!qnt_stage(t,&profile)){
  t->phase=QNT_CONTROL_FAILED;
  if(!qnf_text(r->directory,"wifi-native-state","richiesta_non_accettata\n"))found=-1;
 }
 memset(&profile,0,sizeof profile);return found>=0;
}
static int qnr_scan_cache(struct qnr_runtime *r)
{
 /* Passive results only: never interrupt a playing stream with periodic
  * active scans. The boot station helper owns the initial radio scan. */
 char reply[12288],json[12288];int bytes=qnr_command(r,"SCAN_RESULTS",reply,sizeof reply);unsigned found=0;
 int n=bytes>0?qns_json(reply,(size_t)bytes,json,sizeof json,&found):0;
 if(n<=0)return 0;
 r->scan_networks=found;
 /* A driver may return an empty table in AP mode. Keep the last useful list. */
 struct stat old;if(!found&&!fstatat(r->directory,"wifi-networks.json",&old,AT_SYMLINK_NOFOLLOW))return 1;
 return qnf_publish(r->directory,"wifi-networks.json",json,(size_t)n);
}
static void qnr_initial_scan(struct qnr_runtime *r)
{
 char reply[32];(void)qnr_command(r,"SCAN",reply,sizeof reply);
 uint64_t start=qnr_now();if(!start)return;
 for(unsigned attempt=0;attempt<12&&!quit;attempt++){
  uint64_t now=qnr_now();if(now<start||now-start>=3000)break;
  (void)qnr_scan_cache(r);if(r->scan_networks)break;usleep(250000);
 }
}
static int qnr_dhcp_health(struct qnr_runtime *r,struct qnw_mode *mode,uint64_t now,uint64_t *retry)
{
 if(r->dhcp>0){
  int status;pid_t child=waitpid(r->dhcp,&status,WNOHANG);
  if(child==r->dhcp){r->dhcp=0;*retry=now>UINT64_MAX-5000?UINT64_MAX:now+5000;}
  else if(child<0&&errno!=EINTR)return 0;
 }
 /* STA loss is handled by the shared10-second reconnect policy. A failed
  * AP DHCP daemon must restart too, with a five-second minimum interval. */
 if(!r->dhcp&&mode->active==QNW_AP&&now>=*retry)mode->active=QNW_NONE;
 return 1;
}
static int qnr_exit_code(unsigned code)
{fprintf(stderr,"QC5_WIFI_OWNER_START_FAILURE code=%u\n",code);return (int)code;}
int main(int argc,char **argv)
{
 struct utsname kernel;int station_id=-1;
 if(argc!=3||!qnr_interface(argv[1])||
  (strcmp(argv[2],"none")&&strcmp(argv[2],"auto")&&!qnw_number(argv[2],strlen(argv[2]),&station_id)))return qnr_exit_code(64);
 /* Never apply the new supervisor to the installed3.10 vendor process. */
 if(geteuid()!=0||uname(&kernel)||strcmp(kernel.release,"7.3.1-qc5-sd-tiny"))return qnr_exit_code(65);
 umask(077);if(mkdir(QNR_RUN,0700)&&errno!=EEXIST)return qnr_exit_code(66);
 struct stat dir;if(lstat(QNR_RUN,&dir)||!S_ISDIR(dir.st_mode)||dir.st_uid||dir.st_mode&077)return qnr_exit_code(66);
 struct qnr_runtime r={.control=-1,.sd="/mnt/sda0"};r.directory=qnf_directory(QNR_RUN);if(r.directory<0)return qnr_exit_code(66);
 int lock=openat(r.directory,"controller.lock",O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
 if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return qnr_exit_code(66);
 /* A crashed previous owner cannot leave the portal falsely ready. */
 unlinkat(r.directory,"wifi-controller",0);
 strcpy(r.iface,argv[1]);char suffix[7];if(!qnr_mac_suffix(r.iface,suffix))return qnr_exit_code(67);
 struct sockaddr_un local={.sun_family=AF_UNIX},peer={.sun_family=AF_UNIX};
 if(snprintf(local.sun_path,sizeof local.sun_path,QNR_RUN "/control.%ld",(long)getpid())>=(int)sizeof local.sun_path||
  snprintf(peer.sun_path,sizeof peer.sun_path,"/run/wpa/%s",r.iface)>=(int)sizeof peer.sun_path)return qnr_exit_code(68);
 r.control=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);if(r.control<0)return qnr_exit_code(68);
 if(fcntl(r.control,F_SETFD,FD_CLOEXEC)){close(r.control);return qnr_exit_code(68);}
 strcpy(r.local,local.sun_path);
 if(bind(r.control,(struct sockaddr*)&local,sizeof local)||connect(r.control,(struct sockaddr*)&peer,sizeof peer)){
  close(r.control);unlink(r.local);return qnr_exit_code(68);
 }
 struct qnw_io io={&r,qnr_command,qnr_role};struct qnw_mode mode;
 if(!qnw_init(&mode,&io,station_id,suffix)){close(r.control);unlink(r.local);return qnr_exit_code(69);}
 struct qnt_transaction transaction;if(!qnt_init(&transaction,&mode,&r,qnr_persist))return qnr_exit_code(69);
#ifdef QC5_NETWORK_AP_PROBE
 struct qna_probe ap_probe={0};char consumed[16];int did_probe=qnf_read(r.directory,"ap-probe-consumed",consumed,sizeof consumed)==5&&!strcmp(consumed,"QAP1\n");
#endif
 if(!strcmp(argv[2],"auto")){
  struct qc5_wifi_profile saved;int parsed=qnr_saved_profile(r.sd,r.iface,&saved);
  /* The private supplicant may have survived a controller restart. Reset
   * only its RAM network entries after loading the authoritative profile.
   * This bounds profile allocation and never changes flash credentials. */
  if(parsed<0||!qwr_reset_owned(&mode)){memset(&saved,0,sizeof saved);return qnr_exit_code(69);}
  if(parsed==1){
#ifdef QC5_NETWORK_AP_PROBE
   if(!did_probe){if(!qna_init(&ap_probe,&saved)){memset(&saved,0,sizeof saved);return qnr_exit_code(69);}}
   else
#endif
   if(!qnt_load_saved(&transaction,&saved)){memset(&saved,0,sizeof saved);return qnr_exit_code(69);}
   r.boot_profile=saved;r.migrate_pending=1;
  }
  if(parsed<0)transaction.phase=QNT_JOIN_FAILED;memset(&saved,0,sizeof saved);
 }
 if(!qnr_publish(&r,&transaction)||!qnf_text(r.directory,"wifi-controller","QC5_WIFI_CONTROLLER_1\n"))return qnr_exit_code(69);
 signal(SIGTERM,qnr_signal);signal(SIGINT,qnr_signal);signal(SIGPIPE,SIG_IGN);int ok=1;
 qnr_initial_scan(&r);
 uint64_t scan_after=0,dhcp_retry=0;
 while(!quit){
  uint64_t now=qnr_now();if(!now||!qnr_setup_once(&r,&transaction,now)||!qnr_request(&r,&transaction)||!qnt_tick(&transaction,now,r.dhcp>0&&qnr_leased(r.iface),qnr_portal(now))||!qnr_publish(&r,&transaction)){ok=0;break;}
  qnr_migration_tick(&r,&transaction,now,r.dhcp>0&&qnr_leased(r.iface));
  if(now>=scan_after){(void)qnr_scan_cache(&r);scan_after=now+15000;}
  if(!qnr_dhcp_health(&r,&mode,now,&dhcp_retry)){ok=0;break;}
#ifdef QC5_NETWORK_AP_PROBE
  if(ap_probe.active&&mode.active==QNW_AP&&r.dhcp>0&&ap_probe.observing&&now>=ap_probe.since&&now-ap_probe.since>=8000){
   /* Latch in RAM before selecting STA. A crashed cohort cannot start a
    * second diagnostic AP cycle during this same kernel boot. */
   if(!qnf_text(r.directory,"ap-probe-consumed","QAP1\n")){ok=0;break;}
  }
  int transition=qna_tick(&ap_probe,&transaction,now,r.dhcp>0);
  if(transition<0){ok=0;break;}
  if(transition>0)qnr_probe_log("<6>QC5_NATIVE_AP_PROBE confirmed_ap_and_dhcp_8s recovery_selected\n");
  if(!ap_probe.active&&transaction.phase==QNT_SAVED&&!did_probe){qnr_probe_log("<6>QC5_NATIVE_AP_PROBE station_lease_stable_saved_profile_reused\n");did_probe=1;}
#endif
  usleep(250000);
 }
 unlinkat(r.directory,"wifi-controller",0);
 (void)qnf_text(r.directory,"wifi-phase","stopped\n");
 if(!qnr_stop_child(&r.dhcp))ok=0;close(r.control);unlink(r.local);close(lock);close(r.directory);
 memset(&transaction.profile,0,sizeof transaction.profile);
#ifdef QC5_NETWORK_AP_PROBE
 memset(&ap_probe.saved,0,sizeof ap_probe.saved);
#endif
 fprintf(stderr,"QC5_WIFI_OWNER_EXIT code=%u requested=%u active=%u desired=%u phase=%u\n",ok?0:70,!!quit,mode.active,mode.desired,mode.policy.phase);
 return ok?0:70;
}

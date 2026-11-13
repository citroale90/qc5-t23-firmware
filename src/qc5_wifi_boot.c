/* Cold-start Wi-Fi supervisor. Original uClibc only; no shell, sockets server,
 * video hook, app restart or firewall rules. Production module is cold-only.
 * QC5_WIFI_RAM_TEST is a distinct hardware rehearsal with bounded AP rollback;
 * the firmware builder must reject that test-only module. */
typedef __SIZE_TYPE__ usize;
extern int open(const char *,int,...),close(int),access(const char *,int),mkdir(const char *,unsigned),rename(const char *,const char *),unlink(const char *),fsync(int);
extern __PTRDIFF_TYPE__ read(int,void *,usize),write(int,const void *,usize);
extern unsigned sleep(unsigned);
extern int strcmp(const char *,const char *),memcmp(const void *,const void *,usize);
extern char *strstr(const char *,const char *);
extern void *memset(void *,int,usize),*memcpy(void *,const void *,usize);
extern void *memchr(const void *,int,usize);
extern char *getenv(const char *);
extern int unsetenv(const char *),snprintf(char *,usize,const char *,...);
#ifdef QC5_COLD_RAM_PATCH
extern int qc5_native_patch_apply(void);
extern unsigned qc5_cold_patch_failed_page;
extern void _exit(int) __attribute__((noreturn));
#endif
extern int pthread_attr_init(void *),pthread_attr_destroy(void *);
extern int pthread_attr_setstacksize(void *,usize),pthread_attr_setdetachstate(void *,int);
extern int pthread_create(unsigned long *,const void *,void *(*)(void *),void *);
struct qc5_native_timespec {long sec,nsec;};
extern int clock_gettime(int,struct qc5_native_timespec *);
#include "config_parser.h"
#include "config_read.h"
#include "qc5_wifi_profile.h"
#include "qc5_network_policy.h"
#ifndef QC5_WIFI_HARNESS
#define QC5_STATION_FREESTANDING 1
#include "qc5_station.h"
#define NATIVE_READY (*(volatile unsigned char*)0x0089dcc8u)
#define NATIVE_MODE (*(volatile unsigned*)0x0089f35cu)
#define NATIVE_STATE (*(volatile unsigned*)0x0089f364u)
static void message(const char*);
static int native_connect(const struct qc5_wifi_profile*p){
 if(!NATIVE_READY)return -1;
 /* The native setter resets device network state before the HAL request. */
 ((void(*)(const char*,const char*))0x0048df58u)(p->ssid,p->password);return 0;
}
static int native_stop_ap(void){return ((int(*)(void))0x0048e408u)();}
#include "qc5_wifi_supplicant.h"
static int native_ap(const char*name){return ((int(*)(const char*))0x0048e76cu)(name);}
static int native_refresh_scan(void){
 int tid=0;
 if(NATIVE_MODE!=2||qc5_supplicant_scan(&tid)!=1)return -1;
 volatile unsigned*count=(volatile unsigned*)0x0089f34cu;*count=64;
 int result=((int(*)(void*,unsigned*))0x0048e450u)((void*)0x0089dd4cu,(unsigned*)count);
 if(result||*count>64){*count=0;return -1;}return 0;
}
static int native_save(const struct qc5_wifi_profile*p){return ((int(*)(const char*,const char*))0x0048ef08u)(p->ssid,p->password);}
static int native_get(unsigned char*b){return ((int(*)(const char*,void*,int))0x005af048u)("VEEPAI-WIFI-STA",b,128);}
static void native_snapshot(unsigned char*b){memcpy(b,(const void*)0x0089dccau,128);}
#else
extern unsigned qc5_test_ready,qc5_test_mode,qc5_test_state;
#define NATIVE_READY qc5_test_ready
#define NATIVE_MODE qc5_test_mode
#define NATIVE_STATE qc5_test_state
extern int qc5_supplicant_quiesce(void),native_refresh_scan(void);
extern int native_connect(const struct qc5_wifi_profile*),native_stop_ap(void),native_ap(const char*),native_save(const struct qc5_wifi_profile*),native_get(unsigned char*),qc5_station_online(void),qc5_station_is_ap(void),qc5_station_ap_status(void),qc5_station_ssid(char[33]);
extern void native_snapshot(unsigned char*);
extern int qc5_test_marker_open(const char*);
extern int qc5_test_request_open(const char*);
#endif
static char bytes[8193];
#ifdef QC5_WIFI_RAM_TEST
__attribute__((used))static const char ram_test_marker[]="QC5_NATIVE_WIFI_RAM_TEST";
static const char*configuration_path(void){return "/mnt/sda0/codex-native-060-20261026/config-wifi-test.txt";}
static const char*runtime_path(void){return "/tmp/qc5-wifi-test060";}
#else
static const char*configuration_path(void){const char*p=getenv("QC5_CONFIG");return p?p:"/mnt/sda0/config.txt";}
static const char*runtime_path(void){const char*p=getenv("QC5_RUN");return p?p:"/tmp/qc5-hack";}
#endif
static void message(const char*s){usize n=0;while(s[n])n++;write(2,s,n);}
static int read_path(const char*path,char*out,usize cap){int fd=open(path,0);if(fd<0)return -1;long n=qc5_config_read_fd(fd,out,cap);close(fd);if(n<0||(usize)n>=cap)return -1;out[n]=0;return (int)n;}
static int from_record(const unsigned char*b,struct qc5_wifi_profile*p){
 memset(p,0,sizeof(*p));if(!memchr(b,0,33)||!memchr(b+64,0,64))return 0;
 memcpy(p->ssid,b,33);memcpy(p->password,b+64,64);p->open=!p->password[0];return qc5_wifi_valid(p);
}
static int snapshot(struct qc5_wifi_profile*p){
 unsigned char a[128],b[128];native_snapshot(a);native_snapshot(b);
 int ok=!memcmp(a,b,sizeof(a))&&from_record(a,p);memset(a,0,sizeof(a));memset(b,0,sizeof(b));return ok;
}
static int saved_profile(struct qc5_wifi_profile*p){unsigned char b[128];memset(b,0,sizeof(b));int ok=native_get(b)>0&&from_record(b,p);memset(b,0,sizeof(b));return ok;}
static int matches(const struct qc5_wifi_profile*p){char name[33];return NATIVE_MODE==2&&NATIVE_STATE==2&&qc5_station_online()&&qc5_station_ssid(name)&&!strcmp(name,p->ssid);}
static int hotspot_matches(const char*name){
 int status=qc5_station_ap_status();if(!status)return 0;
 if(status<0)return 1;
 /* An unreported AP ESSID must not repeatedly disconnect a setup client.
  * A reported foreign name or a radio that left AP mode needs recovery. */
 char actual[33];return !qc5_station_ssid(actual)||!strcmp(actual,name);
}
static int marker(const char*name,const char*text,usize n){
 const char*run=runtime_path();char path[600],tmp[600];
 /* MIPS O32 flags, not host flag values. Parent is a private runtime directory. */
 mkdir(run,0700);
 if(snprintf(path,sizeof(path),"%s/%s",run,name)>=(int)sizeof(path)||snprintf(tmp,sizeof(tmp),"%s/.%s-new",run,name)>=(int)sizeof(tmp))return -1;
#ifdef QC5_WIFI_HARNESS
 int fd=qc5_test_marker_open(tmp);
#else
 int fd=open(tmp,1|0x100|0x200|0x20000,0600); /* WRONLY|CREAT|TRUNC|NOFOLLOW */
#endif
 if(fd<0)return -1;usize at=0;int bad=0;
 while(at<n){long k=write(fd,text+at,n-at);if(k<=0){bad=1;break;}at+=(usize)k;}
 if(!bad&&fsync(fd))bad=1;if(close(fd))bad=1;
 if(bad||rename(tmp,path)){unlink(tmp);return -1;}return 0;
}
static void remove_controller_marker(void){
 const char*run=runtime_path();char path[600];
 if(snprintf(path,sizeof(path),"%s/wifi-controller",run)<(int)sizeof(path))unlink(path);
}
static unsigned portal_recently_active(uint64_t now){
 char path[600],text[32];
 if(snprintf(path,sizeof(path),"%s/wifi-portal-active",runtime_path())>=(int)sizeof(path))return 0;
 int n=read_path(path,text,sizeof(text));if(n<2||text[n-1]!='\n')return 0;
 uint64_t when=0;for(int i=0;i<n-1;i++){
  unsigned digit=(unsigned char)text[i]-'0';
  if(digit>9||when>(UINT64_MAX-digit)/10)return 0;when=when*10+digit;
 }
 return when&&now>=when&&now-when<=15000;
}
static int committed_marker(const struct qc5_wifi_profile*p){
 /* Legacy tag filename identifies a confirmed association, not a durable
  * native commit: native_save queues its flash write asynchronously. Publish
  * the private profile first so the gateway can save SD independently. */
 unsigned char record[128];memset(record,0,sizeof(record));memcpy(record,p->ssid,33);memcpy(record+64,p->password,64);
 int error=marker("wifi-profile",(const char*)record,sizeof(record));memset(record,0,sizeof(record));
 if(error)return -1;
 char b[18];uint64_t tag=qc5_wifi_tag(p);
 for(int i=15;i>=0;i--){b[i]="0123456789abcdef"[tag&15];tag>>=4;}b[16]='\n';b[17]=0;
 if(marker("wifi-committed",b,17)){message("QC5_WIFI_MARKER_FAILED\n");return -1;}return 0;
}
#include "qc5_wifi_portal_native.h"
static void ap_name(char out[32]){
 char mac[32],hex[13];int n=read_path("/sys/class/net/wlan0/address",mac,sizeof(mac)),used=0;
 for(int i=0;i<n&&used<12;i++)if((mac[i]>='0'&&mac[i]<='9')||(mac[i]>='a'&&mac[i]<='f')||(mac[i]>='A'&&mac[i]<='F'))hex[used++]=mac[i];
 if(used==12){hex[12]=0;snprintf(out,32,"QC5-%s",hex+6);}else snprintf(out,32,"QC5-Setup");
}
/* Overlay one literal field only when present. Reuse the single file buffer;
 * missing SD keys inherit resident defaults, explicit empty/0 stays explicit. */
static int profile_field(int n,const char*key,char*out,usize cap){
 char value[65]={0};if(cap>sizeof(value))return -1;
 int r=qc5_config_get(bytes,(usize)n,key,value,cap);
 if(r>0)memcpy(out,value,cap);memset(value,0,sizeof(value));return r<0?-1:0;
}
static int load_profile(struct qc5_wifi_profile*p){
 const char*path=configuration_path(),*defaults=getenv("QC5_CONFIG_DEFAULT");
 if(!defaults)defaults=getenv("QC5_DEFAULT_CONFIG");
 if(!defaults)defaults="/system/system/qc5/defaults/config.txt";
 char enable[20]={0},mode[20]={0},apply_wifi[20]={0},open_wifi[20]={0};
 memset(p,0,sizeof(*p));int have_defaults=0,network_bad=0,n=read_path(defaults,bytes,sizeof(bytes));
 /* Parse defaults before reusing bytes for the SD. No second 8 KiB copy. */
 for(int layer=0;layer<2;layer++){
  if(layer){
   n=-1;
   for(int i=0;i<60;i++){
    if(access("/mnt/sda0/QC5-SAFE-MODE",0)==0)goto rejected;
    n=read_path(path,bytes,sizeof(bytes));if(n>=0)break;
    char mounts[2048];int k=read_path("/proc/mounts",mounts,sizeof(mounts));
    if(access("/sys/class/block/mmcblk0",0)||(k>=0&&strstr(mounts," /mnt/sda0 ")))break;
    sleep(2);
   }
  }
  if(n<0){if(!layer)continue;if(!have_defaults)goto rejected;break;}
  if(!layer)have_defaults=1;
  if(profile_field(n,"HACK_ENABLE",enable,sizeof(enable))||profile_field(n,"BOOT_MODE",mode,sizeof(mode))||
     profile_field(n,"WIFI_APPLY",apply_wifi,sizeof(apply_wifi))||profile_field(n,"WIFI_OPEN",open_wifi,sizeof(open_wifi)))goto rejected;
  if(profile_field(n,"SSID",p->ssid,sizeof(p->ssid))||profile_field(n,"PWD",p->password,sizeof(p->password)))network_bad=1;
  memset(bytes,0,sizeof(bytes));
 }
 memset(bytes,0,sizeof(bytes));
 if(strcmp(enable,"1")||!strcmp(mode,"stock")||!strcmp(mode,"ui")||(mode[0]&&strcmp(mode,"full")&&strcmp(mode,"provision")))goto rejected;
 if(!network_bad&&p->ssid[0]&&strcmp(apply_wifi,"0")){
  p->open=!strcmp(open_wifi,"1");if(qc5_wifi_valid(p))return 1;network_bad=1;
 }
 if(network_bad)message("QC5_WIFI_PROFILE_REJECTED_USING_FALLBACK\n");
 memset(p,0,sizeof(*p));return 0;
 rejected:memset(bytes,0,sizeof(bytes));memset(p,0,sizeof(*p));return -1;
}

static void *apply(void*unused){
 struct qc5_wifi_profile manual={0},current={0},target={0},seen={0},saved={0};struct qc5_net_policy policy={0};
 int configured=load_profile(&manual),initialized=0,have_target=0,have_seen=0,committed=0,native_ack=0,last_phase=-1,ap_owned=0,waiting_native_mode=0;unsigned last_native_mode=0,save_attempts=0;uint64_t retry_commit=0,retry_ap=0,scan_hash=0;char name[32];(void)unused;
#ifdef QC5_WIFI_RAM_TEST
 uint64_t ap_test_deadline=0;
#endif
 if(configured<0){message("QC5_WIFI_CONFIG_REJECTED\n");goto done;}
 for(;;){
  if(access("/mnt/sda0/QC5-SAFE-MODE",0)==0)break;
  if(!NATIVE_READY){sleep(2);continue;}
  /* READY becomes true while the USB driver is loading, before the native
   * AP/STA stack has selected a mode. Connecting from mode0 bypasses the
   * native AP-to-STA supplicant initialization and races startup. */
  if(!initialized&&NATIVE_MODE!=1&&NATIVE_MODE!=2){
   if(!waiting_native_mode){message("QC5_WIFI_WAIT_NATIVE_MODE\n");waiting_native_mode=1;}
   sleep(2);continue;
  }
  struct qc5_native_timespec ts;if(clock_gettime(1,&ts)){message("QC5_WIFI_CLOCK_FAILED\n");break;}
  uint64_t now=(uint64_t)(unsigned long)ts.sec*1000+(unsigned long)ts.nsec/1000000;
  int have_current=NATIVE_MODE==2&&snapshot(&current),submitted=0,already_joining=0;
#ifdef QC5_WIFI_RAM_TEST
  if(access("/tmp/qc5-wifi-test060/STOP",0)==0)break;
  if(initialized&&access("/tmp/qc5-wifi-test060/REQUEST_AP",0)==0){
   unlink("/tmp/qc5-wifi-test060/REQUEST_AP");memset(&policy,0,sizeof(policy));policy.phase=QC5_NET_SETUP_AP;policy.ap_up=NATIVE_MODE==1;
   ap_test_deadline=now+180000;retry_ap=0;committed=0;message("QC5_WIFI_RAM_AP_TEST_REQUESTED\n");
   if(NATIVE_MODE!=1&&native_ap(name))message("QC5_WIFI_RAM_AP_START_FAILED\n");
  }
  if(ap_test_deadline&&now>=ap_test_deadline){
   ap_test_deadline=0;
   if(policy.phase==QC5_NET_SETUP_AP&&saved_profile(&saved)){
    target=saved;have_target=1;submitted=1;message("QC5_WIFI_RAM_AP_AUTOMATIC_ROLLBACK\n");
   }
  }
#endif
  if(!initialized){
   /* wlan0 may not exist when the constructor runs. Derive the unique
    * setup name only after the native radio has completed initialization. */
   ap_name(name);
   if(have_current){seen=current;have_seen=1;}
   int have_saved=saved_profile(&saved);
   if(configured==1){target=manual;have_target=1;submitted=1;already_joining=have_current&&qc5_wifi_equal(&current,&target);}
   else if(have_current){target=current;have_target=1;policy.staged=1;already_joining=1;}
   else if(have_saved){target=saved;have_target=1;policy.staged=1;}
   policy.ap_up=NATIVE_MODE==1;initialized=1;
   if(marker("wifi-controller","QC5_WIFI_CONTROLLER_1\n",22)){message("QC5_WIFI_CONTROLLER_MARKER_FAILED\n");break;}
  }else if(have_current&&(last_native_mode==1||!have_seen||!qc5_wifi_equal(&current,&seen))){
   seen=current;have_seen=1;target=current;have_target=1;submitted=1;already_joining=1;committed=0;native_ack=0;save_attempts=0;retry_commit=0;
  }
  struct qc5_wifi_profile requested;int request=take_wifi_request(&requested);
  if(request>0){target=requested;have_target=1;submitted=1;already_joining=0;committed=0;native_ack=0;save_attempts=0;retry_commit=0;message("QC5_WIFI_LOCAL_REQUEST_ACCEPTED\n");}
  else if(request<0)message("QC5_WIFI_LOCAL_REQUEST_REJECTED\n");
  memset(&requested,0,sizeof(requested));
  struct qc5_net_observation o={0};o.now_ms=now;o.has_saved_credentials=(unsigned)have_target;o.credentials_submitted=(unsigned)submitted;
  o.station_online=have_target&&(unsigned)matches(&target);
  o.ap_clients=portal_recently_active(now);
  /* Visibility is unknown while the native AP owns the only interface.
   * Retry saved credentials after an idle interval; never test the cloud. */
  unsigned actions=qc5_net_step(&policy,&o);
  if((actions&QC5_NET_TRY_STA)&&!already_joining&&have_target){
   char note[128];snprintf(note,sizeof(note),"QC5_WIFI_JOIN_STATE ready=%u mode=%u state=%u\n",(unsigned)NATIVE_READY,(unsigned)NATIVE_MODE,(unsigned)NATIVE_STATE);message(note);
   if(native_connect(&target))message("QC5_WIFI_JOIN_REJECTED\n");else{seen=target;have_seen=1;message("QC5_WIFI_JOIN_SUBMITTED\n");}
  }
  if(policy.phase!=QC5_NET_SETUP_AP)ap_owned=0;
  if(policy.phase==QC5_NET_SETUP_AP&&(!ap_owned||NATIVE_MODE!=1||!hotspot_matches(name))&&now>=retry_ap){
   retry_ap=now+10000;
   char note[128];snprintf(note,sizeof(note),"QC5_WIFI_AP_RESET mode=%u state=%u radio_ap=%d\n",(unsigned)NATIVE_MODE,(unsigned)NATIVE_STATE,qc5_station_ap_status());message(note);
   /* start_ap stops the supplicant, but does not stop an existing hostapd.
    * Replacing a vendor AP therefore needs the native stop step first. */
   /* Scanning needs the original wpa_run loop. Cache visible networks
    * before stopping it; the AP helper cannot scan through a dead loop. */
   if(NATIVE_MODE==2){
    if(native_refresh_scan())message("QC5_WIFI_SCAN_CACHE_UNAVAILABLE\n");
    publish_wifi_networks(&scan_hash);
   }
   if(qc5_supplicant_quiesce())message("QC5_WIFI_SUPPLICANT_NOT_QUIESCENT\n");
   else if(native_stop_ap()||native_ap(name))message("QC5_WIFI_AP_FAILED\n");
   else{ap_owned=1;message("QC5_WIFI_AP_REQUESTED\n");}
  }
  if(policy.phase==QC5_NET_CONNECTED&&o.station_online&&!committed){
   committed=committed_marker(&target)==0;
   if(committed)message("QC5_WIFI_ASSOCIATED_PROFILE_READY\n");
  }
  if(policy.phase==QC5_NET_CONNECTED&&o.station_online&&!native_ack&&save_attempts<3&&now>=retry_commit){
   retry_commit=now+30000;
   if(saved_profile(&saved)&&qc5_wifi_equal(&saved,&target))native_ack=1;
   else{save_attempts++;if(!native_save(&target)&&saved_profile(&saved)&&qc5_wifi_equal(&saved,&target))native_ack=1;}
   if(native_ack){
    message("QC5_WIFI_NATIVE_RAM_READBACK_MATCHED\n");
   }else message("QC5_WIFI_NATIVE_COMMIT_UNVERIFIED\n");
  }
  if(actions&QC5_NET_DISCARD_STAGED){committed=0;memset(&manual,0,sizeof(manual));}
  publish_wifi_networks(&scan_hash);
  if(last_phase!=(int)policy.phase){
   const char*phase=policy.phase==QC5_NET_SETUP_AP?"hotspot\n":policy.phase==QC5_NET_CONNECTED?"connected\n":"joining\n";
   usize length=0;while(phase[length])length++;if(!marker("wifi-phase",phase,length))last_phase=(int)policy.phase;
  }
  last_native_mode=NATIVE_MODE;sleep(2);
 }
 done:if(initialized)remove_controller_marker();
#ifdef QC5_WIFI_RAM_TEST
 restore_native_dns();
#endif
 memset(&manual,0,sizeof(manual));memset(&target,0,sizeof(target));memset(&current,0,sizeof(current));memset(&seen,0,sizeof(seen));memset(&saved,0,sizeof(saved));memset(bytes,0,sizeof(bytes));return (void*)0;
}
#ifndef QC5_WIFI_HARNESS
static int signatures(void){
 static const struct {unsigned address;unsigned char bytes[16];} guards[]={
  {0x0048e450u,{0xc1,0x0,0x80,0x10,0x0,0x0,0x0,0x0,0xbf,0x0,0xa0,0x10,0x0,0x0,0x0,0x0}},
  /* Original embedded event-loop termination, pipe and wpa_run lifecycle. */
  {0x004edfccu,{0x01,0,0x03,0x24,0x8c,0,0x02,0x3c,0x08,0,0xe0,0x03,0x98,0xfa,0x43,0xac}},
  {0x004edb70u,{0x58,0,0xe2,0x8e,0x8f,0,0x40,0x14,0,0,0,0,0x40,0,0xf2,0x8e}},
  {0x0048df58u,{0xd8,0xff,0xbd,0x27,0x24,0,0xbf,0xaf,0x18,0,0xa4,0xaf,0xdd,0x9e,0x16,0x0c}},
  {0x0048e408u,{0x76,0,0x04,0x3c,0xe0,0xff,0xbd,0x27,0x1c,0,0xbf,0xaf,0x05,0x37,0x1d,0x0c}},
  {0x0048ed64u,{0xb0,0xff,0xbd,0x27,0x30,0,0xb0,0xaf,0x8a,0,0x10,0x3c,0xc8,0xdc,2,0x92}},
  {0x0048e76cu,{0x08,0xff,0xbd,0x27,0x8a,0,5,0x3c,0xec,0,0xb4,0xaf,0xe4,0,0xb2,0xaf}},
  {0x0048ef08u,{0x23,0,0x80,0x10,0,0,0,0,0x21,0,0xa0,0x10,0,0,0,0}},
  {0x005af048u,{0xb8,0xff,0xbd,0x27,0x30,0,0xb0,0xaf,0x8d,0,0x10,0x3c,0x14,0xe9,2,0x8e}}
 };
 for(unsigned i=0;i<sizeof(guards)/sizeof(guards[0]);i++)if(memcmp((const void*)(usize)guards[i].address,guards[i].bytes,16))return 0;return 1;
}
__attribute__((constructor))static void begin(void){
 const char*enabled=getenv("QC5_SD_WIFI");unsigned long thread;
#ifdef QC5_WIFI_RAM_TEST
 enabled="1";
#endif
 union {unsigned long align;unsigned char bytes[64];} attributes;unsetenv("LD_PRELOAD");
 if(!enabled||strcmp(enabled,"1"))return;
#ifdef QC5_COLD_RAM_PATCH
 if(!signatures()){message("QC5_COLD_PATCH_PROFILE_FAILED\n");_exit(125);}
 int patch_result=qc5_native_patch_apply();
 if(patch_result){char label[96];snprintf(label,sizeof(label),"QC5_COLD_PATCH_APPLY_FAILED_%d_PAGE_%08x\n",patch_result,qc5_cold_patch_failed_page);message(label);_exit(125);}
 message("QC5_COLD_PATCH_APPLIED\n");
#else
 if(!signatures())return;
#endif
#ifdef QC5_WIFI_RAM_TEST
 if(prepare_native_dns()){message("QC5_RAM_DHCP_DNS_PREPARATION_FAILED\n");return;}
#endif
 if(pthread_attr_init(&attributes))return;
 int ok=pthread_attr_setstacksize(&attributes,65536)==0&&pthread_attr_setdetachstate(&attributes,1)==0;
 if(ok)pthread_create(&thread,&attributes,apply,(void*)0);pthread_attr_destroy(&attributes);
}
#endif

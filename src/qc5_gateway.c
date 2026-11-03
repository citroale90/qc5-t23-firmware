#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/mman.h>
#endif
#include <time.h>
#include <unistd.h>
#if defined(__linux__) && !defined(QC5_HOST_TEST)
#include <linux/wireless.h>
#include "qc5_station.h"
#endif
#include "config_parser.h"
#include "qc5_sd.h"
#include "qc5_version.h"
#include "qc5_memory.h"
#include "config_read.h"
#include "pppp_table.h"
/* One process, fixed buffers, no shell/CGI children, no preload or vendor restart.
 * The supported local PPPP interface keeps all device operations in stock code. */
#define CAP (100*1024)
static unsigned char response[CAP] __attribute__((aligned(4096)));
static unsigned char pending[CAP] __attribute__((aligned(4096)));
static unsigned char wire[8192];
static size_t pending_len;
static int pending_dirty;
static unsigned char jpeg_cache[CAP] __attribute__((aligned(4096)));
static size_t jpeg_len;
static long long jpeg_time,video_quiet_until,jpeg_retry_at;
static char config[8193], webroot[512]="/system/system/qc5/www";
static char config_path[512]="/mnt/sda0/config.txt", sdroot[512]="/mnt/sda0";
static char nvs_path[512]="/dev/mtd5ro", host[64]="127.0.0.1";
static char username[64]="admin", password[64];
static int udp=-1,connected,authenticated,web_port=80,reconnect_needed;
static pid_t rtsp_pid;
static int rtsp_listener=-1,rtsp_port;
static volatile sig_atomic_t stopping;
static struct sockaddr_in peer,target;
static int (*media_packet)(int,const struct sockaddr_in*);
static unsigned char uid[20];
static int uid_pinned;
static uint16_t txseq,rxseq;
static size_t response_len;
static char status_cache[4096];
static long long status_time,keepalive_time;
static const char*wifi_state="config_originale";
static const char*setup_state="non_richiesto";
static int move_ack_command=-1;
static long long move_ack_time;
static long long now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
/* These page-aligned BSS buffers retain snapshot data as private dirty RAM
 * on a camera without swap. Once no caller owns the response, release its
 * pages rather than keeping the largest past JPEG resident forever. */
static void release_pages(void*buffer,size_t n){
#ifdef __linux__
 (void)madvise(buffer,n,MADV_DONTNEED);
#else
 (void)buffer;(void)n;
#endif
}
static void release_idle_buffers(int video_requested){
 if(response_len){response_len=0;release_pages(response,sizeof(response));}
 if(pending_dirty&&!pending_len){release_pages(pending,sizeof(pending));pending_dirty=0;}
 if(jpeg_len&&(video_requested||now_ms()-jpeg_time>=10000)){
  jpeg_len=0;jpeg_time=0;release_pages(jpeg_cache,sizeof(jpeg_cache));
 }
}
static uint32_t le32(const unsigned char*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void crypto(unsigned char *p,size_t n,int enc){unsigned char prev=0,h[4]={0};const unsigned char*k=(const unsigned char*)"vstarcam2018";for(;*k;k++){h[0]+=*k;h[1]-=*k;h[2]+=*k/3;h[3]^=*k;}for(size_t i=0;i<n;i++){unsigned char c=p[i];p[i]^=pppp_table[(h[prev&3]+prev)&255];prev=enc?p[i]:c;}}
static int packet(int op,const void *data,size_t n,const struct sockaddr_in*addr){unsigned char b[4096];if(n>sizeof(b)-4)return -1;b[0]=0xf1;b[1]=(unsigned char)op;b[2]=(unsigned char)(n>>8);b[3]=(unsigned char)n;if(n)memcpy(b+4,data,n);crypto(b,n+4,1);return sendto(udp,b,n+4,MSG_DONTWAIT,(const struct sockaddr*)addr,sizeof(*addr))==(ssize_t)n+4?0:-1;}
static void camera_keepalive(void){if(connected&&now_ms()-keepalive_time>=5000){packet(0xe0,NULL,0,&peer);keepalive_time=now_ms();}}
static void disconnect_camera(void){if(udp>=0){if(connected)packet(0xf0,NULL,0,&peer);close(udp);}udp=-1;connected=authenticated=reconnect_needed=0;txseq=rxseq=0;pending_len=0;}
static int receive_packet(int timeout_ms,struct sockaddr_in *source){camera_keepalive();struct pollfd p={udp,POLLIN,0};int r=poll(&p,1,timeout_ms);if(r<=0)return r;socklen_t len=sizeof(*source);ssize_t n=recvfrom(udp,wire,sizeof(wire),MSG_DONTWAIT,(struct sockaddr*)source,&len);if(n<4||(target.sin_addr.s_addr&&source->sin_addr.s_addr!=target.sin_addr.s_addr))return 0;crypto(wire,(size_t)n,0);if(wire[0]!=0xf1||((unsigned)wire[2]*256+wire[3])!=(unsigned)n-4)return 0;if(wire[1]==0xe0)packet(0xe1,NULL,0,source);if(wire[1]==0xf0){if(!connected||source->sin_port!=peer.sin_port)return 0;connected=authenticated=0;return -1;}if(media_packet&&media_packet((int)n,source)<0)return -1;return (int)n;}
static int valid_uid(const unsigned char*data){
 for(int part=0;part<2;part++){int start=part?12:0,count=0;
  for(int i=0;i<8&&data[start+i];i++){unsigned char c=data[start+i];if(c<33||c>126||c=='-')return 0;count++;}
  if(!count)return 0;
 }
 return 1;
}
static int expected_uid(const char*text){
 char prefix[9],digits[11],suffix[9];int used=0;
 if(sscanf(text,"%8[^-]-%10[0-9]-%8[^-]%n",prefix,digits,suffix,&used)!=3||used!=(int)strlen(text))return -1;
 char*end;errno=0;unsigned long long number=strtoull(digits,&end,10);
 if(errno||*end||number>0xffffffffULL)return -1;
 memset(uid,0,sizeof(uid));memcpy(uid,prefix,strlen(prefix));memcpy(uid+12,suffix,strlen(suffix));
 for(int i=0;i<4;i++)uid[8+i]=(unsigned char)(number>>(24-i*8));
 if(!valid_uid(uid))return -1;
 uid_pinned=1;return 0;
}
static int discover_target(void){
 if(!uid_pinned)return -1;
 int yes=1;if(setsockopt(udp,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes)))return -1;
 struct sockaddr_in address=target;
#ifdef QC5_HOST_TEST
 const char*fixture=getenv("QC5_TEST_DISCOVERY_IP");
 if(!fixture||inet_pton(AF_INET,fixture,&address.sin_addr)!=1)return -1;
 return packet(0x30,NULL,0,&address);
#else
 struct ifaddrs*interfaces;if(getifaddrs(&interfaces))return -1;int sent=0;
 for(struct ifaddrs*i=interfaces;i;i=i->ifa_next){
  if(!i->ifa_broadaddr||i->ifa_broadaddr->sa_family!=AF_INET||!(i->ifa_flags&IFF_UP)||!(i->ifa_flags&IFF_BROADCAST))continue;
  address.sin_addr=((struct sockaddr_in*)i->ifa_broadaddr)->sin_addr;
  if(address.sin_addr.s_addr&&packet(0x30,NULL,0,&address)==0)sent=1;
 }
 freeifaddrs(interfaces);return sent?0:-1;
#endif
}
static int camera_connect(void){
 disconnect_camera();udp=socket(AF_INET,SOCK_DGRAM,0);if(udp<0)return -1;
 memset(&target,0,sizeof(target));target.sin_family=AF_INET;target.sin_port=htons(32108);
 int automatic=!strcmp(host,"auto");if(!automatic&&inet_pton(AF_INET,host,&target.sin_addr)!=1){disconnect_camera();return -1;}
 struct sockaddr_in source;long long end=now_ms()+3500,retry=0;
 while(!stopping&&now_ms()<end){
  /* UDP discovery can be lost while a previous session is being released.
   * Retry each handshake phase, keeping both the deadline and identity check. */
  if(now_ms()>=retry){
   int sent=connected?packet(0x41,uid,20,&peer):automatic?discover_target():packet(0x30,NULL,0,&target);
   if(sent){disconnect_camera();return -1;}retry=now_ms()+300;
  }
  int n=receive_packet(100,&source);if(n<24)continue;
  if(wire[1]==0x41){
   if(!valid_uid(wire+4))continue;
   if(connected&&source.sin_port!=peer.sin_port)continue;
   if(uid_pinned&&memcmp(uid,wire+4,20)){if(automatic&&!connected)continue;disconnect_camera();return -1;}
   memcpy(uid,wire+4,20);uid_pinned=1;peer=source;target.sin_addr=source.sin_addr;
   if(packet(0x41,uid,20,&peer)){disconnect_camera();return -1;}connected=1;retry=now_ms()+300;
  }else if(wire[1]==0x42&&connected&&source.sin_port==peer.sin_port&&!memcmp(uid,wire+4,20))return 0;
 }
 fprintf(stderr,"QC5_CONNECT_TIMEOUT phase=%s uptime_ms=%lld\n",connected?"confirmation":"discovery",now_ms());
 disconnect_camera();return -1;
}
static int url_encode(const char*in,char*out,size_t cap){static const char hex[]="0123456789ABCDEF";size_t n=0;for(const unsigned char*p=(const unsigned char*)in;*p;p++){int safe=isalnum(*p)||strchr("-_.~",*p)!=NULL;if(n+(safe?1:3)>=cap)return -1;if(safe)out[n++]=(char)*p;else{out[n++]='%';out[n++]=hex[*p>>4];out[n++]=hex[*p&15];}}out[n]=0;return 0;}
/* ACK idle notifications too: ignoring them can fill the vendor's send window. */
static int rpc_trace_enabled(void){static unsigned lines;const char*v=getenv("QC5_RPC_DIAGNOSTICS");if(!v||strcmp(v,"1")||lines>=512)return 0;lines++;return 1;}
static int control_packet(int n,const struct sockaddr_in *source) {
 if(n<8||wire[1]!=0xd0||source->sin_port!=peer.sin_port)return 0;
 if(wire[5]!=0){
  /* Unused reliable channels can carry local alarm notifications. Drain/ACK
   * them without interpreting payloads, otherwise the SDK retains its send
   * window while our next control request waits behind those notifications.
   * The active media parser already acknowledges its own video channel. */
  if(!(media_packet&&wire[5]==1)){
   unsigned char ack[6]={0xd1,wire[5],0,1,wire[6],wire[7]};packet(0xd1,ack,6,source);
   if(rpc_trace_enabled())fprintf(stderr,"QC5_CONTROL_UNUSED_CHANNEL channel=%u sequence=%u bytes=%d uptime_ms=%lld\n",wire[5],(unsigned)wire[6]*256+wire[7],n-8,now_ms());
  }
  return 0;
 }
 uint16_t seq=(uint16_t)((unsigned)wire[6]*256+wire[7]);
 if(seq==rxseq){
  size_t bytes=(size_t)n-8;if(pending_len+bytes>=CAP)return -1;
  memcpy(pending+pending_len,wire+8,bytes);pending_dirty=1;pending_len+=bytes;rxseq++;
 }else if((int16_t)(seq-rxseq)>=0)return 0;
 unsigned char ack[6]={0xd1,0,0,1,wire[6],wire[7]};packet(0xd1,ack,6,source);return 0;
}
static int complete_frame(unsigned expected) {
 while(pending_len>=8){
  if(pending[0]!=1||pending[1]!=10)return -1;
  size_t wanted=8+(size_t)pending[4]+((size_t)pending[5]<<8)+((size_t)pending[6]<<16);
  if(wanted>=CAP)return -1;
  if(pending_len<wanted)return 0;
  unsigned type=(unsigned)pending[2]|(unsigned)pending[3]<<8;
  int match=expected&&type==expected;
  if(match){memcpy(response,pending,wanted);response_len=wanted;response[wanted]=0;}
  pending_len-=wanted;memmove(pending,pending+wanted,pending_len);
  if(match)return 1;
 }
 return 0;
}
static int field_int(const char *,const char *,int *);
static unsigned reply_type(const char *path) {
 static const struct {const char *name;unsigned code;} commands[]={
  {"audiostream",0x6031},{"livestream",0x6037},{"get_status",0x6001},{"get_params",0x6002},{"get_camera_params",0x6003},{"snapshot",0x6015},
  {"decoder_control",0x6019},{"set_wifi",0x6011},{"camera_control",0x6012},{"set_alarm",0x600c},
  {"get_whiteled_value",0x7116},{"set_whiteled_value",0x7117},
  {"set_sensor_preset",0x60b7},{"trans_cmd_string",0x60d1},{"get_record",0x6006}
 };
 for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);i++)
  if(!strcmp(path,commands[i].name))return commands[i].code;
 return 0;
}
static int packet_acknowledged(int n,unsigned channel,uint16_t sequence){
 if(n<10||wire[1]!=0xd1||wire[5]!=channel)return 0;
 unsigned count=(unsigned)wire[6]*256+wire[7];if(count>(unsigned)(n-8)/2)return 0;
 for(unsigned i=0;i<count;i++)if(wire[8+2*i]==(unsigned char)(sequence>>8)&&wire[9+2*i]==(unsigned char)sequence)return 1;
 return 0;
}
/* Schedule reads and writes share type 0x60d1. A late write reply must
 * not satisfy a subsequent plan read (or a read of a different plan). */
static int record_reply_matches(const char*params,const char*reply){
 int cmd=-1,command=-1,got=-1;
 if(field_int(params,"cmd",&cmd)||cmd!=2017||field_int(params,"command",&command))return 1;
 int result_code=0;if(!field_int(reply,"result",&result_code)&&result_code)return 1;
 if(field_int(reply,"cmd",&got)||got!=cmd||field_int(reply,"command",&got)||got!=command)return 0;
 if(command==11){int type=-1;if(field_int(params,"type",&type)||field_int(reply,"type",&got)||got!=type)return 0;}
 return 1;
}
static int camera_rpc(const char *path,const char *params,int timeout_ms) {
 char eu[192],ep[192],url[2048];unsigned char b[2400];
 int stream_stop=!strcmp(path,"livestream")&&!strcmp(params,"streamid=16");
 int stop=stream_stop||(!strcmp(path,"decoder_control")&&!strcmp(params,"command=1&channel=0"));
 /* A stop after a missing move reply uses the same session immediately. */
 if(reconnect_needed&&!stop)disconnect_camera();
 /* Stream subscriptions belong to this exact peer. Never open a new session
  * merely to stop one which has already been disconnected. */
 if(stream_stop&&!connected)return -1;
 if(!connected&&camera_connect())return -1;
 if(url_encode(username,eu,sizeof(eu))||url_encode(password,ep,sizeof(ep)))return -1;
 int len=snprintf(url,sizeof(url),"/%s.cgi?%s%sloginuse=%s&loginpas=%s&user=%s&pwd=%s&",path,params,params[0]?"&":"",eu,ep,eu,ep);
 if(len<0||(size_t)len>=sizeof(url))return -1;
 size_t total=(size_t)len+4;uint16_t request_seq=txseq++;
 b[0]=0xd1;b[1]=0;b[2]=(unsigned char)(request_seq>>8);b[3]=(unsigned char)request_seq;
 b[4]=1;b[5]=10;b[6]=b[7]=0;
 /* Type is uint16 LE, followed by a separate uint24 LE payload length.
  * Putting the high length byte in the type works only below 256 bytes. */
 b[8]=(unsigned char)total;b[9]=(unsigned char)(total>>8);b[10]=(unsigned char)(total>>16);b[11]=0;
 memcpy(b+12,"GET ",4);memcpy(b+16,url,(size_t)len);
 response_len=0;if(complete_frame(0)<0)return -1;unsigned expected=reply_type(path);
 long long end=now_ms()+timeout_ms,retry=now_ms()+150;int acknowledged=0;
 if(packet(0xd0,b,(size_t)len+16,&peer))return -1;
 while(!stopping&&now_ms()<end) {
  /* Repeat the same reliable packet only until the SDK acknowledges receipt.
   * Never assign a new sequence or repeat an acknowledged device operation. */
  if(!acknowledged&&now_ms()>=retry){if(packet(0xd0,b,(size_t)len+16,&peer))return -1;retry=now_ms()+150;}
  struct sockaddr_in source;int n=receive_packet(50,&source);
  if(n<0)return -1;
  if(n>=10&&source.sin_port==peer.sin_port&&packet_acknowledged(n,0,request_seq)){acknowledged=1;continue;}
  if(control_packet(n,&source)<0)return -1;
  int got=complete_frame(expected);if(got<0)return -1;
  if(got){
   if(expected==0x60d1&&!record_reply_matches(params,(char*)response+8)){
    if(rpc_trace_enabled())fprintf(stderr,"QC5_RPC_STALE_RECORD_REPLY uptime_ms=%lld\n",now_ms());
    continue;
   }
   if(rpc_trace_enabled())fprintf(stderr,"QC5_RPC_REPLY operation=%s elapsed_ms=%lld acknowledged=%d uptime_ms=%lld\n",path,now_ms()-(end-timeout_ms),acknowledged,now_ms());
   if(expected==0x6019){int echoed=-1,code=-1;int requested=(int)strtol(params+8,NULL,10);
    if(field_int((char*)response+8,"command",&echoed))continue;
    field_int((char*)response+8,"result",&code);
    fprintf(stderr,"QC5_MOTOR_ACK command=%d result=%d uptime_ms=%lld\n",echoed,code,now_ms());
    if(!code&&(echoed==0||echoed==2||echoed==4||echoed==6)){move_ack_command=echoed;move_ack_time=now_ms();}
    if(echoed!=requested)continue;
   }
   reconnect_needed=0;return 0;
  }
 }
 fprintf(stderr,"QC5_RPC_TIMEOUT operation=%s uptime_ms=%lld\n",path,now_ms());
 authenticated=0;reconnect_needed=1;return -1;
}
static int field_int(const char*text,const char*key,int*value){size_t n=strlen(key);for(const char*p=text;(p=strstr(p,key));p++){if(p>text&&(isalnum((unsigned char)p[-1])||p[-1]=='_'))continue;const char*q=p+n;while(*q==' '||*q=='\t')q++;if(*q++!='=')continue;while(*q==' '||*q=='"'||*q=='\'')q++;char*end;long v=strtol(q,&end,10);if(end==q||v<0||v>2147483647L)return -1;*value=(int)v;return 0;}return -1;}
static int get_status(int force){if(!force&&status_cache[0]&&now_ms()-status_time<5000&&connected&&!reconnect_needed)return 0;if(camera_rpc("get_status","",4000))return -1;if(!strstr((char*)response+8,"sys_ver")||strstr((char*)response+8,"result=-1"))return -1;size_t n=response_len-8;if(n>=sizeof(status_cache))return -1;memcpy(status_cache,response+8,n);status_cache[n]=0;status_time=now_ms();authenticated=1;return 0;}
/* Inspect each NVS bank independently: a stale empty bank must never cause
 * an existing owner's nonempty record in the other bank to be overwritten. */
static int nvs_slice_bank(const char*key,size_t skip,unsigned char*out,size_t count,int bank){
 int fd=open(nvs_path,O_RDONLY|O_CLOEXEC);if(fd<0)return -1;
 unsigned char h[12],entry[16],name[96];size_t keylen=strlen(key)+1;
 if(keylen>sizeof(name)){close(fd);return -1;}
 off_t base=(off_t)bank*0x20000;
 if(pread(fd,h,sizeof(h),base)!=(ssize_t)sizeof(h)){close(fd);return -1;}
 if(memcmp(h,"PCAM",4)||le32(h+4)!=0x20000){close(fd);return 1;}
 for(int i=0;i<143;i++){
  if(pread(fd,entry,16,base+12+i*16)!=16){close(fd);return -1;}
  uint32_t off=le32(entry),n=le32(entry+8);
  if(off<0x900||off>0x40000||n<keylen||n>0x40000-off||skip>n-keylen||count>n-keylen-skip)continue;
  if(pread(fd,name,keylen,off)!=(ssize_t)keylen||memcmp(name,key,keylen))continue;
  int ok=pread(fd,out,count,off+(off_t)keylen+(off_t)skip)==(ssize_t)count;close(fd);return ok?0:-1;
 }
 close(fd);return 1;
}
static int nvs_slice(const char*key,size_t skip,unsigned char*out,size_t count){
 for(int bank=0;bank<2;bank++)if(!nvs_slice_bank(key,skip,out,count,bank))return 0;
 return -1;
}
static int local_setup_allowed(void){
 unsigned char owner[32];int found=0;
 for(int bank=0;bank<2;bank++){
  int r=nvs_slice_bank("app_param",0x1e0,owner,sizeof(owner),bank);
  if(r<0)return -1;
  if(r)continue;
  found=1;
  if(!memchr(owner,0,sizeof(owner))||(owner[0]&&strcmp((char*)owner,"local"))){memset(owner,0,sizeof(owner));return 0;}
 }
 memset(owner,0,sizeof(owner));return found?1:-1;
}
static int read_credentials(void){
 unsigned char pw[64];if(nvs_slice("app_param",0x138,pw,sizeof(pw)))return -1;
 size_t n=strnlen((char*)pw,sizeof(pw));if(n>=sizeof(pw))return -1;
 memcpy(password,pw,n+1);memset(pw,0,sizeof(pw));return 0;
}
static int load_config(void){
 FILE *f=fopen(config_path,"rb");
 if(!f){if(errno==ENOENT){config[0]=0;return 0;}return -1;}
 char next[sizeof(config)];size_t n=fread(next,1,sizeof(next),f);
 int failed=ferror(f);fclose(f);
 if(failed||n>=sizeof(next))return -1;
 next[n]=0;char value[128];
 if(qc5_config_get(next,n,"CAM_NAME",value,sizeof(value))<0)return -1;
 memcpy(config,next,n+1);return 0;
}
static void cfg(const char*k,char*v,size_t n){if(qc5_config_get(config,strlen(config),k,v,n)<0)v[0]=0;}
static void device_camera_id_from_status(char*out,size_t cap){
 out[0]=0;if(cap<6)return;
 const char*key="realdeviceid";const char*p=status_cache;
 while((p=strstr(p,key))){
  if(p>status_cache&&(isalnum((unsigned char)p[-1])||p[-1]=='_')){p++;continue;}
  p+=strlen(key);while(*p==' '||*p=='\t')p++;if(*p++!='=')continue;
  while(*p==' '||*p=='\"'||*p=='\'')p++;
  size_t n=4;memcpy(out,"qc5-",4);
  while(n+1<cap&&(isalnum((unsigned char)*p)||*p=='_'||*p=='-'))out[n++]=(char)tolower((unsigned char)*p++);
  out[n]=0;if(n==4)out[0]=0;return;
 }
}
static void device_camera_id(char*out,size_t cap){out[0]=0;if(!get_status(0))device_camera_id_from_status(out,cap);}
static int logical_camera_id(char*out,size_t cap){
 int ready=get_status(0)==0;
 cfg("CAM_ID",out,cap);
 if(out[0]&&strcmp(out,"auto"))return ready;
 out[0]=0;if(ready)device_camera_id_from_status(out,cap);
 return ready;
}
static void media_url(const char*key,const char*id,char*out,size_t cap){
 char source[512],encoded[240];cfg(key,source,sizeof(source));size_t e=0;
 for(const unsigned char*p=(const unsigned char*)id;*p;p++){
  if(e+4>=sizeof(encoded)){out[0]=0;return;}
  if(isalnum(*p)||*p=='-'||*p=='_'||*p=='.'||*p=='~')encoded[e++]=(char)*p;
  else{snprintf(encoded+e,4,"%%%02X",*p);e+=3;}
 }
 encoded[e]=0;size_t n=0;
 for(const char*p=source;*p;){
  if(!strncmp(p,"{CAM_ID}",8)){
   if(!encoded[0]||n+e>=cap){out[0]=0;return;}
   memcpy(out+n,encoded,e);n+=e;p+=8;
  }else{if(n+1>=cap){out[0]=0;return;}out[n++]=*p++;}
 }
 out[n]=0;
}
static void json_string(char*out,size_t cap,const char*in){size_t n=0;if(cap<3)return;out[n++]='"';for(const unsigned char*p=(const unsigned char*)in;*p&&n+7<cap;p++){if(*p=='"'||*p=='\\'){out[n++]='\\';out[n++]=(char)*p;}else if(*p<32){int r=snprintf(out+n,cap-n,"\\u%04x",*p);if(r<0)break;n+=(size_t)r;}else out[n++]=(char)*p;}out[n++]='"';out[n]=0;}
static int send_all(int fd,const void*data,size_t n){const unsigned char*p=data;long long end=now_ms()+2500;while(n&&now_ms()<end){struct pollfd s={fd,POLLOUT,0};if(poll(&s,1,200)<=0)continue;ssize_t r=send(fd,p,n,MSG_NOSIGNAL|MSG_DONTWAIT);if(r<0&&(errno==EINTR||errno==EAGAIN))continue;if(r<=0)return -1;p+=r;n-=(size_t)r;}return n?-1:0;}
static void http_reply(int fd,int code,const char*type,const void*body,size_t n){char h[256];int k=snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",code,code==200?"OK":code==503?"Service Unavailable":"Error",type,n);if(k>0&&!send_all(fd,h,(size_t)k))send_all(fd,body,n);}
static void result(int fd,int ok,const char*message){char quoted[512],b[600];json_string(quoted,sizeof(quoted),message);int n=snprintf(b,sizeof(b),"{\"ok\":%s,\"result\":%s}",ok?"true":"false",quoted);http_reply(fd,200,"application/json",b,(size_t)n);}
static int query(const char*q,const char*key,char*out,size_t cap){out[0]=0;size_t k=strlen(key);for(const char*p=q;*p;){const char*end=strchr(p,'&');if(!end)end=p+strlen(p);if((size_t)(end-p)>k&&p[k]=='='&&!memcmp(p,key,k)){size_t n=0;for(const char*s=p+k+1;s<end;s++){unsigned char c=(unsigned char)*s;if(c=='%'){if(s+2>=end||!isxdigit((unsigned char)s[1])||!isxdigit((unsigned char)s[2]))return -1;char hex[3]={s[1],s[2],0};c=(unsigned char)strtoul(hex,NULL,16);s+=2;}else if(c=='+')c=' ';if(!c||n+1>=cap)return -1;out[n++]=(char)c;}out[n]=0;return 1;}p=*end?end+1:end;}return 0;}
static int safe_name(const char*n){size_t s=strlen(n);if(!s||s>20)return 0;for(size_t i=0;i<s;i++)if(!isalnum((unsigned char)n[i])&&n[i]!='_'&&n[i]!='-')return 0;return 1;}
static int preset_directory(int create,int writable){
 char id[80],relative[128];device_camera_id(id,sizeof(id));if(!id[0]){errno=ENODEV;return -1;}
 int n=snprintf(relative,sizeof(relative),"qc5/%s/presets",id);
 if(n<0||(size_t)n>=sizeof(relative))return -1;
 return qc5_sd_directory(sdroot,relative,create,writable);
}
static int slot_read(int directory,const char *name){
 int fd=openat(directory,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
 struct stat st;char bytes[40];
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<11||st.st_size>32){close(fd);return -1;}
 size_t used=0;
 while(used<(size_t)st.st_size){
  ssize_t n=read(fd,bytes+used,(size_t)st.st_size-used);
  if(n<0&&errno==EINTR)continue;
  if(n<=0){close(fd);return -1;}used+=(size_t)n;
 }
 close(fd);bytes[used]=0;
 if(memchr(bytes,0,used)||memcmp(bytes,"QC5_SLOT1\n",10))return -1;
 char *end;long slot=strtol(bytes+10,&end,10);
 if(end==bytes+10||slot<0||slot>15)return -1;
 while(*end&&isspace((unsigned char)*end))end++;
 return *end?-1:(int)slot;
}
static int native_motor(int command,int timeout){char p[64];snprintf(p,sizeof(p),"command=%d&channel=0",command);if(camera_rpc("decoder_control",p,timeout))return -1;int r;if(field_int((char*)response+8,"result",&r)||r!=0)return -1;return 0;}
static void preset_list(int fd,int directory){
 char body[2048]="{\"ok\":true,\"presets\":[";size_t used=strlen(body);int comma=0;
 int copy=dup(directory);DIR *d=copy<0?NULL:fdopendir(copy);
 if(!d&&copy>=0)close(copy);
 if(d){struct dirent *entry;
  while((entry=readdir(d))){
   if(!safe_name(entry->d_name)||slot_read(directory,entry->d_name)<0)continue;
   size_t n=strlen(entry->d_name);if(used+n+5>=sizeof(body))break;
   used+=(size_t)snprintf(body+used,sizeof(body)-used,"%s\"%.20s\"",comma?",":"",entry->d_name);comma=1;
  }
  closedir(d);
 }
 memcpy(body+used,"]}",3);http_reply(fd,200,"application/json",body,used+2);
}
static void presets(int fd,const char *command,const char *name){
 int listing=!strcmp(command,"preset_list"),saving=!strcmp(command,"preset_save");
 if(!listing&&!safe_name(name)){result(fd,0,"Nome preset non valido");return;}
 int directory=preset_directory(saving,saving||!strcmp(command,"preset_del"));
 if(directory<0){
  if(listing&&errno==ENOENT){http_reply(fd,200,"application/json","{\"ok\":true,\"presets\":[]}",24);return;}
  result(fd,0,"SD o cartella preset non disponibile");return;
 }
 if(listing){preset_list(fd,directory);close(directory);return;}
 int slot=slot_read(directory,name),ok=0;const char *message="Preset non disponibile";
 if(!strcmp(command,"preset_del")){
  if(slot<0||get_status(1))goto done;
  if(native_motor(62+slot,2000)){message="Eliminazione nativa non confermata";goto done;}
  if(get_status(1)){message="Stato dopo eliminazione non confermato";goto done;}
  int watch=0;field_int(status_cache,"watch_preset",&watch);
  char params[80];snprintf(params,sizeof(params),"sensorid=254&presetid=%d",watch);
  if(camera_rpc("set_sensor_preset",params,2000)||get_status(1)){message="Persistenza eliminazione non confermata";goto done;}
  int mask=0;
  if(field_int(status_cache,"preset_value",&mask)||(mask&(1<<(15-slot)))){message="Il firmware mantiene ancora il preset";goto done;}
  ok=unlinkat(directory,name,0)==0;message=ok?"Preset eliminato":"Errore scrittura preset";goto done;
 }
 if(!strcmp(command,"preset_go")){
  ok=slot>=0&&native_motor(31+2*slot,2500)==0;
  message=slot<0?"Preset assente o di una versione precedente":"Richiamo preset inviato";goto done;
 }
 if(get_status(1)){message="Camera locale non disponibile";goto done;}
 int mask=0;
 if(field_int(status_cache,"preset_value",&mask)){message="Elenco preset nativo non disponibile";goto done;}
 if(slot<0)for(slot=0;slot<16;slot++)if(!(mask&(1<<(15-slot))))break;
 if(slot>=16){message="Tutti i 16 preset sono occupati";goto done;}
 /* Verify SD creation, write and sync BEFORE consuming a vendor preset slot. */
 char hex[17],temporary[40],content[32];
 if(qc5_random_hex(hex)){message="Creazione preset non disponibile";goto done;}
 snprintf(temporary,sizeof(temporary),".new-%s",hex);
 int file=openat(directory,temporary,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW|O_CLOEXEC,0600);
 if(file<0){message="SD non scrivibile";goto done;}
 int n=snprintf(content,sizeof(content),"QC5_SLOT1\n%d\n",slot);
 ok=write(file,content,(size_t)n)==n&&fsync(file)==0;close(file);
 if(!ok){message="Errore scrittura preset";goto cleanup;}
 ok=0;
 if(native_motor(30+2*slot,2500)||get_status(1)){message="Salvataggio nativo non confermato";goto cleanup;}
 int newmask=0;
 if(field_int(status_cache,"preset_value",&newmask)||!(newmask&(1<<(15-slot)))){message="Il firmware non ha confermato il preset";goto cleanup;}
 ok=renameat(directory,temporary,directory,name)==0;
 message=ok?"Preset salvato sulla SD":"Errore scrittura preset";
 cleanup:if(!ok)unlinkat(directory,temporary,0);
 done:close(directory);result(fd,ok,message);
}
static int station_connected(void);
static int wifi_apply(void){char ssid[33],pwd[65],apply[8],open_net[8],es[100],ep[200],params[400];if(load_config())return -1;if(qc5_config_get(config,strlen(config),"SSID",ssid,sizeof(ssid))<0||qc5_config_get(config,strlen(config),"PWD",pwd,sizeof(pwd))<0)return -1;cfg("WIFI_APPLY",apply,sizeof(apply));cfg("WIFI_OPEN",open_net,sizeof(open_net));if(!ssid[0]||!strcmp(apply,"0"))return 1;size_t n=strlen(pwd);if((n&& (n<8||n>63))||(!n&&strcmp(open_net,"1")))return -1;unsigned char stored[128];
 if(nvs_slice("VEEPAI-WIFI-STA",0,stored,sizeof(stored))==0&&memchr(stored,0,64)&&memchr(stored+64,0,64)&&!strcmp((char*)stored,ssid)&&!strcmp((char*)stored+64,pwd)&&station_connected()){
  memset(stored,0,sizeof(stored));memset(pwd,0,sizeof(pwd));wifi_state="credenziali_corrispondenti";return 2;
 }
 memset(stored,0,sizeof(stored));
 if(url_encode(ssid,es,sizeof(es))||url_encode(pwd,ep,sizeof(ep)))return -1;
 snprintf(params,sizeof(params),"ssid=%s&wpa_psk=%s",es,ep);video_quiet_until=now_ms()+3000;int r=camera_rpc("set_wifi",params,4000),code;if(!r&&(field_int((char*)response+8,"result",&code)||code))r=-1;memset(pwd,0,sizeof(pwd));memset(ep,0,sizeof(ep));memset(params,0,sizeof(params));wifi_state=r?"richiesta_non_accettata":"credenziali_applicate";return r;}
static int station_connected(void){
#ifdef QC5_HOST_TEST
 const char*path=getenv("QC5_TEST_STATION_STATE");if(!path)return 0;
 FILE*f=fopen(path,"rb");if(!f)return 0;int c=fgetc(f);fclose(f);return c=='1';
#elif defined(__linux__)
 return qc5_station_online();
#else
 return 0;
#endif
}
/* Use the observed stock local-setup command once, only after STA/DHCP.
 * Foreign ownership in either saved bank is preserved. A saved local owner is
 * reaffirmed once per boot to restore the native volatile completion flag. */
static int complete_local_setup(void){
 char enabled[8];cfg("LOCAL_SETUP",enabled,sizeof(enabled));
 if(strcmp(enabled,"1")){setup_state="disabilitato";return 1;}
 if(!station_connected()){setup_state="attesa_wifi";return 0;}
 if(get_status(0)){setup_state="attesa_servizio";return 0;}
 int empty=local_setup_allowed();
 if(empty<0){setup_state="configurazione_non_leggibile";return -1;}
 if(!empty){setup_state="configurazione_preservata";return 1;}
 int code=-1,command=-1,kind=-1;
 if(camera_rpc("trans_cmd_string","cmd=9000&command=1&userid=local",4000)||
    field_int((char*)response+8,"result",&code)||code||
    field_int((char*)response+8,"cmd",&kind)||kind!=9000||
    field_int((char*)response+8,"command",&command)||command!=1){
  setup_state="esito_da_verificare";return -1;
 }
 setup_state="completata_locale";return 1;
}
#include "qc5_provision.h"
#include "qc5_captive.h"
static void system_info(int fd){char name[128],id[80],qn[800],qi[500],urls[3][512],qu[3][3100],b[13000],ip[64]="0.0.0.0",date[80];cfg("CAM_NAME",name,sizeof(name));if(!name[0])strcpy(name,"VeePai QC5");int ready=logical_camera_id(id,sizeof(id));json_string(qn,sizeof(qn),name);json_string(qi,sizeof(qi),id);media_url("RESTREAMER_URL",id,urls[0],sizeof(urls[0]));media_url("REC_API",id,urls[1],sizeof(urls[1]));urls[2][0]=0;int s=socket(AF_INET,SOCK_DGRAM,0);if(s>=0){struct ifreq req;memset(&req,0,sizeof(req));strcpy(req.ifr_name,"wlan0");if(ioctl(s,SIOCGIFADDR,&req)==0)inet_ntop(AF_INET,&((struct sockaddr_in*)&req.ifr_addr)->sin_addr,ip,sizeof(ip));close(s);}if(rtsp_listener>=0)snprintf(urls[2],sizeof(urls[2]),"rtsp://%s:%d/live",ip,rtsp_port);for(int i=0;i<3;i++)json_string(qu[i],sizeof(qu[i]),urls[i]);long total=0,free_kb=0,cached=0;FILE*f=fopen("/proc/meminfo","r");if(f){char line[128];long value;while(fgets(line,sizeof(line),f)){char*colon=strchr(line,':');if(!colon)continue;value=strtol(colon+1,NULL,10);colon[1]=0;const char*key=line;if(!strcmp(key,"MemTotal:"))total=value;else if(!strcmp(key,"MemFree:"))free_kb=value;else if(!strcmp(key,"Cached:"))cached=value;}fclose(f);}int motor=0;if(ready)field_int(status_cache,"haveMotor",&motor);time_t t=time(NULL);struct tm tm;gmtime_r(&t,&tm);strftime(date,sizeof(date),"%Y-%m-%d %H:%M:%S UTC",&tm);char marker[640];snprintf(marker,sizeof(marker),"%s", "/tmp/qc5-lan-only/active");int n=snprintf(b,sizeof(b),"{\"ok\":true,\"ip\":\"%s\",\"uptime\":\"%lld s\",\"date\":\"%s\",\"hack\":\"QC5 locale " QC5_VERSION " / PPPP\",\"mem_total_kb\":%ld,\"mem_free_kb\":%ld,\"mem_cached_kb\":%ld,\"mem_reclaimable_estimate_kb\":%ld,\"lan_only\":%s,\"CAM_NAME\":%s,\"CAM_ID\":%s,\"RESTREAMER_URL\":%s,\"REC_API\":%s,\"RTSP_URL\":%s,\"recording\":{\"available\":true},\"intercom\":{\"available\":true,\"sample_rate\":16000,\"https_required\":true,\"tls_termination\":\"reverse_proxy\"},\"adapter\":{\"ok\":%s,\"bridge\":%s,\"snapshot\":true,\"video\":true,\"ptz_ready\":%s,\"wifi\":\"%s\",\"local_setup\":\"%s\"}}",ip,now_ms()/1000,date,total,free_kb,cached,qc5_memory_available_kb(),access(marker,F_OK)==0?"true":"false",qn,qi,qu[0],qu[1],qu[2],ready?"true":"false",ready?"true":"false",ready&&motor?"true":"false",wifi_state,setup_state);if(n<0||(size_t)n>=sizeof(b)){result(fd,0,"Configurazione troppo lunga");return;}http_reply(fd,200,"application/json",b,(size_t)n);}
static void api(int fd,const char*q){char cmd[64],name[64];if(query(q,"command",cmd,sizeof(cmd))<=0){result(fd,0,"Comando mancante o non valido");return;}if(!strcmp(cmd,"sysinfo")){system_info(fd);return;}if(!strncmp(cmd,"preset_",7)){if(strcmp(cmd,"preset_list")&&strcmp(cmd,"preset_save")&&strcmp(cmd,"preset_go")&&strcmp(cmd,"preset_del")){result(fd,0,"Comando non supportato");return;}if(query(q,"preset_name",name,sizeof(name))<0){result(fd,0,"Nome preset non valido");return;}presets(fd,cmd,name);return;}int moves[2]={-1,-1};
 if(!strcmp(cmd,"ptzu"))moves[0]=0;else if(!strcmp(cmd,"ptzd"))moves[0]=2;
 else if(!strcmp(cmd,"ptzl"))moves[0]=4;else if(!strcmp(cmd,"ptzr"))moves[0]=6;
 else if(!strcmp(cmd,"ptzstop"))moves[0]=1;
 else if(!strcmp(cmd,"ptzlu")){moves[0]=4;moves[1]=0;}
 else if(!strcmp(cmd,"ptzru")){moves[0]=6;moves[1]=0;}
 else if(!strcmp(cmd,"ptzld")){moves[0]=4;moves[1]=2;}
 else if(!strcmp(cmd,"ptzrd")){moves[0]=6;moves[1]=2;}
 if(moves[0]>=0){
  int ok=1;
  for(int i=0;i<2&&moves[i]>=0&&ok;i++){
   int motor=moves[i];long long start=now_ms();ok=native_motor(motor,motor==1?3000:250)==0;
   if(motor!=1){long long left=250-(now_ms()-start);if(left>0)usleep((useconds_t)left*1000);int stopped=native_motor(1,3000);int confirmed=ok||(move_ack_command==motor&&move_ack_time>=start);ok=confirmed&&stopped==0;}
  }
  result(fd,ok,ok?"Movimento locale completato":"Comando o arresto non confermato");return;
 }
 if(!strcmp(cmd,"wlon")||!strcmp(cmd,"Won")||!strcmp(cmd,"wlof")||!strcmp(cmd,"Woff")){
 int on=!strcmp(cmd,"wlon")||!strcmp(cmd,"Won");char p[80];snprintf(p,sizeof(p),"light=%d&whiteled=%d&mode=%d",on,on?100:0,on);
 video_quiet_until=now_ms()+2000;int ok=camera_rpc("set_whiteled_value",p,2000)==0;
 if(ok){ok=0;for(int attempt=0;attempt<4;attempt++){
 if(camera_rpc("get_whiteled_value","",2000)==0){int actual=-1;if(field_int((char*)response+8,"light",&actual)==0&&actual==on){ok=1;break;}}
 usleep(150000);
}}
 result(fd,ok,ok?"Luce aggiornata":"Stato luce non confermato");return;
}
if(!strcmp(cmd,"ircutday")||!strcmp(cmd,"irctrlday")||!strcmp(cmd,"ircutnig")||!strcmp(cmd,"irctrlnig")||!strcmp(cmd,"irauto")){
 /* ircut_forceon in get_status is a capability; get_camera_params.ircut is state. */
 int mode=!strcmp(cmd,"irauto")?1:(strstr(cmd,"day")?0:2);int native_value=mode;char p[80];snprintf(p,sizeof(p),"param=14&value=%d&video=1",native_value);
 video_quiet_until=now_ms()+3000;int code=-1;
 int ok=camera_rpc("camera_control",p,2000)==0&&field_int((char*)response+8,"result",&code)==0&&code==0;
 if(ok){ok=0;for(int attempt=0;attempt<8;attempt++){
  int actual=-1;if(camera_rpc("get_camera_params","",2000)==0&&field_int((char*)response+8,"ircut",&actual)==0&&actual==mode){ok=1;break;}
  usleep(200000);
 }}
 fprintf(stderr,"QC5_IR_MODE target=%d native_value=%d ack_result=%d confirmed=%d\n",mode,native_value,code,ok);
 result(fd,ok,ok?"Modalita infrarossi aggiornata":"Modalita non confermata");return;
}
if(!strcmp(cmd,"wifi_apply")){int r=wifi_apply();result(fd,r>=0,r==2?"Credenziali SD gia presenti nella camera":r==0?"Credenziali SD inviate al Wi-Fi":r==1?"SSID vuoto: mantengo il Wi-Fi originale":"Configurazione o collegamento locale non valido");return;}result(fd,0,"Comando non ancora collegato al firmware locale");}
#include "qc5_talk.h"
#include "qc5_record.h"
#include "qc5_motion.h"
#include "qc5_record_policy.h"
#include "qc5_rtsp.h"
static void static_file(int fd,const char*name,const char*type){char path[700],buf[4096],h[300];snprintf(path,sizeof(path),"%s/%s",webroot,name);int f=open(path,O_RDONLY|O_CLOEXEC);struct stat s;if(f<0||fstat(f,&s)||!S_ISREG(s.st_mode)||s.st_size>512*1024){if(f>=0)close(f);http_reply(fd,404,"text/plain","Non trovato",11);return;}int n=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",type,(long long)s.st_size);if(send_all(fd,h,(size_t)n)==0){ssize_t r;while((r=read(f,buf,sizeof(buf)))>0)if(send_all(fd,buf,(size_t)r))break;}close(f);}
static void snapshot_reply(int fd){
 long long now=now_ms();
 /* One native capture per two seconds across all browser/test clients.
  * Return the last frame briefly while the ISP switches day/night mode. */
 if((!jpeg_len||now-jpeg_time>=2000)&&now>=video_quiet_until&&now>=jpeg_retry_at){
  if(camera_rpc("snapshot","sensor=0",4000)==0&&response_len>=12&&response[8]==255&&response[9]==216&&response[response_len-2]==255&&response[response_len-1]==217){
   jpeg_len=response_len-8;memcpy(jpeg_cache,response+8,jpeg_len);jpeg_time=now_ms();
  }else{jpeg_retry_at=now_ms()+2500;fprintf(stderr,"QC5_SNAPSHOT_UNAVAILABLE uptime_ms=%lld\n",now_ms());}
 }
 if(jpeg_len&&now_ms()-jpeg_time<10000)http_reply(fd,200,"image/jpeg",jpeg_cache,jpeg_len);
 else http_reply(fd,503,"text/plain","Immagine locale non disponibile",30);
}
static void http_client(int fd,int captive){
 char request[4096];size_t used=0;int complete=0;long long end=now_ms()+1500;
 while(used+1<sizeof(request)&&now_ms()<end){
  struct pollfd p={fd,POLLIN,0};if(poll(&p,1,100)<=0)continue;
  ssize_t n=recv(fd,request+used,sizeof(request)-used-1,MSG_DONTWAIT);
  if(n<0&&(errno==EINTR||errno==EAGAIN))continue;
  if(n<=0)break;
  used+=(size_t)n;request[used]=0;
  if(strstr(request,"\r\n\r\n")){complete=1;break;}
 }
 request[used]=0;
 if(!complete||memchr(request,0,used)){http_reply(fd,400,"text/plain","Richiesta non valida",20);return;}
 int post=!strncmp(request,"POST ",5);
 if(!post&&strncmp(request,"GET ",4)){http_reply(fd,405,"text/plain","Metodo non ammesso",18);return;}
 char *url=request+(post?5:4),*space=strchr(url,' ');
 if(!space||(strncmp(space," HTTP/1.0\r\n",11)&&strncmp(space," HTTP/1.1\r\n",11))){http_reply(fd,400,"text/plain","Richiesta non valida",20);return;}
 char *headers=strstr(space,"\r\n")+2;
 *space=0;char *q=strchr(url,'?');if(q)*q++=0;else q=(char*)"";
 if(post){
  char*body=strstr(headers,"\r\n\r\n");
  int recording=!strncmp(url,"/api/rec/",9)&&(!strcmp(url+9,"start")||!strcmp(url+9,"stop")||!strcmp(url+9,"stop-now")||!strcmp(url+9,"delete"));
  int motion=!strcmp(url,"/api/motion/config");
  if((strcmp(url,"/setup/wifi")&&!recording&&!motion)||*q||(captive&&(recording||motion))){http_reply(fd,404,"text/plain","Non trovato",11);return;}
  if(!body||body+4-request>2048){http_reply(fd,400,"text/plain","Richiesta non valida",20);return;}body+=4;
  if(recording)record_post(fd,url+9,headers,body,used-(size_t)(body-request),end);
  else if(motion)motion_post(fd,headers,body,used-(size_t)(body-request),end);
  else provision_post(fd,headers,body,used-(size_t)(body-request),end);return;
 }
 if(!strcmp(url,"/setup")||!strcmp(url,"/setup/")){static_file(fd,"setup.html","text/html; charset=utf-8");return;}
 if(!strcmp(url,"/setup.js")){static_file(fd,"setup.js","application/javascript");return;}
 if(!strcmp(url,"/setup/status")){provision_status(fd);return;}
 if(!strcmp(url,"/setup/networks")){provision_networks(fd);return;}
 if(captive&&strcmp(url,"/")&&strcmp(url,"/setup")&&strcmp(url,"/setup/")&&strcmp(url,"/setup.js")){captive_redirect(fd);return;}
 if(!strcmp(url,"/")||!strcmp(url,"/index.html")){static_file(fd,provisioning&&provision_ap()?"setup.html":"index.html","text/html; charset=utf-8");return;}
 if(!strcmp(url,"/media")){video_upgrade(fd,headers);return;}
 if(!strcmp(url,"/listen")){listen_upgrade(fd,headers);return;}
 if(!strcmp(url,"/intercom")){talk_upgrade(fd,headers);return;}
 if(!strcmp(url,"/listen.js")){static_file(fd,"listen.js","application/javascript");return;}
 if(!strcmp(url,"/talk.js")){static_file(fd,"talk.js","application/javascript");return;}
 if(!strcmp(url,"/talk-worklet.js")){static_file(fd,"talk-worklet.js","application/javascript");return;}
 if(!strcmp(url,"/media.js")){static_file(fd,"media.js","text/javascript; charset=utf-8");return;}
 if(!strcmp(url,"/live.html")){static_file(fd,"live.html","text/html; charset=utf-8");return;}
 if(!strcmp(url,"/record.js")){static_file(fd,"record.js","application/javascript");return;}
 if(!strcmp(url,"/motion.js")){static_file(fd,"motion.js","application/javascript");return;}
 if(!strcmp(url,"/api/motion/status")){motion_status(fd);return;}
 if(!strncmp(url,"/api/motion/",12)){http_reply(fd,405,"text/plain","Usare POST",10);return;}
 if(!strcmp(url,"/api/rec/status")){record_status(fd);return;}
 if(!strcmp(url,"/api/rec/list")){record_list(fd);return;}
 if(!strcmp(url,"/api/rec/ready")){record_reader_ready(fd);return;}
 if(!strcmp(url,"/api/rec/download")){record_download(fd,q,headers);return;}
 if(!strncmp(url,"/api/rec/",9)){http_reply(fd,405,"text/plain","Usare POST",10);return;}
 if(!strcmp(url,"/cgi-bin/webui")){api(fd,q);return;}
 if(!strcmp(url,"/cgi-bin/snapshot")){snapshot_reply(fd);return;}
 http_reply(fd,404,"text/plain","Non trovato",11);
}
static void stop_signal(int s){(void)s;stopping=1;}
static int copy_arg(char*out,size_t n,const char*arg){if(strlen(arg)>=n)return -1;strcpy(out,arg);return 0;}
int main(int argc,char**argv){if(argc==2&&!strcmp(argv[1],"--version")){puts(QC5_GATEWAY_MARKER);return 0;}for(int inherited=3;inherited<4096;inherited++)close(inherited);int probe=0,stdin_credential=0,apply_wifi_on_start=0,finish_setup_on_start=0;char bind_ip[64]="0.0.0.0";umask(0077);signal(SIGPIPE,SIG_IGN);signal(SIGCHLD,SIG_DFL);signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--probe"))probe=1;else if(!strcmp(argv[i],"--provision"))provisioning=1;else if(!strcmp(argv[i],"--record-exports"))record_exports=1;else if(!strcmp(argv[i],"--apply-wifi"))apply_wifi_on_start=1;else if(!strcmp(argv[i],"--complete-local-setup"))finish_setup_on_start=1;else if(!strcmp(argv[i],"--credential-stdin"))stdin_credential=1;else if(i+1<argc){const char*k=argv[i++],*v=argv[i];int r=0;if(!strcmp(k,"--host"))r=copy_arg(host,sizeof(host),v);else if(!strcmp(k,"--expected-uid"))r=expected_uid(v);else if(!strcmp(k,"--webroot"))r=copy_arg(webroot,sizeof(webroot),v);else if(!strcmp(k,"--config"))r=copy_arg(config_path,sizeof(config_path),v);else if(!strcmp(k,"--sd"))r=copy_arg(sdroot,sizeof(sdroot),v);else if(!strcmp(k,"--run-dir"))r=copy_arg(provision_run,sizeof(provision_run),v);else if(!strcmp(k,"--record-worker-bin"))r=copy_arg(record_export_bin,sizeof(record_export_bin),v);else if(!strcmp(k,"--record-store-bin"))r=copy_arg(record_store_bin,sizeof(record_store_bin),v);else if(!strcmp(k,"--nvs"))r=copy_arg(nvs_path,sizeof(nvs_path),v);else if(!strcmp(k,"--bind"))r=copy_arg(bind_ip,sizeof(bind_ip),v);else if(!strcmp(k,"--rtsp-port")){char*e;long p=strtol(v,&e,10);if(*e||p<1||p>65535)return 2;rtsp_port=(int)p;}else if(!strcmp(k,"--port")){char*e;long p=strtol(v,&e,10);if(*e||p<1||p>65535)return 2;web_port=(int)p;}else return 2;if(r)return 2;}else return 2;}
 if(stdin_credential){if(!fgets(username,sizeof(username),stdin)||!fgets(password,sizeof(password),stdin))return 3;username[strcspn(username,"\r\n")]=0;password[strcspn(password,"\r\n")]=0;}else if(read_credentials()){fprintf(stderr,"QC5: credenziale locale non leggibile\n");return 3;}if(load_config())return 4;


 if(qc5_random_hex(record_token)||qc5_random_hex(record_token+16))return 4;
 if(provisioning&&(qc5_random_hex(provision_token)||qc5_random_hex(provision_token+16)))return 4;
 if(probe){int s=get_status(1),r=-1;size_t bytes=0;if(s==0){r=camera_rpc("snapshot","sensor=0",4000);if(r==0&&response_len>8&&response[8]==255&&response[9]==216)bytes=response_len-8;}printf("{\"authenticated\":%s,\"jpeg_bytes\":%zu,\"vendor_restart\":false,\"flash_written\":false}\n",s==0?"true":"false",bytes);disconnect_camera();return s||r||!bytes?5:0;}
 int server=socket(AF_INET,SOCK_STREAM,0);if(server<0)return 6;int one=1;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons((uint16_t)web_port);if(fcntl(server,F_SETFL,O_NONBLOCK)||inet_pton(AF_INET,bind_ip,&a.sin_addr)!=1||bind(server,(struct sockaddr*)&a,sizeof(a))||listen(server,4))return 6;fprintf(stderr,"QC5_GATEWAY_READY port=%d\n",web_port);
 captive_main_listener(bind_ip,(unsigned)web_port);
 if(rtsp_open(bind_ip))fprintf(stderr,"QC5_RTSP_START_FAILED\n");
 long long wifi_retry_at=0,setup_retry_at=0;int wifi_attempts=0,setup_attempts=0;
 while(!stopping){
 release_idle_buffers(0);talk_reap();reap_one(&rtsp_pid);reap_one(&record_download_pid);record_export_tick();record_policy_tick();camera_keepalive();provision_tick();captive_update(provision_ready()&&provision_ap());
 if(apply_wifi_on_start&&now_ms()>=wifi_retry_at){
  if(get_status(0)==0){int r=wifi_apply();if(r>=0)apply_wifi_on_start=0;}
  wifi_retry_at=now_ms()+3000;if(++wifi_attempts>=40)apply_wifi_on_start=0;
 }
 if(finish_setup_on_start&&now_ms()>=setup_retry_at){
  int r=complete_local_setup();if(r)finish_setup_on_start=0;
  setup_retry_at=now_ms()+3000;
  if(++setup_attempts>=40){finish_setup_on_start=0;if(!r)setup_state="attesa_scaduta";}
 }
 struct pollfd p[5]={{server,POLLIN,0},{udp,POLLIN,0},{captive_http,POLLIN,0},{captive_dns,POLLIN,0},{rtsp_listener,POLLIN,0}};int n=poll(p,5,200);if(n<=0)continue;if(rtsp_listener>=0&&(p[4].revents&POLLIN))rtsp_accept();if(captive_dns>=0&&(p[3].revents&POLLIN))captive_dns_client();if(captive_http>=0&&(p[2].revents&POLLIN)){int fd=accept(captive_http,NULL,NULL);if(fd>=0){http_client(fd,1);close(fd);}}if(udp>=0&&p[1].revents){struct sockaddr_in src;int bytes=receive_packet(0,&src);if(control_packet(bytes,&src)<0||complete_frame(0)<0)disconnect_camera();}if(p[0].revents&POLLIN){int fd=accept(server,NULL,NULL);if(fd>=0){http_client(fd,captive_main_client(fd));close(fd);}}}if(rtsp_listener>=0)close(rtsp_listener);stop_one(&rtsp_pid);captive_close();talk_shutdown();stop_one(&record_download_pid);stop_one(&record_export_pid);disconnect_camera();close(server);memset(password,0,sizeof(password));return 0;}

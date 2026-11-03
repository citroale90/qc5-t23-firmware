/* Included by the gateway: bounded POST, no shell, no second daemon. */
#include "qc5_wifi_store.h"
static int provisioning;
static char provision_run[512]="/tmp/qc5-hack",provision_token[33];
static struct qc5_wifi_profile provision_pending;
static int provision_queued,provision_have_saved,provision_have_setup;
static uint64_t provision_saved_tag,provision_setup_tag;
static long long provision_next,provision_submit_at;
static const char*provision_state="attesa_configurazione";
static int provision_marker(const char*name,char*out,size_t cap){
 char path[600];if(snprintf(path,sizeof(path),"%s/%s",provision_run,name)>=(int)sizeof(path))return -1;
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
 struct stat st;int good=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&(size_t)st.st_size<cap;
 long n=good?qc5_config_read_fd(fd,out,cap):-1;close(fd);
 if(n<0||(size_t)n>=cap)return -1;out[n]=0;return (int)n;
}
static int provision_ready(void){char b[32],safe[600];snprintf(safe,sizeof(safe),"%s/QC5-SAFE-MODE",sdroot);return provisioning&&access(safe,F_OK)&&provision_marker("wifi-controller",b,sizeof(b))==22&&!strcmp(b,"QC5_WIFI_CONTROLLER_1\n");}
static int provision_station_matches(const struct qc5_wifi_profile*p){
 if(!station_connected())return 0;
#ifdef QC5_HOST_TEST
 const char*name=getenv("QC5_TEST_STATION_SSID");return name&&!strcmp(name,p->ssid);
#elif defined(__linux__)
 char name[33];return qc5_station_ssid(name)&&!strcmp(name,p->ssid);
#else
 return 0;
#endif
}
static int provision_ap(void){
#ifdef QC5_HOST_TEST
 const char*value=getenv("QC5_TEST_AP_MODE");return value&&!strcmp(value,"1");
#elif defined(__linux__)
 if(qc5_station_is_ap())return 1;
 char phase[32];if(provision_marker("wifi-phase",phase,sizeof(phase))!=8||strcmp(phase,"hotspot\n"))return 0;
 struct qc5_station_request request={0};memcpy(request.name,"wlan0",6);int socket_fd=qc5_station_socket();if(socket_fd<0)return 0;
 int ap=0;if(!ioctl(socket_fd,0x8915UL,&request)&&request.value.address.family==2){const unsigned char*ip=request.value.address.data+2;ap=ip[0]==192&&ip[1]==168&&ip[2]==168&&ip[3]==1;}close(socket_fd);return ap;
#else
 return 0;
#endif
}
static int provision_send(const struct qc5_wifi_profile*p){
 char data[256],nonce[17],name[64];int n=snprintf(data,sizeof(data),"SSID=\"%s\"\nPWD=\"%s\"\nWIFI_OPEN=%u\n",p->ssid,p->password,p->open),result=-1;
 if(n<=0||(size_t)n>=sizeof(data)||qc5_random_hex(nonce))goto done;
 int dir=open(provision_run,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)goto done;
 snprintf(name,sizeof(name),".wifi-request-%s",nonce);
 int fd=openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd<0){close(dir);goto done;}
 size_t at=0;while(at<(size_t)n){ssize_t k=write(fd,data+at,(size_t)n-at);if(k<0&&errno==EINTR)continue;if(k<=0)break;at+=(size_t)k;}
 int failed=at!=(size_t)n||fsync(fd);if(close(fd))failed=1;
 /* linkat is atomic and refuses to overwrite an unconsumed request. */
 if(!failed&&!linkat(dir,name,dir,"wifi-request",0))result=0;
 unlinkat(dir,name,0);close(dir);
 done:memset(data,0,sizeof(data));return result;
}
static void provision_tick(void){
 if(!provision_ready())return;
 long long now=now_ms();
 if(provision_queued&&now>=provision_submit_at){
  provision_state=provision_send(&provision_pending)?"richiesta_non_accettata":"connessione_in_corso";
  memset(&provision_pending,0,sizeof(provision_pending));provision_queued=0;
 }
 char waiting[600];snprintf(waiting,sizeof(waiting),"%s/wifi-request",provision_run);if(!access(waiting,F_OK))return;
 if(now<provision_next)return;provision_next=now+3000;
 char marker[32],*end;int n=provision_marker("wifi-committed",marker,sizeof(marker));
 if(n!=17||marker[16]!='\n')return;marker[16]=0;
 for(int i=0;i<16;i++)if(!isxdigit((unsigned char)marker[i]))return;
 uint64_t tag=strtoull(marker,&end,16);if(*end)return;
 struct qc5_wifi_profile p={0};int found=0,durable=0;
 /* Association and flash durability are separate. The native setter returns
  * before its NVS worker commits; a stalled worker must not prevent SD backup. */
 char profile_path[600];snprintf(profile_path,sizeof(profile_path),"%s/wifi-profile",provision_run);
 int profile_fd=open(profile_path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
 if(profile_fd>=0){
  struct stat info;unsigned char record[128];
  if(!fstat(profile_fd,&info)&&S_ISREG(info.st_mode)&&info.st_size==128&&pread(profile_fd,record,sizeof(record),0)==(ssize_t)sizeof(record)&&memchr(record,0,33)&&memchr(record+64,0,64)){
   memcpy(p.ssid,record,33);memcpy(p.password,record+64,64);p.open=!p.password[0];
   found=qc5_wifi_valid(&p)&&qc5_wifi_tag(&p)==tag&&provision_station_matches(&p);
  }
  memset(record,0,sizeof(record));close(profile_fd);
 }
 for(int bank=0;bank<2&&!durable;bank++){
  unsigned char record[128];if(nvs_slice_bank("VEEPAI-WIFI-STA",0,record,sizeof(record),bank))continue;
  if(memchr(record,0,33)&&memchr(record+64,0,64)){
   struct qc5_wifi_profile saved={0};memcpy(saved.ssid,record,33);memcpy(saved.password,record+64,64);saved.open=!saved.password[0];
   durable=qc5_wifi_valid(&saved)&&qc5_wifi_tag(&saved)==tag&&provision_station_matches(&saved);
   if(durable&&!found){p=saved;found=1;}memset(&saved,0,sizeof(saved));
  }
  memset(record,0,sizeof(record));
 }
 if(!found){memset(&p,0,sizeof(p));return;}
 if(!provision_have_saved||tag!=provision_saved_tag){
  if(qc5_wifi_store(sdroot,config,strlen(config),&p)){
   provision_state=durable?"wifi_salvato_in_camera_sd_non_disponibile":"wifi_connesso_salvataggio_non_confermato";provision_next=now+30000;
  }else{
   provision_have_saved=1;provision_saved_tag=tag;provision_state=durable?"wifi_salvato_camera_e_sd":"wifi_salvato_sd_native_in_attesa";
   char path[600];snprintf(path,sizeof(path),"%s/config.txt",sdroot);
   if(strlen(path)<sizeof(config_path)){strcpy(config_path,path);load_config();}
  }
 }
 else provision_state=durable?"wifi_salvato_camera_e_sd":"wifi_salvato_sd_native_in_attesa";
 if(!provision_have_setup||tag!=provision_setup_tag){
  if(complete_local_setup()>0){provision_have_setup=1;provision_setup_tag=tag;}
 }
 memset(&p,0,sizeof(p));
}
static void provision_status(int fd){
 char phase[32];if(!provision_queued&&provision_marker("wifi-phase",phase,sizeof(phase))==8&&!strcmp(phase,"hotspot\n"))provision_state="configurazione_hotspot";
 char b[300];int n=snprintf(b,sizeof(b),"{\"enabled\":%s,\"hotspot\":%s,\"token\":\"%s\",\"state\":\"%s\"}",provision_ready()?"true":"false",provision_ap()?"true":"false",provision_token,provision_state);
 http_reply(fd,200,"application/json",b,(size_t)n);
}
static void provision_networks(int fd){
 char data[12288];int n=provision_marker("wifi-networks.json",data,sizeof(data));
 if(n<0){static const char empty[]="{\"cached\":true,\"networks\":[]}";http_reply(fd,200,"application/json",empty,sizeof(empty)-1);return;}
 http_reply(fd,200,"application/json",data,(size_t)n);
}
/* Strict field count prevents duplicate-name ambiguity in credential input. */
static int provision_form(const char*body,struct qc5_wifi_profile*p,char token[33]){
 unsigned seen=0;const char*at=body;memset(p,0,sizeof(*p));
 while(*at){const char*end=strchr(at,'&');if(!end)end=at+strlen(at);const char*eq=memchr(at,'=',(size_t)(end-at));if(!eq)return -1;
  size_t n=(size_t)(eq-at);unsigned bit=(n==4&&!memcmp(at,"ssid",4))||(n==8&&!memcmp(at,"ssid_hex",8))?1:n==3&&!memcmp(at,"pwd",3)?2:n==4&&!memcmp(at,"open",4)?4:n==5&&!memcmp(at,"token",5)?8:0;
  if(!bit||(seen&bit))return -1;seen|=bit;at=*end?end+1:end;
 }
 char open_net[4],hex[65];int encoded=query(body,"ssid_hex",hex,sizeof(hex));if(encoded<0)return -1;
 if(encoded){size_t n=strlen(hex);if(!n||(n&1))return -1;for(size_t i=0;i<n;i+=2){if(!isxdigit((unsigned char)hex[i])||!isxdigit((unsigned char)hex[i+1]))return -1;char pair[3]={hex[i],hex[i+1],0};p->ssid[i/2]=(char)strtoul(pair,NULL,16);if(!p->ssid[i/2])return -1;}}
 else if(query(body,"ssid",p->ssid,sizeof(p->ssid))!=1)return -1;
 if(seen!=15||query(body,"pwd",p->password,sizeof(p->password))!=1||query(body,"open",open_net,sizeof(open_net))!=1||query(body,"token",token,33)!=1)return -1;
 if(strcmp(open_net,"0")&&strcmp(open_net,"1"))return -1;p->open=!strcmp(open_net,"1");return qc5_wifi_valid(p)?0:-1;
}
static void provision_post(int fd,const char*headers,char*body,size_t available,long long deadline){
 size_t length=0;int have_length=0,have_type=0;
 for(const char*p=headers;*p&&strncmp(p,"\r\n",2);){
  const char*end=strstr(p,"\r\n");if(!end){http_reply(fd,400,"text/plain","Richiesta non valida",20);return;}
  if(!strncasecmp(p,"Transfer-Encoding:",18)){http_reply(fd,400,"text/plain","Richiesta non valida",20);return;}
  if(!strncasecmp(p,"Content-Length:",15)){
   const char*v=p+15;while(v<end&&(*v==' '||*v=='\t'))v++;
   if(have_length++||v==end){http_reply(fd,400,"text/plain","Richiesta non valida",20);return;}
   for(;v<end;v++){if(*v<'0'||*v>'9'||length>512){http_reply(fd,400,"text/plain","Richiesta non valida",20);return;}length=length*10+(unsigned)(*v-'0');}
  }
  if(!strncasecmp(p,"Content-Type:",13)){
   const char*v=p+13;while(v<end&&(*v==' '||*v=='\t'))v++;
   if(have_type++||(size_t)(end-v)!=33||strncasecmp(v,"application/x-www-form-urlencoded",33)){http_reply(fd,415,"text/plain","Formato non ammesso",19);return;}
  }
  p=end+2;
 }
 if(!have_length||!have_type||!length||length>512||available>length){http_reply(fd,400,"text/plain","Richiesta non valida",20);return;}
 while(available<length&&now_ms()<deadline){
  struct pollfd p={fd,POLLIN,0};if(poll(&p,1,100)<=0)continue;
  ssize_t n=recv(fd,body+available,length-available,MSG_DONTWAIT);
  if(n<0&&(errno==EINTR||errno==EAGAIN))continue;if(n<=0)break;available+=(size_t)n;
 }
 if(available!=length||memchr(body,0,length)){http_reply(fd,400,"text/plain","Richiesta non valida",20);return;}body[length]=0;
 struct qc5_wifi_profile p;char token[33];int valid=provision_form(body,&p,token)==0;
 if(!valid){result(fd,0,"SSID o password non validi");}
 else if(!provision_ready()){http_reply(fd,503,"text/plain","Configurazione Wi-Fi non attiva",30);}
 else if(strcmp(token,provision_token)){http_reply(fd,403,"text/plain","Riapri la pagina di configurazione",33);}
 else if(provision_queued){http_reply(fd,409,"text/plain","Tentativo gia in corso",22);}
 else{
  provision_pending=p;provision_queued=1;provision_submit_at=now_ms()+1000;provision_state="richiesta_ricevuta";
  result(fd,1,"Connessione in avvio. Se l'hotspot scompare, torna sulla rete Wi-Fi scelta. Con una password errata l'hotspot ritorna.");
 }
 memset(body,0,length);memset(&p,0,sizeof(p));memset(token,0,sizeof(token));
}

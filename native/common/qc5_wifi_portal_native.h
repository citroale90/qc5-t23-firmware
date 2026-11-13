/* Private on-camera mailbox and public scan metadata. No PPPP/cloud dependency. */
static int take_wifi_request(struct qc5_wifi_profile*p){
 char path[600],data[512],flag[8];const char*run=runtime_path();
 if(snprintf(path,sizeof(path),"%s/wifi-request",run)>=(int)sizeof(path))return 0;
#ifdef QC5_WIFI_HARNESS
 int fd=qc5_test_request_open(path);
#else
 int fd=open(path,0x20000|0x80); /* MIPS NOFOLLOW|NONBLOCK, read only. */
#endif
 if(fd<0)return 0;
 long n=qc5_config_read_fd(fd,data,sizeof(data));close(fd);int good=0;
 memset(p,0,sizeof(*p));
 if(n>0&&(usize)n<sizeof(data)&&qc5_config_get(data,(usize)n,"SSID",p->ssid,sizeof(p->ssid))==1&&
    qc5_config_get(data,(usize)n,"PWD",p->password,sizeof(p->password))==1&&
    qc5_config_get(data,(usize)n,"WIFI_OPEN",flag,sizeof(flag))==1&&(!strcmp(flag,"0")||!strcmp(flag,"1"))){
  p->open=!strcmp(flag,"1");good=qc5_wifi_valid(p);
 }
 if(good){char committed[600];if(snprintf(committed,sizeof(committed),"%s/wifi-committed",run)<(int)sizeof(committed))unlink(committed);}
 unlink(path);memset(data,0,sizeof(data));return good?1:-1;
}
#if !defined(QC5_WIFI_HARNESS)||defined(QC5_WIFI_SCAN_HARNESS)
static void publish_wifi_networks(uint64_t*previous){
#ifdef QC5_WIFI_SCAN_HARNESS
 unsigned count=qc5_test_scan_count();
#else
 unsigned count=*(volatile unsigned*)0x0089f34cu;
#endif
 if(count>64)return;
 char out[12288];usize used=0;int included=0;uint64_t hash=UINT64_C(14695981039346656037);
 int n=snprintf(out,sizeof(out),"{\"cached\":true,\"networks\":[");if(n<0)return;used=(usize)n;
 for(unsigned i=0;i<count;i++){
  unsigned char entry[88];
#ifdef QC5_WIFI_SCAN_HARNESS
  qc5_test_scan_entry(i,entry);
#else
  memcpy(entry,(const void*)(usize)(0x0089dd4cu+i*88),sizeof(entry));
#endif
  unsigned length=0;while(length<33&&entry[length])length++;if(!length||length>32)continue;
  int valid=1;for(unsigned k=0;k<length;k++)if(entry[k]<32||entry[k]==127)valid=0;if(!valid)continue;
  char hex[65];for(unsigned k=0;k<length;k++){hex[k*2]="0123456789abcdef"[entry[k]>>4];hex[k*2+1]="0123456789abcdef"[entry[k]&15];}hex[length*2]=0;
  char auth[7]={0};for(unsigned k=0;k<5;k++){unsigned c=entry[65+k];if(c>='a'&&c<='z')c-=32;auth[k]=(char)c;}
  unsigned signal;memcpy(&signal,entry+84,4);if(signal>100)signal=0;
  int secured=strcmp(auth,"NONE")&&strcmp(auth,"OPEN")&&strcmp(auth,"0");
  n=snprintf(out+used,sizeof(out)-used,"%s{\"ssid_hex\":\"%s\",\"signal\":%u,\"secured\":%s}",included?",":"",hex,signal,secured?"true":"false");
  if(n<0||(usize)n>=sizeof(out)-used)return;used+=(usize)n;included++;
 }
 /* Native AP teardown clears its scan table. Keep the last public,
  * explicitly cached list rather than replacing it with an empty result. */
 if(!included&&*previous)return;
 if(used+3>=sizeof(out))return;out[used++]=']';out[used++]='}';out[used++]='\n';
 for(usize i=0;i<used;i++){hash^=(unsigned char)out[i];hash*=UINT64_C(1099511628211);}
 if(hash!=*previous&&!marker("wifi-networks.json",out,used))*previous=hash;
}
#else
static void publish_wifi_networks(uint64_t*previous){(void)previous;}
#endif
#ifdef QC5_WIFI_RAM_TEST
extern int mprotect(void*,usize,int);
static int ram_dns_changed;
static int prepare_native_dns(void){
 volatile unsigned*word=(volatile unsigned*)0x007661ecu;
 if(*word==0x20736e64u)return 0;if(*word!=0x736e6977u)return -1;
 if(mprotect((void*)0x00766000u,4096,3))return -1;
 *word=0x20736e64u;ram_dns_changed=1;
 return mprotect((void*)0x00766000u,4096,5);
}
static void restore_native_dns(void){
 volatile unsigned*word=(volatile unsigned*)0x007661ecu;
 if(ram_dns_changed&&*word==0x20736e64u&&!mprotect((void*)0x00766000u,4096,3)){*word=0x736e6977u;mprotect((void*)0x00766000u,4096,5);ram_dns_changed=0;}
}
#endif

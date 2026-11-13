#ifndef QC5_STATION_H
#define QC5_STATION_H
/* Linux network ioctls only. Keep this usable by the uClibc preload without
 * linking a second libc. The ifreq storage also fits the 64-bit test host. */
#ifdef QC5_STATION_FREESTANDING
extern int socket(int,int,int),close(int),ioctl(int,unsigned long,...);
#endif
struct qc5_station_request {
 char name[16];
 union {unsigned mode;struct {unsigned short family;unsigned char data[14];} address;struct {void*pointer;unsigned short length,flags;} point;unsigned char space[sizeof(void*)==8?24:16];} value;
};
static inline int qc5_station_socket(void){
#ifdef __mips__
 return socket(2,1,0);
#else
 return socket(2,2,0);
#endif
}
/* AIC8800DC reports WEXT AUTO (0) even while associated. Distinguish its
 * station peer from the interface's own AP address; never trust mode alone. */
static inline int qc5_station_peer(int fd,struct qc5_station_request*r){
 if(ioctl(fd,0x8b15UL,r))return 0;
 unsigned char peer[6];unsigned nonzero=0,nonff=0;
 for(unsigned i=0;i<6;i++){peer[i]=r->value.address.data[i];nonzero|=peer[i];nonff|=peer[i]^255;}
 if(!nonzero||!nonff||ioctl(fd,0x8927UL,r))return 0;
 unsigned different=0;for(unsigned i=0;i<6;i++)different|=peer[i]^r->value.address.data[i];
 return different?1:2;
}
static inline int qc5_station_is_ap(void){
 struct qc5_station_request r={0};int fd=qc5_station_socket();if(fd<0)return 0;
 const char name[]="wlan0";for(unsigned i=0;i<sizeof(name);i++)r.name[i]=name[i];
 int ok=0;if(!ioctl(fd,0x8b07UL,&r)){unsigned mode=r.value.mode;ok=mode==3||(mode==0&&qc5_station_peer(fd,&r)==2);}close(fd);return ok;
}
/* Supervisor-only status: -1 means the AIC WEXT result is inconclusive while
 * the interface is up at the native AP address. Do not tear down live setup
 * clients just because AUTO mode retains a stale station BSSID or ESSID. */
static inline int qc5_station_ap_status(void){
 struct qc5_station_request r={0};int fd=qc5_station_socket();if(fd<0)return 0;
 const char name[]="wlan0";for(unsigned i=0;i<sizeof(name);i++)r.name[i]=name[i];
 int result=0;
 if(ioctl(fd,0x8913UL,&r)||!(r.value.mode&1))goto finish;
 if(!ioctl(fd,0x8b07UL,&r)){
  unsigned mode=r.value.mode;
  if(mode==3){result=1;goto finish;}
  if(mode!=0)goto finish;
  if(qc5_station_peer(fd,&r)==2){result=1;goto finish;}
 }
 if(!ioctl(fd,0x8915UL,&r)&&r.value.address.family==2){
  unsigned char*ip=r.value.address.data+2;
  if(ip[0]==192&&ip[1]==168&&ip[2]==168&&ip[3]==1)result=-1;
 }
 finish:close(fd);return result;
}
static inline int qc5_station_ssid(char out[33]){
 struct qc5_station_request r={0};int fd=qc5_station_socket();if(fd<0)return 0;
 const char name[]="wlan0";for(unsigned i=0;i<sizeof(name);i++)r.name[i]=name[i];
 for(unsigned i=0;i<33;i++)out[i]=0;
 r.value.point.pointer=out;r.value.point.length=33;
 int ok=!ioctl(fd,0x8b1bUL,&r)&&r.value.point.length<=33;close(fd);out[32]=0;return ok&&out[0];
}
static int qc5_station_online(void){
#ifdef __mips__
 const int datagram=1; /* Linux MIPS socket ABI: SOCK_DGRAM. */
#else
 const int datagram=2;
#endif
 int fd=socket(2,datagram,0);if(fd<0)return 0;int online=0;struct qc5_station_request request={0};
 request.name[0]='w';request.name[1]='l';request.name[2]='a';request.name[3]='n';request.name[4]='0';
 if(ioctl(fd,0x8b07UL,&request)||(request.value.mode!=2&&request.value.mode!=0))goto done;
 if(qc5_station_peer(fd,&request)!=1)goto done;
 char ssid[33]={0};request.value.point.pointer=ssid;request.value.point.length=33;
 if(ioctl(fd,0x8b1bUL,&request)||request.value.point.length>33||!ssid[0])goto done;
 if(ioctl(fd,0x8915UL,&request)||request.value.address.family!=2)goto done;
 unsigned char *ip=request.value.address.data+2;
 online=(ip[0]||ip[1]||ip[2]||ip[3])&&ip[0]!=127&&!(ip[0]==169&&ip[1]==254)&&!(ip[0]==192&&ip[1]==168&&ip[2]==168&&ip[3]==1);
 done:close(fd);return online;
}
#endif

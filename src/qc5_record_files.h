#ifndef QC5_RECORD_FILES_H
#define QC5_RECORD_FILES_H
#include <dirent.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include "qc5_sd.h"
/* Native recorder layout, never a caller-supplied filesystem path. Both the
 * browser and destination worker use the same closed-segment checks. */
static int qc5_record_id(const char*s){
 size_t n=strlen(s);if(!n||n>63)return 0;
 for(size_t i=0;i<n;i++)if(!((s[i]>='A'&&s[i]<='Z')||(s[i]>='a'&&s[i]<='z')||(s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'))return 0;
 return 1;
}
static int qc5_record_day(const char*s){
 if(strlen(s)!=10||s[4]!='-'||s[7]!='-')return 0;
 for(int i=0;i<10;i++)if(i!=4&&i!=7&&(s[i]<'0'||s[i]>'9'))return 0;
 int month=(s[5]-'0')*10+s[6]-'0',day=(s[8]-'0')*10+s[9]-'0';
 return month>=1&&month<=12&&day>=1&&day<=31;
}
static int qc5_record_name(const char*s){
 size_t n=strlen(s);if(n<11||n>63||strcmp(s+n-4,".mp4"))return 0;
 for(size_t i=0;i<n-4;i++)if((s[i]<'0'||s[i]>'9')&&s[i]!='_')return 0;
 return 1;
}
static int qc5_record_root(const char*sd,const char*id,int writable){
 char relative[80];if(!qc5_record_id(id)){errno=EINVAL;return -1;}
 snprintf(relative,sizeof(relative),"%s/video",id);return qc5_sd_directory(sd,relative,0,writable);
}
static int qc5_record_day_directory(int root,const char*day){
 int dir=openat(root,day,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return -1;
 int sensor=openat(dir,"s0",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(sensor>=0){close(dir);return sensor;}
 if(errno==ENOENT)return dir; /* Earlier native layouts used the date directly. */
 close(dir);return -1;
}
static uint32_t qc5_record_be32(const unsigned char*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int qc5_record_closed(int fd,struct stat*out){
 struct stat before,after;if(fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_size<32||before.st_size>536870912)return 0;
 /* The native writer appends moov only when closing a segment. It may leave
  * preallocation after moov, which must not be parsed as another MP4 box. */
 off_t pos=0;int ftyp=0,mdat=0,moov=0;
 for(int count=0;count<64&&pos+8<=before.st_size;count++){
  unsigned char h[16];if(pread(fd,h,8,pos)!=8)return 0;
  uint64_t size=qc5_record_be32(h);unsigned header=8;
  if(size==1){if(pread(fd,h+8,8,pos+8)!=8)return 0;size=(uint64_t)qc5_record_be32(h+8)<<32|qc5_record_be32(h+12);header=16;}
  if(size<header||size>(uint64_t)(before.st_size-pos))return 0;
  if(!memcmp(h+4,"ftyp",4))ftyp=1;
  if(!memcmp(h+4,"mdat",4))mdat=1;
  if(!memcmp(h+4,"moov",4)){moov=1;break;}
  pos+=(off_t)size;
 }
 if(!ftyp||!mdat||!moov||fstat(fd,&after)||before.st_size!=after.st_size||before.st_mtime!=after.st_mtime)return 0;
 /* Extra protection for a writer in the final close/rename window. */
 time_t now=time(NULL);if(now<after.st_mtime||now-after.st_mtime<5)return 0;
 if(out)*out=after;
 return 1;
}
static int qc5_record_open(int root,const char*key,int writable,struct stat*out){
 char day[11];const char*name=strchr(key,'/');
 if(!name||name-key!=10){errno=EINVAL;return -1;}
 memcpy(day,key,10);day[10]=0;name++;
 if(!qc5_record_day(day)||!qc5_record_name(name)){errno=EINVAL;return -1;}
 int directory=qc5_record_day_directory(root,day);if(directory<0)return -1;
 int fd=openat(directory,name,(writable?O_RDWR:O_RDONLY)|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);close(directory);
 if(fd<0)return -1;
 if(!qc5_record_closed(fd,out)){close(fd);errno=EBUSY;return -1;}
 return fd;
}
#endif

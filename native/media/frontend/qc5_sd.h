#ifndef QC5_SD_H
#define QC5_SD_H
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Shared by presets and recordings. A writable mount-point directory alone
 * is never accepted as an SD; all directory traversal remains fd-relative. */
static inline int qc5_sd_mounted(const char *root,int writable){
#ifdef QC5_HOST_TEST
 const char *allowed=getenv("QC5_TEST_SD_ROOT");
 if(!allowed||!*allowed)allowed=getenv("QC5_STORE_TEST_SD");
 if(allowed&&*allowed&&!strcmp(root,allowed)){
  const char *ro=getenv("QC5_TEST_SD_READONLY");
  return !writable||!ro||strcmp(ro,"1");
 }
#endif
 FILE *f=fopen("/proc/mounts","r");if(!f)return 0;
 char device[512],mount[512],type[80],options[512],line[1800];int ok=0;
 while(fgets(line,sizeof(line),f)){
  if(sscanf(line,"%511s %511s %79s %511s",device,mount,type,options)!=4)continue;
  if(strcmp(mount,root))continue;
  if(!strcmp(type,"vfat")||!strcmp(type,"exfat")||!strcmp(type,"ext4")){
   char opts[520];snprintf(opts,sizeof(opts),",%s,",options);
   ok=!writable||strstr(opts,",rw,")!=NULL;
  }
  break;
 }
 fclose(f);return ok;
}
static inline int qc5_sd_directory(const char *root,const char *relative,int create,int writable){
 if(!qc5_sd_mounted(root,writable)){errno=ENODEV;return -1;}
 char path[768];size_t n=strlen(relative);
 if(!n||n>=sizeof(path)||relative[0]=='/'||relative[n-1]=='/'||strchr(relative,'\\')){errno=EINVAL;return -1;}
 strcpy(path,relative);
 int current=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(current<0)return -1;
 char *save=NULL;
 for(char *part=strtok_r(path,"/",&save);part;part=strtok_r(NULL,"/",&save)){
  if(!strcmp(part,".")||!strcmp(part,"..")){close(current);errno=EINVAL;return -1;}
  if(create&&mkdirat(current,part,0700)&&errno!=EEXIST){int error=errno;close(current);errno=error;return -1;}
  int next=openat(current,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  if(next<0){int error=errno;close(current);errno=error;return -1;}
  close(current);current=next;
 }
 return current;
}
static inline int qc5_random_hex(char out[17]){
 unsigned char nonce[8];size_t used=0;int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
 if(fd<0)return -1;
 while(used<sizeof(nonce)){
  ssize_t n=read(fd,nonce+used,sizeof(nonce)-used);
  if(n<0&&errno==EINTR)continue;
  if(n<=0){close(fd);return -1;}used+=(size_t)n;
 }
 close(fd);for(unsigned i=0;i<sizeof(nonce);i++)snprintf(out+2*i,3,"%02x",nonce[i]);
 return 0;
}
#endif

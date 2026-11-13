/* Publish verified SquashFS programs/assets through RAM-only symlinks.
 * No payload copies, hardware I/O, network calls or external commands. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef size_t usize;
#include "qc5_sha256.h"
#ifndef QRR_ROOT
#define QRR_ROOT "/qc5root"
#endif
struct qrr_entry {const char *path,*sha;size_t bytes;unsigned mode;};
#include "readonly-links.generated.h"
static int qrr_hash(int fd,const struct qrr_entry *entry)
{
 struct stat st;struct statfs fs;struct statvfs vfs;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1||(size_t)st.st_size!=entry->bytes||
  (st.st_mode&0777)!=entry->mode||fstatfs(fd,&fs)||(unsigned long)fs.f_type!=0x73717368u||fstatvfs(fd,&vfs)||!(vfs.f_flag&ST_RDONLY))return 0;
 Sha s;init(&s);uint8_t block[4096],digest[32];size_t total=0;
 for(;;){ssize_t n=read(fd,block,sizeof block);if(n<0&&errno==EINTR)continue;if(n<0)return 0;if(!n)break;
  if((size_t)n>entry->bytes-total)return 0;total+=(size_t)n;add(&s,block,(size_t)n);}
 if(total!=entry->bytes)return 0;finish(&s,digest);
 for(unsigned i=0;i<32;i++)if(entry->sha[2*i]!="0123456789abcdef"[digest[i]>>4]||entry->sha[2*i+1]!="0123456789abcdef"[digest[i]&15])return 0;
 return 1;
}
static int qrr_parent(const char *path,char name[128])
{
 if(!path||!*path||*path=='/'||strlen(path)>=128)return -1;char copy[128];strcpy(copy,path);
 int dir=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(dir<0)return -1;
 char *part=copy,*slash;
 while((slash=strchr(part,'/'))){
  *slash=0;if(!*part||!strcmp(part,".")||!strcmp(part,".."))goto bad;
  if(mkdirat(dir,part,0700)&&errno!=EEXIST)goto bad;
  int child=openat(dir,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(child<0)goto bad;
  struct stat st;struct statfs fs;
  int ok=!fstat(child,&st)&&st.st_uid==0&&!(st.st_mode&0022)&&!fstatfs(child,&fs)&&
   ((unsigned long)fs.f_type==0x858458f6u||(unsigned long)fs.f_type==0x01021994u);
  if(!ok){close(child);goto bad;}close(dir);dir=child;part=slash+1;
 }
 if(!*part||!strcmp(part,".")||!strcmp(part,".."))goto bad;strcpy(name,part);return dir;
bad:close(dir);return -1;
}
int main(int argc,char **argv)
{
 (void)argv;struct utsname kernel;if(argc!=1||geteuid()||uname(&kernel)||strcmp(kernel.release,"7.3.1-qc5-sd-tiny"))return 64;
 enum {count=sizeof qrr_manifest/sizeof *qrr_manifest};int dirs[count];char names[count][128],sources[count][256],destinations[count][160];
 unsigned created=0,bound=0;size_t bytes=0;for(unsigned i=0;i<count;i++)dirs[i]=-1;
 int root=open(QRR_ROOT,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(root<0)return 1;
 for(unsigned i=0;i<count;i++){
  const struct qrr_entry *e=&qrr_manifest[i];int fd=openat(root,e->path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
  if(fd<0)goto bad;int ok=qrr_hash(fd,e);close(fd);if(!ok)goto bad;
  dirs[i]=qrr_parent(e->path,names[i]);if(dirs[i]<0)goto bad;
  struct stat existing;if(!fstatat(dirs[i],names[i],&existing,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)goto bad;
  int n=snprintf(sources[i],sizeof sources[i],"%s/%s",QRR_ROOT,e->path);if(n<=0||(size_t)n>=sizeof sources[i])goto bad;
  n=snprintf(destinations[i],sizeof destinations[i],"/%s",e->path);if(n<=0||(size_t)n>=sizeof destinations[i])goto bad;
  bytes+=e->bytes;
 }
 for(unsigned i=0;i<count;i++){
  int fd=openat(dirs[i],names[i],O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,qrr_manifest[i].mode);
  if(fd<0)goto bad;close(fd);created++;
  if(mount(sources[i],destinations[i],NULL,MS_BIND,NULL))goto bad;bound++;
  if(mount(NULL,destinations[i],NULL,MS_BIND|MS_REMOUNT|MS_RDONLY|MS_NOSUID|MS_NODEV,NULL))goto bad;
  fd=openat(dirs[i],names[i],O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)goto bad;
  int ok=qrr_hash(fd,&qrr_manifest[i]);close(fd);if(!ok)goto bad;
 }
 for(unsigned i=0;i<count;i++)close(dirs[i]);close(root);
 printf("QC5_READONLY_RUNTIME_BOUND files=%u payload_bytes=%zu no_file_copies=1 regular_nofollow=1\n",count,bytes);return 0;
bad:
 for(unsigned i=bound;i>0;i--)(void)umount2(destinations[i-1],0);
 for(unsigned i=0;i<created;i++)(void)unlinkat(dirs[i],names[i],0);
 for(unsigned i=0;i<count;i++)if(dirs[i]>=0)close(dirs[i]);close(root);
 fprintf(stderr,"QC5_READONLY_RUNTIME_REFUSED errno=%d\n",errno);return 1;
}

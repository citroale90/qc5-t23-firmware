#ifndef QC5_CONFIG_READ_H
#define QC5_CONFIG_READ_H
/* Caller declares POSIX read(). Read the entire literal file even when the
 * filesystem supplies short reads. -2 means too large; never apply a prefix. */
static inline long qc5_config_read_fd(int fd,char *out,unsigned long capacity){
 unsigned long used=0;
 while(used<capacity){
  long n=read(fd,out+used,capacity-used);
  if(n<0)return -1;
  if(!n)return (long)used;
  used+=(unsigned long)n;
 }
 return -2;
}
#endif

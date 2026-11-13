#ifndef QC5_NATIVE_GUARD_H
#define QC5_NATIVE_GUARD_H
/* Before enabling a detector shared with vendor notifications, verify the
 * relevant disabled native entry points. Never rely on a writable marker. */
static const char*native_app_path(void){
#ifdef QC5_HOST_TEST
 const char*fixture=getenv("QC5_TEST_NATIVE_APP");if(fixture&&*fixture)return fixture;
#endif
 return "/system/system/bin/vp_project";
}
static int native_image_words(const char*path,const uint32_t(*words)[2],unsigned word_count,uint32_t required_end){
 int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
 struct stat st;unsigned char eh[52],ph[32];int ok=0;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<52||st.st_size>16*1024*1024||pread(fd,eh,sizeof(eh),0)!=(ssize_t)sizeof(eh))goto done;
 if(memcmp(eh,"\177ELF\1\1\1",7)||eh[18]!=8||eh[19]||eh[42]!=32||eh[43])goto done;
 uint32_t phoff=le32(eh+28);unsigned count=eh[44]|((unsigned)eh[45]<<8);
 if(!count||count>32||(uint64_t)phoff+(uint64_t)count*32>(uint64_t)st.st_size)goto done;
 int writable=!required_end;
 for(unsigned i=0;i<count;i++){
  if(pread(fd,ph,32,(off_t)phoff+i*32)!=32)goto done;
  if(le32(ph)!=1)continue;
  uint32_t off=le32(ph+4),va=le32(ph+8),filesz=le32(ph+16),memsz=le32(ph+20),flags=le32(ph+24);
  if(filesz>memsz||(uint64_t)off+filesz>(uint64_t)st.st_size||(uint64_t)va+memsz>UINT32_MAX)goto done;
  if(required_end&&flags==6&&required_end>=4&&va<=required_end-4&&(uint64_t)va+memsz>=required_end)writable=1;
 }
 if(!writable)goto done;
 for(unsigned n=0;n<word_count;n++){
  int found=0;
  for(unsigned i=0;i<count;i++){
   if(pread(fd,ph,32,(off_t)phoff+i*32)!=32)goto done;
   if(le32(ph)!=1||(le32(ph+24)&5)!=5)continue;
   uint32_t off=le32(ph+4),va=le32(ph+8),size=le32(ph+16),at=words[n][0];
   if(at<va||(uint64_t)at+4>(uint64_t)va+size)continue;
   unsigned char value[4];if(pread(fd,value,4,(off_t)off+at-va)!=4||le32(value)!=words[n][1])goto done;
   found=1;break;
  }
  if(!found)goto done;
 }
 ok=1;
 done:close(fd);return ok;
}
static int native_motion_local_only(void){
 static const uint32_t words[][2]={
  {0x419740,0x03e00008},{0x419744,0x00001021},
  {0x41ca14,0x03e00008},{0x41ca18,0x00001021},
  {0x41a814,0x03e00008},{0x41a818,0x00001021},
  {0x41d2a0,0x10000004},
  {0x48c270,0x03e00008},{0x48c274,0x2402ffff},
  {0x48cbe4,0x03e00008},{0x48cbe8,0x2402ffff},
  {0x49fba8,0x10800002},{0x49fbac,0x2402ffff},
  {0x49fbb0,0xac800000},{0x49fbb4,0x03e00008},{0x49fbb8,0}
 };

 return native_image_words(native_app_path(),words,sizeof(words)/sizeof(*words),0);
}
#include "qc5_record_native_words.h"
static int native_record_policy_supported(const char*path){return native_image_words(path,qc5_record_native_words,sizeof(qc5_record_native_words)/sizeof(*qc5_record_native_words),0);}
#endif

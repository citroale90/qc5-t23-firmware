/* SPDX-License-Identifier: GPL-3.0-only
 * Exec-isolated local file writer. Receives already muxed Matroska pieces;
 * no ISP/DMA, network connection, encoder or second frame allocation.
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/utsname.h>
#include "record-native.h"
#include "record-channel.h"
#ifndef QC5_RECORD_SINK_TEST
#include "record-volume.h"
#endif
/* Only the isolated sink creates the anchored recording directory. */
static int qrs_open_directory(const char *root_path)
{
 int root=open(root_path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(root<0)return -1;
 int made=mkdirat(root,"qc5-recordings",0700);
 if(made&&errno!=EEXIST){int saved=errno;close(root);errno=saved;return -1;}
 if(!made&&fsync(root)){int saved=errno;close(root);errno=saved;return -1;}
 int directory=openat(root,"qc5-recordings",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 int saved=errno;close(root);errno=saved;return directory;
}
static int reply(int fd,unsigned type,unsigned serial)
{struct qrc_header h={QRC_MAGIC,type,serial,0};return send(fd,&h,sizeof h,MSG_NOSIGNAL)==sizeof h;}
int main(int argc,char **argv)
{
 int endpoint=3;
#ifdef QC5_RECORD_SINK_TEST
 if(argc!=3&&argc!=4)return 64;if(argc==4)endpoint=atoi(argv[3]);if(endpoint<3||endpoint>1024)return 64;
#else
 if(argc!=3)return 64;
#endif
#ifndef QC5_RECORD_SINK_TEST
 struct utsname u;if(geteuid()||uname(&u)||strcmp(u.release,"7.3.1-qc5-sd-tiny")||strcmp(argv[1],"/mnt/sda0/qc5-recordings"))return 77;
 /* If card writeback exhausts memory, discard the optional recorder before
  * the camera/encoder owner. This affects only this exec'd writer process. */
 int score=open("/proc/self/oom_score_adj",O_WRONLY|O_CLOEXEC|O_NOFOLLOW);
 if(score<0)return 72;int protected=write(score,"1000",4)==4;close(score);if(!protected)return 72;
#endif
#ifndef QC5_RECORD_SINK_TEST
 uint32_t cid[4];if(!cid_read(cid)||qcv_preflight(cid,NULL,NULL))return 71;
#endif
 int type=0;socklen_t length=sizeof type;if(getsockopt(endpoint,SOL_SOCKET,SO_TYPE,&type,&length)||type!=SOCK_SEQPACKET)return 65;
 char *end;errno=0;uint64_t epoch=strtoull(argv[2],&end,16);if(errno||*end||!epoch||strlen(argv[2])>16)return 66;
#ifndef QC5_RECORD_SINK_TEST
 int directory=qrs_open_directory("/mnt/sda0");
#else
 int directory=open(argv[1],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
#endif
 if(directory<0)return 67;
 struct qrn_recorder r;if(!qrn_init(&r,directory,epoch)||!qrn_open(&r,1)){close(directory);return 68;}
 /* Caller supplies only fd3; ensure an inherited capture/device fd cannot
  * keep a peripheral or network session alive after the media owner exits. */
 for(int fd=3;fd<4096;fd++)if(fd!=directory&&fd!=r.fd&&fd!=endpoint)close(fd);
 if(!reply(endpoint,QRC_READY,0)){qrn_close(&r,0);close(directory);return 69;}
 unsigned active=0,kind=0,serial=0,error=0;off_t checkpoint=0;uint64_t checked_bytes=0;
 uint8_t data[4096];
 for(;;){
  struct qrc_header h;int got=qrc_receive(endpoint,&h,data,sizeof data,0);
  if(got==0)break;if(got!=1){error=1;break;}
  unsigned first=h.type==QRC_FIRST_VIDEO||h.type==QRC_FIRST_AUDIO||h.type==QRC_WHOLE_VIDEO||h.type==QRC_WHOLE_AUDIO;
  unsigned last=h.type==QRC_DATA_COMMIT||h.type==QRC_WHOLE_VIDEO||h.type==QRC_WHOLE_AUDIO;
  if(h.type==QRC_BEGIN_VIDEO||h.type==QRC_BEGIN_AUDIO||first){
   if(active||(first?!h.bytes:h.bytes)||serial==UINT32_MAX||h.serial!=serial+1){error=1;break;}
   checkpoint=lseek(r.fd,0,SEEK_CUR);if(checkpoint<0){error=1;break;}
   checked_bytes=r.bytes;active=1;
   kind=first?(h.type==QRC_FIRST_AUDIO||h.type==QRC_WHOLE_AUDIO?QRC_BEGIN_AUDIO:QRC_BEGIN_VIDEO):h.type;
   serial=h.serial;if(!first)continue;
  }
  if(h.type==QRC_DATA||h.type==QRC_DATA_COMMIT||first){
   if(!active||h.serial!=serial||!h.bytes||!qrn_write(&r,data,h.bytes)){error=1;break;}
   if(last){if(kind==QRC_BEGIN_VIDEO)r.mux.frames++;active=0;}
  }else if(h.type==QRC_COMMIT){
   if(!active||h.serial!=serial||h.bytes){error=1;break;}
   if(kind==QRC_BEGIN_VIDEO)r.mux.frames++;active=0;
  }else{error=1;break;}
 }
 if(active){
  if(ftruncate(r.fd,checkpoint))r.mux.failed=1;
  r.bytes=checked_bytes;
 }
 /* A timed-out or disconnected producer may leave an incomplete unit. Its
  * tail is removed; all earlier committed blocks may still be published. */
 int published=qrn_close(&r,1);if(!published)error=1;
 (void)reply(endpoint,error?QRC_ERROR:QRC_DONE,serial);
 close(directory);close(endpoint);return error?70:0;
}

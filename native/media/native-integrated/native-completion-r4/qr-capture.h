/* SPDX-License-Identifier: GPL-3.0-only
 * Optional bounded copy only while setup owns a valid private RAM slot.
 * The decoder runs outside capture; slow/absent QR never delays the encoder.
 */
#ifndef QC5_QR_CAPTURE_H
#define QC5_QR_CAPTURE_H
#include <sys/mman.h>
#include "qr-frame.h"
#ifndef QQ_CAPTURE_PATH
#define QQ_CAPTURE_PATH "/run/qc5-native-network/" QQ_FRAME_NAME
#endif
static struct {struct qq_frame *frame;int fd;uint64_t next;} qq_capture={.fd=-1};
static void qq_capture_stop(void)
{if(qq_capture.frame)munmap(qq_capture.frame,sizeof(struct qq_frame));if(qq_capture.fd>=0)close(qq_capture.fd);qq_capture.frame=NULL;qq_capture.fd=-1;}
static void qq_capture_luma(const uint8_t *y,unsigned w,unsigned h,unsigned stride,uint64_t us)
{
 if(!us||us<qq_capture.next)return;qq_capture.next=us+500000u;
 struct stat st;
 if(qq_capture.fd>=0&&(fstat(qq_capture.fd,&st)||st.st_nlink!=1)){qq_capture_stop();}
 if(!qq_capture.frame){
  int fd=open(QQ_CAPTURE_PATH,O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return;
  if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&077)||st.st_nlink!=1||st.st_size!=sizeof(struct qq_frame)){close(fd);return;}
  void *m=mmap(NULL,sizeof(struct qq_frame),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(m==MAP_FAILED){close(fd);return;}
  qq_capture.fd=fd;qq_capture.frame=m;
 }
 (void)qq_fill(qq_capture.frame,y,w,h,stride,us);
}
#endif

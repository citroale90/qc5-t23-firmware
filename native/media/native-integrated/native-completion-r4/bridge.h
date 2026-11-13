/* SPDX-License-Identifier: GPL-3.0-only
 * Optional reader of existing native H264 and G711 output. No second
 * encoder, DMA ring, retained access unit, subprocess or durable write.
 */
#ifndef QC5_NATIVE_RTSP_CAPTURE_BRIDGE_H
#define QC5_NATIVE_RTSP_CAPTURE_BRIDGE_H
#include <sys/stat.h>
#include "socket-native.h"
static struct qrl_server native_rtsp={.listener=-1,.peer=-1};
static int native_rtsp_active,native_rtsp_directory=-1,native_rtsp_marked;
static const char native_rtsp_ready[]="QRT73_READY\n";
static void qrt_end(void)
{
 if(native_rtsp_active)qrl_close(&native_rtsp);
 if(native_rtsp_directory>=0)(void)unlinkat(native_rtsp_directory,"rtsp-state",0);
 native_rtsp_active=native_rtsp_marked=0;native_rtsp_directory=-1;
}
static int qrt_begin(int directory,uint32_t address,unsigned port)
{
 if(native_rtsp_active||directory<0)return 0;
 native_rtsp_directory=directory;native_rtsp_marked=0;(void)unlinkat(directory,"rtsp-state",0);
 native_rtsp_active=qrl_open(&native_rtsp,address,port,qrl_us(CLOCK_MONOTONIC));return native_rtsp_active;
}
static void qrt_mark(void)
{
 if(native_rtsp_marked||native_rtsp_directory<0||!native_rtsp.config.sps_n||!native_rtsp.config.pps_n)return;
 /* One small RAM-only status write, no getrandom/fsync in capture. A reader
  * observing a partial write treats RTSP as unavailable until the next poll. */
 int fd=openat(native_rtsp_directory,"rtsp-state",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600);
 if(fd<0)return;struct stat s;int ok=!fstat(fd,&s)&&S_ISREG(s.st_mode)&&s.st_uid==geteuid()&&!(s.st_mode&077)&&s.st_nlink==1;
 if(ok)ok=write(fd,native_rtsp_ready,sizeof native_rtsp_ready-1)==sizeof native_rtsp_ready-1;
 if(close(fd))ok=0;if(ok)native_rtsp_marked=1;else (void)unlinkat(native_rtsp_directory,"rtsp-state",0);
}
static void qrt_poll(void){if(native_rtsp_active)qrl_poll(&native_rtsp);}
static void qrt_video(const uint8_t *data,size_t bytes,uint64_t stamp,int key)
{if(native_rtsp_active){qrl_video(&native_rtsp,data,bytes,stamp,key);qrt_mark();}}
static void qrt_audio(const uint8_t *data,size_t samples,uint64_t stamp)
{if(native_rtsp_active)qrl_audio(&native_rtsp,data,samples,stamp);}
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded numerical diagnostics only. No image or audio export here.
 */
#ifndef QC5_NV12_AUDIT_H
#define QC5_NV12_AUDIT_H
#include <stdint.h>
#include <stdio.h>
struct qnv_sample {uint32_t minimum,maximum,sum,count,hash;};
static int qnv_sample_plane(const volatile uint8_t *p,unsigned bytes,unsigned start,unsigned step,struct qnv_sample *s)
{
 if(!p||!s||!bytes||bytes>921600||start>=bytes||!step||step>1024)return 0;
 *s=(struct qnv_sample){.minimum=255,.hash=2166136261u};
 for(unsigned n=start;n<bytes;n+=step){uint32_t v=p[n];
  if(v<s->minimum)s->minimum=v;if(v>s->maximum)s->maximum=v;
  s->sum+=v;++s->count;s->hash=(s->hash^v)*16777619u;
 }
 return s->count>0;
}
static void qnv_audit_image(const char *name,unsigned bank,const volatile uint8_t *y,const volatile uint8_t *uv)
{
#ifndef QC5_FRAME_TEST
 struct qnv_sample a,b,c;
 if(!qnv_sample_plane(y,921600,0,257,&a)||!qnv_sample_plane(uv,460800,0,254,&b)||!qnv_sample_plane(uv,460800,1,254,&c))return;
 int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);if(fd<0)return;
 char line[320];int n=snprintf(line,sizeof line,
  "<6>QC5_NV12_IMAGE source=%s bank=%u y=%u,%u,%u,%u,%08x u=%u,%u,%u,%u,%08x v=%u,%u,%u,%u,%08x\n",
  name,bank,a.minimum,a.maximum,a.sum,a.count,a.hash,b.minimum,b.maximum,b.sum,b.count,b.hash,c.minimum,c.maximum,c.sum,c.count,c.hash);
 if(n>0&&n<(int)sizeof line)(void)write(fd,line,(unsigned)n);close(fd);
#else
 (void)name;(void)bank;(void)y;(void)uv;
#endif
}
#endif

/* SPDX-License-Identifier: GPL-2.0-only
 * Sparse luma sampling of an owned NV12 capture bank:192 bytes of history,
 * no decoder/snapshots/vendor IPC. Exposure or filter transitions reseed it.
 */
#ifndef QC5_MOTION_LOCAL_H
#define QC5_MOTION_LOCAL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct qml_motion {uint8_t previous[192];unsigned seeded,hits;uint64_t last_us;};
static int qml_sample(struct qml_motion *s,const uint8_t *y,unsigned width,unsigned height,unsigned stride,
 unsigned sensitivity,unsigned transitioning,uint64_t now,unsigned *moving)
{
 if(!s||!y||!moving||width<16||height<12||width>4096||height>4096||stride<width||stride>8192||sensitivity>100||transitioning>1||!now||(s->last_us&&now<=s->last_us))return 0;
 uint8_t next[192];int sum=0;unsigned at=0;
 for(unsigned row=0;row<12;row++)for(unsigned col=0;col<16;col++){
  next[at]=y[(size_t)((2*row+1)*height/24)*stride+(2*col+1)*width/32];sum+=(int)next[at]-s->previous[at];at++;
 }
 int common=sum/192;unsigned changed=0;int threshold=30-(int)(sensitivity*22/100);
 for(unsigned n=0;n<192;n++){int difference=(int)next[n]-s->previous[n]-common;if(difference>threshold||difference<-threshold)changed++;}
 *moving=0;
 if(s->seeded&&!transitioning){
  if(changed>=8+(100-sensitivity)/3){if(s->hits<3)s->hits++;}else s->hits=0;
  *moving=s->hits>=2;
 }else s->hits=0;
 memcpy(s->previous,next,sizeof next);s->seeded=1;s->last_us=now;return 1;
}
#endif

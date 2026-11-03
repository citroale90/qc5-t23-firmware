#ifndef QC5_VIDEO_CLOCK_H
#define QC5_VIDEO_CLOCK_H
#include <stdint.h>
/* Keep the measured cadence separate from any correction to the MP4 clock.
 * A short backwards step must not leave every later frame ahead of its source. */
struct qc5_video_clock {
 uint64_t presentation_ms;int64_t origin;unsigned intervals[5],slot,adjustments,started;
};
static unsigned qc5_video_clock_step(struct qc5_video_clock*c,uint64_t pending,uint64_t next){
 if(!c->started){c->origin=(int64_t)pending;for(unsigned i=0;i<5;i++)c->intervals[i]=67;c->started=1;}
 int64_t delta=(int64_t)next-(int64_t)pending;
 if(delta>0&&delta<=1000){c->intervals[c->slot]=(unsigned)delta;c->slot=(c->slot+1)%5;}
 unsigned sorted[5];for(unsigned i=0;i<5;i++){sorted[i]=c->intervals[i];for(unsigned j=i;j>0&&sorted[j]<sorted[j-1];j--){unsigned tmp=sorted[j];sorted[j]=sorted[j-1];sorted[j-1]=tmp;}}
 unsigned duration=sorted[2];int64_t advance=(int64_t)next-c->origin-(int64_t)c->presentation_ms;
 if(delta>=0&&delta<=2000&&advance>0&&advance<=2000)duration=(unsigned)advance;
 else{
  c->adjustments++;
  /* Duplicates borrow one normal interval without moving the source origin:
   * when the next real stamp catches up, the total duration stays correct. */
  if(delta!=0)c->origin=(int64_t)next-(int64_t)(c->presentation_ms+duration);
 }
 c->presentation_ms+=duration;return duration;
}
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Common guard bands for the finite diagnostic and persistent encoder.
 * Reject a foreign layout before reading or modifying any mapped memory.
 */
#ifndef QC5_HELIX_GUARDS_H
#define QC5_HELIX_GUARDS_H
#include "helix-layout.h"
static int encoder_guards(volatile uint8_t *ram,const struct qc5_helix_layout *p,int initialize)
{
 if(!ram||(initialize!=0&&initialize!=1)||!qc5_helix_layout_valid(p))return 0;
 for(unsigned a=0;a<H_AREA_COUNT;a++){
  uint32_t start=p->region[a].start,bytes=p->region[a].bytes;
  uint32_t end=((start+bytes+4095u)&~4095u)+4096u;
  for(uint32_t n=start+bytes;n<end;n++){
   volatile uint8_t *byte=ram+(n-QC5_CAPTURE_RAM_BASE);
   if(initialize)*byte=0xa5;else if(*byte!=0xa5)return 0;
  }
 }
 return 1;
}
#endif

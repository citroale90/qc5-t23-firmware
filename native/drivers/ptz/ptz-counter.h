/* SPDX-License-Identifier: GPL-2.0-only
 * Exact successful-read decoding of the native ID-0x53 position counters.
 * This is a wrapping step counter rounded upwards to four steps, not an
 * angle in degrees. Call only after checked, stable I2C reads. No I/O.
 */
#ifndef QC5_PTZ_COUNTER_H
#define QC5_PTZ_COUNTER_H
#ifndef __KERNEL__
#include <stdint.h>
#endif
static int qpc_decode(unsigned axis,uint8_t high,uint8_t low,uint16_t *position)
{
 if(axis>1||!position)return 0;
 uint16_t raw=(uint16_t)(((uint16_t)high<<8)|low);
 uint32_t scaled=(uint16_t)((uint32_t)raw*2u);
 if(!axis)*position=(uint16_t)((scaled+3u)&~3u);
 else {
  /* Native Y uses LH followed by signed DIV (towards zero), while X
   * uses LHU and an arithmetic shift of a nonnegative value. */
  int32_t signed_value=scaled>=32768u?(int32_t)scaled-65536:(int32_t)scaled;
  *position=(uint16_t)(((signed_value+3)/4)*4);
 }
 return 1;
}
#endif

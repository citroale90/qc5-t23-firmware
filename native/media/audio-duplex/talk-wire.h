/* SPDX-License-Identifier: GPL-2.0-or-later
 * Existing WebUI QCT1 controls and320-byte PCMA frames. No acknowledgement
 * is generated here: START may be acknowledged only after DAC is ready.
 */
#ifndef QC5_DUPLEX_TALK_WIRE_H
#define QC5_DUPLEX_TALK_WIRE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
enum qtw_kind {QTW_START=1,QTW_STOP=2,QTW_PCM=3};
struct qtw_event {enum qtw_kind kind;uint32_t id;const uint8_t *voice;};
static int qtw_decode(unsigned opcode,const uint8_t *data,size_t bytes,struct qtw_event *out)
{
 if(opcode!=2||!data||!out)return 0;
 if(bytes==320){*out=(struct qtw_event){QTW_PCM,0,data};return 1;}
 if(bytes!=12||memcmp(data,"QCT1",4)||data[4]>1||data[5]||data[6]||data[7])return 0;
 uint32_t id=(uint32_t)data[8]|(uint32_t)data[9]<<8|(uint32_t)data[10]<<16|(uint32_t)data[11]<<24;
 if(!id)return 0;
 *out=(struct qtw_event){data[4]?QTW_START:QTW_STOP,id,NULL};return 1;
}
#endif

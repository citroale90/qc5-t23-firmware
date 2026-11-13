/* SPDX-License-Identifier: GPL-2.0-only
 * U-Boot copies this self-contained decoder above the expanded image.
 * Decompression and cache handoff cannot overwrite unread input or U-Boot.
 */
#include <stdint.h>
#include <stddef.h>
#include "image.generated.h"
extern unsigned char __image_begin[],__image_end[];
int qfd_decode(unsigned char *,size_t,unsigned char *,size_t,void (*)(char *));
static void error(char *message)
{(void)message;for(;;)__asm__ volatile("nop":::"memory");}
void qfd_start(void)
{
 unsigned char *output=(void*)0x80100000u;
 if((uintptr_t)__image_begin<0x80a00000u||(uintptr_t)__image_end>=0x80d00000u||
  (size_t)(__image_end-__image_begin)!=QC5_COMPRESSED_BYTES||QC5_EXPANDED_BYTES>0x790000u||
  qfd_decode(__image_begin,QC5_COMPRESSED_BYTES,output,QC5_EXPANDED_BYTES,error))error("decode");
 uint32_t hash=2166136261u;for(size_t i=0;i<QC5_EXPANDED_BYTES;i++)hash=(hash^output[i])*16777619u;
 if(hash!=QC5_EXPANDED_FNV)error("output");
 for(uintptr_t p=0x80100000u;p<0x80100000u+QC5_EXPANDED_BYTES;p+=32)__asm__ volatile("cache 0x15,0(%0)"::"r"(p):"memory");
 __asm__ volatile("sync":::"memory");
 for(uintptr_t p=0x80100000u;p<0x80100000u+QC5_EXPANDED_BYTES;p+=32)__asm__ volatile("cache 0x10,0(%0)"::"r"(p):"memory");
 __asm__ volatile("sync;ehb":::"memory");
 ((void(*)(void))0x80880000u)();error("returned");
}

/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Adapter for the pinned Linux pre-boot LZMA decoder. No libc, devices,
 * allocator outside the fixed32KiB arena, filesystem or flash operations.
 */
#include <stdint.h>
#include <stddef.h>
#define STATIC static
static unsigned char qfd_heap[32768] __attribute__((aligned(8)));
static unsigned long free_mem_ptr,free_mem_end_ptr;
#include "decompress_unlzma.adapted.c"
int qfd_decode(unsigned char *input,size_t size,unsigned char *output,size_t expanded,void (*error)(char *))
{
 if(!input||!output||!error||size<18||size>0x200000||!expanded||expanded>0x800000||input[0]!=0x5d||input[1]||input[2]||input[3]!=0x40||input[4])return -1;
 uint32_t declared=(uint32_t)input[5]|(uint32_t)input[6]<<8|(uint32_t)input[7]<<16|(uint32_t)input[8]<<24;
 if(input[9]||input[10]||input[11]||input[12]||declared!=expanded)return -1;
 free_mem_ptr=(unsigned long)qfd_heap;free_mem_end_ptr=free_mem_ptr+sizeof qfd_heap;malloc_ptr=0;malloc_count=0;
 long consumed=0;return unlzma(input,size,NULL,NULL,output,&consumed,error);
}

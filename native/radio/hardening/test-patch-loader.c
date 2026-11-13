/* SPDX-License-Identifier: GPL-2.0-only
 * Actual replacement function, fake transport and synthetic metadata only.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;typedef uint8_t u8;
struct rwnx_hw {unsigned unused;};
static int file_size,block_error,pair_error,blocks,pairs,releases;
static unsigned char *allocation;static unsigned char metadata[128];
#define AICWFDBG(level,...) ((void)0)
#define GFP_KERNEL 0
#define kzalloc(n,flags) calloc(1,n)
#define kfree free
#define printk(...) ((void)0)
static int rwnx_request_firmware_common(struct rwnx_hw *hw,u32 **dst,const char *filename)
{
 (void)hw;(void)filename;
 if(file_size<0){*dst=NULL;return file_size;}
 allocation=malloc(file_size?file_size:1);assert(allocation);
 memset(allocation,0x42,file_size);*dst=(u32*)allocation;
 if(file_size>=128){
  (*dst)[0]=0x187c00;memcpy(metadata,allocation,128);
  for(unsigned i=32;i+1<(unsigned)file_size/4;i+=2){(*dst)[i]=0x180000+i*4;(*dst)[i+1]=i;}
 }
 return file_size;
}
static void rwnx_release_firmware_common(u32 **dst)
{assert((void*)*dst==allocation);free(*dst);*dst=NULL;allocation=NULL;releases++;}
static int rwnx_send_dbg_mem_block_write_req(struct rwnx_hw *hw,u32 address,unsigned bytes,const u32 *data)
{(void)hw;blocks++;assert(bytes==128&&address==0x187c00);assert(!memcmp(data,metadata,128));return block_error;}
static int rwnx_send_dbg_mem_write_req(struct rwnx_hw *hw,u32 address,u32 value)
{(void)hw;unsigned index=32+2*pairs;assert(address==0x180000+index*4&&value==index);pairs++;return pairs==pair_error?-EIO:0;}
#ifndef QC5_LOADER_FUNCTION
#define QC5_LOADER_FUNCTION "patch-table-load.inc"
#endif
#include QC5_LOADER_FUNCTION
static void reset(int bytes)
{assert(!allocation);file_size=bytes;blocks=pairs=releases=block_error=pair_error=0;}
int main(int argc,char **argv)
{
 struct rwnx_hw hw={0};
 if(argc==2){reset(atoi(argv[1]));int rc=aicwf_patch_table_load(&hw,"synthetic");assert(rc==-EINVAL);return 0;}
 /* Every truncation/odd-tail shape, including zero and empty tables. */
 for(int n=0;n<=513;n++){
  reset(n);int valid=n>=128&&!((n-128)%8);
  int rc=aicwf_patch_table_load(&hw,"synthetic");
  assert(rc==(valid?0:-EINVAL));assert(releases==1);
  assert(blocks==valid&&pairs==(valid?(n-128)/8:0));
 }
 reset(-ENOENT);assert(aicwf_patch_table_load(&hw,"missing")==-ENOENT);assert(!blocks&&!pairs&&!releases);
 reset(144);block_error=-EIO;assert(aicwf_patch_table_load(&hw,"header-failure")==-EIO);assert(blocks==1&&!pairs&&releases==1);
 reset(152);pair_error=1;assert(aicwf_patch_table_load(&hw,"first-pair-failure")==-EIO);assert(pairs==1&&releases==1);
 reset(152);pair_error=2;assert(aicwf_patch_table_load(&hw,"second-pair-failure")==-EIO);assert(pairs==2&&releases==1);
 puts("514 file lengths; exact metadata/order; load/header/pair errors and cleanup verified");return 0;
}

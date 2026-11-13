#define _GNU_SOURCE
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int qfd_decode(unsigned char *,size_t,unsigned char *,size_t,void (*)(char *));
static jmp_buf recovery;
static void error(char *s){(void)s;longjmp(recovery,1);}
static unsigned char *readfile(const char *name,size_t *n)
{FILE *f=fopen(name,"rb");assert(f&&!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0&&size<=0x800000&&!fseek(f,0,SEEK_SET));unsigned char *p=malloc(size);assert(p&&fread(p,1,size,f)==size&&!fclose(f));*n=size;return p;}
int main(int argc,char **argv)
{
 assert(argc==3);size_t compressed,expanded;unsigned char *input=readfile(argv[1],&compressed),*expected=readfile(argv[2],&expanded),*memory=malloc(expanded+64);assert(memory);memset(memory,0xa5,expanded+64);
 if(setjmp(recovery))abort();assert(!qfd_decode(input,compressed,memory+32,expanded,error));assert(!memcmp(memory+32,expected,expanded));
 for(unsigned n=0;n<32;n++)assert(memory[n]==0xa5&&memory[expanded+32+n]==0xa5);
 assert(qfd_decode(input,compressed,memory+32,expanded-1,error)==-1);unsigned char prop=input[0];input[0]=0xff;assert(qfd_decode(input,compressed,memory+32,expanded,error)==-1);input[0]=prop;
 if(!setjmp(recovery)){int r=qfd_decode(input,compressed-128,memory+32,expanded,error);assert(r!=0);}
 for(unsigned n=0;n<32;n++)assert(memory[n]==0xa5&&memory[expanded+32+n]==0xa5);
 printf("QC5_FLASH_DECODER_PASS expanded_bytes=%zu exact_output=1 bounded_arena=1 canaries=1 wrong_header_and_truncated_stream_refused=1\n",expanded);
 free(input);free(expected);free(memory);return 0;
}

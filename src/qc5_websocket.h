#ifndef QC5_WEBSOCKET_H
#define QC5_WEBSOCKET_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* SHA-1 here is only RFC6455's public handshake checksum, not authentication. */
static uint32_t qc5_ws_rol(uint32_t v,unsigned n){return (v<<n)|(v>>(32-n));}
static int qc5_ws_accept(const char *key,char out[29]){
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 if(strlen(key)!=24||key[22]!='='||key[23]!='=')return -1;
 for(unsigned i=0;i<22;i++)if(!strchr(alphabet,key[i]))return -1;
 const char *last=strchr(alphabet,key[21]);if(!last||((last-alphabet)&15))return -1;
 uint8_t data[128]={0},digest[20];memcpy(data,key,24);memcpy(data+24,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",36);data[60]=128;data[126]=1;data[127]=224;
 uint32_t h[5]={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
 for(unsigned block=0;block<2;block++){
  uint32_t w[80];for(unsigned i=0;i<16;i++){unsigned p=block*64+i*4;w[i]=(uint32_t)data[p]<<24|(uint32_t)data[p+1]<<16|(uint32_t)data[p+2]<<8|data[p+3];}
  for(unsigned i=16;i<80;i++)w[i]=qc5_ws_rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
  uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
  for(unsigned i=0;i<80;i++){uint32_t f,k;if(i<20){f=(b&c)|(~b&d);k=0x5a827999;}else if(i<40){f=b^c^d;k=0x6ed9eba1;}else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdc;}else{f=b^c^d;k=0xca62c1d6;}uint32_t t=qc5_ws_rol(a,5)+f+e+k+w[i];e=d;d=c;c=qc5_ws_rol(b,30);b=a;a=t;}
  h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
 }
 for(unsigned i=0;i<20;i++)digest[i]=(uint8_t)(h[i/4]>>(24-8*(i%4)));
 unsigned used=0;for(unsigned i=0;i<20;i+=3){unsigned count=20-i;uint32_t v=(uint32_t)digest[i]<<16;if(count>1)v|=(uint32_t)digest[i+1]<<8;if(count>2)v|=digest[i+2];out[used++]=alphabet[v>>18];out[used++]=alphabet[(v>>12)&63];out[used++]=count>1?alphabet[(v>>6)&63]:'=';out[used++]=count>2?alphabet[v&63]:'=';}out[28]=0;return 0;
}
/* One complete, masked browser frame. No fragments/extensions; bounded payload.
 * 0=incomplete, -1=invalid, positive=bytes consumed. */
static int qc5_ws_decode(const uint8_t *p,size_t n,unsigned *opcode,uint8_t *out,size_t cap,size_t *length){
 if(n<2)return 0;
 unsigned op=p[0]&15;if((p[0]&0xf0)!=0x80||!(p[1]&128)||(op!=2&&op!=8&&op!=9&&op!=10))return -1;
 size_t len=p[1]&127,header=2;
 if(len==127)return -1;
 if(len==126){if(n<4)return 0;len=(size_t)p[2]*256+p[3];header=4;if(len<126)return -1;}
 if(len>cap||(op>=8&&len>125))return -1;
 if(n<header+4+len)return 0;
 for(size_t i=0;i<len;i++)out[i]=p[header+4+i]^p[header+i%4];
 *opcode=op;*length=len;return (int)(header+4+len);
}
#endif

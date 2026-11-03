/* SHA-256 implementation shared with the read-only firmware verifier.
 * Caller supplies u8/u32/u64/usize unsigned types. No allocation or I/O. */
#ifndef QC5_SHA256_H
#define QC5_SHA256_H
static u32 be32(const u8 *p) { return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | (u32)p[3]; }
static u32 rr(u32 x, unsigned n) { return (x >> n) | (x << (32-n)); }
typedef struct { u32 h[8]; u64 bytes; u8 block[64]; unsigned used; } Sha;
static const u32 k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static void transform(Sha *s) {
    u32 w[64], a,b,c,d,e,f,g,h,t1,t2; unsigned i;
    for (i=0;i<16;++i) w[i]=be32(s->block+4*i);
    for (;i<64;++i) w[i]=w[i-16]+(rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3))+
                                  w[i-7]+(rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10));
    a=s->h[0];b=s->h[1];c=s->h[2];d=s->h[3];e=s->h[4];f=s->h[5];g=s->h[6];h=s->h[7];
    for (i=0;i<64;++i) {
        t1=h+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
        t2=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;
    s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void init(Sha *s) {
    static const u32 initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned i; for(i=0;i<8;++i) s->h[i]=initial[i]; s->bytes=0;s->used=0;
}
static void add(Sha *s, const u8 *p, usize n) {
    usize i; s->bytes+=n;
    for(i=0;i<n;++i) { s->block[s->used++]=p[i]; if(s->used==64) {transform(s);s->used=0;} }
}
static void finish(Sha *s, u8 *out) {
    u64 bits=s->bytes*8;u8 one=128,zero=0,last[8];unsigned i;
    add(s,&one,1);while(s->used!=56) add(s,&zero,1);
    for(i=0;i<8;++i) last[7-i]=(u8)(bits>>(8*i));
    add(s,last,8);
    for(i=0;i<32;++i) out[i]=(u8)(s->h[i/4]>>(24-8*(i%4)));
}
static void hash(const u8 *p, usize n, u8 *out) { Sha s;init(&s);add(&s,p,n);finish(&s,out); }

#endif

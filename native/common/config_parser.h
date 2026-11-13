#ifndef QC5_CONFIG_PARSER_H
#define QC5_CONFIG_PARSER_H
/* Literal KEY=value; optional matching outer quotes. Never shell evaluation. */
static int qc5_space(char c) { return c==' ' || c=='\t' || c=='\r'; }
int qc5_config_get(const char *buf, unsigned long len, const char *key, char *out, unsigned long cap)
{
    unsigned long i=0,klen=0,n,j,a,z,eq,v,end; int found=0;
    if (!cap) return -1;
    out[0]=0;
    if(len>=3 && (unsigned char)buf[0]==239 && (unsigned char)buf[1]==187 && (unsigned char)buf[2]==191) i=3;
    while (key[klen]) ++klen;
    for (j=0;j<len;j++) if (!buf[j]) return -1;
    while (i<len) {
        a=i; while (i<len && buf[i]!='\n') ++i; z=i; if(i<len) ++i;
        while(a<z && qc5_space(buf[a])) ++a;
        while(z>a && qc5_space(buf[z-1])) --z;
        if(a==z || buf[a]=='#' || buf[a]==';') continue;
        eq=a; while(eq<z && buf[eq]!='=') ++eq;
        if(eq==z) return -1;
        v=eq+1; while(v<z && qc5_space(buf[v])) ++v;
        if(v<z && (buf[v]=='\'' || buf[v]=='"')) {
            if(z-v<2 || buf[z-1]!=buf[v]) return -1;
            ++v; --z;
        }
        end=eq; while(end>a && qc5_space(buf[end-1])) --end;
        if(end-a!=klen) continue;
        for(j=0;j<klen && buf[a+j]==key[j];j++) {}
        if(j!=klen) continue;
        n=z-v; if(n>=cap) return -1;
        for(j=0;j<n;j++) { out[j]=buf[v+j]; }
        out[n]=0; found=1;
    }
    return found;
}
#endif

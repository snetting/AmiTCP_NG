/* rxbench -- raw TCP receive-throughput benchmark for AmiTCP_NG.
 *
 * Connects to the SLIRP host gateway's throughput SOURCE (10.0.2.2:9000, which
 * streams zeros), reads as fast as it can for a fixed wall-clock window, and
 * reports KB/s. This measures the bsdsocket RX path itself -- no SMB/FTP protocol
 * overhead -- i.e. exactly what issue #1 (slow downloads) is about. Logs
 * SYS:rxbench.log. Host/port/seconds overridable via the ReadArgs template.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <utility/tagitem.h>

struct Library *SocketBase;

#define NG_TU                       0x80000000UL
#define IFQ_BASE                    (NG_TU + 1900)
#define IFQ_GetSANA2CopyStats       (IFQ_BASE + 31)
#define IFQ_SANA2RxDMAMode          (IFQ_BASE + 43)
#define IFQ_SANA2RxCopyStats        (IFQ_BASE + 44)

struct SANA2CopyStats {
  ULONG s2cs_DMAIn;
  ULONG s2cs_DMAOut;
  ULONG s2cs_ByteIn;
  ULONG s2cs_ByteOut;
  ULONG s2cs_WordOut;
};
struct SANA2RxCopyStats {
  ULONG contiguous_packets, contiguous_bytes;
  ULONG split_packets, split_bytes;
  ULONG fallbacks, retained_headers;
};

struct sockaddr_in {
  UBYTE  sin_len, sin_family;
  UWORD  sin_port;         /* big-endian on this big-endian host == network order */
  ULONG  sin_addr;
  UBYTE  sin_zero[8];
};

static long v_socket(long d,long t,long p){register long _d0 __asm("d0")=d,_d1 __asm("d1")=t,_d2 __asm("d2")=p;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-30)":"+r"(_d0):"r"(_d0),"r"(_d1),"r"(_d2),"r"(_a6):"a0","a1","memory");return _d0;}
static long v_connect(long s,void*name,long len){register long _d0 __asm("d0")=s,_d1 __asm("d1")=len;
  register void*_a0 __asm("a0")=name; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-54)":"+r"(_d0):"r"(_d0),"r"(_a0),"r"(_d1),"r"(_a6):"d2","a1","memory");return _d0;}
static long v_recv(long s,void*buf,long len,long fl){register long _d0 __asm("d0")=s,_d1 __asm("d1")=len,_d2 __asm("d2")=fl;
  register void*_a0 __asm("a0")=buf; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-78)":"+r"(_d0):"r"(_d0),"r"(_a0),"r"(_d1),"r"(_d2),"r"(_a6):"a1","memory");return _d0;}
static void v_close(long s){register long _d0 __asm("d0")=s;register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-120)":"+r"(_d0):"r"(_d0),"r"(_a6):"d1","a0","a1","memory");}
static long v_errno(void){register long _d0 __asm("d0");register struct Library*_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-162)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory");return _d0;}
static long v_query(void *name, void *tags){register long _d0 __asm("d0");
  register void *_a0 __asm("a0")=name; register void *_a1 __asm("a1")=tags;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-468)":"=r"(_d0),"+r"(_a0),"+r"(_a1):"r"(_a6):"d1","memory");return _d0;}

static void logs(BPTR f,const char*s){long n=0;const char*p=s;while(*p++)n++;if(f)Write(f,(APTR)s,n);}
static void lognum(BPTR f,long v){char b[12];int i=11;unsigned long u=v<0?-(unsigned long)v:v;b[i--]=0;
  do{b[i--]='0'+(u%10);u/=10;}while(u); if(v<0)b[i--]='-'; logs(f,b+i+1);}

/* 1/50 s ticks; monotonic within an hour (fine for a short bench). */
static unsigned long now_ticks(void){ struct DateStamp ds; DateStamp(&ds);
  return (unsigned long)ds.ds_Minute*3000UL + (unsigned long)ds.ds_Tick; }

static void query_copy_stats(ULONG *dma, ULONG *byte, ULONG *mode,
                             struct SANA2RxCopyStats *rx)
{
  struct SANA2CopyStats cs;
  struct TagItem tags[4];
  LONG mode_value = 0;

  cs.s2cs_DMAIn = cs.s2cs_DMAOut = cs.s2cs_ByteIn = 0;
  cs.s2cs_ByteOut = cs.s2cs_WordOut = 0;
  rx->contiguous_packets = rx->contiguous_bytes = 0;
  rx->split_packets = rx->split_bytes = rx->fallbacks = rx->retained_headers = 0;
  tags[0].ti_Tag = IFQ_GetSANA2CopyStats; tags[0].ti_Data = (ULONG)&cs;
  tags[1].ti_Tag = IFQ_SANA2RxDMAMode;    tags[1].ti_Data = (ULONG)&mode_value;
  tags[2].ti_Tag = IFQ_SANA2RxCopyStats;  tags[2].ti_Data = (ULONG)rx;
  tags[3].ti_Tag = TAG_END;               tags[3].ti_Data = 0;
  if (v_query((void *)"bench", tags) != 0) {
    cs.s2cs_DMAIn = cs.s2cs_ByteIn = 0;
    mode_value = 0;
    rx->contiguous_packets = rx->contiguous_bytes = 0;
    rx->split_packets = rx->split_bytes = rx->fallbacks = rx->retained_headers = 0;
  }
  *dma = cs.s2cs_DMAIn;
  *byte = cs.s2cs_ByteIn;
  *mode = (ULONG)mode_value;
}

int main(void){
  static UBYTE buf[32768];
  struct sockaddr_in sa;
  long s, n, secs = 15;
  unsigned long total = 0, t0, tnow, elapsed, kbps;
  ULONG dma0, byte0, dma1, byte1, mode1;
  struct SANA2RxCopyStats rx0, rx1;
  BPTR f = Open((STRPTR)"SYS:rxbench.log", MODE_NEWFILE);

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase){ logs(f,"OpenLibrary FAILED\n"); if(f)Close(f); return 20; }

  s = v_socket(2,1,0);                        /* AF_INET, SOCK_STREAM */
  if (s < 0){ logs(f,"socket() failed errno="); lognum(f,v_errno()); logs(f,"\n"); goto out; }

  sa.sin_len=sizeof(sa); sa.sin_family=2; sa.sin_port=9000;
  sa.sin_addr=0x0A000202UL;                   /* 10.0.2.2 (SLIRP host) */
  { int i; for(i=0;i<8;i++) sa.sin_zero[i]=0; }

  logs(f,"connecting to 10.0.2.2:9000 (throughput source) ...\n");
  if (v_connect(s,&sa,sizeof(sa)) < 0){ logs(f,"connect() failed errno="); lognum(f,v_errno()); logs(f,"\n"); v_close(s); goto out; }
  logs(f,"connected -- receiving for "); lognum(f,secs); logs(f," s ...\n");

  query_copy_stats(&dma0, &byte0, &mode1, &rx0);

  t0 = now_ticks();
  for (;;) {
    tnow = now_ticks();
    elapsed = tnow - t0;
    if (elapsed >= (unsigned long)secs*50UL) break;
    n = v_recv(s, buf, sizeof(buf), 0);
    if (n <= 0){ logs(f,"recv ended (n="); lognum(f,n); logs(f," errno="); lognum(f,v_errno()); logs(f,")\n"); break; }
    total += (unsigned long)n;
  }
  elapsed = now_ticks() - t0;
  if (elapsed == 0) elapsed = 1;
  /* KB/s = (total/1024) / (elapsed/50) = total*50 / (1024*elapsed); do the /1024 first to avoid overflow */
  kbps = (total / 1024UL) * 50UL / elapsed;
  query_copy_stats(&dma1, &byte1, &mode1, &rx1);

  logs(f,"RESULT: received "); lognum(f,(long)total); logs(f," bytes in ");
  lognum(f,(long)elapsed); logs(f," ticks (1/50s) = ");
  lognum(f,(long)kbps); logs(f," KB/s mode="); lognum(f,(long)mode1);
  logs(f," dma_in="); lognum(f,(long)(dma1 - dma0));
  logs(f," byte_in="); lognum(f,(long)(byte1 - byte0)); logs(f,"\n");
  logs(f," contig_packets="); lognum(f,(long)(rx1.contiguous_packets-rx0.contiguous_packets));
  logs(f," contig_bytes="); lognum(f,(long)(rx1.contiguous_bytes-rx0.contiguous_bytes));
  logs(f," split_packets="); lognum(f,(long)(rx1.split_packets-rx0.split_packets));
  logs(f," split_bytes="); lognum(f,(long)(rx1.split_bytes-rx0.split_bytes));
  logs(f," fallbacks="); lognum(f,(long)(rx1.fallbacks-rx0.fallbacks));
  logs(f," retained="); lognum(f,(long)(rx1.retained_headers-rx0.retained_headers)); logs(f,"\n");
  v_close(s);

out:
  CloseLibrary(SocketBase);
  if(f)Close(f);
  return 0;
}

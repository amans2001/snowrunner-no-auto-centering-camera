// Runs the real hook machine code against a simulated SnowRunner camera.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include "../build/hook_blob.h"
static unsigned char *blob;
static float obj[0x200/4+8] __attribute__((aligned(16)));
#define F(off) obj[(off)/4]
float call_hook(float x0){
  float out;
  void *entry = blob+96;
  __asm__ volatile(
    "lea 1f(%%rip),%%rax\n\t mov %%rax,(%[b])\n\t"
    "movss %[x],%%xmm0\n\t push %%rbx\n\t mov %[o],%%rbx\n\t jmp *%[e]\n"
    "1:\n\t pop %%rbx\n\t movss %%xmm0,%[out]\n\t"
    : [out]"=m"(out) : [b]"r"(blob),[x]"m"(x0),[o]"r"(obj),[e]"r"(entry)
    : "rax","xmm0","memory","cc");
  return out;
}
static float wrap(float a){ while(a>M_PI)a-=2*M_PI; while(a<-M_PI)a+=2*M_PI; return a; }
int main(){
  blob = mmap(0,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);
  memcpy(blob,HOOK_BLOB,sizeof(HOOK_BLOB));
  float T=1.0f, off=2.0f, offPrev=2.0f; float R=10;
  // initial eye from yaw = T - off
  float yaw=T-off; F(0x178)=0; F(0x180)=0; F(0x16C)=R*cosf(yaw); F(0x174)=R*sinf(yaw); F(0x1E8)=off;
  float yaw0=yaw; double maxdevTurn=0;
  for(int n=0;n<400;n++){
    float turn = (n>=20 && n<200)? 0.02f : 0.0f;          // truck turns frames 20-200
    float stick= (n>=250 && n<300)? 0.03f : 0.0f;          // right stick frames 250-300
    T += turn;
    float x0 = F(0x1E8) + stick;                           // game: addss xmm0,[rbx+1E8]
    offPrev = F(0x1E8);
    float res = call_hook(x0);
    F(0x1E8)=res;                                          // hook performs the write
    yaw = T - offPrev;                                     // eye built with previous offset
    F(0x16C)=R*cosf(yaw); F(0x174)=R*sinf(yaw);
    if(n<240) { double d=fabs(wrap(yaw-yaw0)); if(n>25 && d>maxdevTurn) maxdevTurn=d; }
    if(n==19||n==100||n==199||n==240||n==275||n==305||n==399)
      printf("frame %3d  camera yaw %.4f  (truck %.3f)\n",n,wrap(yaw),wrap(T));
  }
  printf("max camera drift during truck turn: %.4f rad (%.2f deg)\n",maxdevTurn,maxdevTurn*57.3);
  printf("stick moved camera by %.3f rad (expected -1.500 = 50 frames x 0.03, sign as game)\n", wrap(yaw-yaw0));
  return 0;
}

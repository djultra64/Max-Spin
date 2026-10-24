/* Own work djultra64. Requires recomp_types.h and fenv.h. */
#ifndef MC_GUEST_CPU_H
#define MC_GUEST_CPU_H
extern _Thread_local uint32_t g_itail_site;
typedef struct {uint32_t r[12];int df,top,cmp;uint16_t cw,cc;double fp[8];RecompXmm x[8];RecompMmx m[8];fenv_t host;} Cpu;
static void cpu_save(Cpu*s){
 uint32_t r[]={g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_ebp,g_esp,g_seh_ebp,g_fs_base,g_itail_site,0};memcpy(s->r,r,sizeof r);
 s->df=g_df;s->top=g_fp_top;s->cmp=g_fp_cmp;s->cw=g_fp_control_word;s->cc=g_fp_cc;memcpy(s->fp,g_fp_stack,sizeof s->fp);
 RecompXmm x[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};memcpy(s->x,x,sizeof x);
 RecompMmx m[]={g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7};memcpy(s->m,m,sizeof m);fegetenv(&s->host);
}
static void cpu_restore(const Cpu*s){
 g_eax=s->r[0];g_ecx=s->r[1];g_edx=s->r[2];g_ebx=s->r[3];g_esi=s->r[4];g_edi=s->r[5];g_ebp=s->r[6];g_esp=s->r[7];g_seh_ebp=s->r[8];g_fs_base=s->r[9];g_itail_site=s->r[10];
 g_df=s->df;g_fp_top=s->top;g_fp_cmp=s->cmp;g_fp_control_word=s->cw;g_fp_cc=s->cc;memcpy(g_fp_stack,s->fp,sizeof s->fp);
 g_xmm0=s->x[0];g_xmm1=s->x[1];g_xmm2=s->x[2];g_xmm3=s->x[3];g_xmm4=s->x[4];g_xmm5=s->x[5];g_xmm6=s->x[6];g_xmm7=s->x[7];
 g_mm0=s->m[0];g_mm1=s->m[1];g_mm2=s->m[2];g_mm3=s->m[3];g_mm4=s->m[4];g_mm5=s->m[5];g_mm6=s->m[6];g_mm7=s->m[7];fesetenv(&s->host);
}

#endif

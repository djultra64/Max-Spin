/* Default-off host IO adapter. Own work djultra64. */
#define _POSIX_C_SOURCE 200809L
#include "scheduler_lateness_trace.h"
#include "scheduler_observer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
static char path[4096];
static uint64_t anchor_mono,anchor_elapsed,end_elapsed;
static int configured,written;
static int milliseconds(const char*text,unsigned fallback,unsigned maximum,unsigned*out){
 if(!text){*out=fallback;return 1;}unsigned n=0;if(!*text)return 0;
 for(;*text;text++){if(*text<'0'||*text>'9'||n>(maximum-(unsigned)(*text-'0'))/10)return 0;n=n*10+(unsigned)(*text-'0');}
 if(n>maximum)return 0;
 *out=n;return 1;
}
int topspin_scheduler_lateness_active(void){return configured&&!written;}
int topspin_scheduler_lateness_init(uint64_t elapsed_ns){
 const char*p=getenv("TOPSPIN_SCHEDULER_TRACE");if(!p)return 1;
 unsigned start,duration;
 if(configured||!*p||strlen(p)>=sizeof path||!milliseconds(getenv("TOPSPIN_SCHEDULER_START_MS"),25000,86400000,&start)||!milliseconds(getenv("TOPSPIN_SCHEDULER_DURATION_MS"),2000,10000,&duration)||!duration)return 0;
 struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
 uint64_t now=(uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;
 uint64_t span=((uint64_t)start+duration)*1000000u;
 if(now>UINT64_MAX-span||elapsed_ns>UINT64_MAX-span)return 0;
 if(!w32_sched_observer_configure(now+(uint64_t)start*1000000u,now+span,NULL,NULL))return 0;
 memcpy(path,p,strlen(p)+1);anchor_mono=now;anchor_elapsed=elapsed_ns;end_elapsed=elapsed_ns+span;configured=1;return 1;
}
int topspin_scheduler_lateness_poll(uint64_t elapsed_ns,int force){
 if(!configured||written)return 1;
 W32SchedulerStats stats;w32_sched_observer_copy(NULL,0,&stats);
 if(!force&&elapsed_ns<end_elapsed&&stats.enabled)return 1;
 w32_sched_observer_stop();w32_sched_observer_copy(NULL,0,&stats);
 /* Never copy an in-progress row. Normal poll retries after that service
  * cycle; shutdown may disclose an explicitly omitted pending row. */
 if(stats.inflight&&!force)return 1;
 if(stats.published>W32_SCHEDULER_ROWS)return 0;
 size_t bytes=(size_t)stats.published*sizeof(W32SchedulerRow);
 W32SchedulerRow *rows=bytes?malloc(bytes):NULL;if(bytes&&!rows)return 0;
 if(w32_sched_observer_copy(rows,(size_t)stats.published,NULL)!=stats.published){free(rows);return 0;}
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd<0){free(rows);return 0;}
 if(fchmod(fd,0600)){close(fd);unlink(path);free(rows);return 0;}
 FILE*f=fdopen(fd,"w");if(!f){close(fd);unlink(path);free(rows);return 0;}
 int ok=fprintf(f,"# scheduler_observer=1 anchor_mono_ns=%" PRIu64 " anchor_elapsed_ns=%" PRIu64 " published=%" PRIu64 " dropped=%" PRIu64 " faults=%" PRIu64 " inflight=%u\n",anchor_mono,anchor_elapsed,stats.published,stats.dropped,stats.faults,stats.inflight)>=0;
 if(ok)ok=fputs("sequence,wait_begin_mono,deadline_real,cond_done_mono,cond_done_real,turn_request_mono,turn_acquired_mono,service_begin_mono,service_end_mono,cycle_end_mono,thread_cpu_ns,vblank_sample_mono,vblank_deadline_mono,vblank_period,ptimer_sample_mono,ptimer_count,ptimer_rate,valid,wait_result,left_guest_cpu,vblank_fired,ptimer_alarm,ptimer_num,ptimer_den,ptimer_fired,ptimer_reset,deadline_mono\n",f)>=0;
 for(size_t i=0;ok&&i<stats.published;i++){
  W32SchedulerRow*r=&rows[i];
  ok=fprintf(f,"%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u,%u,%u,%u,%u,%u,%u,%u,%u,%" PRIu64 "\n",r->sequence,r->wait_begin_mono,r->wait_deadline_real,r->cond_done_mono,r->cond_done_real,r->turn_request_mono,r->turn_acquired_mono,r->service_begin_mono,r->service_end_mono,r->cycle_end_mono,r->thread_cpu_ns,r->vblank_sample_mono,r->vblank_deadline_mono,r->vblank_period,r->ptimer_sample_mono,r->ptimer_count,r->ptimer_rate,r->valid,r->wait_result,r->blocked,r->vblank_fired,r->ptimer_alarm,r->ptimer_num,r->ptimer_den,r->ptimer_fired,r->ptimer_reset,r->wait_deadline_mono)>=0;
 }
 free(rows);if(fclose(f))ok=0;if(!ok){unlink(path);return 0;}written=1;return 1;
}

#ifndef QC5_RECORD_POLICY_STATE_H
#define QC5_RECORD_POLICY_STATE_H
#include <stdint.h>
/* No wall clock, allocation, disk or native calls. Counter edges come from
 * the local detector before the vendor's 30-second notification coalescer. */
struct qc5_rec_policy_state {
 int continuous,motion,inhibit,event_active,seeded;
 uint32_t sequence;long long last_motion;
};
static inline void qc5_rec_policy_sample_at(struct qc5_rec_policy_state*p,uint32_t sequence,int valid,long long now,long long pulse,long long post){
 if(valid){
  if(!p->seeded){p->sequence=sequence;p->seeded=1;}
  else if(sequence!=p->sequence){
   p->sequence=sequence;if(pulse>p->last_motion)p->last_motion=pulse;
   if(p->motion&&!p->inhibit&&now>=p->last_motion&&now-p->last_motion<post)p->event_active=1;
  }
  if(p->inhibit&&now>=p->last_motion&&now-p->last_motion>=post)p->inhibit=0;
 }
 if(!p->motion||(p->event_active&&now>=p->last_motion&&now-p->last_motion>=post))p->event_active=0;
}
static inline void qc5_rec_policy_sample(struct qc5_rec_policy_state*p,uint32_t sequence,int valid,long long now,long long post){qc5_rec_policy_sample_at(p,sequence,valid,now,now,post);}
static inline int qc5_rec_policy_wanted(const struct qc5_rec_policy_state*p){return p->continuous||(p->motion&&p->event_active&&!p->inhibit);}
static inline void qc5_rec_policy_stop(struct qc5_rec_policy_state*p,long long now){p->continuous=0;p->event_active=0;p->inhibit=1;p->last_motion=now;}
#endif

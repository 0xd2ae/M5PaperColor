#include "buttons.h"
unsigned pc_buttons_update(pc_buttons_t *s,unsigned down,uint64_t ms)
{
    unsigned actions=0;
    for (int i=0;i<3;i++) {
        bool raw=(down&(1u<<i))!=0;
        if (raw!=s->raw[i]) { s->raw[i]=raw; s->changed[i]=ms; }
        if (raw!=s->stable[i] && ms-s->changed[i]>=30) {
            s->stable[i]=raw;
            if (raw) { s->pressed[i]=ms; s->long_sent[i]=false; }
            else if (!s->long_sent[i] && ms-s->pressed[i]<3000) actions|=1u<<i;
        }
        if (s->stable[i] && !s->long_sent[i] && ms-s->pressed[i]>=3000) {
            s->long_sent[i]=true;
            if (i==2) actions|=PC_ACTION_SETUP;
        }
    }
    return actions;
}
void pc_actions_add(pc_actions_t *p,unsigned action,int64_t us)
{
    if (action&PC_ACTION_PREVIOUS) p->delta--;
    if (action&PC_ACTION_NEXT) p->delta++;
    if (p->delta>1200) p->delta=1200;
    if (p->delta< -1200) p->delta=-1200;
    if (action&PC_ACTION_TODAY) { p->today=true; p->delta=0; }
    if (action&PC_ACTION_SETUP) { p->setup=true; p->delta=0; p->today=false; }
    p->changed=us;
}
bool pc_actions_take(pc_actions_t *p,int64_t us,pc_actions_t *out)
{
    if (!(p->today||p->setup||p->delta) || us-p->changed<800000) return false;
    *out=*p; p->delta=0; p->today=false; p->setup=false;
    return true;
}

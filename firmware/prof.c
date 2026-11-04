/*
 * The simulation's cycle counters (prof.h): only in a build with -DS2_PROF (tools/fw.sh PROF=1).
 */
#ifdef S2_PROF
#include <math.h>
#include "prof.h"
#include "dlog.h"
#undef sinf
#undef cosf
#undef atanf

uint32_t prof_t[PF_N], prof_n[PF_N];

float pf_sinf(float x)
{
    PF_VAR(t);
    float r = sinf(x);
    PF_ADD(PF_SIN, t);
    return r;
}

float pf_cosf(float x)
{
    PF_VAR(t);
    float r = cosf(x);
    PF_ADD(PF_COS, t);
    return r;
}

float pf_atanf(float x)
{
    PF_VAR(t);
    float r = atanf(x);
    PF_ADD(PF_ATAN, t);
    return r;
}

void prof_push(uint32_t cycles)
{
    int k = cycles < 150 ? 0 : cycles < 300 ? 1 : cycles < 600 ? 2 : cycles < 1200 ? 3 : cycles < 2400 ? 4 : 5;
    prof_t[PF_PUSH0 + k] += cycles;
    prof_n[PF_PUSH0 + k]++;
}

/* per frame: the mean cycles of each slot and the runs (the log has 4 KB, so numbers only; the order is the enum's):
   "prof: cycles/runs ..." with runs in tenths */
void prof_log(uint32_t frames)
{
    dlog_str("prof:");
    for (int i = 0; i < PF_N; i++) {
        dlog_char(' ');
        dlog_dec((int32_t)(prof_t[i] / frames));
        if (prof_n[i]) {
            dlog_char('/');
            dlog_dec((int32_t)(prof_n[i] * 10 / frames));
        }
    }
    dlog_char('\n');
}
#endif

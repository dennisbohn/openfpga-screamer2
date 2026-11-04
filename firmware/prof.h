/*
 * Cycle counters for the simulation (tools/fw.sh PROF=1 builds with -DS2_PROF; without it every macro is
 * empty and the firmware is the one without them): where the CPU's cycles of a frame go. Each slot sums
 * the cycles of one stretch of code and counts its runs; the viewer writes the means per frame into
 * the log at the end of a simulated race (prof_log). docs/screamer2/performance.md, section 7.
 */
#pragma once
#include <stdint.h>

enum {
    PF_PLAYER_STEP, PF_DRIVER, PF_TYRES, PF_LOOK, PF_WALL, PF_GROUND, PF_GSTEP, PF_WPIC, PF_ENGINE, PF_STEER, PF_OUTPUTS,
    PF_AI_STEP, PF_COLLIDE, PF_LINE, PF_PLACES, PF_SCENE, PF_SOUND,
    PF_CULL_SETUP, PF_CULL_WALK, PF_CULL_OBJ, PF_CULL_SORT, PF_PC_STATIC, PF_PC_OBJ, PF_PC_MULTI, PF_PUSH,
    PF_N
};

#ifdef S2_PROF
#include "hw.h"
extern uint32_t prof_t[PF_N], prof_n[PF_N];
#define PF_VAR(v)       uint32_t v = rdcycle()
#define PF_SET(v)       ((v) = rdcycle())
#define PF_ADD(id, v)   (prof_t[id] += rdcycle() - (v), prof_n[id]++)
void prof_log(uint32_t frames);
#else
#define PF_VAR(v)
#define PF_SET(v)       ((void)0)
#define PF_ADD(id, v)   ((void)0)
#endif

/*
 * Cycle counters for the simulation (tools/fw.sh PROF=1 builds with -DS2_PROF; without it every macro is
 * empty and the firmware is the one without them): where the CPU's cycles of a frame go. Each slot sums
 * the cycles of one stretch of code and counts its runs; the viewer writes the totals per frame into
 * the log at the end of a simulated race (prof_log, prof.c). docs/screamer2/performance.md, section 7.
 */
#pragma once
#include <stdint.h>

enum {
    PF_PLAYER_STEP, PF_DRIVER, PF_TYRES, PF_LOOK, PF_WALL, PF_GROUND, PF_GSTEP, PF_WPIC, PF_ENGINE, PF_STEER, PF_OUTPUTS,
    PF_AI_STEP, PF_COLLIDE, PF_LINE, PF_PLACES, PF_SCENE, PF_SOUND,
    PF_CULL_SETUP, PF_CULL_WALK, PF_CULL_OBJ, PF_CULL_SORT, PF_PC_STATIC, PF_PC_OBJ, PF_PC_MULTI, PF_PUSH,
    PF_TYRE, PF_ANGLES, PF_CORNERS, PF_FIND_ROAD, PF_SPRINGS, PF_SPEED, PF_LEAN, PF_SIN, PF_COS, PF_ATAN, PF_SQRT,
    PF_PUSH0, PF_PUSH1, PF_PUSH2, PF_PUSH3, PF_PUSH4, PF_PUSH5,     /* the pieces' pushes by the time they took: < 150, 300, 600, 1200, 2400, more */
    PF_PC_LOD, PF_PC_MAT, PF_PC_FACE,       /* a static piece's hand-over before the push: the block and the level of detail, the matrix and its conversions, the row and the faces turned away */
    PF_N
};

#ifdef S2_PROF
#include "hw.h"
#include <math.h>
extern uint32_t prof_t[PF_N], prof_n[PF_N];
#define PF_VAR(v)       uint32_t v = rdcycle()
#define PF_SET(v)       ((v) = rdcycle())
#define PF_ADD(id, v)   (prof_t[id] += rdcycle() - (v), prof_n[id]++)
void prof_log(uint32_t frames);
void prof_push(uint32_t cycles);
/* the library's sine, cosine and arctangent counted (prof.c); in the files that include this header after <math.h> */
float pf_sinf(float x);
float pf_cosf(float x);
float pf_atanf(float x);
#define sinf(x)         pf_sinf(x)
#define cosf(x)         pf_cosf(x)
#define atanf(x)        pf_atanf(x)
#else
#define PF_VAR(v)
#define PF_SET(v)       ((void)0)
#define PF_ADD(id, v)   ((void)0)
#endif

/* after each piece handed to the geometry unit (kind 0: a static piece, 1: a car's block, 2: a block of a mesh with several):
   the PC study's counting build (tools/s2/perf.h) records its counters here */
#ifndef PF_PIECE
#define PF_PIECE(kind)  ((void)0)
#endif

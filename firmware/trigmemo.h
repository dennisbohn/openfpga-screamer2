/*
 * The library's sine and cosine, remembered. Each costs a few hundred cycles (picolibc computes them in double precision,
 * which this CPU does in software), and the car's step and its drawing ask for the same angle again and again: the car's
 * pitch, roll and heading in the corners, the tyres and the outputs, the cosine of the road's slope six times. The last
 * answers kept by the angle's bits are the very same floats, so no number changes. Include after <math.h> (and prof.h):
 * sinf / cosf in the file are then these. The tables are one pair for all the files that include this (weak definitions, the
 * linker keeps one): the physics (s2car.c) and the cars' drawing (car.c) ask for the same headings.
 */
#pragma once
#include <stdint.h>
#include <string.h>

#define TRIG_N 32
struct trig_slot { uint32_t k; float v; };
__attribute__((weak)) struct trig_slot sin_memo[TRIG_N], cos_memo[TRIG_N];

static float memo_sin(float x)
{
    uint32_t k;
    memcpy(&k, &x, sizeof k);
    if(!(k << 1)) return x;                             /* +0 and -0 */
    unsigned h = (k ^ k >> 11 ^ k >> 19) % TRIG_N;
    if(sin_memo[h].k != k) { sin_memo[h].k = k; sin_memo[h].v = sinf(x); }
    return sin_memo[h].v;
}

static float memo_cos(float x)
{
    uint32_t k;
    memcpy(&k, &x, sizeof k);
    if(!(k << 1)) return 1.0f;
    unsigned h = (k ^ k >> 11 ^ k >> 19) % TRIG_N;
    if(cos_memo[h].k != k) { cos_memo[h].k = k; cos_memo[h].v = cosf(x); }
    return cos_memo[h].v;
}

#undef sinf
#undef cosf
#define sinf(x) memo_sin(x)
#define cosf(x) memo_cos(x)

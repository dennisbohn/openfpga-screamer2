/*
 * The firmware's choice of the windows' reflection picture by the kind of the ground (s2_env_phase(), firmware/s2env.c) for every kind 0 .. 255, one line a kind in
 * the text tools/s2/check_refl_s2.py makes from STARTL.EXE's code: "<kind> <phase hex> <rate>". Run by tools/s2/check_refl_s2.sh.
 */
#include <stdio.h>
#include "../../firmware/s2env.h"

int main(void)
{
    for (int kind = 0; kind < 256; kind++) {
        float rate = 0.0f;
        const uint32_t phase = s2_env_phase(kind, &rate);
        printf("%d %x %.4f\n", kind, (unsigned)phase, (double)rate);
    }
    return 0;
}

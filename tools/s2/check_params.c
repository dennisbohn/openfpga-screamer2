/*
 * Host test of the firmware's .DTR reader (firmware/s2params.c), run by tools/s2/check_params.sh: reads
 * DATA/GAME.DAT and DATA/MAPn.DAT of the unzipped game through the firmware's container reader and prints
 * every field of the 20 cars (CAR_PARAMS) and of the 7 tracks (TRACK_DATA) in the text tools/s2/check_params.py
 * prints from its own reading of the same files. The script compares the two.
 *
 * Usage: check_params <game folder>
 */
#include "../../firmware/s2params.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *dir, const char *name, uint32_t *size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if(!f) { fprintf(stderr, "cannot read %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc((size_t)n + 1);
    if(!p || fread(p, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "cannot read %s\n", path); exit(2); }
    fclose(f);
    *size = (uint32_t)n;
    return p;
}

#define G(x) (double)(x)

int main(int argc, char **argv)
{
    if(argc != 2) { fprintf(stderr, "usage: %s <game folder>\n", argv[0]); return 2; }
    static uint8_t scratch[0x8000];
    static struct s2_car_params cars[S2_CARS_TOTAL];
    uint32_t size;
    uint8_t *dat = slurp(argv[1], "DATA/GAME.DAT", &size);
    if(!s2_params_cars_dat(dat, size, scratch, sizeof scratch, cars)) { fprintf(stderr, "CAR_PARAMS not read\n"); return 1; }
    free(dat);
    for(int i = 0; i < S2_CARS_TOTAL; i++) {
        const struct s2_car_params *c = &cars[i];
        printf("car %d.%d speed=%.9g rpm=%.9g auto=%d gears=%d steer=%.9g accel=%.9g grip=%.9g wheelgrip=%.9g,%.9g,%.9g,%.9g "
               "traction=%.9g,%.9g,%.9g,%.9g drittizer=%.9g brake=%.9g susp=%d,%d,%d cam=%d,%d\n",
               i / 5, i % 5, G(c->max_speed), G(c->max_rpm), c->automatic, c->gears, G(c->max_steer), G(c->acceleration),
               G(c->grip), G(c->wheel_grip[0]), G(c->wheel_grip[1]), G(c->wheel_grip[2]), G(c->wheel_grip[3]),
               G(c->traction[0]), G(c->traction[1]), G(c->traction[2]), G(c->traction[3]), G(c->drittizer), G(c->brake),
               c->susp_stiffness, c->susp_height, c->susp_extent, c->cam_along, c->cam_up);
    }
    for(int n = 1; n <= 7; n++) {
        char name[32];
        static struct s2_track_params t;
        snprintf(name, sizeof name, "DATA/MAP%d.DAT", n);
        dat = slurp(argv[1], name, &size);
        if(!s2_params_track_dat(dat, size, n, scratch, sizeof scratch, &t)) { fprintf(stderr, "TRACK_DATA %d not read\n", n); return 1; }
        free(dat);
        printf("track %d far=%d background=%d,%d,%d entry=%d asmoke=%d,%d,%d a2smoke=%d,%d,%d pressure=%d,%d,%d\n", n, t.far_plane,
               t.background[0], t.background[1], t.background[2], t.background_entry, t.asmoke[0], t.asmoke[1], t.asmoke[2],
               t.a2smoke[0], t.a2smoke[1], t.a2smoke[2], t.tyre_pressure[0], t.tyre_pressure[1], t.tyre_pressure[2]);
        for(int k = 0; k < S2_KINDS; k++) {
            const struct s2_road_kind *r = &t.kind[k];
            printf("track %d kind %d id=%d grip=%.9g,%.9g,%.9g rough=%d smoke=%d,%d,%d,%d\n", n, k, r->id, G(r->grip[0]),
                   G(r->grip[1]), G(r->grip[2]), r->rough, r->smoke[0], r->smoke[1], r->smoke[2], r->smoke[3]);
        }
    }
    return 0;
}

/*
 * Host harness for the animated scenery (firmware/s2anim.c), run by tools/s2/check_anim_s2.sh: it loads one track's rows, paths and meshes from the game's folder exactly as the
 * viewer does (the two containers read whole), and for every line "F <clock> <px> <py> <lap>" on the standard input runs one frame (s2anim_frame()), then prints one line a row:
 *     R <row> <shown> <heard> <x> <y> <height> <heading> <climb>
 * (the angles, in 2048ths of a turn, only for a row that is shown and has a path; 0 else). tools/s2/check_anim_s2.py compares those with its own reading of the original's
 * routines (tools/s2/anim_ref.py) and with the running original's memory (tools/s2/dosbox_anim.py).
 *
 * Usage: check_anim_s2 <game folder> <track> < scenario > states
 */
#include "../../firmware/s2anim.h"
#include "../../firmware/car.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* what the drawing would need and the host does not have: the meshes are checked for being read (their radius and class key), the blocks are not made here */
uint32_t track_phase(void) { return 0; }
int track_class_key(const uint8_t *obj) { (void)obj; return 0; }
int car_mesh_draw(const uint8_t *mesh, const struct car_look *lk, const struct track_cam *cam, struct track_block *out)
{
    (void)mesh; (void)lk; (void)cam; (void)out;
    return 0;
}

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

int main(int argc, char **argv)
{
    if(argc < 3) { fprintf(stderr, "usage: check_anim_s2 <game folder> <track>\n"); return 2; }
    int track = atoi(argv[2]);
    char name[64];
    uint32_t gfx_n, data_n;
    snprintf(name, sizeof name, "GFX/MAP%dH01.DAT", track);
    uint8_t *gfx = slurp(argv[1], name, &gfx_n);
    snprintf(name, sizeof name, "DATA/MAP%d.DAT", track);
    uint8_t *data = slurp(argv[1], name, &data_n);
    static uint8_t scratch[65536] __attribute__((aligned(4)));
    int rows = s2anim_load(track, gfx, gfx_n, data, data_n, scratch, sizeof scratch);
    fprintf(stderr, "track %d: %d rows\n", track, rows);
    s2anim_begin();
    char line[256];
    while(fgets(line, sizeof line, stdin)) {
        float clock;
        int px, py, lap;
        if(sscanf(line, "F %f %d %d %d", &clock, &px, &py, &lap) != 4) continue;
        s2anim_frame(clock, px, py, lap);
        printf("F %.6f %d %d %d\n", (double)clock, px, py, lap);
        for(int i = 0; i < rows; i++) {
            const struct s2anim_row *r = s2anim_row(i);
            int heading = 0, climb = 0;
            if(r->shown) s2anim_angles(i, r->part, &heading, &climb);
            printf("R %d %d %d %d %d %d %d %d\n", i, r->shown, r->heard, (int)r->place[0], (int)r->place[1], (int)r->place[2], heading, climb);
        }
    }
    return 0;
}

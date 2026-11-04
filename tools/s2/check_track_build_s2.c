/*
 * Host test of the Screamer 2 track build (firmware/track_build.c, track_build_s2()), run by
 * tools/s2/check_track_build_s2.sh: the image built as the firmware builds it from the containers a track needs
 * (the map, the track's data and the windows' reflection picture) and the grid's cars (each from its team's
 * container, unpacked by s2_car_unpack() as s2_cars_load() does), compared byte for byte with the image
 * tools/s2/make_track_s2.py made from the same files.
 * The output buffer starts out filled with junk, as the SDRAM is. The scratch is the viewer's (SCRATCH_MAX): the
 * build must fit it.
 *
 * Usage: check_track_build_s2 <game folder> <reference image> <n> [grid, "1:1,2:2,3:3,4:4": team:car per slot]
 */
#include "../../firmware/track_build.h"
#include "../../firmware/s2teams.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAP         (7u << 20)      /* room for the image, as the viewer gives it */
#define SCRATCH     0x1C0000u       /* and the scratch: SCRATCH_MEM up to the program (viewer.c) */

/* s2teams.c brings in the ZIP reader, which this test does not use */
uint32_t slot_size(uint32_t id) { (void)id; return 0; }
int slot_read(uint32_t id, uint32_t offset, uint32_t addr, uint32_t len)
{
    (void)id; (void)offset; (void)addr; (void)len;
    return -1;
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
    printf("%s: %u bytes\n", name, *size);
    return p;
}

int main(int argc, char **argv)
{
    if(argc < 4 || argc > 5) { fprintf(stderr, "usage: %s <game folder> <reference image> <n> [grid]\n", argv[0]); return 2; }
    int track = atoi(argv[3]);
    char gfx[32], data[32], env[32];
    snprintf(gfx, sizeof gfx, "GFX/MAP%dH01.DAT", track);
    snprintf(data, sizeof data, "DATA/MAP%d.DAT", track);
    snprintf(env, sizeof env, "GFX/ENVP%d01.DAT", track);
    struct s2_files f;
    f.gfx = slurp(argv[1], gfx, &f.gfx_size);
    f.data = slurp(argv[1], data, &f.data_size);
    f.env = slurp(argv[1], env, &f.env_size);

    /* the grid's cars: each team's container whole, the car's two entries unpacked into a blob of its own */
    static uint32_t blobs[S2_CARS * S2_CAR_ROOM / 4];
    struct s2_car cars[S2_CARS];
    const char *g = argc == 5 ? argv[4] : "1:1,2:2,3:3,4:4";
    f.ncars = 0;
    while(*g && f.ncars < S2_CARS) {
        int team = 0, car = 0, used = 0;
        if(sscanf(g, "%d:%d%n", &team, &car, &used) != 2 || team < 1 || team > 4 || car < 1 || car > 5) {
            fprintf(stderr, "bad grid\n");
            return 2;
        }
        char cont[32];
        snprintf(cont, sizeof cont, "GFX/CARS%d01.DAT", team);
        uint32_t csize;
        const uint8_t *file = slurp(argv[1], cont, &csize);
        struct s2_car *c = &cars[f.ncars];
        if(!s2_car_unpack(file, csize, car, (uint8_t *)blobs + f.ncars * S2_CAR_ROOM, S2_CAR_ROOM, c)) {
            printf("FAIL: team %d car %d is not in %s\n", team, car, cont);
            return 1;
        }
        printf("slot %d: team %d car %d: .MLB %u, .3DX %u bytes\n", f.ncars, team, car, c->mlb_size, c->x3d_size);
        f.ncars++;
        g += used;
        if(*g == ',') g++;
    }
    f.car = cars;
    uint32_t ref_size;
    uint8_t *ref = slurp(".", argv[2], &ref_size);

    uint8_t *out = malloc(CAP), *scratch = malloc(SCRATCH);
    for(uint32_t k = 0; k < CAP; k++) out[k] = (uint8_t)(0xA5 ^ k ^ (k >> 11));
    memset(scratch, 0x5A, SCRATCH);
    struct track_build_info info;
    uint32_t size = track_build_s2(&f, track, out, CAP, scratch, SCRATCH, &info);
    if(!size) { printf("FAIL: track_build_s2 stopped at \"%s\"\n", info.fail); return 1; }
    printf("built: %u bytes, reference: %u bytes, scratch used %u of %u\n", size, ref_size, info.scratch, SCRATCH);

    /* the first difference, named by the reference's section that holds it (the car materials lie in front of the MLB) */
    static const char *const names[] = { "MLB", "UNR", "palette", "placements", "camera path",
        "ground index", "markers", "cars", "car models", "road points", "walls",
        "race screen", "backdrop", "opponents" };
    static const uint32_t at[] = { 0x08, 0x10, 0x18, 0x1C, 0x24, 0x2C, 0x34, 0x3C, 0x44, 0x4C, 0x54,
        0x68, 0x78, 0x80 };
    uint32_t n = size < ref_size ? size : ref_size, differ = 0, first = 0;
    for(uint32_t k = 0; k < n; k++)
        if(out[k] != ref[k] && !differ++) first = k;
    if(differ) {
        const char *in = first < 0x100 ? "header" : "car materials";
        for(int s = 0; s < 14 && first >= 0x100; s++) {
            uint32_t o = ref[at[s]] | ref[at[s] + 1] << 8 | ref[at[s] + 2] << 16 | (uint32_t)ref[at[s] + 3] << 24;
            if(first >= o) in = names[s];
        }
        printf("%u bytes differ, the first at 0x%x (%s): 0x%02x, expected 0x%02x\n", differ, first, in,
               out[first], ref[first]);
    }
    if(differ || size != ref_size) { printf("FAIL\n"); return 1; }
    printf("PASS: byte-identical with the reference\n");
    return 0;
}

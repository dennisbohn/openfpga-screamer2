/*
 * Host test of the cars' loader (firmware/s2teams.c), run by tools/s2/check_cars_s2.sh: s2_cars_load() on a ZIP
 * of the team containers, stored (only the directory and the two entries of each car are read) and deflated (the
 * whole container is inflated), with a stand-in for the data slot read that holds to the APF's rules (as
 * tools/check_zip.c's). The unpacked cars must equal what s2_car_unpack() makes of the container as a file, for
 * the default grid, for another grid (the cars of the changed slots only are read again), for the night look, and
 * a repeated call must read nothing. A wrong grid must be refused. The "SDRAM" is one arena, so the address
 * sanitizer sees every byte past a buffer.
 *
 * Usage: check_cars_s2 <game folder> <stored.zip> <deflated.zip>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../firmware/slot.h"

#define SLOT        3
#define ARENA_SIZE  0x400000u
#define Z_AT        0x20000u        /* struct zip: up to 0x3C000 */
#define ZS_AT       0x40000u        /* zip_open's scratch */
#define STAGE_AT    0x100000u       /* the place for the entries (1 MB) */
#define STAGE_MAX   0x100000u
#define BLOB_AT     0x200000u       /* the four cars */

static uint8_t *arena;
static const uint8_t *zip_mem;
static uint32_t zip_size, n_reads;
static uint64_t n_bytes;

static void die(const char *what)
{
    printf("FAIL: %s\n", what);
    exit(1);
}

static uint32_t arena_addr(const void *p)
{
    if((const uint8_t *)p < arena || (const uint8_t *)p >= arena + ARENA_SIZE) die("a buffer outside the SDRAM");
    return (uint32_t)((const uint8_t *)p - arena);
}
#define ZIP_ADDR(p) arena_addr(p)
#include "../../firmware/zip.c"
#include "../../firmware/s2teams.h"

uint32_t slot_size(uint32_t id) { return id == SLOT ? zip_size : 0; }

int slot_read(uint32_t id, uint32_t off, uint32_t addr, uint32_t len)
{
    if(id != SLOT) die("read of another slot");
    if(off & 3) die("a slot read at an unaligned offset");
    uint64_t end = len == SLOT_REST ? zip_size : (uint64_t)off + len;
    if(len != SLOT_REST && ((len & 3) || len == 0)) die("a slot read of a length that is no multiple of 4");
    if(off >= zip_size || end > zip_size) die("a slot read past the file's end");
    uint64_t n = end - off, padded = (n + 3) & ~3ull;
    if(addr + padded > ARENA_SIZE) die("a slot read outside the SDRAM");
    memcpy(arena + addr, zip_mem + off, n);
    memset(arena + addr + n, 0, padded - n);
    n_reads++;
    n_bytes += n;
    return 0;
}

static uint8_t *slurp(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if(!f) { printf("cannot read %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc((size_t)n + 8);
    if(!p || fread(p, 1, (size_t)n, f) != (size_t)n) { printf("cannot read %s\n", path); exit(2); }
    fclose(f);
    *size = (uint32_t)n;
    return p;
}

static const char *game;

/* the cars in the blobs against the containers as files */
static void compare(const struct s2_car *cars, const struct s2_pick *grid, int n, int look, const char *what)
{
    for(int i = 0; i < n; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/GFX/CARS%d%d1.DAT", game, grid[i].team, look);
        uint32_t size;
        uint8_t *file = slurp(path, &size);
        static uint8_t ref[S2_CAR_ROOM];
        struct s2_car c;
        if(!s2_car_unpack(file, size, grid[i].car, ref, sizeof ref, &c)) die("the container does not unpack as a file");
        if(cars[i].team != grid[i].team || cars[i].car != grid[i].car) die("a car does not say who it is");
        if(cars[i].mlb_size != c.mlb_size || cars[i].x3d_size != c.x3d_size) { printf("%s, slot %d: ", what, i); die("sizes differ"); }
        if(memcmp(cars[i].mlb, c.mlb, c.mlb_size) || memcmp(cars[i].x3d, c.x3d, c.x3d_size)) { printf("%s, slot %d: ", what, i); die("bytes differ"); }
        if((uintptr_t)cars[i].mlb != (uintptr_t)(arena + BLOB_AT + i * S2_CAR_ROOM)) die("a car is not in its blob");
        free(file);
    }
}

/* a race's four cars (RACE_CARS) */
#define GRID    4

static void load(struct zip *z, const struct s2_pick *grid, int look, const char *what, uint32_t expect_min, uint32_t expect_max)
{
    uint32_t bytes = 0, reads0 = n_reads;
    const char *failed = "";
    const struct s2_car *cars = s2_cars_load(z, grid, GRID, look, arena + STAGE_AT, STAGE_MAX, arena + BLOB_AT, &bytes, &failed);
    if(!cars) { printf("%s: stopped at %s\n", what, failed); die("s2_cars_load failed"); }
    compare(cars, grid, GRID, look, what);
    printf("  %-34s %8u bytes, %3u slot reads\n", what, bytes, n_reads - reads0);
    if(bytes < expect_min || bytes > expect_max) { printf("%s: %u bytes read, expected %u to %u\n", what, bytes, expect_min, expect_max); die("wrong amount read"); }
    if(!bytes && n_reads != reads0) die("nothing to read but the slot was read");
}

static void run(const char *path, const char *kind, int stored)
{
    uint32_t size;
    zip_mem = slurp(path, &size);
    zip_size = size;
    memset(arena, 0xA5, ARENA_SIZE);
    struct zip *z = (struct zip *)(arena + Z_AT);
    int r = zip_open(z, SLOT, arena + ZS_AT, ZIP_SCRATCH);
    if(r) { printf("zip_open %d\n", r); die("cannot open the test ZIP"); }
    printf("%s ZIP (%u bytes):\n", kind, size);
    /* a stored ZIP: the container's last 4 KB and the two entries of a car, 53 to 76 KB; a deflated one: the container */
    const uint32_t lo = stored ? 45000 : 350000, hi = stored ? 80000 : 850000;
    struct s2_pick grid[GRID] = { { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 4 } };
    /* (the loader keeps what it has loaded between the runs, but the arena is new: each run starts with the other look) */
    load(z, grid, 1, "night look, default grid", 4 * lo, 4 * hi);
    load(z, grid, 0, "day look, default grid", 4 * lo, 4 * hi);
    load(z, grid, 0, "again, same grid", 0, 0);
    grid[2] = (struct s2_pick){ 4, 5 };
    load(z, grid, 0, "slot 2 changed to team 4 car 5", lo, hi);
    struct s2_pick other[GRID] = { { 3, 5 }, { 1, 2 }, { 4, 1 }, { 3, 5 } };
    load(z, other, 0, "another grid, a car twice", 3 * lo, 4 * hi);
    load(z, other, 1, "the night look", 4 * lo, 4 * hi);
    load(z, grid, 0, "day look, slot 2 changed", 4 * lo, 4 * hi);
    struct s2_pick bad[GRID] = { { 1, 1 }, { 2, 2 }, { 5, 3 }, { 4, 4 } };
    uint32_t bytes = 0;
    const char *failed = "";
    if(s2_cars_load(z, bad, GRID, 0, arena + STAGE_AT, STAGE_MAX, arena + BLOB_AT, &bytes, &failed)) die("team 5 was accepted");
    bad[2] = (struct s2_pick){ 3, 6 };
    if(s2_cars_load(z, bad, GRID, 0, arena + STAGE_AT, STAGE_MAX, arena + BLOB_AT, &bytes, &failed)) die("car 6 was accepted");
    if(s2_cars_load(z, bad, S2_CARS + 1, 0, arena + STAGE_AT, STAGE_MAX, arena + BLOB_AT, &bytes, &failed)) die("six cars were accepted");
    printf("  a wrong grid is refused (\"%s\")\n", failed);
    free((void *)zip_mem);
}

int main(int argc, char **argv)
{
    if(argc != 4) { printf("usage: %s <game folder> <stored.zip> <deflated.zip>\n", argv[0]); return 2; }
    game = argv[1];
    arena = malloc(ARENA_SIZE);
    if(!arena) return 2;
    run(argv[2], "stored", 1);
    run(argv[3], "deflated", 0);
    printf("PASS\n");
    return 0;
}

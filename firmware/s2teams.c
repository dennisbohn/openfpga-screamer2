/*
 * The cars of a race (s2teams.h)
 */
#include "s2teams.h"
#include "dat.h"
#include <string.h>

#define TAIL_MAX    4096u                   /* the end of a container: its directory (21 records of 32 bytes) and size */

/* the two entries of the car, "AUTO<car>.MLB" and ".3DX", in the directory (dir_size bytes: the container's end is enough) */
static int find(const uint8_t *dir, uint32_t dir_size, int car, struct dat_entry d[2])
{
    for(int k = 0; k < 2; k++) {
        char nm[16] = "AUTO1.MLB";
        nm[4] = (char)('0' + car);
        if(k) memcpy(nm + 5, ".3DX", 5);
        if(!dat_find(dir, dir_size, nm, &d[k]) || d[k].size == 0) return 0;
    }
    return 1;
}

/* entry d (stored bytes at file, size bytes of file) unpacked at *to, *room bytes left; the next one 4-aligned after it */
static int put(const uint8_t *file, uint32_t size, const struct dat_entry *d, uint8_t **to, uint32_t *room,
               const uint8_t **at)
{
    if(dat_extract(file, size, d, *to, *room) != d->size) return 0;
    uint32_t n = (d->size + 3) & ~3u;
    *at = *to;
    *to += n;
    *room -= n < *room ? n : *room;
    return 1;
}

int s2_car_unpack(const uint8_t *file, uint32_t size, int car, uint8_t *to, uint32_t room, struct s2_car *c)
{
    struct dat_entry d[2];
    if(!find(file, size, car, d)) return 0;
    c->mlb_size = d[0].size;
    c->x3d_size = d[1].size;
    return put(file, size, &d[0], &to, &room, &c->mlb) && put(file, size, &d[1], &to, &room, &c->x3d);
}

/* one car from its container in the ZIP; stage as s2_cars_load() */
static int load_one(struct zip *z, const struct zip_entry *e, int car, uint8_t *stage, uint32_t stage_max, uint8_t *to,
                    struct s2_car *c, uint32_t *bytes)
{
    struct dat_entry d[2];
    uint32_t room = S2_CAR_ROOM;
    if(e->method != 0) {                                    /* deflated: the whole container, then as a file */
        if(e->size + ZIP_SLACK > stage_max || zip_read(z, e, stage, stage_max)) return 0;
        *bytes += e->size;
        return s2_car_unpack(stage, e->size, car, to, room, c);
    }
    uint32_t tail = e->size < TAIL_MAX ? e->size : TAIL_MAX;
    if(zip_read_range(z, e, e->size - tail, tail, stage, stage_max) || !find(stage, tail, car, d)) return 0;
    *bytes += tail;
    for(int k = 0; k < 2; k++) {                            /* the entry's stored bytes, then unpacked */
        struct dat_entry l = d[k];
        if(l.stored + ZIP_SLACK > stage_max || zip_read_range(z, e, l.offset, l.stored, stage, stage_max)) return 0;
        *bytes += l.stored;
        l.offset = 0;                                       /* the bytes now begin at stage */
        if(!put(stage, l.stored, &l, &to, &room, k ? &c->x3d : &c->mlb)) return 0;
    }
    c->mlb_size = d[0].size;
    c->x3d_size = d[1].size;
    return 1;
}

const struct s2_car *s2_cars_load(struct zip *z, const struct s2_pick *grid, int n, int look, uint8_t *stage,
                                  uint32_t stage_max, uint8_t *blobs, uint32_t *bytes, const char **what)
{
    static struct s2_car cars[S2_CARS];
    static struct s2_pick have[S2_CARS];                    /* what is in the blobs (team 0: nothing) and its look */
    static uint8_t have_look;
    static char name[24];
    if(look < 0 || look > 1 || n < 1 || n > S2_CARS) {
        *what = "the look or the grid";
        return 0;
    }
    if(look != have_look) {                                 /* the other look: nothing of the blobs is right */
        memset(have, 0, sizeof have);
        have_look = (uint8_t)look;
    }
    for(int i = 0; i < n; i++) {
        const struct s2_pick p = grid[i];
        if(p.team < 1 || p.team > 4 || p.car < 1 || p.car > 5) {
            *what = "the grid's cars";
            return 0;
        }
        if(have[i].team == p.team && have[i].car == p.car) continue;
        have[i].team = 0;
        memcpy(name, "GFX/CARS101.DAT", 16);
        name[8] = (char)('0' + p.team);
        name[9] = (char)('0' + look);
        const struct zip_entry *e = zip_find(z, name);
        if(!e || !load_one(z, e, p.car, stage, stage_max, blobs + i * S2_CAR_ROOM, &cars[i], bytes)) {
            *what = name;
            return 0;
        }
        have[i] = p;
        cars[i].team = p.team;
        cars[i].car = p.car;
    }
    return cars;
}

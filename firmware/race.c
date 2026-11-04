/*
 * The race of the driving mode (race.h): the start. The player and three computer cars on the game's grid, held in neutral
 * for 3 s (the start lights), then let go. The record words (F / I) are the game's car record, which s2car.c keeps word for
 * word.
 */
#include "race.h"
#include <string.h>

#define DT          0.02f

union word { int32_t i; uint32_t u; float f; };
#define F(c, o) (((union word *)(c)->rec)[(o) >> 2].f)
#define I(c, o) (((union word *)(c)->rec)[(o) >> 2].i)

static struct {
    int track, n, running;
    float clock;                                        /* the race's clock (0x13335c) */
    uint32_t steps;
    float floor_z;
    struct s2car_state car[RACE_CARS];
} R;

struct s2car_state *race_car(int i) { return &R.car[i]; }
int race_cars(void) { return R.n; }
uint32_t race_steps(void) { return R.steps; }
uint32_t race_respawns(void) { return 0; }
int race_line_length(void) { return 0; }

int race_init(int track, float floor_z)
{
    if(track < 1 || track > 7) return 0;
    R.n = 0;
    R.track = track;
    R.floor_z = floor_z;
    R.running = 0;
    R.clock = 0.0f;
    R.steps = 0;
    for(int i = 0; i < RACE_CARS; i++) {
        const int grid = RACE_CARS - 1 - i;             /* the player starts last; the player's team and car, then the next ones in turn */
        if(!s2car_init_grid(&R.car[i], i, i, track, grid, i > 0)) return 0;
        I(&R.car[i], 0) = i;
    }
    R.n = RACE_CARS;
    return 1;
}

void race_step_player(const struct s2car_input *in)
{
    R.clock += DT;
    if(!R.running && R.clock > 3.0f) {                  /* the lights are out: the cars are let go */
        R.running = 1;
        for(int i = 0; i < R.n; i++) R.car[i].hold = 0;
    }
    s2car_step(&R.car[0], in);
    R.steps++;
}

/* the computers' cars have no driver yet: they stand */
void race_step_others(uint32_t count)
{
    static const struct s2car_input none;
    for(uint32_t step = 0; step < count; step++)
        for(int i = 1; i < R.n; i++) s2car_step(&R.car[i], &none);
}

void race_step(const struct s2car_input *in)
{
    race_step_player(in);
    race_step_others(1);
}

void race_get_hud(struct race_hud *h)
{
    h->count = R.clock < 4.0f ? 3 - (int)R.clock : -1;
    h->laps = RACE_LAPS;
    h->lap = 1;
    h->place = RACE_CARS;
    h->cars = R.n;
    h->finished = 0;
    h->time = R.running ? R.clock - 3.0f : 0.0f;
    h->lap_time = h->time;
    h->best_lap = -1.0f;
}

/*
 * A fingerprint of the normal (arcade) race and of the replay's cameras, for tools/s2/check_attract_s2.sh: the same source is built against two trees of firmware/ (the base
 * commit's and the working tree's), run, and the two outputs must be identical. On each of the seven tracks an arcade race (the player a pilot with swerves, the gearbox
 * switched, the handbrake and the brake now and then) runs 3,000 steps in frames of one to four steps and every car's whole record goes into a running hash, printed every 500
 * steps; the trackside cameras of the replay (CAMERAS<n>.MCM, the car followed changed now and then by the keys' call) run over the race, their place and angles into a second
 * hash. -DNEG=1 (the working tree only) sets race_attract first: the output must then differ, which shows that the comparison sees a change of the race.
 * It uses nothing of the attract race but that one line.
 */
#define main check_ghost_main
#include "check_ghost_s2.c"
#undef main
#include "s2cam.h"

static uint32_t hash_race(uint32_t h)
{
    for (int i = 0; i < RACE_CARS; i++) {
        const struct s2car_state *c = race_car(i);
        for (unsigned k = 0; k < sizeof c->rec / 4; k++) {
            h ^= c->rec[k];
            h *= 16777619u;
        }
    }
    return h;
}

static int image(const char *dir, int track)
{
    char path[512];
    snprintf(path, sizeof path, "%s/car%d.bin", dir, track);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }
    size_t n = fread(TEX_FILE, 1, 0x700000, f);
    fclose(f);
    return n > 1000 && track_open(TEX_FILE) && track_init() && road_build() > 0;
}

int main(int argc, char **argv)
{
    map();
    s2car_damages = 0;
    if (argc < 2) return 2;
    static const int plan[8] = { 1, 1, 2, 1, 3, 1, 4, 1 };
    for (int track = 1; track <= 7; track++) {
        if (!image(argv[1], track)) return 2;
#ifdef NEG
        race_attract = 1;
#endif
        race_kind = RACE_NORMAL;
        race_ghost = 0;
        race_replay = 0;
        race_laps = 3;
        race_level = 1;
        for (int i = 0; i < RACE_CARS; i++) race_grid[i] = (struct s2_pick){ (uint8_t)(i + 1), (uint8_t)(i + 1) };
        struct pilot pl = { -1e9f, 85.0f, PILOT_SIGN };
        race_car(0)->manual = 0;
        if (!race_init(track, road_floor() - 3000.0f)) return 2;
        s2cam_begin(track, S2CAM_TRACKSIDE);
        rng_state = 31337 + (uint32_t)track;
        uint32_t h = 2166136261u, hc = 2166136261u, k = 0, next = 500;
        const struct s2car_state *cars[RACE_CARS];
        for (int i = 0; i < RACE_CARS; i++) cars[i] = race_car(i);
        printf("track %d", track);
        for (int f = 0; k < 3000; f++) {
            const int n = plan[f % 8];
            for (int b = 0; b < n; b++, k++) {
                struct s2car_input in = pilot_input(&pl, race_car(0), 0);
                const uint32_t r = rnd();
                if (r % 400 == 0) race_car(0)->manual = !race_car(0)->manual;
                if ((r >> 10) % 300 == 0) in.handbrake = 1;
                if ((r >> 14) % 500 == 0) in.brake = 1;
                if (k % 700 > 600) in.steer = (float)(int)((r >> 16) % 3) - 1.0f;
                if (race_car(0)->manual) in.gear_up = (r >> 4) % 90 == 0, in.gear_down = (r >> 7) % 120 == 0;
                race_step_player(&in);
                race_step_others(1);
            }
            if (k % 400 < 4) s2cam_next_car(RACE_CARS);                 /* the key's call (F11) */
            struct track_cam cam;
            s2cam_frame(cars, RACE_CARS, (float)n * 0.02f, &cam);
            const float v[6] = { cam.x, cam.y, cam.h, cam.yaw, cam.pitch, cam.roll };
            for (int q = 0; q < 6; q++) {
                uint32_t u;
                memcpy(&u, &v[q], 4);
                hc ^= u;
                hc *= 16777619u;
            }
            h = hash_race(h);
            if (k >= next) {
                printf(" %08x/%08x", h, hc);
                next += 500;
            }
        }
        printf(" end %08x/%08x\n", h, hc);
    }
    return 0;
}

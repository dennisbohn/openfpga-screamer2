/*
 * The cameras of the CAR page and the CAR CUSTOMIZATION page on the PC (firmware/s2garage.c): included by ui_screen.c after room_screen.c (whose room_mem the rooms are built in). What is
 * tested is the camera frame by frame against the model of tools/ui_garage_ref.py (a second reading of the rules in Python, in double precision as the x87 works, from the program's
 * code): the places and aims the firmware reads from DATA/GEN.DAT's P_<t>.MCM of the real ZIP for the four teams (the three records), and scripted visits of two teams' rooms: the CAR
 * page in its states with the car changed and with the sway already turned, the CAR CUSTOMIZATION page with the cursor on every item: every frame's angles, place, sway and the five
 * shadows' places equal. The camera of the model was compared with the running original (tools/garage.sh, tools/s2/dosbox_garage.py); a negative control: a sway that is 0.0005 off is told
 * from the model. The pictures are the room's and the cars' (tools/garage.sh compares them with the original's screenshots), the pages' 2D parts those of custom_screen.c.
 */
#include "../s2garage.h"

static int garage_frame_ok(const char *line, int car_page, int shadows)
{
    int state, choose, a0, a1, x, y, z, sh[15];
    long long sway;
    if (sscanf(line, "f %d %d %d %d %d %d %d %lld %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", &state, &choose, &a0, &a1, &x, &y, &z, &sway, &sh[0], &sh[1], &sh[2], &sh[3], &sh[4], &sh[5],
               &sh[6], &sh[7], &sh[8], &sh[9], &sh[10], &sh[11], &sh[12], &sh[13], &sh[14]) != 23)
        return -1;
    if (car_page) {
        if (choose >= 0) s2garage_car_choose(choose);
        int ok = 1;
        for (int i = 0; i < 5 && shadows; i++) {
            int32_t at[3];
            s2garage_shadow(i, at);                                         /* (from the camera as it stands before the step) */
            ok &= at[0] == sh[3 * i] && at[1] == sh[3 * i + 1] && at[2] == sh[3 * i + 2];
        }
        s2garage_car_step(state != 0);
        const struct s2garage_cam *c = s2garage_camera();
        return ok && c->a0 == a0 && c->a1 == a1 && c->pos[0] == x && c->pos[1] == y && c->pos[2] == z && (double)c->sway * 1e9 - (double)sway < 1.5 && (double)sway - (double)c->sway * 1e9 < 1.5;
    }
    s2garage_cust_step(state);
    const struct s2garage_cam *c = s2garage_camera();
    return c->a0 == a0 && c->a1 == a1 && c->pos[0] == x && c->pos[1] == y && c->pos[2] == z;
}

static void check_garage(const char *ref, const char *prefix)
{
    (void)prefix;
    char path[600];
    snprintf(path, sizeof path, "%s/garage_ref.txt", ref);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "the reference %s", path);
    if (!f) return;
    static char line[2400];
    int mcm_ok = 0, runs = 0, frames = 0;
    long at = 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "mcm ", 4)) {
            fseek(f, at, SEEK_SET);
            break;
        }
        at = ftell(f);
        char *p = line + 4;
        const int team = (int)strtol(p, &p, 10);
        s2room_forget();
        CHECK(s2room_load(s2gfx_zip(), &room_mem, team), "the room of team %d for the garage's places", team);
        int same = 1;
        for (int r = 0; r < S2ROOM_RECORDS; r++) {
            const int n = (int)strtol(p, &p, 10);
            same &= s2room_record_count(r) == n;
            int v[2][15][3];
            for (int a = 0; a < 2; a++)
                for (int k = 0; k < n; k++)
                    for (int i = 0; i < 3; i++) v[a][k][i] = (int)strtol(p, &p, 10);
            for (int a = 0; a < 2 && same; a++)
                for (int k = 0; k < n; k++)
                    for (int i = 0; i < 3; i++) same &= s2room_record(r, k, a)[i] == v[a][k][i];
        }
        CHECK(same, "the three records of team %d's P_%d.MCM (DATA/GEN.DAT) are the reference's", team, team);
        mcm_ok += same;
    }
    CHECK(mcm_ok == 4, "the places of the four teams' rooms: %d of 4", mcm_ok);
    int team = 0, chosen = 0, car_page = 0, bad_runs = 0;
    char name[64], kind[8];
    static char frames_text[700][400];
    int n = 0, pending = 0;
    for (;;) {
        const int more = fgets(line, sizeof line, f) != 0;
        if (pending && (!more || !strncmp(line, "run ", 4))) {
            s2room_forget();
            CHECK(s2room_load(s2gfx_zip(), &room_mem, team), "the room of team %d", team);
            struct s2_menu_cfg c = { .team = team };
            (void)c;
            if (car_page) {
                s2garage_car_begin(chosen);
                struct s2garage_cam cam = *s2garage_camera();
                cam.sway = !strcmp(name, "car_again") ? 0.3125f : 0.0f;                         /* (the sway outlives a visit: the model's runs say which) */
                s2garage_set(chosen, &cam);
            } else {
                s2garage_cust_begin(chosen);
            }
            int bad = 0;
            for (int t = 0; t < n; t++) {
                const int ok = garage_frame_ok(frames_text[t], car_page, car_page);
                if (ok != 1 && bad++ < 3) {
                    const struct s2garage_cam *c = s2garage_camera();
                    printf("FAIL: garage run %s team %d frame %d: the firmware's camera (a0 %d a1 %d, %d %d %d) is not the model's (%s)\n", name, team, t, c->a0, c->a1, c->pos[0], c->pos[1], c->pos[2],
                           frames_text[t]);
                }
                frames += ok == 1;
            }
            bad_runs += bad > 0;
            failures += bad > 0;
            runs++;
            pending = 0;
        }
        if (!more) break;
        if (!strncmp(line, "run ", 4)) {
            if (sscanf(line, "run %63s %7s %d %d", name, kind, &team, &chosen) != 4) {
                CHECK(0, "garage_ref.txt: line not understood: %s", line);
                continue;
            }
            car_page = !strcmp(kind, "car");
            n = 0;
            pending = 1;
        } else if (pending && line[0] == 'f' && n < 700) {
            snprintf(frames_text[n++], sizeof frames_text[0], "%s", line);
        }
    }
    fclose(f);
    /* a negative control: the cars' state's camera with the sway 0.0005 off is not the model's */
    s2room_forget();
    CHECK(s2room_load(s2gfx_zip(), &room_mem, 1), "the room of team 1");
    s2garage_car_begin(1);
    struct s2garage_cam off = *s2garage_camera();
    off.sway = 0.0005f;
    s2garage_set(1, &off);
    int differ = 0;
    for (int t = 0; t < 100; t++) {                                                         /* (the model's first visit, its first 100 frames) */
        s2garage_car_step(0);
    }
    snprintf(path, sizeof path, "%s/garage_ref.txt", ref);
    f = fopen(path, "r");
    int seen = 0;
    while (f && fgets(line, sizeof line, f) && seen < 101)
        if (!strncmp(line, "run car_cars car 1", 18)) seen = 1;
        else if (seen && line[0] == 'f' && ++seen == 101) {
            int state, choose, a0, a1, x, y, z;
            sscanf(line, "f %d %d %d %d %d %d %d", &state, &choose, &a0, &a1, &x, &y, &z);
            const struct s2garage_cam *c = s2garage_camera();
            differ = c->a0 != a0 || c->a1 != a1 || c->pos[0] != x || c->pos[1] != y || c->pos[2] != z;
        }
    if (f) fclose(f);
    CHECK(differ, "the check tells a sway 0.0005 off from the model");
    printf("ui: the garage: %d camera runs of the CAR page and the CAR CUSTOMIZATION page (%d frames) equal to the model frame by frame, the places of four teams' P_t.MCM, the shadows; a negative control\n", runs - bad_runs, frames);
}

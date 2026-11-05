/*
 * The options room on the PC (firmware/s2room.c, the MULTIPLAYER MENU of s2pages.c, the menu's use of them in s2menu.c): included by ui_screen.c, which has the stand-ins of the
 * hardware (the swap, the scripted buttons, the sounds) and pages3d_screen.c's geometry unit model. What is tested here is the page with the real files: the team's room built from
 * MENU<t>1.DAT of the real ZIP (the stations read from P_<t>.MCM against the game's own file as tools/ui_room_ref.py reads it), the texts, and ten scripted visits: every frame's station,
 * camera place and angles equal to the model of tools/ui_room_ref.py (a second reading of the rules, in Python, checked against the running original's memory by
 * tools/s2/dosbox_arcade.py --options), the result, the frames the page took, the sounds; the MULTIPLAYER MENU's keys and message; and the menu on the room
 * (a stand-in room that answers what the test says): the choices, ESC to TEAM SELECTION, a room that cannot be shown. The pictures were compared with the running
 * original's screenshots (tools/room.sh): the 3D room agrees at the outline and not pixel for pixel, the texts and arrows and the MULTIPLAYER MENU and GAME SETTINGS pages do.
 */
#include "../s2room.h"
#include "../s2pages.h"

/* what the page's camera was in every frame of a run: filled by the swap's stand-in (frame_probe, ui_screen.c) */
static struct { int station, x, y, z, a0, a1; } room_rec[600];
static int room_nrec;

static void room_probe(void)
{
    if (calls < 1 || calls > 600) return;
    int at;
    const struct s2room_cam *c = s2room_camera(&at);
    room_rec[calls - 1] = (typeof(room_rec[0])){ at, c->pos[0], c->pos[1], c->pos[2], c->a0, c->a1 };
    room_nrec = (int)calls;
}

static const struct s2room_mem room_mem = { (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2800000), 0x400000, (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000), 0x700000,
                                            (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2F00000), 0x100000 };

/* the room of a team, kept built (a visit that has to build it first would use a frame of the script for the black screen) */
static int room_visit(const char *keys, const char *prefix, int *frames)
{
    char script[640];
    put_keys(script, sizeof script, keys);
    begin_run(script, prefix);
    front = 0;
    room_nrec = 0;
    frame_probe = room_probe;
    const int r = s2room_run(s2gfx_zip(), &room_mem, 1);
    frame_probe = 0;
    room_probe();                                               /* (the last frame's state: the run's end leaves no further swap) */
    *frames = (int)calls;
    return r;
}

/* the station the room is left at: the player moves to station 0 and then k stations on, the visit ending with ESC */
static void room_goto(int k, const char *prefix)
{
    char spec[160] = "", item[16];
    int frames, at = 3;
    for (int i = 0; i < 5; i++, at += 4) { snprintf(item, sizeof item, "%d:L ", at); strcat(spec, item); }
    for (int i = 0; i < k; i++, at += 4) { snprintf(item, sizeof item, "%d:R ", at); strcat(spec, item); }
    snprintf(item, sizeof item, "%d:A", at + 3);
    strcat(spec, item);
    room_visit(spec, prefix, &frames);
}

static void check_room(const char *ref, const char *prefix)
{
    s2save_reset();
    s2save_set(S2S_PROFILE, 1);
    char path[600];
    snprintf(path, sizeof path, "%s/room_ref.txt", ref);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "the reference %s", path);
    if (!f) return;
    /* the room of each team built from the real ZIP, its five stations as the reference reads P_<t>.MCM of the game's own container */
    char line[1200];
    int stations_ok = 0;
    for (long at = ftell(f); fgets(line, sizeof line, f); at = ftell(f)) {
        if (strncmp(line, "mcm ", 4)) {                         /* (the first line of the scripts: read again below) */
            fseek(f, at, SEEK_SET);
            break;
        }
        int team, v[30], n = 0;
        char *p = line + 4;
        team = (int)strtol(p, &p, 10);
        for (; n < 30; n++) v[n] = (int)strtol(p, &p, 10);
        s2room_forget();
        CHECK(s2room_load(s2gfx_zip(), &room_mem, team), "the room of team %d is built from MENU%d1.DAT", team, team);
        int same = 1;
        for (int k = 0; k < 5; k++)
            for (int a = 0; a < 2; a++)
                for (int i = 0; i < 3; i++) same &= s2room_station(k, a)[i] == v[(a * 5 + k) * 3 + i];
        CHECK(same, "the stations of team %d's P_%d.MCM are the reference's", team, team);
        stations_ok += same;
    }
    CHECK(stations_ok == 4, "the stations of the four teams' rooms: %d of 4", stations_ok);
    s2room_forget();
    CHECK(s2room_load(s2gfx_zip(), &room_mem, 1), "the room of team 1");
    CHECK(s2room_load(s2gfx_zip(), &room_mem, 1), "the same room again is not built again");
    /* the texts: STRINGS.TXT's lines as the room asks for them */
    CHECK(!strcmp(s2gfx_line("OPTIONS ROOM", 0, "?"), "OPTIONS ROOM"), "the title");
    static const char *const names[5] = { "ARCADE", "CHAMPIONSHIP", "TIME ATTACK", "MULTIPLAYER", "GAME SETTINGS" };
    for (int k = 0; k < 5; k++) CHECK(!strcmp(s2gfx_line("MAIN MENU", 1 + k, "?"), names[k]), "the name of station %d: %s", k, s2gfx_line("MAIN MENU", 1 + k, "?"));

    /* the scripts: the page against the model, frame by frame */
    int runs = 0, frames_checked = 0;
    char name[40], spec[400];
    int start, expect = 0, result = 0, calls_expected = 0, nclick_expected = 0, clicks_expected[8];
    static int t_station[600], t_x[600], t_y[600], t_z[600], t_a0[600], t_a1[600];
    int nt = 0, pending = 0;
    for (;;) {
        const int more = fgets(line, sizeof line, f) != 0;
        if (pending && (!more || !strncmp(line, "run ", 4))) {                    /* the script before is complete: run it */
            room_goto(start, prefix);
            int frames, r = room_visit(spec, prefix, &frames);
            CHECK(r == result && frames == calls_expected, "room script %s: result %d after %d frames (expected %d after %d)", name, r, frames, result, calls_expected);
            CHECK(nclicks == nclick_expected, "room script %s: %d sounds (expected %d)", name, nclicks, nclick_expected);
            for (int i = 0; i < nclicks && i < nclick_expected; i++) CHECK(clicks[i] == clicks_expected[i], "room script %s: sound %d is %d (expected %d)", name, i, clicks[i], clicks_expected[i]);
            int bad = 0;
            for (int t = 0; t < nt && t < room_nrec; t++) {
                const int same = room_rec[t].station == t_station[t] && room_rec[t].x == t_x[t] && room_rec[t].y == t_y[t] && room_rec[t].z == t_z[t] && room_rec[t].a0 == t_a0[t] &&
                                 room_rec[t].a1 == t_a1[t];
                if (!same && bad++ < 3)
                    printf("FAIL: room script %s frame %d: station %d (%d, %d, %d) a0 %d a1 %d, the model has %d (%d, %d, %d) %d %d\n", name, t, room_rec[t].station, room_rec[t].x, room_rec[t].y,
                           room_rec[t].z, room_rec[t].a0, room_rec[t].a1, t_station[t], t_x[t], t_y[t], t_z[t], t_a0[t], t_a1[t]);
                frames_checked += same;
            }
            failures += bad > 0;
            CHECK(room_nrec == nt, "room script %s: %d frames drawn, the model has %d", name, room_nrec, nt);
            runs++;
            pending = 0;
            nt = 0;
        }
        if (!more) break;
        if (!strncmp(line, "run ", 4)) {
            sscanf(line, "run %39s %d %399[^\n]", name, &start, spec);
            pending = 1;
        } else if (!strncmp(line, "expect ", 7)) {
            char *p = line + 7;
            result = (int)strtol(p, &p, 10);
            calls_expected = (int)strtol(p, &p, 10);
            nclick_expected = 0;
            for (;;) {
                char *q;
                const long c = strtol(p, &q, 10);
                if (q == p) break;
                clicks_expected[nclick_expected++ & 7] = (int)c;
                p = q;
            }
            (void)expect;
        } else if (!strncmp(line, "t ", 2)) {
            int tt;
            sscanf(line, "t %d %d %d %d %d %d %d", &tt, &t_station[nt], &t_x[nt], &t_y[nt], &t_z[nt], &t_a0[nt], &t_a1[nt]);
            nt++;
        }
    }
    fclose(f);
    CHECK(runs == 10 && stations_ok == 4, "ten scripted visits of the room (%d), the stations of the four teams' rooms: %d of 4", runs, stations_ok);

    /* the MULTIPLAYER MENU: DOWN to NETWORK GAME, ENTER: its message under the items (5 s: 250 frames), then gone; ESC leaves at once with a cut */
    char script[640];
    static uint8_t shot_a[S2G_PIC_BYTES], shot_b[S2G_PIC_BYTES], shot_c[S2G_PIC_BYTES];
    put_keys(script, sizeof script, "4:D 8:B 300:A");
    begin_run(script, prefix);
    front = 0;
    cap_at(2, 0);                                               /* before: the three items, the cursor's blinking */
    cap_at(30, 1);                                              /* with the message */
    cap_at(290, 2);                                             /* and after it */
    s2_page_multi();
    memcpy(shot_a, cap[0], S2G_PIC_BYTES);
    memcpy(shot_b, cap[1], S2G_PIC_BYTES);
    memcpy(shot_c, cap[2], S2G_PIC_BYTES);
    CHECK(calls == 301 && nclicks == 0, "MULTIPLAYER MENU: ESC at frame 300 leaves after %u frames (a cut), %d sounds", calls, nclicks);
    int msg = 0, msg_after = 0, same_rest = 1;
    for (int y = 176; y < 196; y++)
        for (int x = 0; x < 320; x++) {
            msg += shot_b[y * 320 + x] != shot_a[y * 320 + x];
            msg_after += shot_c[y * 320 + x] != shot_a[y * 320 + x];
        }
    for (int i = 0; i < 176 * 320; i++) same_rest &= shot_b[i] == shot_c[i] || (i / 320 >= 60 && i / 320 < 135);      /* (the items' rows blink) */
    CHECK(msg > 300 && msg_after == 0 && same_rest, "MULTIPLAYER MENU: the message is up for the 250 frames (%d pixels), then gone (%d), the rest alike", msg, msg_after);
    put_keys(script, sizeof script, "3:B 5:A");
    begin_run(script, prefix);
    front = 0;
    s2_page_multi();                                            /* the cursor starts on COMBAT MODE: ENTER answers NOT AVAILABLE, ESC leaves */
    CHECK(calls == 6, "MULTIPLAYER MENU: ENTER on COMBAT MODE and ESC (%u frames)", calls);

    /* the menu on the room: a stand-in room answers what the test says */
    int (*saved)(const struct s2_menu_cfg *) = s2menu_room;
    struct s2_menu_cfg c = { .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1 };
    s2menu_room = room_stand_in;
    s2menu_load(&c);
    /* the MULTIPLAYER station runs its page (ESC), and the room comes again; CHAMPIONSHIP ends the menu with it */
    answers[0] = S2ROOM_MULTI;
    answers[1] = S2ROOM_CHAMP;
    answered = asked = 0;
    put_keys(script, sizeof script, "2:A");
    begin_run(script, prefix);
    front = 0;
    int r = s2menu_run(&c);
    CHECK(r == S2M_CHAMP && asked == 2, "menu on the room: the MULTIPLAYER page, CHAMPIONSHIP: result %d after %d visits", r, asked);
    answers[0] = S2ROOM_ATTACK;
    answered = asked = 0;
    r = s2menu_run(&c);
    CHECK(r == S2M_ATTACK && asked == 1, "menu on the room: TIME ATTACK (%d)", r);
    /* GAME SETTINGS runs the page (ESC), ESC in the room is TEAM SELECTION (ESC asks EXIT, ENTER on YES leaves it: the room again, as the core is left from the Pocket's menu) */
    answers[0] = S2ROOM_SETTINGS;
    answers[1] = S2ROOM_BACK;
    answers[2] = S2ROOM_ATTACK;
    answered = asked = 0;
    put_keys(script, sizeof script, "3:A 20:A 25:B");
    begin_run(script, prefix);
    front = 0;
    r = s2menu_run(&c);
    CHECK(r == S2M_ATTACK && asked == 3, "menu on the room: GAME SETTINGS and back, ESC to TEAM SELECTION and YES, TIME ATTACK: result %d after %d visits", r, asked);
    /* the name comes after the team: ESC in the room, ENTER, ENTER (the name is kept), the room again */
    answers[0] = S2ROOM_BACK;
    answers[1] = S2ROOM_CHAMP;
    answered = asked = 0;
    put_keys(script, sizeof script, "4:B 40:B");
    begin_run(script, prefix);
    front = 0;
    r = s2menu_run(&c);
    CHECK(r == S2M_CHAMP && asked == 2 && s2save_get(S2S_PROFILE) == 1, "menu on the room: ESC to TEAM SELECTION, the name, the room again (%d after %d visits)", r, asked);
    /* a room that cannot be shown leaves the arcade pages instead (ESC on the title is back), and the room is asked again */
    answers[0] = S2ROOM_FAIL;
    answers[1] = S2ROOM_CHAMP;
    answered = asked = 0;
    put_keys(script, sizeof script, "3:A");
    begin_run(script, prefix);
    front = 0;
    r = s2menu_run(&c);
    CHECK(r == S2M_CHAMP && asked == 2, "menu: the room could not be shown, the arcade pages (ESC), the room again (%d, %d visits)", r, asked);
    s2menu_room = saved;
    s2room_forget();
    s2save_reset();
    printf("ui: options room: the four rooms built from the ZIP, the stations, %d visits' %d frames of the camera equal to the model, the MULTIPLAYER MENU, the menu on the room\n", runs,
           frames_checked);
}

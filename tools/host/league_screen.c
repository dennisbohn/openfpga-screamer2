/*
 * The championship's pages on the PC (firmware/s2leaguepages.c, s2league.c, and the championship's end screen of s2end.c): included by ui_screen.c, which has the
 * stand-ins of the hardware (the swap, the scripted buttons, the sounds) and the helpers. Against the independent model of tools/ui_league_ref.py: LEAGUE SELECTION,
 * WELCOME TO LEAGUE n and the LEAGUE REPORT in scripted runs (every frame compared pixel for pixel, the result, the frames the page takes, the sounds, the cursor, what
 * a won league opens in the save), and the end screen of a league race over three race pictures. The rules themselves are tested by tools/s2/check_league_s2.sh.
 */
#include "../s2league.h"
#include "../s2leaguepages.h"

/* a frame of the league pages against the model's: the picture and what is under the dark box (.idx), the passes of darkening (.dk, 0 or 1), what is drawn over it (.b) */
static void check_league_frame(const struct expect *e)
{
    char path[512];
    uint32_t n[4];
    snprintf(path, sizeof path, "%s/lg_%s.idx", expect_ref, e->name);
    uint8_t *a = slurp(path, &n[0]);
    snprintf(path, sizeof path, "%s/lg_%s.dk", expect_ref, e->name);
    uint8_t *dk = slurp(path, &n[1]);
    snprintf(path, sizeof path, "%s/lg_%s.b", expect_ref, e->name);
    uint8_t *b = slurp(path, &n[2]);
    snprintf(path, sizeof path, "%s/lg_%s.pal", expect_ref, e->name);
    uint8_t *pal = slurp(path, &n[3]);
    expect_hits++;
    if (!a || !dk || !b || !pal || n[0] != S2G_PIC_BYTES || n[1] != S2G_PIC_BYTES || n[2] != S2G_PIC_BYTES || n[3] != 1024) {
        CHECK(0, "%s: reference frame missing", e->name);
    } else {
        /* the table of the page's own palette (the picture's, unfaded): the model has none, the firmware's table is checked against a search in double precision */
        static uint32_t page_pal[256];
        static uint8_t table[256];
        const char *pic = e->league < 0 ? "BKG_3L.BKK" : 0;
        char name[16];
        if (!pic) {
            snprintf(name, sizeof name, "LEAG_%dL.BKK", e->league + 1);
            pic = name;
        }
        static uint8_t scratch[S2G_PIC_BYTES + 64] __attribute__((aligned(4)));
        CHECK(s2gfx_bkk(pic, scratch, page_pal), "%s", pic);
        s2gfx_dark_table(page_pal, table, 256);
        CHECK(!table_wrong(page_pal, table, 256), "darkening table of %s", pic);
        const volatile uint8_t *fb = FB(front);
        int differ = 0, first = -1;
        for (int i = 0; i < S2G_PIC_BYTES; i++) {
            uint8_t v = a[i];
            for (int p = 0; p < dk[i]; p++) v = table[v];
            if (b[i]) v = b[i];
            if (fb[i] != v && differ++ == 0) first = i;
        }
        CHECK(!differ, "%s: %d pixels differ from the model, first at x %d y %d (%02x)", e->name, differ, first % 320, first / 320, first >= 0 ? fb[first] : 0);
        int bad = 0;
        for (int i = 0; i < 256; i++) bad += (VID_PAL[i] & 0xFFFFFF) != ((const uint32_t *)pal)[i];
        CHECK(!bad, "%s: %d palette entries differ from the model", e->name, bad);
        if (e->shot) save(e->name);
    }
    free(a);
    free(dk);
    free(b);
    free(pal);
}

static void league_save(int lopen, int topen, int car5, const char *name)
{
    s2save_reset();
    s2save_unlock_all = 0;
    s2save_set(S2S_LEAGUE_OPEN, lopen);
    s2save_set(S2S_TRACKS_OPEN, topen);
    for (int t = 0; t < 4; t++) s2save_set_car5(t + 1, car5 >> t & 1);
    char padded[8];
    snprintf(padded, sizeof padded, "%s", name);
    for (char *p = padded; *p; p++)
        if (*p == '_') *p = ' ';
    s2save_set_name(padded);
}

static int car5_mask(void)
{
    int m = 0;
    for (int t = 0; t < 4; t++) m |= s2save_car5_open(t + 1) << t;
    return m;
}

static void check_league_pages(const char *ref, const char *prefix)
{
    char path[512];
    snprintf(path, sizeof path, "%s/league.txt", ref);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "no league.txt");
    static char line[2048];
    int runs = 0, frames = 0;
    while (f && fgets(line, sizeof line, f)) {
        char name[64], pname[16], script[512], clicks_text[128], frame_text[1024];
        int page, league, done, p[4], team, lopen, topen, car5, res, calls_want, lopen2, topen2, car52, final_league;
        if (sscanf(line, "case %63s %d %d %d %d %d %d %d %d %15s %d %d %d %511s %d %d %127s %d %d %d %1023s %d", name, &page, &league, &done, &p[0], &p[1], &p[2], &p[3],
                   &team, pname, &lopen, &topen, &car5, script, &res, &calls_want, clicks_text, &lopen2, &topen2, &car52, frame_text, &final_league) != 22) {
            CHECK(0, "league.txt: line not understood: %s", line);
            continue;
        }
        league_save(lopen, topen, car5, pname);
        struct s2_league L;
        s2l_begin(&L, league, team, 1);
        L.races_done = done;
        for (int i = 0; i < 4; i++) L.points[i] = p[i];
        begin_run(script, prefix);
        expect_ref = ref;
        expect_hits = 0;
        if (strcmp(frame_text, "-"))
            for (char *q = frame_text; *q && nexpects < 40;) {
                int tick = atoi(q);
                char *end = strchr(q, ',');
                expects[nexpects].at = (uint32_t)tick + 1;
                snprintf(expects[nexpects].name, sizeof expects[nexpects].name, "%s_%d", name, tick);
                expects[nexpects].shot = 1;
                expects[nexpects].league = page == 0 ? -1 : league;                  /* (which picture's palette) */
                expects[nexpects].kind = 1;
                nexpects++;
                q = end ? end + 1 : q + strlen(q);
            }
        const int want_frames = nexpects;
        front = 0;
        int r = 0, at_end = league;
        if (page == 0) {
            at_end = league;
            r = s2lp_select(&at_end);
        } else if (page == 1) {
            r = s2lp_welcome(league);
        } else {
            r = s2lp_report(&L);
        }
        CHECK(r == res, "%s: the page ended with %d, expected %d", name, r, res);
        CHECK(at_end == final_league, "%s: ends on league %d, expected %d", name, at_end, final_league);
        CHECK((int)calls == calls_want, "%s: the page took %u frames (calls), expected %d", name, calls, calls_want);
        CHECK(expect_hits == want_frames, "%s: %d of %d frames were compared", name, expect_hits, want_frames);
        char got[128] = "";
        for (int i = 0; i < nclicks; i++) snprintf(got + strlen(got), sizeof got - strlen(got), "%s%d", i ? "," : "", clicks[i]);
        CHECK(!strcmp(nclicks ? got : "-", clicks_text), "%s: sounds %s, expected %s", name, nclicks ? got : "-", clicks_text);
        CHECK(s2save_get(S2S_LEAGUE_OPEN) == lopen2 && s2save_get(S2S_TRACKS_OPEN) == topen2 && car5_mask() == car52,
              "%s: the save has league %d, tracks %d, fifth cars %d after the page, expected %d %d %d", name, s2save_get(S2S_LEAGUE_OPEN), s2save_get(S2S_TRACKS_OPEN),
              car5_mask(), lopen2, topen2, car52);
        runs++;
        frames += want_frames;
    }
    if (f) fclose(f);
    nexpects = 0;
    s2save_reset();
    printf("ui: championship pages: %d scripted runs, %d frames equal to the model pixel for pixel (LEAGUE SELECTION, WELCOME TO LEAGUE n, LEAGUE REPORT with its question), "
           "keys, sounds, results and what a won league opens in the save checked\n", runs, frames);
}

/* the championship's end screen: the model's runs frame by frame over a race picture, ENTER, and the league's lists of the save */
static void check_league_end(const char *ref, const char *prefix)
{
    char path[512];
    snprintf(path, sizeof path, "%s/leagueend.txt", ref);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "no leagueend.txt");
    static uint32_t pal[256];
    static uint8_t table[256];
    static char line[2048];
    int runs = 0, frames = 0;
    shot_prefix = prefix;
    s2save_reset();
    while (f && fgets(line, sizeof line, f)) {
        char name[16], race[64], rest[1500];
        struct s2_end_in in;
        memset(&in, 0, sizeof in);
        double ptime, best;
        int n = sscanf(line, "run %15s %63s %d %d %d %lf %lf %d %d %1499[^\n]", name, race, &in.track, &in.laps, &in.place, &ptime, &best, &in.team, &in.car, rest);
        CHECK(n == 10, "leagueend.txt: line not understood: %s", line);
        if (n != 10) continue;
        in.time = (float)ptime;
        in.best_lap = (float)best;
        in.league = 1;
        in.replay = name[0] == 'R' || name[0] == 'P';               /* (the model's names: R the race can be run again, P it is the replay's own screen) */
        in.replaying = name[0] == 'P';
        char *q = rest;
        for (int i = 0; i < 4; i++) {
            int team, car, place, done, used = 0;
            char nm[8];
            double t;
            CHECK(sscanf(q, "%7s %d %d %d %d %lf%n", nm, &team, &car, &place, &done, &t, &used) == 6, "leagueend.txt: car %d of %s", i, name);
            memcpy(in.cars[i].name, nm, 4);
            in.cars[i].team = (uint8_t)team;
            in.cars[i].car = (uint8_t)car;
            in.cars[i].place = (uint8_t)place;
            in.cars[i].done = (uint8_t)done;
            in.cars[i].time = (float)t;
            q += used;
        }
        int keep[16], nkeep = 0;
        for (char *k = q; *k && nkeep < 16;) {
            keep[nkeep++] = atoi(k);
            char *comma = strchr(k, ',');
            if (!comma) break;
            k = comma + 1;
        }
        char file[96];
        snprintf(file, sizeof file, "frame_%s", race);
        uint8_t *base = ref_file(ref, file, "idx", S2G_PIC_BYTES);
        uint8_t *pb = ref_file(ref, file, "pal", 1024);
        if (!base || !pb) continue;
        memcpy(pal, pb, 1024);
        for (int i = 0; i < 256; i++) VID_PAL[i] = pal[i];
        s2gfx_dark_table(pal, table, 256);
        CHECK(!table_wrong(pal, table, 256), "darkening table of the race palette of %s", race);
        int early = 0, last = keep[nkeep - 1];
        for (int j = 0; j <= last; j++) {
            memcpy((void *)FB(0), base, S2G_PIC_BYTES);
            uint32_t press = j == 50 ? KEY_B : 0;
            if (!strcmp(name, "R2") && j == 300) press = KEY_RIGHT;
            int got = s2end_frame(FB(0), 0, pal, &in, (uint32_t)j, 1000 + (uint32_t)j, press);
            if (j == 50) early = got;
            for (int k = 0; k < nkeep; k++)
                if (keep[k] == j) {
                    char what[40];
                    snprintf(what, sizeof what, "lend_%s_%d", name, j);
                    uint8_t *want = ref_file(ref, what, "idx", S2G_PIC_BYTES), *dk = ref_file(ref, what, "dk", S2G_PIC_BYTES), *ov = ref_file(ref, what, "ov", S2G_PIC_BYTES);
                    if (want && dk && ov) compare_frame((const uint8_t *)FB(0), want, dk, ov, base, table, what);
                    free(want);
                    free(dk);
                    free(ov);
                    front = 0;
                    save(what);
                    frames++;
                }
        }
        CHECK(early == S2END_NONE, "run %s: ENTER in the first four seconds took effect (%d)", name, early);
        int result = s2end_frame(FB(0), 0, pal, &in, (uint32_t)last + 1, 1000 + (uint32_t)last + 1, KEY_START);
        CHECK(result == (in.replay && strcmp(name, "R2") ? S2END_REPLAY : S2END_CONTINUE), "run %s: ENTER gave %d", name, result);
        result = s2end_frame(FB(0), 0, pal, &in, (uint32_t)last + 2, 1000 + (uint32_t)last + 2, in.replay ? KEY_A | KEY_SELECT : KEY_LEFT | KEY_RIGHT | KEY_A | KEY_SELECT);
        CHECK(result == S2END_NONE, "run %s: %s do nothing (%d)", name, in.replay ? "A and Select" : "left, right, A and Select", result);
        /* the time goes into the championship's lists, not the arcade's: the total of the race's length, the best lap (none if the run has none) */
        int list = s2_laps_list(in.laps);
        const struct s2save_row *t = s2save_row(S2S_KIND_LEAGUE, in.track, list, 0), *l = s2save_row(S2S_KIND_LEAGUE, in.track, S2S_LAP, 0);
        CHECK(t->time == in.time && !strcmp(t->name, "MIL") && t->team == in.team && t->car == in.car, "run %s: the league's total list holds %g %s", name, t->time, t->name);
        CHECK(best > 0 ? l->time == in.best_lap : l->time >= S2S_NO_TIME, "run %s: the league's lap list holds %g", name, l->time);
        CHECK(s2save_row(S2S_KIND_ARCADE, in.track, list, 0)->time >= S2S_NO_TIME, "run %s: nothing went into the arcade's list", name);
        runs++;
        free(base);
        free(pb);
    }
    if (f) fclose(f);
    s2save_reset();
    /* the loading screen's records come from the league's lists too */
    s2save_add_time(S2S_KIND_LEAGUE, 2, 1, 77.5f, "LGE", 1, 1, 0);
    struct s2_record total, lap;
    s2_best_get_kind(S2S_KIND_LEAGUE, 2, 3, &total, &lap);
    CHECK(total.time == 77.5f && !strcmp(total.name, "LGE"), "the loading screen's BEST TIME of a league race: %g", total.time);
    s2_best_get_kind(S2S_KIND_ARCADE, 2, 3, &total, &lap);
    CHECK(total.time >= S2S_NO_TIME, "the arcade's BEST TIME is its own");
    s2save_reset();
    printf("ui: championship end screen: %d runs, %d frames equal to the model, CONTINUE, the league's lists and the loading screen's records checked\n", runs, frames);
}

/* a league's pages run in order from their scripts: select, welcome, report */
static void check_league_flow(const char *prefix)
{
    char script[400];
    s2save_reset();
    s2save_set(S2S_PROFILE, 1);
    /* select (RIGHT, ENTER on the second league, which is open), welcome (ENTER), as the viewer runs them */
    s2save_unlock_all = 0;
    s2save_set(S2S_LEAGUE_OPEN, 1);
    put_keys(script, sizeof script, "3:R 8:B 40:B");
    begin_run(script, prefix);
    front = 0;
    int league = 0;
    CHECK(s2lp_select(&league) == S2LP_GO && league == 1, "LEAGUE SELECTION chose league %d", league + 1);
    CHECK(s2lp_welcome(league) == S2LP_GO, "WELCOME");
    CHECK(nclicks == 2 && clicks[0] == 0 && clicks[1] == 6, "the sounds of the selection: %d", nclicks);
    s2save_unlock_all = S2SAVE_UNLOCK_ALL;
    s2save_reset();
    printf("ui: championship flow: the selection and the welcome page in order checked\n");
}

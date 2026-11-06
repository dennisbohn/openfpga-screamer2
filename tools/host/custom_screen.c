/*
 * The set-up pages on the PC (firmware/s2arcade.c's CAR pages of the championship and of time attack with the list of set-ups and the choice, firmware/s2custom.c): included by
 * ui_screen.c, which has the stand-ins of the hardware (the swap, the scripted buttons, the sounds) and the geometry unit's C model of pages3d_screen.c. Against the independent
 * model of tools/ui_custom_ref.py: scripted runs of the CAR pages (the keys, the state they are in, the result, the choices they leave in the menu's configuration, the sounds, the
 * frames they take; CUSTOMIZE runs the other page and comes back to the list) and of the CAR CUSTOMIZATION page (every key in every place, a value changed and kept or dropped, SAVE
 * and EXIT, what is in the save after it); every listed frame is compared with the model's overlay, the pixels the 2D toolkit draws over the 3D scene (the list, the panel, the choice;
 * the page's pictures, scales and texts), pixel for pixel, and its palette (the fade). The cars under it are the 3D pages' business (pages3d_screen.c).
 */
#include "../s2custom.h"

/* the six numbers of Custom 1 .. 4 of every car of team 1 (tools/ui_custom_ref.py's CUSTOMS) */
static const int8_t CUSTOMS[4][6] = { { 3, 1, 2, 4, 0, 3 }, { 0, 4, 1, 3, 3, 0 }, { 2, 2, 0, 2, 2, 2 }, { 4, 4, 2, 0, 1, 2 } };

static void check_custom_frame(const struct expect *e)
{
    char path[512];
    uint32_t n[2];
    snprintf(path, sizeof path, "%s/cu_%s.ov", expect_ref, e->name);
    uint8_t *ov = slurp(path, &n[0]);
    snprintf(path, sizeof path, "%s/cu_%s.pal", expect_ref, e->name);
    uint8_t *pal = slurp(path, &n[1]);
    expect_hits++;
    if (!ov || !pal || n[0] != S2G_PIC_BYTES || n[1] != 1024) {
        CHECK(0, "%s: reference frame missing", e->name);
    } else {
        const volatile uint8_t *fb = FB(front);
        int differ = 0, first = -1, drawn = 0;
        for (int i = 0; i < S2G_PIC_BYTES; i++) {
            drawn += ov[i] != 0;
            if (ov[i] && fb[i] != ov[i] && differ++ == 0) first = i;
        }
        CHECK(!differ, "%s: %d of %d overlay pixels differ from the model, first at x %d y %d (%02x, the model's %02x)", e->name, differ, drawn, first % 320, first / 320,
              first >= 0 ? fb[first] : 0, first >= 0 ? ov[first] : 0);
        int bad = 0;
        for (int i = 0; i < 256; i++) bad += (VID_PAL[i] & 0xFFFFFF) != ((const uint32_t *)pal)[i];
        CHECK(!bad, "%s: %d palette entries differ from the model", e->name, bad);
        if (e->shot) save(e->name);
    }
    free(ov);
    free(pal);
}

/* how many of the overlay's pixels the picture in `shot` does not have (a negative control for check_custom_frame) */
static int overlay_differs(const uint8_t *shot, const char *ref, const char *name)
{
    char path[512];
    uint32_t n;
    snprintf(path, sizeof path, "%s/cu_%s.ov", ref, name);
    uint8_t *ov = slurp(path, &n);
    int d = -1;
    if (ov && n == S2G_PIC_BYTES) {
        d = 0;
        for (int i = 0; i < S2G_PIC_BYTES; i++) d += ov[i] && shot[i] != ov[i];
    }
    free(ov);
    return d;
}

static void check_custom(const char *ref, const char *prefix)
{
    char path[512];
    static char walk[1024];
    snprintf(path, sizeof path, "%s/custom.txt", ref);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "no custom.txt");
    static char line[2048], script[1024];
    int runs = 0, frames = 0;
    shot_prefix = prefix;
    while (f && fgets(line, sizeof line, f)) {
        char name[64], sounds[256], saved[64], frame_text[1024];
        int page, kind, car, setup, result, calls_want, setup2, car2, free2;
        if (sscanf(line, "case %63s %d %d %d %d %1023s %d %d %255s %d %d %d %63s %1023s", name, &page, &kind, &car, &setup, script, &result, &calls_want, sounds, &setup2, &car2, &free2,
                   saved, frame_text) != 14) {
            CHECK(0, "custom.txt: line not understood: %s", line);
            continue;
        }
        if (!strcmp(name, "cu_walk")) strcpy(walk, script);
        s2save_reset();
        s2save_unlock_all = 1;                                              /* (all five cars: the model's) */
        for (int c = 1; c <= 4; c++)
            for (int k = 0; k < 4; k++) s2save_set_setup(1, c, k, CUSTOMS[k]);
        struct s2_menu_cfg cfg = { .track = 1, .team = 1, .car = car, .laps = 1, .skill = 1, .setup = page == 0 ? setup : 0 };
        begin_run(script, prefix);
        expect_ref = ref;
        expect_hits = 0;
        if (strcmp(frame_text, "-"))
            for (char *q = frame_text; *q && nexpects < 40;) {
                const int tick = atoi(q);
                char *end = strchr(q, ',');
                expects[nexpects].at = (uint32_t)tick + 1;
                snprintf(expects[nexpects].name, sizeof expects[nexpects].name, "%s_%d", name, tick);
                expects[nexpects].shot = 1;
                expects[nexpects].league = 0;
                expects[nexpects].kind = 2;
                nexpects++;
                q = end ? end + 1 : q + strlen(q);
            }
        const int want_frames = nexpects;
        front = 0;
        int r;
        if (page == 0) r = s2arcade_page(kind ? S2ARC_ATTACK : S2ARC_LEAGUE, &cfg);
        else r = s2custom_page(&cfg, setup - 1) ? 1 : 0;
        CHECK(r == result, "%s: the page ended with %d, expected %d", name, r, result);
        CHECK((int)calls == calls_want, "%s: the pages took %u frames (calls), expected %d", name, calls, calls_want);
        CHECK(expect_hits == want_frames, "%s: %d of %d frames were compared", name, expect_hits, want_frames);
        char got[256] = "";
        for (int i = 0; i < nclicks; i++) snprintf(got + strlen(got), sizeof got - strlen(got), "%s%d", i ? "," : "", clicks[i]);
        CHECK(!strcmp(nclicks ? got : "-", sounds), "%s: sounds %s, expected %s", name, nclicks ? got : "-", sounds);
        if (page == 0) CHECK(cfg.setup == setup2 && cfg.car == car2 && cfg.free_run == free2, "%s: the choices are set-up %d car %d free run %d, expected %d %d %d", name, cfg.setup,
                             cfg.car, cfg.free_run, setup2, car2, free2);
        /* the save: SAVE wrote the six of the page, nothing else changed */
        for (int c = 1; c <= 4; c++)
            for (int k = 0; k < 4; k++) {
                const int8_t *s = s2save_setup(1, c, k);
                int want[6];
                for (int j = 0; j < 6; j++) want[j] = CUSTOMS[k][j];
                if (page == 1 && c == car && k == setup - 1 && strcmp(saved, "-"))
                    sscanf(saved, "%d,%d,%d,%d,%d,%d", &want[0], &want[1], &want[2], &want[3], &want[4], &want[5]);
                CHECK(s[0] == want[0] && s[1] == want[1] && s[2] == want[2] && s[3] == want[3] && s[4] == want[4] && s[5] == want[5], "%s: car %d Custom %d is %d %d %d %d %d %d in the save, expected %d %d %d %d %d %d",
                      name, c, k + 1, s[0], s[1], s[2], s[3], s[4], s[5], want[0], want[1], want[2], want[3], want[4], want[5]);
            }
        runs++;
        frames += want_frames;
    }
    if (f) fclose(f);
    /* a negative control: the frame of tick 55 of the walk over the items has the model's overlay of tick 55 and not that of tick 61 (the cursor has moved: the check can tell) */
    s2save_reset();
    s2save_unlock_all = 1;
    for (int c = 1; c <= 4; c++)
        for (int k = 0; k < 4; k++) s2save_set_setup(1, c, k, CUSTOMS[k]);
    struct s2_menu_cfg cfg = { .track = 1, .team = 1, .car = 2, .laps = 1, .skill = 1 };
    begin_run(walk, prefix);
    cap_at(56, 0);
    front = 0;
    s2custom_page(&cfg, 0);
    CHECK(overlay_differs(cap[0], ref, "cu_walk_55") == 0, "the walk's frame 55 has the model's overlay");
    const int other = overlay_differs(cap[0], ref, "cu_walk_61");
    CHECK(other > 50, "the walk's frame 55 is not the model's frame 61 (%d overlay pixels differ)", other);
    nexpects = 0;
    s2save_unlock_all = S2SAVE_UNLOCK_ALL;
    s2save_reset();
    printf("ui: set-up pages: %d scripted runs of the CAR pages (the list, the panel, the choice, CUSTOMIZE and back) and of CAR CUSTOMIZATION, %d frames' overlays equal to the model pixel for pixel, "
           "keys, sounds, results, the choices and what SAVE writes into the save checked\n", runs, frames);
}

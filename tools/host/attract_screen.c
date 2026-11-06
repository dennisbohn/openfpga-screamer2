/*
 * The attract sequence on the PC (firmware/s2attract.c, its use by s2room.c, s2pages3d.c and s2menu.c): included by ui_screen.c, which has the stand-ins of the hardware (the swap,
 * the scripted buttons, the blitter's model) and room_screen.c's room. What is tested:
 *
 *   text      ATTRACT.TXT read from the real ZIP's DATA/LOCAL.DAT by s2att_load(): every line as tools/attract_reference.py reads it (empty lines kept)
 *   screen    eight scenarios (tools/attract_reference.py: the whole sequence with no press, and a press in the first frame, while the first logo slides, while the second stands,
 *             in the white fade, while a group shows, in the gap between two groups, and in the last frames): after every step the state (stage, line, x, the timers, the flags,
 *             as float bits) and what the frame shows equal the model's, which follows STARTL.EXE's routine 0x37ec1 instruction by instruction; and about 200 frames drawn over a
 *             background (the logos, their lines, the title, the credits, the fades' palettes, the last frame's S2 logo) equal the model's pixel for pixel and entry for entry
 *   timers    the idle timer's steps to run out (the model's), reset by a button
 *   pages     the options room with no button for 30 s ends with S2ROOM_IDLE, with a button every 20 s it does not; TEAM SELECTION ends with S2P3D_IDLE after 30 s in its team state,
 *             not in the name state; the menu turns both into S2M_ATTRACT and comes back to the page it left (the room, or TEAM SELECTION)
 *   edges     a sprite wholly right of the screen sends no blitter command (s2gfx.c)
 *   tracks    the six tracks of the sequence in the program's order, round
 */
#include "../s2attract.h"

#define MAX_FRAMES 2400

static void check_attract_text(const char *ref)
{
    struct s2att a;
    char path[512];
    snprintf(path, sizeof path, "%s/attract_text.txt", ref);
    uint32_t n;
    uint8_t *t = slurp(path, &n);
    CHECK(t != 0, "no attract_text.txt");
    if (!t) return;
    t[n] = 0;
    CHECK(s2att_load(&a, s2gfx_zip()), "s2att_load");
    int k = 0;
    for (char *p = (char *)t; *p && k < a.n;) {
        char *e = strchr(p, '\n');
        if (e) *e = 0;
        CHECK(!strcmp(a.line[k], p), "ATTRACT.TXT line %d: '%s', the reference has '%s'", k, a.line[k], p);
        k++;
        if (!e) break;
        p = e + 1;
    }
    CHECK(k == a.n, "ATTRACT.TXT has %d lines, the reference %d", a.n, k);
    struct s2att small;
    CHECK(!s2att_text(&small, "AND\r\nPRESENTS\r\n") && small.n == 2, "a file of two lines is not a sequence");
    CHECK(s2att_text(&small, "A\n\nB\r\n#\n") && small.n == 4 && !strcmp(small.line[0], "A") && !strcmp(small.line[1], "") && !strcmp(small.line[2], "B") && !strcmp(small.line[3], "#"),
          "lines of a made-up text (the empty one is a line)");
    free(t);
    printf("attract: ATTRACT.TXT read from the ZIP: %d lines, equal to the reference's\n", a.n);
}

static uint32_t bits_of(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static struct { char name[32]; int step; } frame_list[MAX_FRAMES];
static int nframe_list;

static int wants_frame(const char *name, int step)
{
    for (int i = 0; i < nframe_list; i++)
        if (frame_list[i].step == step && !strcmp(frame_list[i].name, name)) return 1;
    return 0;
}

static void check_attract_screen(const char *ref, const char *prefix)
{
    char path[512];
    snprintf(path, sizeof path, "%s/attract_scenarios.txt", ref);
    uint32_t n;
    char *spec = (char *)slurp(path, &n);
    CHECK(spec != 0, "no attract_scenarios.txt");
    if (!spec) return;
    spec[n] = 0;
    snprintf(path, sizeof path, "%s/attract_bg.idx", ref);
    uint32_t bgn, paln;
    uint8_t *bg = slurp(path, &bgn);
    snprintf(path, sizeof path, "%s/attract_bg.pal", ref);
    uint8_t *palf = slurp(path, &paln);
    CHECK(bg && palf && bgn == S2G_PIC_BYTES && paln == 1024, "the background and its palette");
    if (!bg || !palf) return;
    uint32_t base[256];
    memcpy(base, palf, 1024);
    struct s2att a;
    CHECK(s2att_load(&a, s2gfx_zip()), "s2att_load");
    nframe_list = 0;
    static struct { char name[32]; int press, steps; } sc[16];
    int nsc = 0;
    for (char *line = strtok(spec, "\n"); line; line = strtok(0, "\n")) {
        if (!strncmp(line, "scenario ", 9) && nsc < 16) sscanf(line, "scenario %31s %d %d", sc[nsc].name, &sc[nsc].press, &sc[nsc].steps), nsc++;
        else if (!strncmp(line, "frame ", 6) && nframe_list < MAX_FRAMES) sscanf(line, "frame %31s %d", frame_list[nframe_list].name, &frame_list[nframe_list].step), nframe_list++;
    }
    int total_steps = 0, frames = 0, bad_state = 0, bad_frame = 0, bad_pal = 0;
    for (int s = 0; s < nsc; s++) {
        snprintf(path, sizeof path, "%s/attract_%s.states", ref, sc[s].name);
        uint32_t sn;
        char *st = (char *)slurp(path, &sn);
        CHECK(st != 0, "no %s", path);
        if (!st) continue;
        st[sn] = 0;
        char *cursor = st;
        s2att_begin(&a);
        int hit_over = 0;
        for (int step = 0; step < sc[s].steps; step++) {
            struct s2att_view v;
            s2att_step(&a, step == sc[s].press, &v);
            int w_step, w_stage, w_line, w_cued, w_over, w_black, w_sprite, w_sx, w_caption, w_title, w_first, w_count, w_exit, w_music;
            uint32_t w_x, w_u, w_t, w_b, w_l, w_lit, w_white, w_dark;
            char *nl = strchr(cursor, '\n');
            if (nl) *nl = 0;
            const int got = sscanf(cursor, "%d %d %d %x %x %x %x %x %d %d %x | %d %d %d %d %d %d %d %x %x %d %d", &w_step, &w_stage, &w_line, &w_x, &w_u, &w_t, &w_b, &w_l, &w_cued,
                                   &w_over, &w_lit, &w_black, &w_sprite, &w_sx, &w_caption, &w_title, &w_first, &w_count, &w_white, &w_dark, &w_exit, &w_music);
            cursor = nl ? nl + 1 : cursor + strlen(cursor);
            if (got != 22) { CHECK(0, "%s: a line of the states file: %d fields", sc[s].name, got); break; }
            const int same = w_step == step && w_stage == a.stage && w_line == a.group && w_x == bits_of(a.x) && w_u == bits_of(a.shown) && (a.stage < 4 || w_t == bits_of(a.gap)) &&
                             w_b == bits_of(a.black) && w_l == bits_of((float)a.leave) && w_cued == a.cued && w_over == a.over && w_lit == bits_of(a.lit) && w_black == v.black &&
                             w_sprite == v.sprite && (v.sprite < 0 || w_sx == v.sx) && w_caption == v.caption && w_title == v.title && (!v.count || w_first == v.first) &&
                             w_count == v.count && w_white == bits_of(v.white) && w_dark == bits_of(v.dark) && w_exit == v.exit && w_music == v.music;
            if (!same) {
                bad_state++;
                if (bad_state < 6)
                    printf("FAIL: %s step %d: stage %d/%d line %d/%d x %08x/%08x U %08x/%08x B %08x/%08x white %08x/%08x dark %08x/%08x count %d/%d sprite %d/%d sx %d/%d\n", sc[s].name,
                           step, a.stage, w_stage, a.group, w_line, bits_of(a.x), w_x, bits_of(a.shown), w_u, bits_of(a.black), w_b, bits_of(v.white), w_white, bits_of(v.dark), w_dark,
                           v.count, w_count, v.sprite, w_sprite, v.sx, w_sx);
            }
            total_steps++;
            if (wants_frame(sc[s].name, step)) {
                static struct blit_list l;
                static uint32_t out[256], want_pal[256];
                uint32_t in, ip;
                snprintf(path, sizeof path, "%s/attract_%s_%d.idx", ref, sc[s].name, step);
                uint8_t *e = slurp(path, &in);
                snprintf(path, sizeof path, "%s/attract_%s_%d.pal", ref, sc[s].name, step);
                uint8_t *p = slurp(path, &ip);
                if (!e || !p || in != S2G_PIC_BYTES || ip != 1024) {
                    CHECK(0, "no frame %s %d", sc[s].name, step);
                } else {
                    memcpy(want_pal, p, ip);
                    memcpy((void *)FB(0), bg, S2G_PIC_BYTES);
                    blit_start(&l, 0);
                    s2att_draw(&l, &a, &v);
                    blit_model(FB(0), &l);
                    int diff = 0, pd = 0;
                    for (int i = 0; i < S2G_PIC_BYTES; i++) diff += ((const volatile uint8_t *)FB(0))[i] != e[i];
                    const int changed = s2att_palette(base, &v, out);
                    for (int i = 0; i < 256; i++) pd += (changed ? out[i] : base[i]) != want_pal[i];
                    frames++;
                    if (diff || pd) {
                        bad_frame += diff != 0;
                        bad_pal += pd != 0;
                        if (bad_frame + bad_pal < 6) printf("FAIL: %s frame %d: %d pixels and %d palette entries differ\n", sc[s].name, step, diff, pd);
                    }
                    if (!strcmp(sc[s].name, "full") && (step == 0 || step == 56 || step == 119 || step == 224 || step == 301 || step == 427 || step == 441 || step == 469 || step == 504 ||
                                                         step == 1204 || step == 4755 || step == 4779)) {
                        snprintf(path, sizeof path, "%s_attract_%d.rgb", prefix, step);
                        FILE *o = fopen(path, "wb");
                        for (int i = 0; i < S2G_PIC_BYTES; i++) {
                            const uint32_t col = changed ? out[((const volatile uint8_t *)FB(0))[i]] : base[((const volatile uint8_t *)FB(0))[i]];
                            const uint8_t rgb[3] = { (uint8_t)(col >> 16), (uint8_t)(col >> 8), (uint8_t)col };
                            fwrite(rgb, 1, 3, o);
                        }
                        fclose(o);
                    }
                }
                free(e);
                free(p);
            }
            if (a.over) {
                hit_over = 1;
                CHECK(step == sc[s].steps - 1, "%s: over at step %d, the model's last step is %d", sc[s].name, step, sc[s].steps - 1);
                break;
            }
        }
        CHECK(hit_over, "%s: the sequence ended", sc[s].name);
        free(st);
    }
    CHECK(bad_state == 0, "%d steps whose state or view differs from the model", bad_state);
    CHECK(bad_frame == 0 && bad_pal == 0, "%d frames and %d palettes differ from the model", bad_frame, bad_pal);
    CHECK(frames > 150, "frames compared: %d", frames);
    printf("attract: %d scenarios, %d steps: state and view equal the model's at every step; %d frames drawn equal the model's pixel for pixel and palette entry for palette entry\n", nsc,
           total_steps, frames);
    free(spec);
    free(bg);
    free(palf);
}

/* the room of team 1 for a script of up to 4000 frames (room_visit has 640) */
static int room_long(const char *spec, const char *prefix, int *frames)
{
    static char script[4000];
    put_keys(script, sizeof script, spec);
    begin_run(script, prefix);
    front = 0;
    frame_probe = 0;
    const int r = s2room_run(s2gfx_zip(), &room_mem, 1);
    *frames = (int)calls;
    return r;
}

/* the idle timer's run, the pages' use of it, the menu, the edges, the tracks */
static void check_attract_idle(const char *ref, const char *prefix)
{
    char path[512];
    uint32_t n;
    snprintf(path, sizeof path, "%s/attract_idle.txt", ref);
    char *t = (char *)slurp(path, &n);
    CHECK(t != 0, "no attract_idle.txt");
    if (!t) return;
    t[n] = 0;
    const int want = atoi(t);
    free(t);
    float timer;
    int steps = 0;
    s2att_idle_reset(&timer);
    while (!s2att_idle_step(&timer) && steps < 5000) steps++;
    steps++;
    CHECK(steps == want, "the idle timer runs out after %d steps, the model says %d", steps, want);
    s2att_idle_reset(&timer);
    for (int i = 0; i < 1000; i++) s2att_idle_step(&timer);
    s2att_idle_reset(&timer);
    int more = 0;
    while (!s2att_idle_step(&timer) && more < 5000) more++;
    CHECK(more + 1 == want, "a reset starts the 30 s again (%d)", more + 1);

    /* the options room: nothing pressed for 30 s ends it (after the fade out of 25 frames); a button every 20 s does not */
    s2save_reset();
    s2save_set(S2S_PROFILE, 1);
    int frames;
    int r = room_long("", prefix, &frames);
    CHECK(r == S2ROOM_IDLE && frames >= want && frames <= want + 30, "the room left alone: result %d after %d frames (the model's timer runs out at %d, the fade out takes 25)", r, frames, want);
    r = room_long("990:R 1990:R 2990:A", prefix, &frames);
    CHECK(r == S2ROOM_BACK && frames >= 2990 && frames <= 2995, "a button every 20 s keeps the room: result %d after %d frames", r, frames);
    r = room_long("1499:R 2998:A", prefix, &frames);                  /* (a button in the last frame before the timer would run out) */
    CHECK(r == S2ROOM_BACK && frames >= 2998 && frames <= 3003, "a button just before it runs out keeps the room: result %d after %d frames", r, frames);

    /* TEAM SELECTION: the team state runs the timer down, a button resets it, the name state does not run it */
    static char script[4000];
    struct s2_menu_cfg c = { .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1 };
    put_keys(script, sizeof script, "");
    s2save_reset();
    begin_run(script, prefix);
    front = 0;
    r = s2_page_team(&c);
    CHECK(r == S2P3D_IDLE && (int)calls >= want && (int)calls <= want + 3, "TEAM SELECTION left alone: result %d after %d frames (the model's %d)", r, (int)calls, want);
    put_keys(script, sizeof script, "1000:R 2000:R 2900:B 3000:B");
    s2save_reset();
    begin_run(script, prefix);
    front = 0;
    r = s2_page_team(&c);
    CHECK(r == S2P3D_DONE && (int)calls > 3000, "TEAM SELECTION with a button every 1000 frames is not left alone (result %d after %d frames)", r, (int)calls);
    put_keys(script, sizeof script, "5:B 3100:B");                    /* the name state: ENTER at frame 5, then nothing for 62 s: no idle; ENTER at 3100 keeps the name */
    s2save_reset();
    begin_run(script, prefix);
    front = 0;
    r = s2_page_team(&c);
    CHECK(r == S2P3D_DONE && (int)calls > 3100, "the name state is never idle: result %d after %d frames", r, (int)calls);

    /* the menu: the room's idle is S2M_ATTRACT and the room comes again; ESC to TEAM SELECTION, its idle is S2M_ATTRACT and the page comes again before the room */
    s2menu_room = room_stand_in;
    s2save_reset();
    s2save_set(S2S_PROFILE, 1);
    s2menu_load(&c);
    answers[0] = S2ROOM_IDLE;
    answers[1] = S2ROOM_CHAMP;
    answered = asked = 0;
    put_keys(script, sizeof script, "");
    begin_run(script, prefix);
    int go = s2menu_run(&c);
    CHECK(go == S2M_ATTRACT && asked == 1, "the room's idle: the menu ended with %d after %d visits", go, asked);
    go = s2menu_run(&c);
    CHECK(go == S2M_CHAMP && asked == 2, "after the sequence the room comes again: %d, %d visits", go, asked);
    answers[0] = S2ROOM_BACK;
    answers[1] = S2ROOM_CHAMP;
    answered = asked = 0;
    begin_run(script, prefix);
    front = 0;
    go = s2menu_run(&c);
    CHECK(go == S2M_ATTRACT && asked == 1, "TEAM SELECTION's idle after ESC in the room: the menu ended with %d after %d visits", go, asked);
    put_keys(script, sizeof script, "5:B 40:B");
    begin_run(script, prefix);
    front = 0;
    go = s2menu_run(&c);
    CHECK(go == S2M_CHAMP && asked == 2, "after the sequence TEAM SELECTION comes again (ENTER, ENTER), then the room: %d, %d visits", go, asked);
    s2save_reset();
    s2save_set(S2S_PROFILE, 1);

    /* a sprite wholly right of the screen sends no command, one half out sends one (the blitter's x is 10 bits wide) */
    static struct blit_list l;
    blit_start(&l, 0);
    s2gfx_game_sprite(&l, S2ATT_SPR_VIRGIN, 1000, 10);
    CHECK(l.n == 0, "a sprite at x 1000 sends %d commands", l.n);
    s2gfx_game_sprite(&l, S2ATT_SPR_VIRGIN, 320, 10);
    CHECK(l.n == 0, "a sprite at x 320 sends %d commands", l.n);
    s2gfx_game_sprite(&l, S2ATT_SPR_VIRGIN, 319, 10);
    CHECK(l.n == 1, "a sprite at x 319 sends %d commands", l.n);
    s2gfx_game_sprite(&l, S2ATT_SPR_VIRGIN, -120, 10);
    CHECK(l.n == 1, "a sprite at x -120 (right edge off the screen) sends %d commands", l.n);

    /* the tracks: England, California, Finland, Switzerland, Columbia, Egypt, round */
    static const int order[6] = { 1, 3, 4, 5, 6, 2 };
    int bad = 0;
    for (int i = 0; i < 14; i++) {
        bad += s2att_track() != order[i % 6];
        s2att_next_track();
    }
    for (int i = 14; i % 6; i++) s2att_next_track();                 /* (back to England: the harness may run again) */
    CHECK(!bad, "the tracks of the sequence: %d out of order", bad);
    printf("attract: the idle timer (%d steps), TEAM SELECTION's team state and not its name state, the menu's S2M_ATTRACT and its return to the page, the sprite clip, the six tracks in order\n", want);
}

static void check_attract(const char *ref, const char *prefix)
{
    s2save_reset();
    CHECK(s2gfx_game_init(s2gfx_zip()), "s2gfx_game_init (GAMEL.SPR, GAMEL.FNT)");
    check_attract_text(ref);
    check_attract_screen(ref, prefix);
    check_attract_idle(ref, prefix);
}

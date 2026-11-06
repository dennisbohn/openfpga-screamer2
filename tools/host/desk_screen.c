/*
 * The championship's desk scene on the PC (firmware/s2desk.c): included by ui_screen.c, which has the stand-ins of the hardware (the swap, the scripted buttons, the sounds) and
 * pages3d_screen.c's geometry unit model. The scene is run by the page runner from the real ZIP: how long it takes (the frame the fade starts in: the original's 294 of scene 1 and
 * 588 of scene 4, tools/desk.sh; the other two by the rule of the same arithmetic written out again here), what ENTER and ESC do (ENTER ends it at once through the fade of 25
 * frames, ESC and the arrows do nothing), the sound (MENU.DTR 14, not in scene 4), a scene that is not in the ZIP (no black screen, nothing shown), and the viewer's call after a
 * won league (s2desk_won: the scene of the league, after league 4 the second with the fifth car, which becomes the chosen one; any other league leaves the car). tools/desk.sh compares the
 * scene with the running original's recording, frame by frame.
 */
#include "../s2desk.h"

/* the frame the scene's fade starts in, 0x3bdb8 written out again: t += (1 / seconds) dt in the x87's extended precision, stored as a float, at most 1; the fade from t >= 0.98 */
static int desk_fade_frame(float seconds)
{
    float t = 0.0f;
    for (int n = 0;; n++) {
        t = (float)((1.0L / (long double)seconds) * (long double)0.02f + (long double)t);
        if (t > 1.0f) t = 1.0f;
        if ((long double)t >= 0.98L) return n;
    }
}

static int desk_run(int scene, const char *keys, const char *prefix, int *ticks)
{
    char script[640];
    put_keys(script, sizeof script, keys);
    begin_run(script, prefix);
    front = 0;
    const struct s2room_mem mem = { (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2800000), 0x400000, (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000), 0x700000, (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2F00000),
                                    0x100000 };
    const int r = s2desk_run(s2gfx_zip(), &mem, scene, 1, scene == 4 ? 5 : 2);
    *ticks = (int)calls;
    return r;
}

static void check_desk(const char *prefix)
{
    static const float seconds[4] = { 6.0f, 8.0f, 8.0f, 12.0f };
    int ticks, ran = 0, fades[4];
    for (int scene = 1; scene <= S2DESK_SCENES; scene++) {
        const int fade = desk_fade_frame(seconds[scene - 1]);
        fades[scene - 1] = fade;
        if (scene == 1 || scene == 4) CHECK(fade == (scene == 1 ? 294 : 588), "scene %d: the fade starts in frame %d, the original's is %d", scene, fade, scene == 1 ? 294 : 588);
        /* no button: the black screen (a swap), the scene up to the frame the fade starts in, the fade's 25 frames */
        CHECK(desk_run(scene, "", prefix, &ticks) == 1, "scene %d did not run", scene);
        CHECK(ticks == 1 + fade + 25, "scene %d took %d frames, expected %d (black screen, %d, fade 25)", scene, ticks, 1 + fade + 25, fade);
        CHECK(scene == 4 ? nclicks == 0 : nclicks == 1 && clicks[0] == 14, "scene %d: the sound: %d clicks", scene, nclicks);
        ran++;
    }
    /* ESC and the arrows do nothing; ENTER ends it through the fade (a fresh press: the page's first frame is the black screen's one swap later) */
    CHECK(desk_run(1, "20:A 30:U 40:L 50:R 60:D 70:s", prefix, &ticks) == 1 && ticks == 1 + 294 + 25, "ESC and the arrows: %d frames", ticks);
    CHECK(desk_run(1, "20:A 60:B", prefix, &ticks) == 1 && ticks == 1 + 60 - 1 + 25, "ENTER at frame 60: %d frames, expected %d", ticks, 1 + 60 - 1 + 25);
    CHECK(desk_run(4, "100:S", prefix, &ticks) == 1 && ticks == 1 + 100 - 1 + 25, "Start at frame 100 of scene 4: %d frames", ticks);
    for (int league = -1; league <= 5; league++)                /* 0x3bcb2 written out again: the scene of the league in hand */
        CHECK(s2desk_scene(league) == (league < 0 ? 1 : league == 0 ? 1 : league == 1 ? 2 : league == 2 ? 3 : league == 3 ? 3 : 4), "the scene of league %d is %d", league, s2desk_scene(league));
    /* the viewer's call after a won league: league 1 (0) shows scene 1 and leaves the chosen car; league 4 (3) shows scene 3 and then scene 4, and the fifth car is the chosen one from then on,
       unless it was already (one scene) */
    {
        char script[640];
        put_keys(script, sizeof script, "5:B");
        begin_run(script, prefix);
        front = 0;
        CHECK(s2desk_won(0, 1, 2) == 2, "league 1 won: the car stays the chosen one");
        const int one = (int)calls;
        put_keys(script, sizeof script, "5:B 50:B");
        begin_run(script, prefix);
        front = 0;
        CHECK(s2desk_won(3, 1, 2) == 5, "league 4 won: the fifth car is the chosen one");
        CHECK((int)calls > one && nclicks == 1 && clicks[0] == 14, "league 4 won: two scenes, the first with its sound (%d clicks, %u frames)", nclicks, calls);
        put_keys(script, sizeof script, "5:B");
        begin_run(script, prefix);
        front = 0;
        CHECK(s2desk_won(3, 1, 5) == 5 && (int)calls == one, "league 4 won with the fifth car chosen: the one scene (%u frames, league 1's took %d)", calls, one);
    }
    /* a scene the ZIP does not have: nothing is shown, no black screen */
    begin_run("", prefix);
    front = 0;
    const struct s2room_mem mem = { (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2800000), 0x400000, (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000), 0x700000, (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2F00000),
                                    0x100000 };
    CHECK(s2desk_run(s2gfx_zip(), &mem, 5, 1, 1) == 0 && calls == 0, "scene 5 is not a scene");
    printf("ui: desk scene: %d scenes run by the page runner from the real files (the fade starts at frame %d, %d, %d and %d), ENTER, ESC, the sound and s2desk_won checked\n", ran, fades[0], fades[1],
           fades[2], fades[3]);
}

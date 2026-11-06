/*
 * Time attack's pages on the PC (firmware/s2trackmap.c, s2trackpage.c, the free run's loading screen): included by ui_screen.c, which
 * has the stand-ins of the hardware (the swap, the scripted buttons, the sounds) and the geometry unit's C model of pages3d_screen.c. What is tested here is what scripted buttons
 * make of the pages: the maps read from the ZIP and how many pieces each is (the six blocks over 512 vertices cut in two), the keys, the sounds, the frames a page takes (the fade
 * out is 13 frames, a move 13 frames before the next is taken), the results and the choices. The pages' pictures were compared with the running original's screenshots
 * (tools/trackmap.sh, docs/screamer2/menus.md) and the cut of the maps' big blocks in tools/s2/check_cut_s2.sh.
 */
#include "../s2trackpage.h"
#include "../s2trackmap.h"
#include "../s2arcade.h"
#include "../s2load.h"

static int ta_run(const char *keys, const char *prefix, struct s2_menu_cfg *c, int *frames)
{
    char script[600];
    put_keys(script, sizeof script, keys);
    begin_run(script, prefix);
    front = 0;
    const int r = s2tp_attack(c);
    *frames = (int)calls;
    return r;
}

static void check_trackpages(const char *prefix)
{
    s2save_reset();
    s2save_set(S2S_PROFILE, 1);
    CHECK(s2map_load(s2gfx_zip()), "GFX/MAPS1.DAT: the seven maps and their materials are read");
    static const int pieces[S2MAP_TRACKS] = { 2, 1, 2, 4, 2, 3, 3 };           /* AMAP1 .. 7: the blocks (those over 512 vertices, AMAP1's 2nd, AMAP4's first three, AMAP5's 1st, AMAP6's 2nd, are cut for drawing) */
    for (int t = 1; t <= S2MAP_TRACKS; t++) {
        const struct s2map *m = s2map_get(t);
        CHECK(m && m->n == pieces[t - 1], "map %d has %d blocks (expected %d)", t, m ? m->n : -1, pieces[t - 1]);
    }
    CHECK(s2map_get(0) == 0 && s2map_get(8) == 0, "no map for another track");

    struct s2_menu_cfg c = { .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1 };
    int frames, r;
    /* all seven open: R moves to Egypt (the second place; sound 12), a second R before the move is over is not taken, a third a second later moves to California, L back
       to Egypt, B (ENTER) takes it: the fade out is 13 frames, the sound 13 */
    s2save_unlock_all = 1;
    r = ta_run("5:R 9:R 60:R 120:L 180:B", prefix, &c, &frames);
    CHECK(r == S2TP_GO && c.track == 3 && frames == 180 + 13, "TIME ATTACK TRACK SELECTION: result %d, track %d, %d frames (expected GO, 3, 193)", r, c.track, frames);
    CHECK(nclicks == 4 && clicks[0] == 12 && clicks[1] == 12 && clicks[2] == 12 && clicks[3] == 13, "the sounds: %d (12, 12, 12, 13)", nclicks);
    /* the move takes 13 frames: R at 5 and R at 17 (12 frames later) is not taken, R at 19 is */
    c.track = 1;
    r = ta_run("5:R 17:R 19:R 60:B", prefix, &c, &frames);
    CHECK(r == S2TP_GO && c.track == 4 && nclicks == 3, "a move is over after 13 frames: track %d, %d sounds", c.track, nclicks);
    /* ESC is a cut; L on the first place and R on the last do nothing */
    c.track = 1;
    r = ta_run("3:L 6:A", prefix, &c, &frames);
    CHECK(r == S2TP_BACK && c.track == 1 && frames == 7 && nclicks == 0, "ESC: result %d after %d frames, %d sounds", r, frames, nclicks);
    c.track = 7;
    r = ta_run("3:R 6:A", prefix, &c, &frames);
    CHECK(r == S2TP_BACK && c.track == 7 && nclicks == 0, "the last track: R does nothing (track %d)", c.track);
    /* a new save has three tracks open: England, Egypt, California */
    s2save_unlock_all = 0;
    s2save_set(S2S_TRACKS_OPEN, 0);
    c.track = 1;
    r = ta_run("3:R 30:R 60:R 90:R 130:B", prefix, &c, &frames);
    CHECK(r == S2TP_GO && c.track == 4 && nclicks == 3, "three tracks open: the third is the last (track %d, %d sounds)", c.track, nclicks);
    c.track = 5;                                                /* a track that is not open: the first */
    r = ta_run("3:B", prefix, &c, &frames);
    CHECK(r == S2TP_GO && c.track == 1, "a track that is not open: the page starts on the first (%d)", c.track);
    s2save_set(S2S_TRACKS_OPEN, 4);                             /* all seven by the unlocks, in the original's order */
    c.track = 7;
    r = ta_run("3:L 30:L 60:B", prefix, &c, &frames);
    CHECK(r == S2TP_GO && c.track == 6 && nclicks == 3, "from The Tunnel two places back: Switzerland (%d)", c.track);

    /* the league's TRACK page: ESC before the first race is back, between two races it asks LEAVING LEAGUE? (NO first; LEFT is YES, RIGHT NO, ESC closes it; YES leaves at once,
       NO and ENTER close it), ENTER fades out after 13 frames with the sound 13 */
    char script[400];
    s2save_unlock_all = 1;
    put_keys(script, sizeof script, "4:A");
    begin_run(script, prefix);
    front = 0;
    CHECK(s2tp_league(0, 0, 1) == S2TP_BACK && calls == 5 && nclicks == 0, "league TRACK page: ESC before the first race is back (%u frames)", calls);
    put_keys(script, sizeof script, "4:A 10:R 20:A 30:A 40:L 50:R 60:L 70:B");
    begin_run(script, prefix);
    front = 0;
    r = s2tp_league(2, 1, 4);
    CHECK(r == S2TP_LEAVE && calls == 71 && nclicks == 0, "league TRACK page: the question's YES leaves (result %d after %u frames, %d sounds)", r, calls, nclicks);
    put_keys(script, sizeof script, "4:A 30:B 60:B");
    begin_run(script, prefix);
    front = 0;
    r = s2tp_league(1, 3, 5);
    CHECK(r == S2TP_GO && calls == 60 + 13 && nclicks == 1 && clicks[0] == 13, "league TRACK page: NO closes the question, ENTER goes on (%d after %u frames, %d sounds)", r, calls, nclicks);
    put_keys(script, sizeof script, "4:B");
    begin_run(script, prefix);
    front = 0;
    CHECK(s2tp_league(3, 0, 6) == S2TP_GO && s2tp_league(0, 0, 7) == S2TP_FAIL && s2tp_league(0, 0, 0) == S2TP_FAIL, "the league never races on the Tunnel (no TKG_7L)");

    /* the CAR page of time attack and the championship (the list of set-ups, the choice, CUSTOMIZE): custom_screen.c */

    /* the free run's LOADING screen shows no records: the rows under the banner are the picture's */
    s2save_reset();
    s2save_add_time(S2S_KIND_ATTACK, 1, 1, 77.7f, "ATK", 3, 4, 0);
    begin_run("", prefix);
    const uint32_t *pal = s2load_show_kind(S2S_KIND_ATTACK, 1, 3);
    CHECK(pal != 0, "LOADING of time attack");
    wait_swap_done();                                           /* (the swap goes in) */
    const volatile uint8_t *fb = FB(front);
    int text = 0;
    static uint8_t pic[S2G_PIC_BYTES];
    memcpy(pic, s2gfx_pic(0), S2G_PIC_BYTES);
    for (int i = 72 * 320; i < 136 * 320; i++) text += fb[i] != pic[i];
    CHECK(text > 500, "time attack's LOADING screen has its lines of records (%d pixels)", text);
    begin_run("", prefix);
    pal = s2load_show_kind(S2S_KINDS, 1, 3);
    wait_swap_done();
    fb = FB(front);
    text = 0;
    for (int i = 72 * 320; i < 136 * 320; i++) text += fb[i] != pic[i];
    CHECK(pal != 0 && text == 0, "the free run's LOADING screen has none (%d pixels differ from the picture)", text);
    s2save_unlock_all = S2SAVE_UNLOCK_ALL;
    s2save_reset();
    printf("ui: time attack's pages: seven maps in 17 blocks, TRACK SELECTION and the league's TRACK page, the CAR page's choice and the loading screens by scripted buttons\n");
}

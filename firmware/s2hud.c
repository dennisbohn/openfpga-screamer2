/*
 * The race screen in the chase camera (s2hud.h). Places, fonts, colours and the rules for when each element shows are read from STARTL.EXE
 * (addresses in the comments; the race screen is 0x45a4e, drawn after the scene by 0x2b873): the arithmetic is the original's, in single
 * precision. A frame is one blitter list.
 */
#include "s2hud.h"
#include "s2gfx.h"
#include "s2load.h"
#include "s2save.h"
#include "s2car.h"
#include "race.h"
#include "track.h"
#include "setup.h"
#include "frame.h"
#include "console.h"
#include "dat.h"
#include "dlog.h"
#include <math.h>
#include <string.h>

/* the sprites of GAMEL.SPR and the fonts of GAMEL.FNT (the original's numbers) */
enum { SPR_MARK = 4, SPR_PLAYER = 9, SPR_DIAL = 0x1a, SPR_ARROW = 0x1b, SPR_BAR = 0x25, SPR_LEADER = 0x26, SPR_LIGHTS = 0x42, SPR_LAUREL = 0x72 };
enum { FONT_TITLE = 0, FONT_BIG = 1, FONT_GEAR = 2, FONT_LABEL = 3, FONT_MID = 4, FONT_CHECK = 8 };

/* the car record's words (s2car.h keeps the game's layout) */
union word { int32_t i; uint32_t u; float f; };
#define RF(c, o) (((const union word *)(c)->rec)[(o) >> 2].f)
#define RI(c, o) (((const union word *)(c)->rec)[(o) >> 2].i)

#define NEEDLE_COLOUR   0x5b
#define MAX_CHK         64                      /* checkpoints a race has: 5 a lap, at most 10 laps */
#define MAX_BENDS       16
#define MESSAGE_S       4.0f                    /* the checkpoint gap and BEST LAP! stay 4 s (0x2008d, 0x202c5) */
#define BAR_Y           3                       /* the bar of the cars along the track (0x458c7 is called with y 3) */

/* the texts of DATA/LOCAL.DAT's GAME.TXT, from THIS LAP on (the table at 0x1245d8); the English ones where the file has none */
enum { T_THIS_LAP, T_TOTAL, T_BEST_LAP, T_BEST_TOTAL, T_BEST_LAP_NEW, T_WRONG_WAY, T_POSITION, T_LAP, T_UNIT, TEXTS };
static const char *const english[TEXTS] = { "THIS LAP", "TOTAL", "BEST LAP", "BEST TOTAL", "BEST LAP!", "WRONG WAY", "POSITION", "LAP", "MPH" };

static struct blit_list list;                   /* a frame's commands */
static struct blit_list fixed;                  /* the commands that do not change from frame to frame (labels, records, the bar's frame), made for the buffer 0 */
static struct blit_list *out = &list;           /* where the helpers below draw */
static int fixed_top = -1, fixed_stale = 1;     /* `top` fixed was made for; the records changed since */
static int ready, miles, position_x;                        /* miles: the unit's word begins with M (0x2ef4e): the speed shows as km/h / 1.6 */
static const char *text[TEXTS];

static struct {
    float speed, revs;                          /* the dial's numbers eased towards the car's (0x133098, 0x1330a0) */
    float wrong;                                /* seconds the player's car has been the wrong way round (car + 0x20c) */
    float msg_left, best_left, bend_left;       /* the checkpoint gap (0x133078), BEST LAP! (0x13307c), the bend's arrow (0x133090): seconds left */
    int bend_next, bend_id, nbends;             /* the next of the bends' nodes (0x133080), the arrow now shown (0x133088) */
    struct { int32_t x, z, id; } bend[MAX_BENDS];
    float rec_lap, rec_total;                   /* the track's best lap and total time (0x130d18, 0x130d14), 3599.999 for none (the free run: the best lap since the menu) */
    int kind;                                   /* race_kind */
    float seen_best;                            /* the player's best lap as of the last frame */
    int chk[RACE_CARS], chk_max[RACE_CARS];     /* each car's checkpoint (car + 0x4b0) and the furthest it has been (+ 0x4b4) */
    int32_t leader[MAX_CHK];                    /* the whole seconds at which the car leading got to each checkpoint (0x11ccec) */
    char msg[16];                               /* the gap as the text of 0x132f98: control byte 1, '+', mm:ss:hh */
} S;

int s2hud_init(struct zip *z)
{
    ready = 0;
    if (!s2gfx_game_init(z)) return 0;
    for (int i = 0; i < TEXTS; i++) text[i] = s2gfx_line("THIS LAP", i, english[i]);
    miles = (text[T_UNIT][0] | 0x20) == 'm';
    position_x = 0x138 - s2gfx_game_text(0, FONT_LABEL, 3, -1, 0, 0, text[T_POSITION]);       /* POSITION ends at x 312 */
    ready = 1;
    return 1;
}

int s2hud_ready(void)
{
    return ready;
}

/* ------------------------------------------------------------------------------------------------ the numbers as text */

static char *put2(char *p, uint32_t v)
{
    *p++ = (char)('0' + v / 10 % 10);
    *p++ = (char)('0' + v % 10);
    return p;
}

/* "mm:ss:hh" for seconds as 0x37e1e writes it: the minutes, seconds and hundredths each cut off; 3599.999 and more is the empty record */
static char *digits(char *p, float t)
{
    if (t > 3599.9f) return fmt_str(p, "--:--:--");
    uint32_t whole = t > 0.0f ? (uint32_t)t : 0, hundredths = (uint32_t)((t - (float)whole) * 100.0f);
    p = put2(p, whole / 60);
    *p++ = ':';
    p = put2(p, whole % 60);
    *p++ = ':';
    p = put2(p, hundredths);
    *p = 0;
    return p;
}

/* the clocks begin with the control byte 1: every character the width of the widest, so the digits do not move */
static void clock_text(char *p, float t)
{
    *p++ = 1;
    digits(p, t);
}

/* ------------------------------------------------------------------------------------------------ a race begins */

void s2hud_begin(int track)
{
    struct s2_record total, lap;
    static uint8_t spl[8 + 56 * MAX_BENDS + 8];
    memset(&S, 0, sizeof S);
    S.kind = race_kind;
    if (S.kind == RACE_ATTACK) s2_best_get_kind(S2S_KIND_ATTACK, track, 3, &total, &lap);       /* time attack: its own lists, three laps */
    else s2_best_get(track, race_laps, &total, &lap);
    S.rec_total = total.time;
    S.rec_lap = lap.time;
    if (S.kind == RACE_FREE) S.rec_lap = race_free_state()->best_lap;         /* the free run: the best lap since the menu */
    fixed_stale = 1;
    S.seen_best = RF(race_car(0), 0xc40);
    /* the bends' arrows: MAPnDIR.SPL, a count and 56 bytes a node (x +4, y +8, the arrow +0xc) */
    uint32_t size;
    const uint8_t *map = s2car_map_dat(&size);
    char name[16] = "MAP0DIR.SPL";
    struct dat_entry e;
    name[3] = (char)('0' + track);
    if (map && dat_find(map, size, name, &e) && dat_extract(map, size, &e, spl, sizeof spl) >= 8) {
        int32_t count;
        memcpy(&count, spl, 4);
        if (count > 0 && count <= MAX_BENDS && e.size >= 8u + 56u * (uint32_t)count) {
            for (int i = 0; i < count; i++) {
                int32_t w[4];
                memcpy(w, spl + 8 + 56 * i, 16);
                S.bend[i].x = w[1];
                S.bend[i].z = w[2];
                S.bend[i].id = w[3];
            }
            S.nbends = count;
        }
    }
}

/* ------------------------------------------------------------------------------------------------ what changes from frame to frame */

/* the checkpoints: five to a lap (0x16789: the car's place along the line in nodes, laps and all, times 5 over the line's nodes). A car that
   gets further than ever before: the car that leads notes the second it got there, a player behind it is shown how far he is behind
   (0x1ffcb: the leader's second against his own time) for 4 s */
static void checkpoints(const struct s2car_state *const car[], int n)
{
    const int nodes = race_line_nodes();
    if (nodes < 3) return;
    for (int i = 0; i < n; i++) {
        int chk = (int)(RF(car[i], 0x4b8) * 5.0f / (float)nodes);
        if (chk == S.chk[i]) continue;
        S.chk[i] = chk;
        if (chk <= S.chk_max[i] || chk >= MAX_CHK) continue;
        S.chk_max[i] = chk;
        if (RI(car[i], 0xc30) == 1) {
            S.leader[chk] = (int32_t)RF(car[i], 0xc38);
        } else if (i == 0) {
            S.msg[0] = 1;
            S.msg[1] = '+';
            digits(S.msg + 2, fabsf((float)S.leader[chk] - RF(car[i], 0xc38)));
            S.msg_left = MESSAGE_S;
        }
    }
}

/* the timers and the dial's numbers, `steps` of 20 ms on. finished: the player's car has done its laps (car + 0x4c bit 0) */
static void update(const struct s2car_state *const car[], int n, uint32_t steps, int finished)
{
    const struct s2car_state *p = car[0];
    const float dt = (float)steps * 0.02f;
    float k = 1.0f;
    for (uint32_t i = 0; i < steps; i++) k *= 0.75f;            /* the table's row 0x32 at 50 pictures a second: 0.75 to the power of the pictures (0x315a4) */
    const float speed = RF(p, 0x500) * 1.513f, revs = RF(p, 0x60) != 0.0f ? RF(p, 0xc78) / RF(p, 0x60) : 0.0f;
    S.speed = (S.speed - speed) * k + speed;                    /* 0x111d6 */
    S.revs = (S.revs - revs) * k + revs;
    S.msg_left = S.msg_left > dt ? S.msg_left - dt : 0.0f;
    S.wrong = RF(p, 0x1f0) == -1.0f ? S.wrong + dt : 0.0f;
    checkpoints(car, n);
    if (finished) return;
    /* the bends (0x456c8): within 8,000 units of the next node, its arrow shows for a second */
    if (S.nbends) {
        const int32_t dx = (int32_t)RF(p, 0x4d8) - S.bend[S.bend_next].x, dz = (int32_t)RF(p, 0x4e0) - S.bend[S.bend_next].z;
        const float fx = (float)dx * 0.01f, fz = (float)dz * 0.01f;
        if (sqrtf(fx * fx + fz * fz) < 80.0f) {
            S.bend_left = 1.0f;
            S.bend_id = S.bend[S.bend_next].id;
            if (++S.bend_next >= S.nbends) S.bend_next = 0;
        }
        S.bend_left = S.bend_left > dt ? S.bend_left - dt : 0.0f;
    }
    S.best_left = S.best_left > dt ? S.best_left - dt : 0.0f;
    /* a lap quicker than the player's best and than the track's record (0x201ee): the record is his, BEST LAP! blinks */
    float best = RF(p, 0xc40);
    if (best < S.seen_best) {
        S.seen_best = best;
        if (best < S.rec_lap) {
            S.rec_lap = best;
            fixed_stale = 1;
            S.best_left = MESSAGE_S;
        }
    }
}

/* ------------------------------------------------------------------------------------------------ drawing */

static void sprite(int n, int x, int y) { s2gfx_game_sprite(out, n, x, y); }
static void centred(int n, int y) { s2gfx_game_sprite_centred(out, n, y); }
static int txt(int font, int colour, int space, int x, int y, const char *s) { return s2gfx_game_text(out, font, colour, space, x, y, s); }
static void txt_centred(int font, int colour, int space, int y, const char *s) { s2gfx_game_text_centred(out, font, colour, space, y, s); }

/* the dial (0x46d42): the sprite, the speed and the gear in its two windows, the rev needle from its pivot (0x4923d: three lines to one
   tip, one from the pivot and one from a point a pixel to each side of it) */
static void dial(const struct s2car_state *p)
{
    char t[16], *q;
    sprite(SPR_DIAL, 238, 132);
    const float shown = miles ? S.speed / 1.6f : S.speed;
    uint32_t v = shown > 0.0f ? (uint32_t)shown : 0;
    q = t;
    if (v < 100) *q++ = '0';
    if (v < 10) *q++ = '0';
    fmt_udec(q, v);
    txt(FONT_GEAR, 5, 1, 267, 177, t);
    fmt_udec(t, (uint32_t)(RI(p, 0xc7c) > 0 ? RI(p, 0xc7c) : 0));
    txt(FONT_GEAR, 4, 2, 299, 177, t);
    float a = S.revs * 3.1415927f - 2.0f;
    if (fabsf(a) < 0.001f) a = 0.0f;
    const float s = sinf(a), c = cosf(a);
    const int px = 271, py = 168, tx = (int)((float)px + s * 30.0f), ty = (int)((float)py - c * 30.0f);
    blit_line(out, px, py, tx, ty, 0, 0, 320, 200, NEEDLE_COLOUR);
    blit_line(out, (int)((float)px + c), (int)((float)py + s), tx, ty, 0, 0, 320, 200, NEEDLE_COLOUR);
    blit_line(out, (int)((float)px - c), (int)((float)py - s), tx, ty, 0, 0, 320, 200, NEEDLE_COLOUR);
}

/* the bar of the four cars along the track (0x458c7): a darkened box and the bar (fixed), a mark for each other car (its team's) 50 pixels to the left of
   the middle for each 200,000 units it is behind, to the right for each it is ahead, the player's in the middle */
static void bar_marks(const struct s2car_state *const car[], int n, int y)
{
    for (int i = 1; i < n; i++) {
        float d = (RF(car[0], 0x4bc) - RF(car[i], 0x4bc)) / 200000.0f;
        if (d > 1.0f) d = 1.0f;
        if (d < -1.0f) d = -1.0f;
        sprite(SPR_MARK + race_grid[i].team - 1, (int)((160.0f - d * 50.0f) + -4.0f), y);
    }
    sprite(SPR_PLAYER, 0x9c, y);
}

/* What does not change from frame to frame: the labels, the track's records, the bar's box and frame. Making the commands (each character looks up its
   glyph) took more than half of the screen's cost; they are made when `top` or a record changes and copied into each frame's list. They go
   after the 1ST mark and before everything that is drawn over them (the clocks, the bar's marks); none of them overlaps what comes before. */
static void make_fixed(int top)
{
    char t[24];
    out = &fixed;
    blit_start(&fixed, 0);
    if (S.kind == RACE_FREE) {                                  /* the free run (0x460ae ..): THIS LAP and BEST LAP at the top, the five markers' letters */
        txt(FONT_LABEL, 3, -1, 8, 16, text[T_THIS_LAP]);
        txt(FONT_LABEL, 3, -1, 0x10e, 16, text[T_BEST_LAP]);
        for (int k = 0; k < 5; k++) {
            const char letter[2] = { (char)('A' + k), 0 };
            txt(FONT_LABEL, 3, -1, 8, 0x38 + 8 * k, letter);
            txt(FONT_LABEL, 3, -1, 0x105, 0x38 + 8 * k, letter);
        }
        blit_fill(out, 0x6c, BAR_Y + top + 1, 0x68, 7, 0);
        out = &list;
        fixed_top = top;
        fixed_stale = 0;
        return;
    }
    txt(FONT_LABEL, 3, -1, 8, 2 + top, text[T_LAP]);
    txt(FONT_LABEL, 3, -1, 8, 0x38, text[T_THIS_LAP]);
    txt(FONT_LABEL, 3, -1, 8, 0x48, text[T_TOTAL]);
    txt(FONT_LABEL, 3, -1, 0x10e, 0x38, text[T_BEST_LAP]);
    clock_text(t, S.rec_lap);
    txt(FONT_LABEL, 1, -1, 0x10e, 0x40, t);
    txt(FONT_LABEL, 3, -1, 0x10e, 0x48, text[T_BEST_TOTAL]);
    clock_text(t, S.rec_total);
    txt(FONT_LABEL, 1, -1, 0x10e, 0x50, t);
    blit_fill(out, 0x6c, BAR_Y + top + 1, 0x68, 7, 0);          /* (the original darkens what is there; this is dark) */
    sprite(SPR_BAR, 0x6c, BAR_Y + top - 1);
    out = &list;
    fixed_top = top;
    fixed_stale = 0;
}

/* the fixed commands into the frame's list, for the buffer the frame is drawn into */
static void append_fixed(int buffer)
{
    for (int i = 0; i < fixed.n && list.n < BLIT_MAX; i++, list.n++) {
        uint32_t *c = list.c[list.n];
        c[0] = fixed.c[i][0];
        c[1] = fixed.c[i][1] ^ (uint32_t)buffer << 30;
        c[2] = fixed.c[i][2];
        c[3] = fixed.c[i][3];
        list.src[list.n] = fixed.src[i];
    }
}

/* the 1ST mark over the car that leads when it is not the player's (0x4962c): sprite 0x26 at the point 500 above the car as the scene projects it,
   10 to the left and 12 up */
static void leader_mark(const struct s2car_state *const car[], int n, const struct track_cam *cam, const struct su_view *view)
{
    float f[3], r[3], u[3];
    for (int i = 1; i < n; i++) {
        if (RI(car[i], 0xc30) != 1) continue;
        track_cam_axes(cam, f, r, u);
        const float dx = car[i]->pos[0] - cam->x, dy = car[i]->pos[1] - cam->y, dh = car[i]->pos[2] + 500.0f - cam->h;
        const float depth = dx * f[0] + dy * f[1] + dh * f[2];
        if (depth <= 0.0f || depth > 20000.0f) continue;
        const float k = (float)view->scale / 16.0f / (depth + 128.0f);        /* the lens: depth from the eye, 128 behind the camera */
        const int x = (int)((float)view->cx / 16.0f + (dx * r[0] + dy * r[1] + dh * r[2]) * k) - 10;
        const int y = (int)((float)view->cy / 16.0f - (dx * u[0] + dy * u[1] + dh * u[2]) * k) - 12;
        if (x > -15 && x < 320 && y > -11 && y < 200) sprite(SPR_LEADER, x, y);
    }
}

void s2hud_draw(const struct track_cam *cam, const struct su_view *view, uint32_t steps, int top)
{
    if (!ready) return;
    struct race_hud h;
    const struct s2car_state *car[RACE_CARS];
    const int n = race_cars();
    race_get_hud(&h);
    for (int i = 0; i < n; i++) car[i] = race_car(i);
    update(car, n, steps, h.finished);
    blit_start(&list, (int)draw_buf);
    leader_mark(car, n, cam, view);
    dial(car[0]);
    if (!h.finished && S.kind == RACE_FREE) {                   /* the free run: no lap block, no places; the lap's clock, the best lap and the list of the five markers */
        char t[24];
        const struct race_free *fr = race_free_state();
        if (fixed_stale || fixed_top != top) make_fixed(top);
        append_fixed((int)draw_buf);
        if (S.bend_left > 0.0f) centred(SPR_ARROW + S.bend_id, 0x32 + top);
        clock_text(t, h.lap_time);
        txt(FONT_LABEL, 1, -1, 8, 24, t);
        clock_text(t, S.rec_lap);
        txt(FONT_LABEL, 1, -1, 0x10e, 24, t);
        for (int k = 0; k < 5; k++) {
            clock_text(t, fr->lt[k]);
            txt(FONT_LABEL, 1, -1, 0x12, 0x38 + 8 * k, t);
            t[0] = 1;
            t[1] = fr->sign[k];
            digits(t + 2, fr->rt[k]);
            txt(FONT_LABEL, 1, -1, 0x10a, 0x38 + 8 * k, t);
        }
        if (S.best_left > 0.0f && ((int)(S.best_left * 2.0f) & 1)) txt_centred(FONT_TITLE, 1, 2, 0xf + top, text[T_BEST_LAP_NEW]);
    } else if (!h.finished) {                                   /* (0x45cfb: none of the rest once the player has done his laps) */
        char t[24], *q;
        if (fixed_stale || fixed_top != top) make_fixed(top);
        append_fixed((int)draw_buf);
        if (S.bend_left > 0.0f) centred(SPR_ARROW + S.bend_id, 0x32 + top);
        if (n > 1) {                                            /* (time attack without a ghost: one car, no places, no bar) */
            if (h.place == 1) sprite(SPR_LAUREL, 0x10e, 4);
            txt(FONT_LABEL, 3, -1, position_x, 2, text[T_POSITION]);
            fmt_udec(t, (uint32_t)h.place);
            txt(FONT_BIG, 1, 2, 0x11e, 0xb, t);
        }
        if (S.msg_left > 0.0f) txt_centred(FONT_CHECK, 3, -1, 0x23 + top, S.msg);
        q = fmt_udec(t, (uint32_t)h.lap);
        *q++ = ':';                                             /* ("%d%c": the figures' font has the colon after the nine) */
        *q = 0;
        const int w = txt(FONT_BIG, 1, 2, 8, 0xb + top, t);
        fmt_udec(t, (uint32_t)h.laps);
        txt(FONT_MID, 1, 2, w + 5, 0x14 + top, t);
        clock_text(t, h.lap_time);
        txt(FONT_LABEL, 1, -1, 8, 0x40, t);
        clock_text(t, h.time);
        txt(FONT_LABEL, 1, -1, 8, 0x50, t);
        if (S.best_left > 0.0f && ((int)(S.best_left * 2.0f) & 1)) txt_centred(FONT_TITLE, 1, 2, 0xf + top, text[T_BEST_LAP_NEW]);
        if (n > 1) bar_marks(car, n, BAR_Y + top);
    }
    if (((int)S.wrong & 1)) txt_centred(FONT_TITLE, 1, 2, 0xf + top, text[T_WRONG_WAY]);
    if (h.count >= 0 && !h.finished) centred(SPR_LIGHTS + 3 - h.count, 60);
    hw_blit(&list);
}

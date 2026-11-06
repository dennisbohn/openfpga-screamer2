/*
 * TEAM SELECTION and ENTER YOUR NAME (s2pages3d.h)
 *
 * Read from STARTL.EXE with tools/dis.sh -f (flat addresses; the page is 0x3ffab .. 0x41a60, its setup 0x3ffb9 .. 0x40259, a frame 0x402be ..
 * 0x40a64, the end 0x40a67 ..). A frame of the original: the four badges (turn, glide), the keys, the clocks, the letter tyres in the name state, the
 * engine's drawing of the objects (0x75570), the page's text. The step of every clock is 1/50 s (the Pocket's frame); the original's own is the time of the
 * last picture. Everything is whole numbers and floats as the original has them: positions are whole (the glide cuts), the angles are 2048ths cut to a
 * whole number each frame (the chosen badge turns 20.48 of them a frame, which becomes 20; the others 5.12, which becomes 5).
 *
 * The objects (the engine's records: x, y, z whole, three angles a0, a1, a2): a badge of team i starts at the origin with a1 = 0x200 and glides to its
 * slot, 10 % of the way a frame (the easing table's row 0x50); the slots are (0, 300, 3500) in front, (-3000, 0, 1000) left, (0, 2000, 1000) above and
 * (3000, 0, 1000) right, rotated round as many times as the team's number (the chosen team's badge is always in front). In the name state the
 * badges go on to (slot x, slot y + 500, slot z - 500) for the chosen one and (slot x, slot y - 500, slot z - 25000) for the others. The camera
 * stands at (0, 500, 4800), looking along -z.
 *
 * The letters (0x3625d): letter k of 26 stands on a ring of radius (spin^2 5000 + 3500) round the origin at the angle (k - chosen) * -2 pi / 26, the angle
 * blended from the one it had to the new one by t (0 to 1 at 8 a second after each turn of the ring), seen along its own heading; spin is 1 and falls
 * to 0 at 2 a second (the letters fly in from the outside); each tilts about x, the chosen one to -0.314, the others to 2.827 radians, at 4 pi a second.
 */
#include "s2pages3d.h"
#include "s2scene.h"
#include "s2gfx.h"
#include "s2page.h"
#include "s2sound.h"
#include "s2save.h"
#include "s2attract.h"
#include "dlog.h"
#include "hw.h"
#include <math.h>
#include <string.h>

#define DT          0.02f
#define BADGES      4
#define LETTERS     26
#define PI          3.1415927f
#define TWO_PI      6.2831853f
#define SOUND_MOVE  4                       /* MENU.DTR ids of the page's sounds (0x4067c, 0x40543, 0x40560, 0x4067c) */
#define SOUND_NAME  6
#define SOUND_KEEP  1
#define SOUND_LETTER 9
#define FADE_FRAMES 13                      /* the flash to white: 0.25 s, 4 a second (0x40ade: the fade is dt * 4 a frame) */

struct obj { int32_t pos[3]; int a0, a1; };
struct letter { int flag; float tilt, spin; };

static struct page {
    struct s2_menu_cfg *cfg;
    int team, state, yn, cursor, cur, prev;
    char name[4];
    float t;                                /* the ring's turn, 0 .. 1 */
    int32_t slot[BADGES][3];
    struct obj badge[BADGES], let_obj[LETTERS];
    struct letter let[LETTERS];
    uint32_t before;                        /* the buttons held in the last frame */
    uint32_t clock;                         /* the original's blink clock (0x4cfd6): hundredths, from the start of the page */
    int leaving, fade_frames, result;
    float idle;                             /* seconds left of the 30 on TEAM SELECTION without a button (0x40236) */
    uint32_t pal_work[256];
    int fresh;
    const uint8_t *mlb, *badge_mesh[BADGES], *letter_mesh;
    const char *title, *name_title, *exit_text, *yes, *no;
} P;

static uint32_t pal[256];

/* ------------------------------------------------------------------------------------------------ the engine's small routines */

static int32_t cut(float f)
{
    if (!(f > -2147483648.0f)) return INT32_MIN;
    if (f >= 2147483648.0f) return INT32_MAX;
    return (int32_t)f;
}

/* 0x3364d / 0x11212: a whole number eased towards a target (the row of the easing table), cut */
static void ease_int(int32_t *v, float target, int row)
{
    *v = cut(((float)*v - target) * s2scene_easing(row) + target);
}

/* 0x33703: the n records of 3 numbers turned round one place: direction 1 (each moves on, the last comes first) or 0 */
static void rotate_slots(int32_t a[BADGES][3], int dir)
{
    int32_t tmp[3];
    if (dir) {
        memcpy(tmp, a[BADGES - 1], sizeof tmp);
        for (int k = BADGES - 1; k > 0; k--) memcpy(a[k], a[k - 1], sizeof tmp);
        memcpy(a[0], tmp, sizeof tmp);
    } else {
        memcpy(tmp, a[0], sizeof tmp);
        for (int k = 0; k < BADGES - 1; k++) memcpy(a[k], a[k + 1], sizeof tmp);
        memcpy(a[BADGES - 1], tmp, sizeof tmp);
    }
}

/* ------------------------------------------------------------------------------------------------ the page */

static void letters_reset(struct page *p)
{
    for (int k = 0; k < LETTERS; k++) {
        p->let[k].flag = 1;
        p->let[k].spin = 1.0f;
    }
}

static void setup(struct page *p)
{
    memset(&p->badge, 0, sizeof p->badge);
    memset(&p->let_obj, 0, sizeof p->let_obj);
    memset(&p->let, 0, sizeof p->let);
    for (int i = 0; i < BADGES; i++) {
        p->slot[i][0] = i == 0 ? 0 : (i - 1) * 3000 - 3000;
        p->slot[i][1] = i == 0 ? 300 : i == 2 ? 2000 : 0;
        p->slot[i][2] = i == 0 ? 3500 : 1000;
        p->badge[i].a1 = 0x200;
    }
    for (int k = 0; k < p->team; k++) rotate_slots(p->slot, 1);
    letters_reset(p);
    p->state = 0;
    p->yn = 0;
    p->cursor = 0;
    p->t = 1.0f;                            /* (0x40: the clocks are started when the first letter turns) */
    p->leaving = p->fade_frames = p->result = 0;
}

/* the badges (0x402be .. 0x40421): the chosen one turns faster; the glide to the slots */
static void team_frame(void *self, uint32_t tick)
{
    struct page *p = self;
    p->clock = tick * 2;
    for (int i = 0; i < BADGES; i++) {
        struct obj *o = &p->badge[i];
        o->a1 = cut((float)o->a1 + DT * (i == p->team ? 1024.0f : 256.0f)) & 0x7FF;
        for (int k = 0; k < 3; k++) {
            float target = (float)p->slot[i][k];
            if (p->state == 1) {
                if (k == 1) target += i == p->team ? 500.0f : -500.0f;
                if (k == 2) target -= i == p->team ? 500.0f : 25000.0f;
            }
            ease_int(&o->pos[k], target, 0x50);
        }
    }
    if (p->leaving) p->fade_frames++;
}

static void sound(int id) { s2sound_menu(id); }

static int team_key(void *self, uint32_t press)
{
    struct page *p = self;
    (void)press;                            /* (the original's own: a press once, the ring's keys held) */
    uint32_t keys = in_keys(), edge = keys & ~p->before;
    p->before = keys;
    if (p->leaving) return p->fade_frames > FADE_FRAMES ? p->result : 0;
    if (keys) s2att_idle_reset(&p->idle);   /* (0x405ed ..: a key resets the timer in every state; it runs down in the team state only, 0x4083c) */
    else if (p->state == 0 && s2att_idle_step(&p->idle)) return S2P3D_IDLE;     /* (0x4085d: the fade is set to 1.0 at once: a cut to black, no fade out) */
    uint32_t enter = S2PAGE_ENTER, esc = S2PAGE_ESC;
    if (edge & esc) {
        if (p->state == 1) {
            letters_reset(p);                    /* (the tyres are hidden again; they fly in once more the next time) */
            p->state = 0;
        } else {
            p->state = p->state == -1 ? 0 : -1;
            p->yn = 0;
        }
    } else if (edge & enter) {
        if (p->state == 0) {
            p->state = 1;
            p->t = 1.0f;
            sound(SOUND_NAME);
        } else if (p->state == 1) {
            sound(SOUND_KEEP);
            p->leaving = 1;
            p->result = S2P3D_DONE;
        } else if (p->yn == 0) {
            p->leaving = 1;
            p->result = S2P3D_QUIT;
        } else {
            p->state = 0;
        }
    } else if (p->state == 0 && (edge & (KEY_LEFT | KEY_RIGHT))) {
        int right = (edge & KEY_RIGHT) != 0;
        p->team = (p->team + (right ? 1 : 3)) & 3;
        rotate_slots(p->slot, right);
        sound(SOUND_MOVE);
    } else if (p->state == 1 && (keys & (KEY_LEFT | KEY_RIGHT)) && p->t > 0.95f) {
        sound(SOUND_LETTER);
        p->prev = p->cur;
        if (keys & KEY_RIGHT) p->cur = p->cur + 1 > LETTERS - 1 ? 0 : p->cur + 1;
        else p->cur = p->cur - 1 < 0 ? LETTERS - 1 : p->cur - 1;
        p->t = 0.0f;
    } else if (p->state == -1 && (edge & KEY_RIGHT) && p->yn == 0) {
        p->yn = 1;
    } else if (p->state == -1 && (edge & KEY_LEFT) && p->yn > 0) {
        p->yn = 0;
    } else if (p->state == 1 && (edge & KEY_UP) && p->cursor > 0) {
        p->cursor--;
    } else if (p->state == 1 && (edge & KEY_DOWN) && p->cursor < 2) {
        p->cursor++;
    }
    return 0;
}

/* one letter tyre (0x3625d) */
static void letter_step(struct page *p, int k)
{
    struct letter *r = &p->let[k];
    struct obj *o = &p->let_obj[k];
    r->spin -= DT * 2.0f;
    if (r->spin < 0.0f) r->spin = 0.0f;
    float f1 = (float)(k - p->cur), f2 = (float)(k - p->prev);
    if (f1 < 0.0f) f1 += 26.0f;
    if (f2 < 0.0f) f2 += 26.0f;
    const float step = 0.2416609823703766f;
    float radius = (float)cut(r->spin * r->spin * 5000.0f + 3500.0f);
    float a1 = f1 * -step, a2 = f2 * -step;
    if (fabsf(a1 - a2) > 1.570796370506286f) {
        if (a1 > a2) a1 += -TWO_PI;
        else a2 += -TWO_PI;
    }
    float ang = a2 - (a2 - a1) * p->t;
    o->pos[0] = cut(radius * sinf(ang));        /* 0x10388: (0, 0, radius) turned about the vertical */
    o->pos[1] = 0;
    o->pos[2] = cut(radius * cosf(ang));
    float yaw = (float)o->a1 * 0.0009765625f * PI;
    if (p->t < 1.0f) s2scene_ease_angle(&yaw, f1 * step, 0x37);
    else yaw = f1 * step;
    o->a1 = cut(yaw * 1024.0f / PI) & 0x7FF;
    o->a0 = cut(r->tilt * 1024.0f / PI) & 0x7FF;
    if (r->flag) {
        if (r->tilt != 2.827433466911316f) {
            r->tilt += DT * 12.566370964050293f;
            if (r->tilt > 2.827433466911316f) r->tilt = 2.8274335861206055f;
        }
    } else if (r->tilt != -0.3141592741012573f) {
        r->tilt -= DT * 12.566370964050293f;
        if (r->tilt < -0.3141592741012573f) r->tilt = -0.3141592741012573f;
    }
}

/* the flash to white of the way out (0x40ad8: the palette blended towards 255, 255, 255 by f) */
static void flash(const struct page *p, float f, uint32_t out[256])
{
    for (int i = 0; i < 256; i++) {
        uint32_t c = pal[i];
        uint32_t r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
        r += (uint32_t)((255.0f - (float)r) * f);
        g += (uint32_t)((255.0f - (float)g) * f);
        b += (uint32_t)((255.0f - (float)b) * f);
        out[i] = r << 16 | g << 8 | b;
    }
    (void)p;
}

static void team_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct page *p = self;
    (void)tick;
    const int buffer = l->buffer;
    if (p->state == 1) {
        if (p->t < 1.0f) {
            p->t += DT * 8.0f;
            if (p->t > 1.0f) p->t = 1.0f;
        }
        for (int k = 0; k < LETTERS; k++) p->let[k].flag = k != p->cur;
        for (int k = 0; k < LETTERS; k++) letter_step(p, k);
    }
    const struct track_cam cam = s2scene_camera(0.0f, 500.0f, 4800.0f, 0.0f);
    struct track_block b[BADGES + LETTERS];
    float key[BADGES + LETTERS];
    int n = 0;
    for (int i = 0; i < BADGES + (p->state == 1 ? LETTERS : 0); i++) {
        const int badge = i < BADGES;
        const struct obj *o = badge ? &p->badge[i] : &p->let_obj[i - BADGES];
        struct track_block t;
        s2scene_object(badge ? p->badge_mesh[i] : p->letter_mesh, (float)o->pos[0], (float)o->pos[1], (float)o->pos[2], o->a0, o->a1,
                       badge ? 0u : ((uint32_t)(i - BADGES) * 0x9D8u + 0x226u) >> 2, &t);
        float d = s2scene_dist2(&cam, (float)o->pos[0], (float)o->pos[1], (float)o->pos[2]);
        int j = n++;
        for (; j > 0 && key[j - 1] < d; j--) {                  /* far first */
            b[j] = b[j - 1];
            key[j] = key[j - 1];
        }
        b[j] = t;
        key[j] = d;
    }
    s2scene_begin(p->mlb, buffer, p->fresh);
    p->fresh = 0;
    s2scene_blocks(&cam, b, n);
    s2scene_end();
    blit_start(l, buffer);                                      /* the text goes over the 3D (the blitter waits for the units) */
    if (p->state != 1) {
        s2gfx_text_centred(l, S2G_FONT_TITLE, S2G_WHITE, 2, 4, p->title);
        s2gfx_text_centred(l, S2G_FONT_MEDIUM, S2G_WHITE, 2, 175, s2menu_team_name(p->team + 1));
    } else {
        s2gfx_text_centred(l, S2G_FONT_TITLE, S2G_WHITE, 2, 4, p->name_title);
        p->name[p->cursor] = (char)('A' + p->cur);
        char s[8] = { 1, p->name[0], p->name[1], p->name[2], 0 };
        s2gfx_text_centred(l, S2G_FONT_MEDIUM, S2G_WHITE, 2, 176, s);
        s2gfx_text(l, S2G_FONT_MEDIUM, S2G_WHITE, 2, 136 + 19 * p->cursor, 186, "-");
    }
    if (p->state == -1) {
        s2gfx_text_centred(l, S2G_FONT_SMALL, S2G_WHITE, 2, 92, p->exit_text);
        const int blink = (p->clock & 0x3F) <= 0x20;
        const int yes_on = p->yn == 0 && blink, no_on = p->yn == 1 && blink;
        s2gfx_text_boxed(l, yes_on ? S2G_FONT_SMALL2 : S2G_FONT_SMALL, yes_on ? 3 : S2G_WHITE, 2, 120, 40, 110, p->yes);
        s2gfx_text_boxed(l, no_on ? S2G_FONT_SMALL2 : S2G_FONT_SMALL, no_on ? 3 : S2G_WHITE, 2, 160, 40, 110, p->no);
    }
    if (p->leaving) {
        float f = (float)p->fade_frames / (float)FADE_FRAMES;
        flash(p, f > 1.0f ? 1.0f : f, p->pal_work);
    }
}

/* ------------------------------------------------------------------------------------------------ loading */

#define MODELS_PAD(n)   (((n) + 255u) & ~255u)

static int load(struct page *p, struct zip *z)
{
    static struct s2_cont c;
    uint8_t *to = s2scene_models();
    uint32_t room = S3D_MODELS_MAX, n;
    if (!s2scene_open(z, &c, "GFX/LOGO1.DAT")) return 0;
    p->mlb = to;
    if (!(n = s2scene_entry(&c, "LOGO.MLB", to, room))) return 0;
    to += MODELS_PAD(n);
    room -= MODELS_PAD(n);
    for (int i = 0; i < BADGES; i++) {
        char name[16] = "A1LOG-00.3DX";
        name[1] = (char)('1' + i);
        p->badge_mesh[i] = to;
        if (!(n = s2scene_entry(&c, name, to, room))) return 0;
        to += MODELS_PAD(n);
        room -= MODELS_PAD(n);
    }
    p->letter_mesh = to;
    return s2scene_entry(&c, "ALETTER.3DX", to, room) != 0;
}

int s2_page_team(struct s2_menu_cfg *c)
{
    struct zip *z = s2gfx_zip();
    if (!s2gfx_ready() || !s2gfx_bkk("LOGOL.BKK", s2gfx_pic(0), pal)) return S2P3D_FAIL;
    struct page *p = &P;
    memset(p, 0, sizeof *p);
    const uint32_t t0 = rdcycle();
    if (load(p, z)) {
        dlog_str("pages3d: LOGO1.DAT in ");
        dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
        dlog_str(" ms\n");
    } else {
        dlog_str("pages3d: LOGO1.DAT could not be read\n");
        return S2P3D_FAIL;
    }
    p->cfg = c;
    p->team = c->team - 1;
    p->title = s2gfx_line("TEAM SELECTION", 0, "TEAM SELECTION");
    p->name_title = s2gfx_line("ENTER YOUR NAME", 0, "ENTER YOUR NAME");
    p->exit_text = s2gfx_line("EXIT SCREAMER 2?", 0, "EXIT SCREAMER 2?");
    p->yes = s2gfx_line("ARE YOU SURE ?", 1, "YES");
    p->no = s2gfx_line("ARE YOU SURE ?", 2, "NO");
    const char *nm = s2save_name();
    memcpy(p->name, nm, 3);
    p->name[3] = 0;
    p->cur = p->name[0] >= 'A' && p->name[0] <= 'Z' ? p->name[0] - 'A' : 0;
    p->prev = p->cur;
    p->fresh = 1;
    s2att_idle_reset(&p->idle);
    setup(p);
    memcpy(p->pal_work, pal, sizeof pal);
    struct s2_page pg = { s2gfx_pic(0), p->pal_work, 0, 0, team_frame, team_key, team_draw };
    p->before = in_keys();
    int r = s2page_run(&pg, p);
    if (r == S2P3D_DONE) {
        for (int i = 0; i < 3; i++)
            if (p->name[i] < 'A' || p->name[i] > 'Z') p->name[i] = 'A';
        c->team = p->team + 1;
        s2save_set(S2S_TEAM, c->team);
        s2save_set_name(p->name);
        s2save_set(S2S_PROFILE, 1);
        if (c->car > 4 && !s2save_car5_open(c->team)) c->car = 1;
    }
    return r;
}

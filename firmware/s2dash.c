/*
 * The cockpit's pictures and instruments (s2dash.h)
 */
#include "s2dash.h"
#include "s2gfx.h"
#include "s2car.h"
#include "tb_hud.h"
#include "dat.h"
#include "dlog.h"
#include "hw.h"
#include <string.h>

/* SDRAM_BASE + 0xC80000 (free: the race screen's set at 0xC00000 has 0.5 MB, the next user is at 0x1000000): the unpacked picture, the mirror's boxes, the eight cockpits' */
#define MEM         (SDRAM_BASE + 0xC80000u)
#define UNPACKED    ((uint8_t *)(uintptr_t)MEM)                       /* the largest picture is 27,160 bytes */
#define UNPACKED_MAX 0x8000u
#define MIRROR_BOX  ((uint8_t *)(uintptr_t)(MEM + 0x8000u))
#define MIRROR_MAX  0x2000u
#define COCKPIT(m)  ((uint8_t *)(uintptr_t)(MEM + 0x10000u + 0x10000u * (uint32_t)(m)))
#define COCKPIT_MAX 0x10000u

#define NEEDLE_COLOUR   12                      /* 0x46755: colour 12 of the palette */

static struct s2ck_dash dash[S2CK_DASHES];
static int have_table;
static uint8_t have_pic[S2CK_DASHES];
static int have_mirror;

static void settle(void)
{
    while (!(MEM_STATUS & MEM_WRITES_IDLE))
        ;
}

/* a picture of the container made boxes at out: 1 if it is there; *boxes its count */
static int picture(const char *name, uint8_t *out, uint32_t cap, uint32_t *boxes)
{
    const uint32_t size = s2gfx_game_entry(name, UNPACKED, UNPACKED_MAX);
    if (!size) return 0;
    if (!tb_hud_picture(UNPACKED, size, out, cap)) return 0;
    *boxes = *(const uint32_t *)out;
    return 1;
}

int s2dash_init(void)
{
    const uint32_t t0 = rdcycle();
    uint32_t boxes = 0, most = 0;
    int n = 0;
    have_mirror = picture("MIRROR_L.PKC", MIRROR_BOX, MIRROR_MAX, &boxes);
    for (int m = 0; m < S2CK_DASHES; m++) {
        char name[16] = "COCK0_L.PKC";
        name[4] = (char)('1' + m);
        have_pic[m] = (uint8_t)picture(name, COCKPIT(m), COCKPIT_MAX, &boxes);
        if (have_pic[m]) {
            n++;
            if (boxes > most) most = boxes;
        }
    }
    settle();
    dlog_str("ui: ");
    dlog_dec(n);
    dlog_str(" cockpits, the mirror's frame ");
    dlog_str(have_mirror ? "yes" : "NO");
    dlog_str(", at most ");
    dlog_dec((int32_t)most);
    dlog_str(" boxes, ");
    dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
    dlog_str(" ms\n");
    return n == S2CK_DASHES && have_mirror;
}

int s2dash_begin(void)
{
    uint32_t size, text_size;
    static uint8_t text[2304];                  /* CRUSC.DTR is 2,064 bytes */
    const uint8_t *game = s2car_game_dat(&size);
    struct dat_entry e;
    have_table = 0;
    if (!game || !dat_find(game, size, "CRUSC.DTR", &e) || e.size > sizeof text) return 0;
    text_size = dat_extract(game, size, &e, text, sizeof text);
    have_table = text_size && s2ck_dash_parse(text, text_size, dash);
    return have_table;
}

const struct s2ck_dash *s2dash_row(int model)
{
    return have_table && model >= 0 && model < S2CK_DASHES ? &dash[model] : 0;
}

int s2dash_have(int model)
{
    return have_table && model >= 0 && model < S2CK_DASHES && have_pic[model];
}

/* a picture as boxes (tb_hud_picture()'s table, then the pixels) at their places */
static void boxes(struct blit_list *l, const uint8_t *m)
{
    const struct blit_image *b = (const struct blit_image *)(m + 4);
    const uint32_t n = *(const uint32_t *)m;
    for (uint32_t i = 0; i < n; i++)
        blit_copy(l, m + b[i].off, b[i].w, b[i].h, b[i].w, b[i].x, b[i].y, BLIT_COPY, 0);
}

void s2dash_mirror_frame(struct blit_list *l)
{
    if (have_mirror) boxes(l, MIRROR_BOX);
}

void s2dash_cockpit(struct blit_list *l, int model, int gear, float speed, float revs)
{
    if (!s2dash_have(model)) return;
    const struct s2ck_dash *d = &dash[model];
    boxes(l, COCKPIT(model));
    int x, y, spr;
    s2ck_lamp(d, model, gear, &x, &y, &spr);
    s2gfx_game_sprite(l, spr, x, y);
    struct s2ck_line ln[3];
    for (int k = 0; k < 2; k++) {
        if (k == 0) s2ck_needle(&d->speed, d->speed_max ? speed / (float)d->speed_max : 0.0f, ln);
        else s2ck_needle(&d->revs, revs, ln);
        for (int i = 0; i < 3; i++) blit_line(l, ln[i].x0, ln[i].y0, ln[i].x1, ln[i].y1, 0, 0, 320, 200, NEEDLE_COLOUR);
    }
}

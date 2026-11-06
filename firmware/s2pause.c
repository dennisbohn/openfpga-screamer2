/*
 * The race's pause menu (s2pause.h)
 */
#include "s2pause.h"
#include "s2gfx.h"
#include "s2page.h"
#include "s2sound.h"
#include "music.h"
#include "dlog.h"
#include "hw.h"

#define ITEMS       3                   /* the most: RESUME, RESTART, EXIT (the championship's has no RESTART) */
#define DIM         15                  /* the dim items are font colour 15: the palette entries 241 .. 255, which the still never uses */
#define BOX_X       0x50                /* the box darkened once more under the items (the original's numbers) */
#define BOX_W       0xA0
#define EASE        874                 /* the effects' level (0x8000 whole) falls by this a video frame: 0xaee60 takes the sound manager's master (0x5fff, the default effects volume, measured) down by
                                           (int)(dt x 32767) a frame (0x4a231): 0.75 s to nothing */
#define PAUSE_CLICKS 1                  /* the original's pause is silent (MENU.DTR is not loaded in a race); ours clicks, as its menu pages do */

static struct {
    const char *item[ITEMS];
    int id[ITEMS];                      /* what each item answers */
    int n, sel;
    int32_t effects;                    /* the race's effects at this of 0x8000 */
} p;

static uint32_t dimmed[256];

static int item_y(int i)
{
    return i * 0x16 - p.n * 0xB + 0x66;
}

static void pause_frame(void *self, uint32_t tick)
{
    (void)self;
    (void)tick;
    music_frame();                      /* the song goes on: its ring is kept filled */
    if (p.effects > 0) {
        p.effects -= EASE;
        if (p.effects < 0) p.effects = 0;
        s2sound_effects(p.effects);
    }
}

static int pause_key(void *self, uint32_t press)
{
    (void)self;
    int d = ((press & KEY_DOWN) ? 1 : 0) - ((press & KEY_UP) ? 1 : 0);
    if (d) {
        p.sel = (p.sel + p.n + d) % p.n;
        if (PAUSE_CLICKS) s2sound_menu(S2PAGE_SND_MOVE);
    }
    if (press & S2PAGE_ESC) {
        if (PAUSE_CLICKS) s2sound_menu(S2PAGE_SND_BACK);
        return S2PAUSE_RESUME;
    }
    if (press & S2PAGE_ENTER) {
        if (PAUSE_CLICKS) s2sound_menu(S2PAGE_SND_SELECT);
        return p.id[p.sel];
    }
    return 0;
}

/* The items dim, the chosen one over them bright for the half of every 0.64 s (bit 0x20 of the original's hundredths) */
static void pause_draw(void *self, struct blit_list *l, uint32_t tick)
{
    (void)self;
    for (int i = 0; i < p.n; i++) s2gfx_text_centred(l, S2G_FONT_MEDIUM, DIM, 2, item_y(i), p.item[i]);
    if (!((tick * 2) & 0x20)) s2gfx_text_centred(l, S2G_FONT_MEDIUM, S2G_WHITE, 2, item_y(p.sel), p.item[p.sel]);
}

int s2pause_run(int front, const uint32_t *pal)
{
    return s2pause_run_items(front, pal, 1);
}

int s2pause_run_items(int front, const uint32_t *pal, int restart)
{
    static uint8_t dark[256];
    uint32_t t0 = rdcycle();
    uint8_t *still = s2gfx_pic(1);
    p.n = 0;
    p.item[p.n] = s2gfx_line("RESUME", 0, "RESUME");
    p.id[p.n++] = S2PAUSE_RESUME;
    if (restart) {                      /* (the championship's race has none: the race is run once, 0x49f8e) */
        p.item[p.n] = s2gfx_line("RESUME", 1, "RESTART");
        p.id[p.n++] = S2PAUSE_RESTART;
    }
    p.item[p.n] = s2gfx_line("RESUME", 3, "EXIT");
    p.id[p.n++] = S2PAUSE_EXIT;
    p.sel = 0;
    p.effects = 0x8000;
    /* the still: the picture on show through the darkening table (never into the dim ramp), the box under the items once more; the
       palette the same but for that ramp, the white one at 0.45 */
    s2gfx_dark_table(pal, dark, DIM * 16 + 1);
    s2gfx_snapshot(front, still, dark);
    s2gfx_dark_box(still, BOX_X, 0x5E - p.n * 0xB, BOX_W, p.n * 0x16 + 0xC, dark);
    for (int i = 0; i < 256; i++) dimmed[i] = pal[i];
    for (int s = 1; s < 16; s++) {
        uint32_t c = pal[S2G_WHITE * 16 + s];
        dimmed[DIM * 16 + s] = ((c >> 16 & 255) * 45 / 100) << 16 | ((c >> 8 & 255) * 45 / 100) << 8 | (c & 255) * 45 / 100;
    }
    uint32_t t1 = rdcycle();
    const struct s2_page page = { still, dimmed, 0, 0, pause_frame, pause_key, pause_draw };
    static int logged;
    if (!logged++) {                    /* (the log has 4 KB: the first pause tells what it cost) */
        dlog_str("pause: still and palette ");
        dlog_dec((int32_t)((t1 - t0) / (CPU_HZ / 1000)));
        dlog_str(" ms\n");
    }
    int r = s2page_run(&page, 0);
    s2sound_effects(0x8000);
    return r;
}

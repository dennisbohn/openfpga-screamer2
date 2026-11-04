/*
 * The page runner (s2page.h)
 */
#include "s2page.h"
#include "s2gfx.h"
#include "hw.h"

#define REPEAT_FIRST    25              /* a held d-pad button repeats after 0.5 s, every 6th frame (the start menu's) */
#define REPEAT_EVERY    6
#define FULL            0x8000

void s2page_keys_init(struct s2_keys *k)
{
    k->before = in_keys();
    k->held = 0;
}

uint32_t s2page_press(struct s2_keys *k)
{
    const uint32_t dirs = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
    uint32_t keys = in_keys(), press = keys & ~k->before;
    k->held = (keys & dirs) && keys == k->before ? k->held + 1 : 0;
    if (k->held >= REPEAT_FIRST && (k->held - REPEAT_FIRST) % REPEAT_EVERY == 0) press |= keys & dirs;
    k->before = keys;
    return press;
}

int s2page_run(const struct s2_page *p, void *self)
{
    struct s2_keys keys;
    s2page_keys_init(&keys);
    int32_t fade = p->fade_in ? 0 : FULL;
    int result = 0, leaving = 0;
    for (uint32_t tick = 0;; tick++) {
        struct blit_list *l = s2gfx_begin();                /* (waits for the last swap) */
        uint32_t press = s2page_press(&keys);
        if (p->frame) p->frame(self, tick);
        if (!leaving && (result = p->key(self, press)) != 0) {
            if (!p->fade_out || result < 0) return result;      /* (a negative result leaves at once: the original's ESC is a cut) */
            leaving = 1;
        }
        if (p->picture) s2gfx_picture(l, p->picture);
        if (p->draw) p->draw(self, l, tick);
        s2gfx_end(p->pal, fade);
        if (leaving) {
            fade -= (FULL + p->fade_out - 1) / p->fade_out;
            if (fade <= 0) return result;
        } else if (fade < FULL) {
            fade += (FULL + p->fade_in - 1) / p->fade_in;
            if (fade > FULL) fade = FULL;
        }
    }
}

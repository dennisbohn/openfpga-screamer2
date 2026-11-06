/*
 * The CAR CUSTOMIZATION page's 2D parts (firmware/s2custom.c's custom_overlay, its static drawing: the file is included) for tools/host/garage_match.c, which compares them with the
 * running original's screenshots: the title, the panel of the six pictures and the two buttons, the cursor, the item's name.
 */
#include "../s2custom.c"

/* the page with the cursor on `item`, no picture open, at `clock` hundredths of the blinking's clock */
void garage_custom_overlay(struct blit_list *l, int item, int clock)
{
    struct s2_menu_cfg cfg = { .track = 1, .team = 1, .car = 2, .laps = 1, .skill = 1 };
    struct page pg = { .cfg = &cfg, .item = item, .six = { 2, 2, 2, 2, 2, 2 } };
    pg.now = (uint32_t)clock / 2;
    pg.t0 = 0;
    custom_overlay(&pg, l);
}

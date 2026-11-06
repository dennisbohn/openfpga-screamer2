/*
 * The 2D toolkit (s2gfx.h)
 */
#include "s2gfx.h"
#include "frame.h"
#include "hw.h"
#include "dat.h"
#include "dlog.h"
#include "screen.h"
#include "zip.h"
#include <string.h>

/* the SDRAM map (s2gfx.h) */
#define MEM         (SDRAM_BASE + 0xA00000)
#define PIC(k)      ((uint8_t *)(uintptr_t)(MEM + 0x10000u * (k)))
#define STAGE       ((uint8_t *)(uintptr_t)(MEM + 0x20000))
#define STAGE_MAX   0x28000u
#define UNPACK      ((uint8_t *)(uintptr_t)(MEM + 0x48000))
#define UNPACK_MAX  0x12000u
#define SECT        ((uint8_t *)(uintptr_t)(MEM + 0x60000))
#define SECT_MAX    0xA0000u
#define WHOLE       ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x1000000))    /* a deflated GRAF1.DAT, whole */
#define WHOLE_MAX   0x800000u
#define GAME_SPR    ((uint8_t *)(uintptr_t)(MEM + 0x100000))            /* GAMEL.SPR, unpacked (197,668 bytes) */
#define GAME_MAX    0x40000u
#define GAME_FIRST  0x27                        /* GAMEL.SPR's seven track banners: 0x27 .. 0x2d */
/* the race screen's set (s2gfx_game_init): GAMEL.SPR as the file is (197,668 bytes), then its fonts' glyphs; GAME1.DAT of a deflated ZIP is
   inflated whole at GWHOLE (after GRAF1.DAT's), only while the set is read */
#define GMEM        ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0xC00000))
#define GSPR_MAX    0x40000u
#define GGLYPH_MAX  0x40000u
#define GWHOLE      ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x1500000))
#define GDIR_MAX    1024u                       /* GAME1.DAT's directory: 20 records of 32 bytes and a size */

#define BKK_BYTES   64792u                      /* "V652", 256, 320, 200, 64000, 0; 768 palette bytes; the pixels */
#define BKK_MAGIC   0x32353656u
#define TAIL_MAX    4096u                       /* the end of GRAF1.DAT: its directory (60 records of 32 bytes) and size */
#define IMAGES      1024
#define GIMAGES     768                         /* GAMEL's: 115 sprites and 578 glyphs */
#define FONTS       12                          /* (MENUL.FNT has 6, GAMEL.FNT 9) */
#define LINES       400

static struct zip *zip;
static const struct zip_entry *graf;            /* GFX/GRAF1.DAT */
static uint32_t dir_buf[TAIL_MAX / 4 + 4];
static const uint8_t *dir;                      /* what dat_find reads: the container's end, or all of it when it is deflated */
static uint32_t dir_len;
static struct blit_list list;
/* a set of sprites and fonts: images 0 .. nsprites - 1 are the sprites, then each font's glyphs in turn; an image's pixels are at base + off */
struct set {
    struct blit_image *img;
    struct blit_font *font;
    const uint8_t *base;
    uint32_t nsprites, nfonts, max_img;
};
static struct blit_image img[IMAGES], gimg[GIMAGES];            /* MENUL.SPR's sprites, then the fonts' glyphs; GAMEL's the same */
static struct blit_font font[FONTS], gfont[FONTS];
static struct set menu = { img, font, 0, 0, 0, IMAGES }, game = { gimg, gfont, 0, 0, 0, GIMAGES };
static int banner_img;                          /* the image number of the first race banner (after the menu's fonts), 0: none */
static int clip_text_y0, clip_text_y1 = FB_H, clip_spr_y0, clip_spr_y1 = FB_H;
static struct { int x, y, w, h, passes; const uint8_t *table; } later[4];
static int nlater;

static int ready;

int s2gfx_ready(void)
{
    return ready;
}

struct zip *s2gfx_zip(void)
{
    return zip;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint8_t *s2gfx_pic(int k)
{
    return PIC(k);
}

/* The entry `name` of a container (its ZIP entry c, its directory: the container's end, or all of it when the ZIP is deflated) unpacked
   to out (max bytes): its size, 0 if it is missing or damaged. A stored ZIP gives the entry's stored bytes by range (stage), a
   deflated one has the container whole in memory. */
static uint32_t entry_of(const struct zip_entry *c, const uint8_t *cdir, uint32_t cdir_len, const char *name, uint8_t *out, uint32_t max)
{
    struct dat_entry d;
    if (!c || !dat_find(cdir, cdir_len, name, &d)) return 0;
    if (c->method != 0) return dat_extract(cdir, cdir_len, &d, out, max);
    if (d.stored + ZIP_SLACK > STAGE_MAX || zip_read_range(zip, c, d.offset, d.stored, STAGE, STAGE_MAX)) return 0;
    d.offset = 0;                                           /* the bytes begin at the stage now */
    return dat_extract(STAGE, d.stored, &d, out, max);
}

/* GRAF1.DAT's entry */
static uint32_t entry(const char *name, uint8_t *out, uint32_t max)
{
    return entry_of(graf, dir, dir_len, name, out, max);
}

/* the SDRAM holds what the CPU wrote (the blitter reads it there) */
static void settle(void)
{
    while (!(MEM_STATUS & MEM_WRITES_IDLE))
        ;
}

int s2gfx_bkk(const char *name, uint8_t *dst, uint32_t *pal)
{
    const uint8_t *b = UNPACK;
    if (entry(name, UNPACK, UNPACK_MAX) != BKK_BYTES || rd32(b) != BKK_MAGIC || rd32(b + 4) != 256 || rd32(b + 8) != 320 ||
        rd32(b + 12) != 200 || rd32(b + 16) != S2G_PIC_BYTES) return 0;
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = b + 24 + 3 * i;
        pal[i] = (uint32_t)(c[0] & 63) * 4 << 16 | (uint32_t)(c[1] & 63) * 4 << 8 | (uint32_t)(c[2] & 63) * 4;
    }
    const uint32_t *from = (const uint32_t *)(b + 24 + 768);        /* (4-aligned: 792 bytes into an aligned buffer) */
    uint32_t *to = (uint32_t *)dst;
    for (int i = 0; i < S2G_PIC_BYTES / 4; i++) to[i] = from[i];
    settle();
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the texts */

#define TEXT_MAX    0x1000u
static char text[TEXT_MAX + 8];
static const char *line[LINES];
static int nlines;

/* a file of LOCAL.DAT's lines into the list: '\n' ends one, a trailing '\r' or space goes; comments (*) and empty lines are left out */
static void add_lines(const uint8_t *local, uint32_t local_size, const char *name, uint32_t *used)
{
    struct dat_entry d;
    if (!dat_find(local, local_size, name, &d) || d.size >= TEXT_MAX - *used) return;
    char *t = text + *used;
    uint32_t n = dat_extract(local, local_size, &d, (uint8_t *)t, TEXT_MAX - *used);
    if (!n) return;
    t[n] = 0;
    *used += n + 1;
    for (char *p = t; *p && nlines < LINES;) {
        char *s = p;
        while (*p && *p != '\n') p++;
        char *end = p;
        if (*p) p++;
        while (end > s && (end[-1] == '\r' || end[-1] == ' ')) end--;
        *end = 0;
        if (*s && *s != '*') line[nlines++] = s;
    }
}

const char *s2gfx_line(const char *key, int k, const char *fallback)
{
    for (int i = 0; i < nlines; i++)
        if (!strcmp(line[i], key)) return i + k >= 0 && i + k < nlines ? line[i + k] : fallback;
    return fallback;
}

/* ------------------------------------------------------------------------------------------------ the tables */

/* MENUL.SPR (count, size; per sprite x, y, width, height, offset; the pixels) stays as it is at the start of the section: a
   sprite's image is its pixels in the file. MENUL.FNT is unpacked into the glyphs' bytes after it (see s2gfx_init). */
static int build_sprites(struct set *s, const uint8_t *spr, uint32_t size)
{
    if (size < 8) return 0;
    uint32_t n = rd32(spr), head = 8 + 20 * n;
    if (n > 256 || head > size || n > s->max_img) return 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *r = spr + 8 + 20 * i;
        uint32_t w = rd32(r + 8), h = rd32(r + 12), at = rd32(r + 16);
        if (w > 0x7FFF || h > 0x7FFF || head + at > size || w * h > size - head - at) return 0;
        s->img[i] = (struct blit_image){ head + at, (uint16_t)w, (uint16_t)h, (int16_t)rd32(r), (int16_t)rd32(r + 4) };
    }
    s->nsprites = n;
    return 1;
}

/* MENUL.FNT of Screamer 2: u8 fonts; per font i32 height, first character, count, a fourth number, the picture's name (16 bytes); per
   font count x (i32 width, i32 offset); i32 size; the pixels, two a byte (high half first), rows of (width + 1) / 2 bytes. Each
   glyph becomes an image of one byte a pixel (the shade 0..15) at `to`, its width x height bytes. Returns the bytes used, 0 if damaged. */
static uint32_t build_fonts(struct set *s, const uint8_t *f, uint32_t size, uint8_t *to, uint32_t room, uint32_t room_off)
{
    if (size < 5 || f[0] < 1 || f[0] > FONTS) return 0;
    uint32_t nf = f[0], p = 1 + 32 * nf, pixels = p, used = 0, ni = s->nsprites;
    for (uint32_t k = 0; k < nf; k++) {
        const uint8_t *h = f + 1 + 32 * k;
        pixels += 8 * rd32(h + 8);
    }
    if (pixels + 4 > size) return 0;
    pixels += 4;
    p = 1 + 32 * nf;
    for (uint32_t k = 0; k < nf; k++) {
        const uint8_t *h = f + 1 + 32 * k;
        uint32_t height = rd32(h), count = rd32(h + 8), widest = 1;
        if (height > 255 || count > 256 || ni + count > s->max_img) return 0;
        s->font[k].first_image = ni;
        for (uint32_t i = 0; i < count; i++, p += 8, ni++) {
            uint32_t w = rd32(f + p), at = rd32(f + p + 4), bw = (w + 1) / 2;
            if (w > 255 || (w && height && (uint64_t)pixels + at + (uint64_t)(height - 1) * bw + (w - 1) / 2 >= size)) return 0;
            if (w * height > room - used) return 0;
            uint8_t *px = to + used;
            for (uint32_t r = 0; w && r < height; r++) {
                const uint8_t *row = f + pixels + at + r * bw;
                for (uint32_t c = 0; c < w; c++) *px++ = (uint8_t)(row[c >> 1] >> (c & 1 ? 0 : 4) & 15);
            }
            s->img[ni] = (struct blit_image){ room_off + used, (uint16_t)w, (uint16_t)height, 0, 0 };
            used += w * height;
            if (w > widest) widest = w;
        }
        s->font[k].height = height;
        s->font[k].first = rd32(h + 4);
        s->font[k].count = count;
        s->font[k].widest = widest;
    }
    s->nfonts = nf;
    return used;
}

/* GAME1.DAT's GAMEL.SPR (the race screen's sprites): its seven track banners join the image table after the fonts, their pixels where
   the file was unpacked. 0 if the ZIP has no GAME1.DAT or it is damaged (the loading screen's banners stand in). */
static uint32_t game_dir_buf[TAIL_MAX / 4 + 4];

static int load_banners(struct zip *z, int first_free)
{
    const struct zip_entry *g = zip_find(z, "GFX/GAME1.DAT");
    const uint8_t *gdir;
    uint32_t glen;
    banner_img = 0;
    if (!g || first_free + 7 > IMAGES) return 0;
    if (g->method == 0) {                                   /* the container's end: its directory */
        uint32_t tail = g->size < TAIL_MAX ? g->size : TAIL_MAX;
        if (zip_read_range(z, g, g->size - tail, tail, (uint8_t *)game_dir_buf, sizeof game_dir_buf)) return 0;
        gdir = (const uint8_t *)game_dir_buf;
        glen = tail;
    } else {                                                /* deflated: all of it, behind GRAF1.DAT's */
        uint8_t *whole = WHOLE + (graf->method ? (graf->size + 0xFFFFu) & ~0xFFFFu : 0);
        if (whole + g->size + ZIP_SLACK > WHOLE + WHOLE_MAX || zip_read(z, g, whole, (uint32_t)(WHOLE + WHOLE_MAX - whole))) return 0;
        gdir = whole;
        glen = g->size;
    }
    uint32_t size = entry_of(g, gdir, glen, "GAMEL.SPR", GAME_SPR, GAME_MAX);
    if (size < 8) return 0;
    uint32_t n = rd32(GAME_SPR), head = 8 + 20 * n;
    if (n < GAME_FIRST + 7 || n > 256 || head > size) return 0;
    for (int k = 0; k < 7; k++) {
        const uint8_t *r = GAME_SPR + 8 + 20 * (GAME_FIRST + k);
        uint32_t w = rd32(r + 8), h = rd32(r + 12), at = rd32(r + 16);
        if (w > 0x7FFF || h > 0x7FFF || head + at > size || w * h > size - head - at) return 0;
        img[first_free + k] = (struct blit_image){ (uint32_t)(GAME_SPR - SECT) + head + at, (uint16_t)w, (uint16_t)h,
                                                   (int16_t)rd32(r), (int16_t)rd32(r + 4) };
    }
    banner_img = first_free;
    return 1;
}

int s2gfx_banner(int track)
{
    if (track < 1 || track > 7) track = 1;
    return banner_img ? banner_img + track - 1 : 0x13 + track - 1;
}

int s2gfx_init(struct zip *z)
{
    uint32_t t0 = rdcycle();
    zip = z;
    ready = 0;
    graf = zip_find(z, "GFX/GRAF1.DAT");
    if (!graf) {
        dlog_str("ui: GFX/GRAF1.DAT is not in the ZIP\n");
        return 0;
    }
    if (graf->method == 0) {                                /* the container's end: its directory */
        uint32_t tail = graf->size < TAIL_MAX ? graf->size : TAIL_MAX;
        if (zip_read_range(z, graf, graf->size - tail, tail, (uint8_t *)dir_buf, sizeof dir_buf)) return dlog_str("ui: GRAF1.DAT unreadable\n"), 0;
        dir = (const uint8_t *)dir_buf;
        dir_len = tail;
    } else {                                                /* deflated: all of it, once */
        if (graf->size + ZIP_SLACK > WHOLE_MAX || zip_read(z, graf, WHOLE, WHOLE_MAX)) return dlog_str("ui: GRAF1.DAT unreadable\n"), 0;
        dir = WHOLE;
        dir_len = graf->size;
    }
    menu.base = SECT;
    menu.nsprites = menu.nfonts = 0;
    nlines = 0;
    uint32_t spr = entry("MENUL.SPR", SECT, SECT_MAX), glyphs = (spr + 3) & ~3u, used;
    if (!spr || !build_sprites(&menu, SECT, spr)) return dlog_str("ui: MENUL.SPR unusable\n"), 0;
    uint32_t fnt = entry("MENUL.FNT", UNPACK, UNPACK_MAX);
    if (!fnt || !(used = build_fonts(&menu, UNPACK, fnt, SECT + glyphs, SECT_MAX - glyphs, glyphs))) return dlog_str("ui: MENUL.FNT unusable\n"), 0;
    int banners = load_banners(z, (int)(menu.font[menu.nfonts - 1].first_image + menu.font[menu.nfonts - 1].count));
    nlater = 0;
    s2gfx_clip(0, FB_H, 0, FB_H);
    settle();
    /* the texts of DATA/LOCAL.DAT (STRINGS.TXT 2.6 KB, GAME.TXT 0.6 KB), through the stage */
    const struct zip_entry *e = zip_find(z, "DATA/LOCAL.DAT");
    if (e && e->size + ZIP_SLACK <= STAGE_MAX && !zip_read(z, e, STAGE, STAGE_MAX)) {
        uint32_t u = 0;
        add_lines(STAGE, e->size, "GAME.TXT", &u);
        add_lines(STAGE, e->size, "STRINGS.TXT", &u);
    }
    dlog_str("ui: ");
    dlog_dec((int32_t)menu.nsprites);
    dlog_str(" sprites, ");
    dlog_dec((int32_t)menu.nfonts);
    dlog_str(" fonts (");
    dlog_dec((int32_t)((glyphs + used) / 1024));
    dlog_str(" KB), ");
    dlog_dec(nlines);
    dlog_str(banners ? " text lines, GAMEL banners, " : " text lines, ");
    dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
    dlog_str(" ms\n");
    return ready = 1;
}

/* GAMEL.SPR and GAMEL.FNT of GFX/GAME1.DAT, each into its own memory, apart from the menu's: its directory (the container's end, or all of it when the
   ZIP is deflated), the sprites as the file is, the glyphs after them */
static uint32_t gdir[GDIR_MAX / 4 + 4];
static const struct zip_entry *game_c;          /* GFX/GAME1.DAT and its directory as s2gfx_game_init() found them (s2gfx_game_entry) */
static const uint8_t *game_dr;
static uint32_t game_dr_len;

uint32_t s2gfx_game_entry(const char *name, uint8_t *out, uint32_t max)
{
    return game_c ? entry_of(game_c, game_dr, game_dr_len, name, out, max) : 0;
}

int s2gfx_game_init(struct zip *z)
{
    uint32_t t0 = rdcycle();
    const struct zip_entry *c = zip_find(z, "GFX/GAME1.DAT");
    if (!c) return dlog_str("ui: GFX/GAME1.DAT is not in the ZIP\n"), 0;
    const uint8_t *dr;
    uint32_t dr_len;
    zip = z;
    if (c->method == 0) {
        dr_len = c->size < GDIR_MAX ? c->size : GDIR_MAX;
        if (zip_read_range(z, c, c->size - dr_len, dr_len, (uint8_t *)gdir, sizeof gdir)) return dlog_str("ui: GAME1.DAT unreadable\n"), 0;
        dr = (const uint8_t *)gdir;
    } else {
        if (c->size + ZIP_SLACK > WHOLE_MAX - 0x500000u || zip_read(z, c, GWHOLE, WHOLE_MAX - 0x500000u)) return dlog_str("ui: GAME1.DAT unreadable\n"), 0;
        dr = GWHOLE;
        dr_len = c->size;
    }
    game.base = GMEM;
    game.nsprites = game.nfonts = 0;
    game_c = c;
    game_dr = dr;
    game_dr_len = dr_len;
    uint32_t spr = entry_of(c, dr, dr_len, "GAMEL.SPR", GMEM, GSPR_MAX), glyphs = (spr + 3) & ~3u, used;
    if (!spr || !build_sprites(&game, GMEM, spr)) return dlog_str("ui: GAMEL.SPR unusable\n"), 0;
    uint32_t fnt = entry_of(c, dr, dr_len, "GAMEL.FNT", UNPACK, UNPACK_MAX);
    if (!fnt || !(used = build_fonts(&game, UNPACK, fnt, GMEM + glyphs, GSPR_MAX + GGLYPH_MAX - glyphs, glyphs))) return dlog_str("ui: GAMEL.FNT unusable\n"), 0;
    settle();
    dlog_str("ui: game set ");
    dlog_dec((int32_t)game.nsprites);
    dlog_str(" sprites, ");
    dlog_dec((int32_t)game.nfonts);
    dlog_str(" fonts (");
    dlog_dec((int32_t)((glyphs + used) / 1024));
    dlog_str(" KB), ");
    dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
    dlog_str(" ms\n");
    return 1;
}

/* ------------------------------------------------------------------------------------------------ a frame */

struct blit_list *s2gfx_begin(void)
{
    uint32_t st = wait_swap_done();
    blit_start(&list, !(st & VID_FRONT));
    nlater = 0;
    return &list;
}

struct blit_list *s2gfx_begin_on(int buffer)
{
    blit_start(&list, buffer);
    nlater = 0;
    return &list;
}

void s2gfx_flush(void)
{
    settle();
    hw_blit(&list);
    while (RS_DRAW & RS_BUSY)
        ;
    for (int i = 0; i < nlater; i++)
        for (int p = 0; p < later[i].passes; p++) s2gfx_dark_fb(FB(list.buffer), later[i].x, later[i].y, later[i].w, later[i].h, later[i].table);
}

void s2gfx_end(const uint32_t *pal, int32_t fade)
{
    settle();
    hw_blit(&list);
    pal_faded(pal, fade);
    while (RS_DRAW & RS_BUSY)                               /* (the bit holds the blitter too) */
        ;
    for (int i = 0; i < nlater; i++)
        for (int p = 0; p < later[i].passes; p++) s2gfx_dark_fb(FB(list.buffer), later[i].x, later[i].y, later[i].w, later[i].h, later[i].table);
    VID_SWAP = 1;
}

/* an image w wide, cut to the rows [y0, y1); one wholly right of or left of the screen is not sent (the blitter's x is 10 bits wide, signed:
   a sprite sliding in from x 1000 would come in at the left edge, wrapped); the blitter cuts what hangs over an edge itself */
static void copy_clipped(struct blit_list *l, const uint8_t *src, int w, int h, int x, int y, int kind, int colour, int y0, int y1)
{
    if (x >= FB_W || x + w <= 0) return;
    if (y < y0) {
        src += (y0 - y) * w;
        h -= y0 - y;
        y = y0;
    }
    if (y + h > y1) h = y1 - y;
    if (h < 1) return;
    blit_copy(l, src, w, h, w, x, y, kind, colour);
}

void s2gfx_clip(int text_y0, int text_y1, int sprite_y0, int sprite_y1)
{
    clip_text_y0 = text_y0;
    clip_text_y1 = text_y1;
    clip_spr_y0 = sprite_y0;
    clip_spr_y1 = sprite_y1;
}

void s2gfx_picture(struct blit_list *l, const uint8_t *pic)
{
    blit_copy(l, pic, 320, 200, 320, 0, 0, BLIT_COPY | BLIT_OPAQUE, 0);
}

/* a sprite of the set, or (menu set) one of the race banners that follow its fonts */
static int sprite_ok(const struct set *set, int n)
{
    return n >= 0 && (n < (int)set->nsprites || (set == &menu && banner_img && n >= banner_img && n < banner_img + 7));
}

static void sprite_of(const struct set *set, struct blit_list *l, int n, int x, int y)
{
    if (!sprite_ok(set, n)) return;
    const struct blit_image *s = &set->img[n];
    copy_clipped(l, set->base + s->off, s->w, s->h, x + s->x, y + s->y, BLIT_COPY, 0, clip_spr_y0, clip_spr_y1);
}

static void sprite_centred_of(const struct set *set, struct blit_list *l, int n, int y)
{
    if (!sprite_ok(set, n)) return;
    const struct blit_image *s = &set->img[n];
    sprite_of(set, l, n, (320 - (s->w + s->x)) / 2 - (s->x - (s->x >> 31)) / 2, y);
}

/* The original's text routine (the engine's 0xc67f0, its width 0xc6c88): between two characters `space` more pixels, none before the first or
   after the last (a width has no trailing space). After the control byte 1 every character sits in a cell of the font's widest + space, centred
   (the cell less the glyph's width, halved, the shift arithmetic), only the first character's cell is the widest without the space. */
static int text_of(const struct set *set, struct blit_list *l, int font_no, int colour, int space, int x, int y, const char *s)
{
    if (font_no < 0 || font_no >= (int)set->nfonts) return 0;
    const struct blit_font *f = &set->font[font_no];
    int cell = (int)f->widest, fixed = 0, started = 0, x0 = x;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        int ch = *p;
        if (ch < (int)f->first || ch >= (int)(f->first + f->count)) {
            if (ch == 1) fixed = 1;
            else if (ch == 2) fixed = 0;
            continue;
        }
        const struct blit_image *g = &set->img[f->first_image + (uint32_t)(ch - (int)f->first)];
        int w = g->w, at = fixed ? x + ((cell - w) >> 1) : started ? x + space : x;
        if (l && ch != ' ') copy_clipped(l, set->base + g->off, w, g->h, at, y, BLIT_FONT, colour, clip_text_y0, clip_text_y1);   /* (the space has no pixels) */
        x = fixed ? x + cell : at + w;
        if (!started) cell += space;
        started = 1;
    }
    return x - x0;
}

static void text_centred_of(const struct set *set, struct blit_list *l, int font_no, int colour, int space, int y, const char *s)
{
    text_of(set, l, font_no, colour, space, (320 - text_of(set, 0, font_no, colour, space, 0, 0, s)) >> 1, y, s);
}

void s2gfx_sprite(struct blit_list *l, int n, int x, int y) { sprite_of(&menu, l, n, x, y); }
void s2gfx_sprite_centred(struct blit_list *l, int n, int y) { sprite_centred_of(&menu, l, n, y); }

void s2gfx_sprite_boxed(struct blit_list *l, int n, int x0, int width, int y)
{
    if (sprite_ok(&menu, n)) sprite_of(&menu, l, n, x0 + ((width - menu.img[n].w) >> 1), y);
}
int s2gfx_text(struct blit_list *l, int font_no, int colour, int space, int x, int y, const char *s) { return text_of(&menu, l, font_no, colour, space, x, y, s); }
void s2gfx_text_centred(struct blit_list *l, int font_no, int colour, int space, int y, const char *s) { text_centred_of(&menu, l, font_no, colour, space, y, s); }

void s2gfx_text_boxed(struct blit_list *l, int font_no, int colour, int space, int x0, int width, int y, const char *s)
{
    text_of(&menu, l, font_no, colour, space, x0 + ((width - text_of(&menu, 0, font_no, colour, space, 0, 0, s)) >> 1), y, s);
}

void s2gfx_game_sprite(struct blit_list *l, int n, int x, int y) { sprite_of(&game, l, n, x, y); }
void s2gfx_game_sprite_centred(struct blit_list *l, int n, int y) { sprite_centred_of(&game, l, n, y); }
int s2gfx_game_sprite_info(int n, int *x, int *w)
{
    if (!sprite_ok(&game, n)) return 0;
    *x = game.img[n].x;
    *w = game.img[n].w;
    return 1;
}
int s2gfx_game_text(struct blit_list *l, int font_no, int colour, int space, int x, int y, const char *s) { return text_of(&game, l, font_no, colour, space, x, y, s); }
void s2gfx_game_text_centred(struct blit_list *l, int font_no, int colour, int space, int y, const char *s) { text_centred_of(&game, l, font_no, colour, space, y, s); }

/* ------------------------------------------------------------------------------------------------ the darkening */

void s2gfx_dark_table(const uint32_t *pal, uint8_t table[256], int keep)
{
    for (int i = 0; i < 256; i++) {
        const float want[3] = { (float)(pal[i] >> 16 & 255) * 0.67f, (float)(pal[i] >> 8 & 255) * 0.67f, (float)(pal[i] & 255) * 0.67f };
        int best = 0;
        for (int pass = 0; pass < 2; pass++) {
            float least = 2.21e7f;
            const float k = pass ? 0.7f : 1.0f;
            for (int q = 0; q < keep; q++) {
                float dr = (float)(pal[q] >> 16 & 255) - want[0] * k, dg = (float)(pal[q] >> 8 & 255) - want[1] * k,
                      db = (float)(pal[q] & 255) - want[2] * k, d = db * db + dg * dg + dr * dr;
                if (d < least) {
                    least = d;
                    best = q;
                }
            }
            if (best != i) break;
        }
        table[i] = (uint8_t)best;
    }
}

void s2gfx_snapshot(int buffer, uint8_t *dst, const uint8_t *table)
{
    const volatile uint32_t *fb = (const volatile uint32_t *)FB(buffer);
    uint32_t *out = (uint32_t *)dst;
    for (int i = 0; i < S2G_PIC_BYTES / 4; i++) {
        uint32_t w = fb[i];
        if (table) w = (uint32_t)table[w & 255] | (uint32_t)table[w >> 8 & 255] << 8 | (uint32_t)table[w >> 16 & 255] << 16 | (uint32_t)table[w >> 24] << 24;
        out[i] = w;
    }
}

void s2gfx_dark_box(uint8_t *pic, int x, int y, int w, int h, const uint8_t table[256])
{
    for (int r = y < 0 ? 0 : y; r < y + h && r < 200; r++)
        for (int c = x < 0 ? 0 : x; c < x + w && c < 320; c++) pic[r * 320 + c] = table[pic[r * 320 + c]];
}

void s2gfx_dark_fb(volatile uint8_t *fb, int x, int y, int w, int h, const uint8_t table[256])
{
    x &= ~3;
    w &= ~3;
    if (x < 0 || x >= FB_W) return;
    if (x + w > FB_W) w = FB_W - x;
    for (int r = y < 0 ? 0 : y; r < y + h && r < FB_H; r++) {
        volatile uint32_t *p = (volatile uint32_t *)(fb + r * FB_W + x);
        for (int c = 0; c < w / 4; c++, p++) {
            uint32_t v = *p;
            *p = (uint32_t)table[v & 255] | (uint32_t)table[v >> 8 & 255] << 8 | (uint32_t)table[v >> 16 & 255] << 16 | (uint32_t)table[v >> 24] << 24;
        }
    }
}

void s2gfx_dark_after(int x, int y, int w, int h, int passes, const uint8_t *table)
{
    if (nlater >= 4) return;
    later[nlater].x = x;
    later[nlater].y = y;
    later[nlater].w = w;
    later[nlater].h = h;
    later[nlater].passes = passes;
    later[nlater++].table = table;
}

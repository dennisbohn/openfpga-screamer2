/*
 * The track pages' 3D maps (firmware/s2trackpage.c, s2trackmap.c) against the running original: tools/trackmap.sh gives this the raw DOSBox screenshots of
 * tools/s2/dosbox_arcade.py --timeattack and --league (320 x 200 palette indices, tools/pages3d_idx.py, and the label of each) and it looks, for each frame of a page with a map
 * that stands still (the map at the origin, turning: a screenshot cannot tell its angle), for the angle of the turn at which the page's own drawing equals it best: the banner
 * tells which track it is, then every one of the 2048 angles is tried (a coarse pass of 16, a fine pass round the best). It compares the part of the screen left of the text box
 * (x 0 .. 166, y 36 .. 172) pixel for pixel and tells how many of the pixels the map covers in the screenshot differ, at the outline (one picture has the map there, the
 * other the picture) and inside it (both have the map, with other texels: the textures are minified; the matrices are built from floats here, from 16-bit tables there), and
 * how many of those have the screenshot's colour within a pixel's reach. Built with -DGU_MAX_VERT=1024 (the second binary of tools/trackmap.sh) the geometry unit of the model takes any number of
 * vertices and nothing is cut: the pieces are the whole blocks, as the original draws them.
 *
 * Usage: trackmap_match <SCREAMER2.zip> <folder of .idx / .pal> <labels file> [<out prefix: pictures of the best match per frame (DUMP=<part of a name>)>]
 */
#define _GNU_SOURCE
#include "../hw.h"
#include "../s2scene.h"
#include "../s2gfx.h"
#include "../s2trackmap.h"
#include "../s2menu.h"
#include "../s2page.h"
#include "../slot.h"
#include "../dlog.h"
#include "../dat.h"
#include "../geom.h"
#include "../raster.h"
#include "../frame.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define SCRATCH     ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D60000))
#define SCRATCH_MAX (PROG_BASE - (SDRAM_BASE + 0x2D60000))

static FILE *zf;
static uint32_t zsize;
static struct zip zip;

uint32_t slot_size(uint32_t id) { return id == 3 ? zsize : 0; }
void slot_set_size(uint32_t id, uint32_t size) { (void)id; (void)size; }
#define OUTSIDE 0xE0000000u
static uint8_t *outside;
uint32_t zip_addr(const void *p)
{
    if ((uintptr_t)p >= SDRAM_BASE && (uintptr_t)p < SDRAM_BASE + SDRAM_SIZE) return (uint32_t)(uintptr_t)p;
    outside = (uint8_t *)p;
    return OUTSIDE;
}
int slot_read(uint32_t id, uint32_t off, uint32_t addr, uint32_t len)
{
    void *to = addr == OUTSIDE ? outside : (void *)(uintptr_t)addr;
    if (id != 3 || (off & 3) || off >= zsize || (len != SLOT_REST && ((len & 3) || off + len > zsize))) return 2;
    uint32_t n = len == SLOT_REST ? zsize - off : len;
    memset(to, 0, (n + 3) & ~3u);
    return fseek(zf, off, SEEK_SET) || fread(to, 1, n, zf) != n ? 2 : 0;
}

/* the hardware's stand-ins (as tools/host/pages3d_match.c) */
int back_wait;
uint32_t draw_buf, draw_wait, back_clocks, back_cycles, tl_scene, frames_shown;
void (*hw_idle)(void);
uint32_t wait_swap_done(void) { return 0; }
void hw_blit(const struct blit_list *l) { blit_model(FB(l->buffer), l); }
void pal_faded(const uint32_t *pal, int32_t f) { (void)pal; (void)f; }
void su_view_regs(const struct su_view *v)
{
    SU_CENTRE = RS_XY(v->cx, v->cy);
    SU_SCALE = v->scale;
    SU_FAR = (uint32_t)v->far8;
}
void hw_piece(const struct gu_piece *g)
{
    const struct su_view v = { (int16_t)(SU_CENTRE & 0xFFFF), (int16_t)(SU_CENTRE >> 16), SU_SCALE, (int32_t)SU_FAR };
    const struct rs_clip clip = { (int)(RS_CLIP0 & 0xFFFF), (int)(RS_CLIP0 >> 16), (int)(RS_CLIP1 & 0xFFFF), (int)(RS_CLIP1 >> 16) };
    struct gu_stats st = { 0, 0, 0, 0 };
    gu_draw(FB((GU_MODE >> 8) & 1), &clip, &v, (const uint8_t *)(uintptr_t)(SDRAM_BASE + GU_MLB), g, &st);
}
void tl_mark(uint32_t t) { (void)t; }
void tl_begin(uint32_t t) { (void)t; }
void s2sound_menu(int id) { (void)id; }
void s2sound_effects(int32_t f) { (void)f; }
void music_frame(void) {}

/* what the pages need of the rest of the firmware */
static const int order[7] = { 1, 3, 4, 5, 6, 2, 7 };
int s2save_track_at(int pos) { return order[pos]; }
int s2arcade_tracks_open(void) { return 7; }
int s2page_run(const struct s2_page *p, void *self) { (void)p; (void)self; return 0; }

#include "../s2trackpage.c"

/* ------------------------------------------------------------------------------------------------ the match */

static uint8_t frame_idx[S2G_PIC_BYTES];
static uint32_t pic_pal[2][256];
static uint8_t picture_buf[2][S2G_PIC_BYTES];
static uint8_t tkg_buf[S2G_PIC_BYTES];
static uint32_t tkg_pal[256];

#define X0 0
#define X1 166
#define Y0 36
#define Y1 172

static uint8_t only3d[S2G_PIC_BYTES];                           /* the map, the picture and the box's darkening, no text: what render() drew */

/* the whole page as its own drawing makes it, with the arrows shown or not (the page's clock 0 or 64 hundredths): the text, banner and arrows are the pixels where it
   differs from the map alone */
static void page_full(const uint8_t *pic, int ta, int track, int a1, int arrows, int asking, int yes)
{
    struct blit_list *l = s2gfx_begin_on(0);
    s2gfx_picture(l, pic);
    if (ta) {
        int pos = 0;
        for (int i = 0; i < 7; i++) if (order[i] == track) pos = i;
        struct ta p = { .cfg = 0, .cur = pos, .prev = pos, .n = 7, .t = 1.0f, .a1 = a1, .now = arrows ? 0 : 32, .t0 = 0, .fresh = 1 };
        ta_draw(&p, l, 0);
    } else {
        struct lg p = { .league = 0, .race = 1, .track = track, .asking = asking, .yes = yes, .a1 = a1, .fresh = 1, .now = arrows ? 0 : 32 };
        lg_draw(&p, l, 0);
    }
    s2gfx_flush();
}

static void render(const uint8_t *pic, int track, int a1)
{
    struct blit_list *l = s2gfx_begin_on(0);
    s2gfx_picture(l, pic);
    const struct item it = { track, { 0, 0, 0 } };
    int fresh = 1;
    draw_maps(l, &it, 1, a1, &fresh);
    s2gfx_flush();
}

static int diff(void)
{
    const volatile uint8_t *fb = FB(0);
    int d = 0;
    for (int y = Y0; y < Y1; y++)
        for (int x = X0; x < X1; x++) d += fb[y * 320 + x] != frame_idx[y * 320 + x];
    return d;
}

static int banner_diff(const uint8_t *pic, int track)
{
    struct blit_list *l = s2gfx_begin_on(0);
    s2gfx_picture(l, pic);
    s2gfx_sprite_boxed(l, SPR_BANNER + track - 1, 160, 160, 44);
    s2gfx_flush();
    const volatile uint8_t *fb = FB(0);
    int d = 0;
    for (int y = 44; y < 94; y++)
        for (int x = 168; x < 312; x++) d += fb[y * 320 + x] != frame_idx[y * 320 + x];
    return d;
}

static void outline(const uint8_t *pic, int *sil, int *inner, int *near, int *covered)
{
    const volatile uint8_t *fb = FB(0);
    *sil = *inner = *near = *covered = 0;
    for (int y = Y0; y < Y1; y++)
        for (int x = X0; x < X1; x++) {
            int i = y * 320 + x, mine = fb[i] != pic[i], ref = frame_idx[i] != pic[i];
            *covered += ref;
            if (mine != ref) (*sil)++;
            else if (mine && fb[i] != frame_idx[i]) {
                (*inner)++;
                int found = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if (fb[i + dy * 320 + dx] == frame_idx[i]) found = 1;
                *near += found;
            }
        }
}

static int same_palette(const uint32_t *shot, const uint32_t *mine)
{
    for (int i = 0; i < 256; i++)
        for (int s = 0; s < 24; s += 8)
            if ((shot[i] >> (s + 2) & 63) != (mine[i] >> (s + 2) & 63)) return 0;
    return 1;
}

static void dump(const char *prefix, const char *name, const char *what, const uint32_t *p, int diffmap)
{
    char path[512];
    snprintf(path, sizeof path, "%s_%.*s_%s.rgb", prefix, (int)(strlen(name) - 4), name, what);
    FILE *o = fopen(path, "wb");
    if (!o) return;
    const volatile uint8_t *fb = FB(0);
    for (int i = 0; i < S2G_PIC_BYTES; i++) {
        uint32_t c;
        if (diffmap) c = fb[i] != frame_idx[i] && i / 320 >= Y0 && i / 320 < Y1 && i % 320 < X1 ? 0xFFFFFF : 0;
        else c = p[strcmp(what, "ref") ? fb[i] : frame_idx[i]];
        uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s <SCREAMER2.zip> <folder> <labels> [out prefix]\n", argv[0]); return 2; }
    int fd = memfd_create("sdram", 0);
    if (fd < 0 || ftruncate(fd, SDRAM_SIZE) ||
        mmap((void *)(uintptr_t)SDRAM_BASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED ||
        mmap((void *)0xF0000000u, 0x4000000u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        perror("map");
        return 1;
    }
    *(volatile uint32_t *)0xF000000Cu = 0x1Fu;
    if (!(zf = fopen(argv[1], "rb"))) { perror(argv[1]); return 1; }
    fseek(zf, 0, SEEK_END);
    zsize = (uint32_t)ftell(zf);
    dlog_init();
    if (zip_open(&zip, 3, SCRATCH, SCRATCH_MAX) != ZIP_OK || !s2gfx_init(&zip)) { fprintf(stderr, "zip / toolkit\n"); return 1; }
    if (!s2map_load(&zip)) { fprintf(stderr, "the maps\n"); return 1; }
    s2gfx_bkk("LEAG_0L.BKK", picture_buf[0], pic_pal[0]);
    const char *only = getenv("ONLY"), *dump_name = getenv("DUMP");
    if (only && !*only) only = 0;
    if (dump_name && !*dump_name) dump_name = 0;
    static char names[512][64], labels[512][48];
    int nn = 0;
    DIR *dir = opendir(argv[2]);
    struct dirent *de;
    while (dir && (de = readdir(dir)) && nn < 512)
        if (strstr(de->d_name, ".idx")) snprintf(names[nn++], 64, "%s", de->d_name);
    closedir(dir);
    for (int i = 0; i < nn; i++)
        for (int j = i + 1; j < nn; j++)
            if (strcmp(names[i], names[j]) > 0) { char t[64]; strcpy(t, names[i]); strcpy(names[i], names[j]); strcpy(names[j], t); }
    FILE *lf = fopen(argv[3], "r");
    for (int i = 0; i < nn; i++) {
        labels[i][0] = 0;
        if (!lf || fscanf(lf, "%47s", labels[i]) != 1) labels[i][0] = 0;
    }
    if (lf) fclose(lf);
    int frames = 0, sum_cov = 0, sum_sil = 0, sum_in = 0, sum_near = 0, sum2d_n = 0, sum2d_same = 0;
    for (int f = 0; f < nn; f++) {
        if (only && !strstr(names[f], only) && !strstr(labels[f], only)) continue;
        const int ta = !strncmp(labels[f], "ta_", 3), league = !strncmp(labels[f], "trackmap", 8);
        if (!ta && !league) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/%s", argv[2], names[f]);
        FILE *h = fopen(path, "rb");
        if (!h || fread(frame_idx, 1, S2G_PIC_BYTES, h) != S2G_PIC_BYTES) { fprintf(stderr, "%s\n", path); return 1; }
        fclose(h);
        snprintf(path, sizeof path, "%s/%.*s.pal", argv[2], (int)(strlen(names[f]) - 4), names[f]);
        uint32_t fp[256];
        h = fopen(path, "rb");
        if (h) { if (fread(fp, 4, 256, h) != 256) memset(fp, 0, sizeof fp); fclose(h); }
        /* the page: time attack's picture, or the league's TKG_nL (the one whose palette it is) */
        const uint8_t *pic = 0;
        const uint32_t *pp = 0;
        int track = 0, bd = 1 << 30;
        if (ta && same_palette(fp, pic_pal[0])) {
            pic = picture_buf[0];
            pp = pic_pal[0];
        } else if (league) {
            for (int n = 1; n <= 6 && !pic; n++) {
                char nm[16] = "TKG_1L.BKK";
                nm[4] = (char)('0' + n);
                if (s2gfx_bkk(nm, tkg_buf, tkg_pal) && same_palette(fp, tkg_pal)) {
                    pic = tkg_buf;
                    pp = tkg_pal;
                    track = n;
                }
            }
        }
        if (!pic) { printf("%s (%s): palette is not the page's\n", names[f], labels[f]); continue; }
        s2gfx_dark_table(pp, dark, 256);
        if (ta) {
            for (int n = 1; n <= 7; n++) {
                const int d = banner_diff(pic, n);
                if (d < bd) { bd = d; track = n; }
            }
        }
        int best = 1 << 30, ba = 0;
        for (int a = 0; a < 2048; a += 16) {
            render(pic, track, a);
            const int c = diff();
            if (c < best) { best = c; ba = a; }
        }
        const int centre = ba;
        for (int a = centre - 15; a <= centre + 15; a++) {
            render(pic, track, (a + 2048) & 2047);
            const int c = diff();
            if (c < best) { best = c; ba = (a + 2048) & 2047; }
        }
        render(pic, track, ba);
        int sil, inner, near, covered;
        outline(pic, &sil, &inner, &near, &covered);
        memcpy(only3d, (const void *)FB(0), S2G_PIC_BYTES);
        /* the 2D parts: the title, the banner, the texts and the arrows at the best of the two blink phases */
        int best2d = -1, best_n = 0, best_arrows = 1;
        for (int arrows = 1; arrows >= 0; arrows--) {
            page_full(pic, ta, track, ba, arrows, 0, 0);
            int n = 0, same = 0;
            const volatile uint8_t *fb = FB(0);
            for (int i = 0; i < S2G_PIC_BYTES; i++)
                if (fb[i] != only3d[i]) {
                    n++;
                    same += fb[i] == frame_idx[i];
                }
            if (same > best2d) { best2d = same; best_n = n; best_arrows = arrows; }
        }
        sum2d_n += best_n;
        sum2d_same += best2d;
        printf("%s (%s): 2D parts (title, banner, texts, arrows %s): %d of %d pixels equal\n", names[f], labels[f], best_arrows ? "shown" : "hidden", best2d, best_n);
        render(pic, track, ba);
        printf("%s (%s): track %d, angle %d: %d of %d map pixels differ (%d at the outline, %d inside, of those %d within a pixel)\n", names[f], labels[f], track, ba, best, covered, sil, inner, near);
        frames++;
        sum_cov += covered;
        sum_sil += sil;
        sum_in += inner;
        sum_near += near;
        if (dump_name && argc > 4 && (strstr(names[f], dump_name) || strstr(labels[f], dump_name))) {
            dump(argv[4], names[f], "mine", pp, 0);
            dump(argv[4], names[f], "ref", pp, 0);
            dump(argv[4], names[f], "diff", pp, 1);
        }
        fflush(stdout);
    }
    printf("%d frames: of %d map pixels in the screenshots %.1f %% differ at the outline, %.1f %% inside (%.1f %% of the inside within a pixel of the screenshot's colour)\n", frames, sum_cov,
           sum_cov ? 100.0 * sum_sil / sum_cov : 0.0, sum_cov ? 100.0 * sum_in / sum_cov : 0.0, sum_in ? 100.0 * sum_near / sum_in : 0.0);
    printf("2D parts of those frames: %d of %d pixels equal\n", sum2d_same, sum2d_n);
    return 0;
}

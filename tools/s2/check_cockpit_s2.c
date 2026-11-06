/*
 * Host test of the views from the car (firmware/s2cockpit.c, s2dash.c's pictures through tb_hud.c, s2cam.c): the cockpits' boxes against an independent reading of the pictures
 * (tools/s2/check_cockpit_s2.py), the dashboards' table, the needles and gear lamps against the rules of 0x46755 worked out in double precision, the cameras of the views 2, 4
 * and 6 against the original's camera records recorded frame by frame in DOSBox (tools/s2/dosbox_cockpit.py; skipped when there is no recording), the views' bookkeeping.
 * Built and run by tools/s2/check_cockpit_s2.sh. NEGATIVE=1 changes what the firmware is asked (a needle at the wrong part, a box shifted, a camera of the wrong view): every
 * check must then fail. Not part of the firmware.
 *
 * Usage: check_cockpit_s2 <work folder> [recording.bin crusc.bin]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "hw.h"
#include "s2cockpit.h"
#include "s2cam.h"
#include "s2car.h"
#include "s2teams.h"
#include "tb_hud.h"
#include "blit.h"
#include "track.h"

static int negative, fails, checks;
#define CHECK(c, ...) do { checks++; if(!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while(0)

static uint8_t *slurp(const char *dir, const char *name, size_t *n)
{
    char path[512];
    if(dir) snprintf(path, sizeof path, "%s/%s", dir, name);
    else snprintf(path, sizeof path, "%s", name);
    FILE *f = fopen(path, "rb");
    if(!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n + 8);
    if(fread(b, 1, *n, f) != *n) exit(2);
    b[*n] = 0;
    fclose(f);
    return b;
}

/* a picture's boxes (tb_hud_picture) drawn by the blitter's model into a 320 x 200 buffer: the number of opaque pixels and the hash over places and colours the Python reading prints */
static void boxes_hash(const uint8_t *pkc, size_t size, uint32_t *opaque, uint32_t *hash, uint32_t *nboxes)
{
    static uint8_t mem[0x20000];
    static uint8_t fb[320 * 200];
    static struct blit_list l;
    uint32_t n = tb_hud_picture(pkc, (uint32_t)size, mem, sizeof mem);
    *opaque = *hash = *nboxes = 0;
    if(!n) return;
    memset(fb, 0, sizeof fb);
    blit_start(&l, 0);
    const struct blit_image *b = (const struct blit_image *)(mem + 4);
    const uint32_t count = *(const uint32_t *)mem;
    for(uint32_t i = 0; i < count; i++)
        blit_copy(&l, mem + b[i].off, b[i].w, b[i].h, b[i].w, b[i].x + (negative && i == 0), b[i].y, BLIT_COPY, 0);
    blit_model(fb, &l);
    for(int i = 0; i < 320 * 200; i++)
        if(fb[i]) {
            (*opaque)++;
            *hash += (uint32_t)(i + 1) * fb[i];
        }
    *nboxes = count;
}

static void pictures_and_dashboards(const char *dir)
{
    size_t n;
    char *exp = (char *)slurp(dir, "expected.txt", &n);
    uint32_t most = 0;
    for(int i = 0; i < 9; i++) {
        char file[32], name[32], want[96];
        if(i < 8) {
            snprintf(file, sizeof file, "cock%d_l.pkc", i + 1);
            snprintf(name, sizeof name, "COCK%d_L.PKC", i + 1);
        } else {
            snprintf(file, sizeof file, "mirror_l.pkc");
            snprintf(name, sizeof name, "MIRROR_L.PKC");
        }
        size_t size;
        uint8_t *pkc = slurp(dir, file, &size);
        uint32_t opaque, hash, nb;
        boxes_hash(pkc, size, &opaque, &hash, &nb);
        snprintf(want, sizeof want, "pic %s %u %u\n", name, opaque, hash);
        CHECK(strstr(exp, want), "%s: the boxes draw %u pixels, hash %u; the reading of the file says otherwise", name, opaque, hash);
        if(nb > most) most = nb;
        free(pkc);
    }
    printf("cockpits: 8 pictures and the mirror's frame: the pixels the boxes draw are the pictures' (%u boxes at the most)\n", most);
    size_t tsize;
    uint8_t *text = slurp(dir, "crusc.dtr", &tsize);
    static struct s2ck_dash d[S2CK_DASHES];
    CHECK(s2ck_dash_parse(text, (uint32_t)tsize, d), "CRUSCOTTI did not parse");
    for(int m = 0; m < S2CK_DASHES; m++) {
        char want[512], *q = want;
        const uint32_t *w = (const uint32_t *)&d[m];
        q += sprintf(q, "dash %d", m);
        for(int k = 0; k < 22; k++) q += sprintf(q, " %u", w[k] + (negative && k == 3));
        sprintf(q, "\n");
        const char *at = strstr(exp, want);
        CHECK(at && (at == exp || at[-1] == '\n'), "dashboard %d differs from the reading of CRUSC.DTR", m);
        for(int g = 0; g < 5; g++) {
            int x, y, spr;
            s2ck_lamp(&d[m], m, g, &x, &y, &spr);
            snprintf(want, sizeof want, "lamp %d %d %d %d %d\n", m, g, x + negative, y, spr);
            CHECK(strstr(exp, want), "lamp %d %d: %d %d sprite %d", m, g, x, y, spr);
        }
    }
    printf("dashboards: the 8 rows of CRUSCOTTI and 40 gear lamps equal the reading\n");
    char *nl = (char *)slurp(dir, "needles.txt", &n);
    int total = 0, exact = 0, within = 0, worse = 0;
    for(char *line = strtok(nl, "\n"); line; line = strtok(0, "\n")) {
        int m, k, v[12];
        char kind[8];
        if(sscanf(line, "needle %d %7s %d %d %d %d %d %d %d %d %d %d %d %d %d", &m, kind, &k, v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6, v + 7, v + 8, v + 9, v + 10, v + 11) != 15) continue;
        const struct s2ck_needle *nd = kind[0] == 's' ? &d[m].speed : &d[m].revs;
        struct s2ck_line out[3];
        s2ck_needle(nd, (float)k / 100.0f + (negative ? 0.3f : 0.0f), out);
        int worst = 0;
        for(int i = 0; i < 3; i++) {
            const int g[4] = { out[i].x0, out[i].y0, out[i].x1, out[i].y1 };
            for(int j = 0; j < 4; j++) {
                int dd = abs(g[j] - v[4 * i + j]);
                if(dd > worst) worst = dd;
            }
        }
        total++;
        if(!worst) exact++; else if(worst == 1) within++; else worse++;
    }
    CHECK(total == 8 * 2 * 21 && worse == 0 && exact * 100 >= total * 97, "needles: %d positions, %d exact, %d a pixel off, %d worse", total, exact, within, worse);
    printf("needles: %d positions of 16 needles, %d exact, %d a pixel off at a rounding edge (float against double), %d worse\n", total, exact, within, worse);
    free(exp);
    free(nl);
    free(text);
}

/* ---------------------------------------------------------------------------------------- the recording */

#define REC 0xCC0
#define CAMSZ 0xAEC
#define HEAD 20
#define FRAME (HEAD + REC + 2 * CAMSZ + 0x40)

static float fl(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static int32_t i32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

static float wrap(float d)
{
    while(d > 3.14159265f) d -= 6.2831853f;
    while(d < -3.14159265f) d += 6.2831853f;
    return d;
}

static void cameras(const char *recfile, const char *crusc_file)
{
    size_t total, csize;
    FILE *probe = fopen(recfile, "rb");
    if(!probe) { printf("cameras: no recording (%s): skipped\n", recfile); return; }
    fclose(probe);
    uint8_t *data = slurp(0, recfile, &total);
    uint8_t *cr = slurp(0, crusc_file, &csize);
    const long frames = (long)(total / FRAME);
    struct { long n; float worst[6]; int rows_bad; } v[7];
    memset(v, 0, sizeof v);
    static struct s2car_state st;
    struct s2ck_state k;
    s2ck_reset(&k);
    int body = 0, body_seen = 0, last_view = -1, model = 1;
    long changes = 0;
    for(long fr = 0; fr < frames; fr++) {
        const uint8_t *r = data + fr * FRAME, *car = r + HEAD, *cam = car + REC;
        const int view = i32(cam + 0xAE4);
        memcpy(st.rec, car, REC);
        const float x = (float)i32(car + 0x4D8), z = (float)i32(car + 0x4E0);
        memcpy(&st.rec[0x4D8 / 4], &x, 4);                      /* (the game keeps the place in whole numbers, the firmware as floats) */
        memcpy(&st.rec[0x4E0 / 4], &z, 4);
        if(view == 2 && !body_seen) {                            /* the body height (record +0x218) is the camera's height less the rest */
            body = i32(cam + 0x4C) - (i32(car + 0x4DC) + i32(car + 0x4F4) + 400);
            body_seen = 1;
        }
        if(view != last_view) changes++;
        last_view = view;
        if(view != 2 && view != 4 && view != 6) { k.last_view = view; continue; }
        struct track_cam c;
        s2ck_camera(&st, negative ? (view == 2 ? 4 : 2) : view, body, &k, &c);
        const float want[6] = { (float)i32(cam + 0x48), (float)i32(cam + 0x50), (float)i32(cam + 0x4C), fl(cam + 0x58), fl(cam + 0x54), fl(cam + 0x5C) };
        const float got[6] = { c.x, c.y, c.h, c.yaw, c.pitch, c.roll };
        for(int j = 0; j < 6; j++) {
            const float dd = fabsf(j < 3 ? got[j] - want[j] : wrap(got[j] - want[j]));
            if(dd > v[view].worst[j]) v[view].worst[j] = dd;
        }
        v[view].n++;
        if(i32(cam + 0x1C) != s2ck_rows(view, &((const struct s2ck_dash *)cr)[model])) v[view].rows_bad++;
    }
    const char *names[7] = { "", "", "INCAR", "", "NEAR", "", "INSIDE" };
    for(int view = 2; view <= 6; view += 2) {
        if(!v[view].n) continue;
        const float tp = 1.5f, ta = 0.003f;
        const int ok = v[view].worst[0] <= tp && v[view].worst[1] <= tp && v[view].worst[2] <= tp && v[view].worst[3] <= ta && v[view].worst[4] <= ta && v[view].worst[5] <= ta &&
                       !v[view].rows_bad;
        CHECK(ok, "view %d %s: %ld frames, worst x %.2f z %.2f height %.2f units, heading %.4f pitch %.4f roll %.4f rad, %d rows differ", view, names[view], v[view].n,
              (double)v[view].worst[0], (double)v[view].worst[1], (double)v[view].worst[2], (double)v[view].worst[3], (double)v[view].worst[4], (double)v[view].worst[5], v[view].rows_bad);
        printf("camera view %d %s: %ld frames, worst error x %.2f z %.2f height %.2f units, heading %.4f pitch %.4f roll %.4f rad, rows %s\n", view, names[view], v[view].n,
               (double)v[view].worst[0], (double)v[view].worst[1], (double)v[view].worst[2], (double)v[view].worst[3], (double)v[view].worst[4], (double)v[view].worst[5],
               v[view].rows_bad ? "DIFFER" : "equal");
    }
    printf("cameras: %ld frames recorded, the body's height (record +0x218) %d, %ld view changes\n", frames, body, changes);
    free(data);
    free(cr);
}

int main(int argc, char **argv)
{
    if(argc < 2) return 2;
    negative = getenv("NEGATIVE") != 0;
    pictures_and_dashboards(argv[1]);
    CHECK(s2ck_hides_car(S2CK_INCAR) && s2ck_hides_car(S2CK_INSIDE) && s2ck_hides_car(S2CK_MIRROR) && !s2ck_hides_car(S2CK_BACK) && !s2ck_hides_car(S2CK_NEAR) &&
          !s2ck_hides_car(S2CK_EXTERNAL), "which views take the car off the draw list");
    const struct s2_pick p[4] = { { 1, 1 }, { 2, 4 }, { 3, 5 }, { 4, 5 } };
    CHECK(s2ck_model(&p[0]) == 0 && s2ck_model(&p[1]) == 3 && s2ck_model(&p[2]) == 6 && s2ck_model(&p[3]) == 7, "the dashboard of a car and of a bonus car");
    CHECK(s2ck_rows(S2CK_INCAR, 0) == 200 && s2ck_rows(S2CK_BACK, 0) == 200 && s2ck_rows(S2CK_MIRROR, 0) == 35, "the scene's rows");
    if(argc > 3) cameras(argv[2], argv[3]);
    printf("%d checks, %d failed%s\n", checks, fails, negative ? " (the negative control: they are meant to fail)" : "");
    return fails ? 1 : 0;
}

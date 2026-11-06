/*
 * The pages with 3D objects (firmware/s2pages3d.c, s2showroom.c, the arcade CAR page of s2arcade.c) on the PC: the same code that runs on the
 * Pocket, the geometry unit's C model for the hardware (hw_piece reads the registers the firmware wrote and draws the piece into the back
 * buffer with gu_draw), scripted buttons, and the independent references of tools/pages3d_ref.c. Included by ui_screen.c (it shares its stand-ins for the
 * swap, the keys and the sound); called from ui_tests().
 */
#include "../s2pages3d.h"
#include "../s2showroom.h"
#include "../s2scene.h"
#include "../geom.h"
#include "../track.h"
#include "../raster.h"
#include "../car.h"
#include <math.h>

/* ------------------------------------------------------------------------------------------------ the hardware's stand-ins */

int back_wait;
uint32_t draw_buf, draw_wait, back_clocks, back_cycles, tl_scene, frames_shown;
void (*hw_idle)(void);
static uint32_t pieces_drawn, faces_drawn, pixels_drawn, vertices_drawn;

void su_view_regs(const struct su_view *v)
{
    SU_CENTRE = RS_XY(v->cx, v->cy);
    SU_SCALE = v->scale;
    SU_FAR = (uint32_t)v->far8;
}

/* a piece into the geometry unit: the C model draws it from the registers as the hardware would (the material file, the view, the clip, the buffer) */
void hw_piece(const struct gu_piece *g)
{
    const struct su_view v = { (int16_t)(SU_CENTRE & 0xFFFF), (int16_t)(SU_CENTRE >> 16), SU_SCALE, (int32_t)SU_FAR };
    const struct rs_clip clip = { (int)(RS_CLIP0 & 0xFFFF), (int)(RS_CLIP0 >> 16), (int)(RS_CLIP1 & 0xFFFF), (int)(RS_CLIP1 >> 16) };
    struct gu_stats st = { 0, 0, 0, 0 };
    pixels_drawn += gu_draw(FB((GU_MODE >> 8) & 1), &clip, &v, (const uint8_t *)(uintptr_t)(SDRAM_BASE + GU_MLB), g, &st);
    pieces_drawn++;
    faces_drawn += st.faces;
    vertices_drawn += st.vertices;
}

/* the see-through table into the rasterizer (main.c writes it through RS_SMG): the model reads the table the firmware hands over */
void smg_load(const uint8_t *table) { rs_smg = table; }

/* ------------------------------------------------------------------------------------------------ the independent reference */

/* The references of the 3D pages: each page's state worked out again from the rules in the file headers of s2pages3d.c and s2showroom.c in plain C (the
   glide of a whole number cut towards zero, the turn of an angle cut to 2048ths, the slots), and the objects drawn through the geometry unit's C model by
   a piece builder of its own: double precision, the camera's frame worked out from the angles (track.c's convention: forward = (0, -cos p, sin p) in
   (x, y, height) = the engine's (x, z, y), right = forward x up, up = right x forward), not the firmware's track_block_piece(). */

#define REFMEM  ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0xD00000))     /* the reference's own copy of the models (the SDRAM is free there on the PC) */

static const uint8_t *r_logo_mlb, *r_badge[4], *r_letter, *r_car_mlb[5], *r_car_mesh[5];

static int32_t r_rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static int r_rd16s(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

/* The cars' windows as the reference has them (the game draws routine 160 with ENVM<team>1's picture, a 128 x 128 frame its filler wraps at 128): the
   frame tiled 2 x 2 as 256 x 256 under a texture record of its own (frames 1, the picture's word, the frame's place) after the car's file, and every
   routine-160 material record of the car pointing at it (a record's texture offset, its word +4: the records run up to the lowest texture record). */
static void ref_windows(uint8_t *mlb, uint32_t size, const uint8_t *env, uint8_t *block)
{
    const int32_t ero = r_rd32(env + 4);                        /* ENV.MLB: the first material, its texture record: count, word, frame offsets */
    const uint8_t *pic = env + r_rd32(env + ero + 8);
    const uint32_t at = (uint32_t)(block - mlb);
    memset(block, 0, 256);
    *(int32_t *)block = 1;
    *(int32_t *)(block + 4) = r_rd32(env + ero + 4);
    *(uint32_t *)(block + 8) = at + 256;
    for (int r = 0; r < 256; r++)
        for (int c = 0; c < 256; c++) block[256 + r * 256 + c] = pic[(r & 127) * 256 + (c & 127)];
    uint32_t table = size;
    for (uint32_t k = 0; (k + 1) * 16 <= table; k++) {
        const int32_t ro = r_rd32(mlb + 16 * k + 4);
        if (ro > 0 && (uint32_t)ro + 12 <= size && (uint32_t)ro < table) table = (uint32_t)ro;
    }
    for (uint32_t k = 0; k < table / 16; k++)
        if (r_rd32(mlb + 16 * k) == 160 && r_rd32(mlb + 16 * k + 4) > 0) *(uint32_t *)(mlb + 16 * k + 4) = at;
}

static int ref_load(struct zip *z, int team)
{
    static struct s2_cont c;
    uint8_t *to = REFMEM;
    uint32_t n;
    if (!s2scene_open(z, &c, "GFX/LOGO1.DAT")) return 0;
    r_logo_mlb = to;
    n = s2scene_entry(&c, "LOGO.MLB", to, 0x100000);
    to += (n + 255) & ~255u;
    for (int i = 0; i < 4; i++) {
        char nm[16] = "A1LOG-00.3DX";
        nm[1] = (char)('1' + i);
        r_badge[i] = to;
        n = s2scene_entry(&c, nm, to, 0x10000);
        if (!n) return 0;
        to += (n + 255) & ~255u;
    }
    r_letter = to;
    if (!s2scene_entry(&c, "ALETTER.3DX", to, 0x10000)) return 0;
    char path[24] = "GFX/CARS101.DAT";
    path[8] = (char)('0' + team);
    uint8_t *env_blocks = REFMEM + 0x100000 + 5 * 0x38000, *env = env_blocks + 5 * 0x10100;
    char env_path[24] = "GFX/ENVM11.DAT";
    env_path[8] = (char)('0' + team);
    if (!s2scene_open(z, &c, env_path) || !s2scene_entry(&c, "ENV.MLB", env, 0x10000)) return 0;
    if (!s2scene_open(z, &c, path)) return 0;
    for (int k = 0; k < 5; k++) {
        char nm[16] = "AUTO1.MLB";
        nm[4] = (char)('1' + k);
        uint8_t *at = REFMEM + 0x100000 + (uint32_t)k * 0x38000;
        uint32_t m = s2scene_entry(&c, nm, at, 0x38000);
        r_car_mlb[k] = at;
        const uint32_t mlb_size = m;
        m = (m + 255) & ~255u;
        memcpy(nm + 5, ".3DX", 5);
        r_car_mesh[k] = at + m;
        if (!m || !s2scene_entry(&c, nm, at + m, 0x38000 - m)) return 0;
        ref_windows(at, mlb_size, env, env_blocks + (uint32_t)k * 0x10100);
    }
    return 1;
}

static uint32_t ref_faces, ref_pieces;               /* what the last ref_draw handed the unit: the pieces and the faces it drew */

struct ref_cam { double x, y, h, pitch; };      /* the engine's (x, z, y) for the camera's place */

/* a block with the matrix M (rows the track's x, y, height; columns the block's x, y up, z) at O as a piece */
static void ref_piece(const uint8_t *mesh, const uint8_t *blk, const double M[9], const double O[3], const struct ref_cam *cam, uint32_t phase, struct gu_piece *g)
{
    const double sp = sin(cam->pitch), cp = cos(cam->pitch);
    const double basis[3][3] = { { -1.0, 0.0, 0.0 }, { 0.0, sp, cp }, { 0.0, -cp, sp } };      /* right, up, forward */
    const double C[3] = { cam->x, cam->y, cam->h };
    for (int k = 0; k < 3; k++) {
        for (int j = 0; j < 3; j++)
            g->m[3 * k + j] = (int32_t)((basis[k][0] * M[j] + basis[k][1] * M[3 + j] + basis[k][2] * M[6 + j]) * 65536.0);
        g->t[k] = (int32_t)((basis[k][0] * (O[0] - C[0]) + basis[k][1] * (O[1] - C[1]) + basis[k][2] * (O[2] - C[2]) + (k == 2 ? 128.0 : 0.0)) * 256.0);
    }
    double e[3];                                /* the camera in the block's frame */
    for (int j = 0; j < 3; j++) e[j] = M[j] * (C[0] - O[0]) + M[3 + j] * (C[1] - O[1]) + M[6 + j] * (C[2] - O[2]);
    for (int j = 0; j < 3; j++) g->e[j] = (int32_t)((e[j] - (double)g->m[6 + j] * (128.0 / 65536.0)) * 256.0);
    const double p[3] = { e[0] + r_rd16s(blk + 4), e[1] + r_rd16s(blk + 6), e[2] + r_rd16s(blk + 8) };      /* the face row: the sector the camera is in */
    const int below = p[1] < 0.0, xpos = p[0] >= 0.0, zpos = p[2] >= 0.0, zneg = p[2] < 0.0;
    const int c0 = p[2] > p[0], c2 = -p[2] <= p[0], c4 = -p[2] > -p[0], c6 = p[2] <= -p[0];
    g->row = below * 8 + (xpos ? (zpos ? !c0 : 2 + !c2) : (zneg ? 4 + !c4 : 6 + !c6));
    g->mirror = 0;
    g->phase = phase;
    g->refl = 0;
    g->obj = mesh;
    g->blk = blk;
}

struct ref_item {
    const uint8_t *mesh, *mlb;
    int32_t pos[3];                             /* the engine's x, y up, z */
    int a0, a1;
    uint32_t phase;
    int car;
};

/* the objects far to near over the picture (a copy of it in fb), through the unit's model */
static void ref_draw(volatile uint8_t *fb, const uint8_t *pic, const struct ref_item *it, int n, const struct ref_cam *cam)
{
    memcpy((void *)fb, pic, S2G_PIC_BYTES);
    ref_faces = ref_pieces = 0;
    const struct su_view v = { S3D_CX * 16, S3D_CY * 16, 128 * 16, (int32_t)((43808.0f + 128.0f) * 256.0f) };
    const struct rs_clip clip = { 0, 0, 320, 200 };
    int order[64], m = 0;
    double key[64];
    for (int i = 0; i < n; i++) {
        const double dx = it[i].pos[0] - cam->x, dy = it[i].pos[2] - cam->y, dh = it[i].pos[1] - cam->h, d = dx * dx + dy * dy + dh * dh;
        int j = m++;
        for (; j > 0 && key[j - 1] < d; j--) { order[j] = order[j - 1]; key[j] = key[j - 1]; }
        order[j] = i;
        key[j] = d;
    }
    for (int q = 0; q < m; q++) {
        const struct ref_item *o = &it[order[q]];
        struct gu_stats st = { 0, 0, 0, 0 };
        struct gu_piece g;
        if (o->car) {
            const struct track_cam tc = { (float)cam->x, (float)cam->y, (float)cam->h, 0.0f, (float)cam->pitch, 0.0f };
            const struct car_look lk = { (float)o->pos[0], (float)o->pos[2], (float)o->pos[1], (float)o->a1 * (6.2831853f / 2048.0f), 0, 0, 0, 0, 0, 0, 0 };
            struct track_block b[CAR_BLOCKS];
            const int nb = car_mesh_draw(o->mesh, &lk, &tc, b);
            /* the windows' reflection offsets: the original's object adds its way since it was last drawn, rotated into the view, times the rate to three accumulators
               (the page's rate 0.002, measured), so a car that stands at its place has the camera's right and forward axes' share of the whole place: u from -(right . p),
               v from -(forward . p), p the place as (x, z, y), each rounded and kept to 8 bits */
            const double sp = sin(cam->pitch), cp = cos(cam->pitch), place[3] = { (double)o->pos[0], (double)o->pos[2], (double)o->pos[1] };
            const uint32_t refl = (uint32_t)((int)lrint(0.002 * place[0]) & 255) << 16 | (uint32_t)((int)lrint(0.002 * (cp * place[1] - sp * place[2])) & 255) << 24;
            for (int k = 0; k < nb; k++) {
                double M[9], O[3] = { b[k].o[0], b[k].o[1], b[k].o[2] };
                for (int j = 0; j < 9; j++) M[j] = b[k].m[j];
                ref_piece(o->mesh, b[k].blk, M, O, cam, b[k].phase, &g);
                g.refl = refl;
                gu_draw(fb, &clip, &v, o->mlb, &g, &st);
                ref_pieces++;
                ref_faces += st.faces;
                st.faces = 0;
            }
            continue;
        }
        /* the object's matrix in the engine's (x, y up, z): turned about x by a0, then about y by a1, then the track's order of rows (x, z, y) */
        const double a = o->a0 * 2.0 * M_PI / 2048.0, h = o->a1 * 2.0 * M_PI / 2048.0;
        const double X[9] = { 1, 0, 0, 0, cos(a), -sin(a), 0, sin(a), cos(a) };           /* y towards z for a positive angle: y' = (0, cos a, sin a) */
        const double Y[9] = { cos(h), 0, -sin(h), 0, 1, 0, sin(h), 0, cos(h) };           /* x towards z: x' = (cos h, 0, sin h) */
        double R[9];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) R[3 * i + j] = Y[3 * i] * X[j] + Y[3 * i + 1] * X[3 + j] + Y[3 * i + 2] * X[6 + j];
        const double M[9] = { R[0], R[1], R[2], R[6], R[7], R[8], R[3], R[4], R[5] };     /* rows x, z, y */
        const uint8_t *blk = o->mesh + r_rd32(o->mesh + 0x18);
        const int32_t so = r_rd32(blk + 0x0C);
        double t[3] = { 0, 0, 0 };                                                        /* the block's own place in the mesh (its position record) */
        if (so > 0)
            for (int j = 0; j < 3; j++) t[j] = r_rd32(o->mesh + so + 0x10 + 4 * j);
        const double O[3] = { o->pos[0] + M[0] * t[0] + M[1] * t[1] + M[2] * t[2], o->pos[2] + M[3] * t[0] + M[4] * t[1] + M[5] * t[2],
                              o->pos[1] + M[6] * t[0] + M[7] * t[1] + M[8] * t[2] };
        ref_piece(o->mesh, blk, M, O, cam, o->phase, &g);
        gu_draw(fb, &clip, &v, o->mlb, &g, &st);
        ref_pieces++;
        ref_faces += st.faces;
    }
}

/* a whole number's glide towards its target as the page does it (cut towards zero), from `from`, for as long as it takes: where it stops */
static int32_t glide_to(int32_t v, double target, int row)
{
    for (int i = 0; i < 500; i++) v = (int32_t)(((double)v - target) * (0.5 + row * 0.005) + target);
    return v;
}

/* pixels of the rows y0 .. y1 that differ between the frame and the reference */
static int frame_diff(const uint8_t *shown, const uint8_t *ref, int y0, int y1)
{
    int d = 0;
    for (int i = y0 * 320; i < y1 * 320; i++) d += shown[i] != ref[i];
    return d;
}

/* ------------------------------------------------------------------------------------------------ the pages */

static uint8_t ref_fb[S2G_PIC_BYTES];

static void rgb_of(const char *prefix, const char *name, const uint8_t *px, const uint32_t *pal)
{
    char path[512];
    snprintf(path, sizeof path, "%s_%s.rgb", prefix, name);
    FILE *o = fopen(path, "wb");
    if (!o) return;
    for (int i = 0; i < S2G_PIC_BYTES; i++) {
        uint32_t c = pal[px[i]];
        uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
}

#define LIMIT_PIXELS    40                      /* what a frame of a page may differ from the reference by: the matrices' last bits (float against double) */

static void keys_at(char *script, size_t n, const char *spec)
{
    memset(script, '.', n - 1);
    script[n - 1] = 0;
    for (const char *p = spec; *p;) {
        int at = atoi(p);
        const char *c = strchr(p, ':');
        script[at] = c[1];
        p = c + 2;
        while (*p == ' ') p++;
    }
}

static void check_pages3d(const char *prefix)
{
    static char script[1400];
    static uint32_t pal[256];
    struct zip *z = s2gfx_zip();
    CHECK(ref_load(z, 1), "the reference's models");
    s2gfx_bkk("LOGOL.BKK", s2gfx_pic(1), pal);
    static uint8_t logo_pic[S2G_PIC_BYTES];
    memcpy(logo_pic, s2gfx_pic(1), S2G_PIC_BYTES);
    const uint32_t *p0 = pal;

    /* ---- TEAM SELECTION and ENTER YOUR NAME, scripted: the team page settled (frame 150), ENTER at 160 into the name state, settled (frame 280), the
       ring turned one letter and the cursor moved, ENTER at 340 keeps the name and leaves */
    s2save_reset();
    struct s2_menu_cfg c = { .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1 };
    keys_at(script, sizeof script, "160:B 300:R 320:D 340:B");
    begin_run(script, prefix);
    cap_at(151, 0);                             /* the frame of tick 150 */
    cap_at(281, 1);                             /* tick 280 */
    front = 0;
    int r = s2_page_team(&c);
    CHECK(r == S2P3D_DONE, "the team page ended with %d", r);
    CHECK(c.team == 1 && s2save_get(S2S_TEAM) == 1 && s2save_get(S2S_PROFILE) == 1, "the team and the profile are kept");
    CHECK(!strcmp(s2save_name(), "NNL"), "the name is %s, expected NNL (R turned the ring to N, D moved the cursor and copied it)", s2save_name());
    /* the team page at tick 150: 151 steps of the badges (0x200 + 20 a step for the chosen, + 5 for the others), the places the glide stops at */
    {
        static struct ref_item it[4];
        const int32_t slot[4][3] = { { 0, 300, 3500 }, { -3000, 0, 1000 }, { 0, 2000, 1000 }, { 3000, 0, 1000 } };      /* team 1 in front: no turn of the slots */
        for (int i = 0; i < 4; i++) {
            int a = 0x200;
            for (int k = 0; k < 151; k++) a = (int)((double)a + 0.02 * (i == 0 ? 1024.0 : 256.0)) & 0x7FF;
            it[i] = (struct ref_item){ r_badge[i], r_logo_mlb, { glide_to(0, slot[i][0], 0x50), glide_to(0, slot[i][1], 0x50), glide_to(0, slot[i][2], 0x50) }, 0, a, 0, 0 };
        }
        const struct ref_cam cam = { 0, 4800, 500, 0.0 };
        ref_draw(ref_fb, logo_pic, it, 4, &cam);
        int d = frame_diff(cap[0], ref_fb, 32, 168);
        CHECK(d <= LIMIT_PIXELS, "TEAM SELECTION: %d pixels differ from the reference", d);
        rgb_of(prefix, "team3d_ref", ref_fb, p0);
        rgb_of(prefix, "team3d", cap[0], p0);
        printf("pages3d: TEAM SELECTION at tick 150: %d of %d pixels differ from the independent reference (the 3D rows); %u pieces, %u faces a frame\n", d, 136 * 320, ref_pieces, ref_faces);
    }
    /* the name state at tick 280: the chosen badge's glide to its place up and back, the 26 letters on the ring (cur = 'M'), the tyres' tilts settled */
    {
        static struct ref_item it[27];
        int n = 0;
        const int32_t base[3] = { 0, 300, 3500 };
        int a = 0x200;
        for (int k = 0; k < 281; k++) a = (int)((double)a + 0.02 * 1024.0) & 0x7FF;
        int32_t px = glide_to(0, base[0], 0x50), py = glide_to(0, base[1], 0x50), pz = glide_to(0, base[2], 0x50);
        for (int k = 0; k < 120; k++) {                                              /* the 120 steps of the team page's glide, then the name state's */
            px = glide_to(px, base[0], 0x50);
            py = glide_to(py, base[1] + 500, 0x50);
            pz = glide_to(pz, base[2] - 500, 0x50);
        }
        it[n++] = (struct ref_item){ r_badge[0], r_logo_mlb, { px, py, pz }, 0, a, 0, 0 };
        const int cur = 'M' - 'A';
        for (int k = 0; k < 26; k++) {
            int f = k - cur;
            if (f < 0) f += 26;
            const double ang = f * -(2.0 * M_PI / 26.0), tilt = k == cur ? -0.3141592741012573 : 2.8274335861206055;
            it[n++] = (struct ref_item){ r_letter, r_logo_mlb, { (int32_t)(3500.0 * sin(ang)), 0, (int32_t)(3500.0 * cos(ang)) },
                                         (int)(tilt * 1024.0 / M_PI) & 0x7FF, (int)(f * (2.0 * M_PI / 26.0) * 1024.0 / M_PI) & 0x7FF, ((uint32_t)k * 0x9D8u + 0x226u) >> 2, 0 };
        }
        const struct ref_cam cam = { 0, 4800, 500, 0.0 };
        ref_draw(ref_fb, logo_pic, it, n, &cam);
        int d = frame_diff(cap[1], ref_fb, 32, 170);
        printf("pages3d: ENTER YOUR NAME at tick 280: %d of %d pixels differ from the independent reference (the 3D rows); %u pieces, %u faces a frame\n", d, 138 * 320, ref_pieces, ref_faces);
        rgb_of(prefix, "name3d_ref", ref_fb, p0);
        rgb_of(prefix, "name3d", cap[1], p0);
        CHECK(d <= 400, "ENTER YOUR NAME: %d pixels differ from the reference", d);
    }

    /* ---- the keys of the team page: ESC leaves at once (the original asks EXIT SCREAMER 2? YES / NO here, a core cannot end itself): nothing is kept, no sound */
    s2save_reset();
    c = (struct s2_menu_cfg){ .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1 };
    keys_at(script, sizeof script, "5:A");
    begin_run(script, prefix);
    front = 0;
    r = s2_page_team(&c);
    CHECK(r == S2P3D_QUIT && c.team == 1 && s2save_get(S2S_PROFILE) == 0 && nclicks == 0, "ESC: result %d, team %d, profile %d, %d sounds", r, c.team, s2save_get(S2S_PROFILE), nclicks);
    CHECK(calls == 20, "leaving takes the flash's 13 frames after the ESC (tick 5): %u frames, expected 20", calls);

    /* RIGHT, RIGHT, LEFT choose team 2, 3, 2 (sound 4 each; the slots turn, the badges glide), ENTER (sound 6) the name, ENTER (sound 1) keeps it: team 2 */
    s2save_reset();
    c = (struct s2_menu_cfg){ .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1 };
    keys_at(script, sizeof script, "5:R 15:R 25:L 35:B 60:B");
    begin_run(script, prefix);
    front = 0;
    r = s2_page_team(&c);
    static const int moves[] = { 4, 4, 4, 6, 1 };
    CHECK(r == S2P3D_DONE && c.team == 2 && s2save_get(S2S_TEAM) == 2 && nclicks == 5 && !memcmp(clicks, moves, sizeof moves), "team keys: result %d team %d, %d sounds", r, c.team, nclicks);
    CHECK(!strcmp(s2save_name(), "MIL") && s2save_get(S2S_PROFILE) == 1, "an unchanged name stays MIL (%s)", s2save_name());

    /* LEFT from team 1 goes round to team 4; a car 5 of team 1 that team 4 has not won goes back to car 1 */
    s2save_reset();
    s2save_unlock_all = 0;
    c = (struct s2_menu_cfg){ .track = 1, .team = 1, .car = 5, .laps = 1, .skill = 1 };
    s2save_set_car5(1, 1);
    keys_at(script, sizeof script, "5:L 30:B 60:B");
    begin_run(script, prefix);
    front = 0;
    r = s2_page_team(&c);
    CHECK(r == S2P3D_DONE && c.team == 4 && c.car == 1, "LEFT wraps to team 4 (%d) and its car 5 is not won: car %d", c.team, c.car);
    s2save_unlock_all = 1;

    /* the name: R turns the ring to N (name N), D moves the cursor (the letter is copied to the second place), R again (O), U back (the first place takes O),
       ESC goes back to the team, ENTER again shows the same name and keeps it: OOL; the sounds 6 (name), 9, 9 (letters), 6, 1 */
    s2save_reset();
    c = (struct s2_menu_cfg){ .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1 };
    keys_at(script, sizeof script, "10:B 40:R 60:D 80:R 100:U 120:A 140:B 170:B");
    begin_run(script, prefix);
    front = 0;
    r = s2_page_team(&c);
    static const int letters[] = { 6, 9, 9, 6, 1 };
    CHECK(r == S2P3D_DONE && !strcmp(s2save_name(), "OOL") && nclicks == 5 && !memcmp(clicks, letters, sizeof letters), "name keys: result %d, name %s, %d sounds", r, s2save_name(), nclicks);

    /* R held on the ring: a letter every 6 frames (the turn is taken again above 0.95 of 1, at 0.16 a frame: 6 steps), so 7 in 41 frames: M + 7 = T */
    s2save_reset();
    c = (struct s2_menu_cfg){ .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1 };
    keys_at(script, sizeof script, "10:B");
    for (int i = 40; i < 81; i++) script[i] = 'R';
    script[120] = 'B';
    begin_run(script, prefix);
    front = 0;
    r = s2_page_team(&c);
    int nine = 0;
    for (int i = 0; i < nclicks; i++) nine += clicks[i] == 9;
    CHECK(r == S2P3D_DONE && nine == 7 && !strncmp(s2save_name(), "TIL", 3), "R held for 41 frames: %d letters, the name %s", nine, s2save_name());

    /* a team page that fails to load (no LOGO1.DAT) is not shown, the menu goes on */
    printf("pages3d: team page: keys, sounds, wrap, the name and the held ring checked\n");

    /* ---- the CAR page (s2arcade.c, the carousel of s2showroom.c) against the same kind of reference: the settled ring at tick 100 (car 2 of 5 chosen), after
       RIGHT at tick 110 settled again (tick 230), after ENTER at 250 in the page's second state settled (tick 380), ENTER at 400 leaves */
    s2save_reset();
    c = (struct s2_menu_cfg){ .track = 1, .team = 1, .car = 2, .laps = 1, .skill = 1 };
    keys_at(script, sizeof script, "110:R 250:B 400:B");
    begin_run(script, prefix);
    cap_at(101, 2);
    cap_at(231, 3);
    cap_at(381, 4);
    front = 0;
    r = s2arcade_page(S2ARC_CAR, &c);
    CHECK(r == S2ARC_NEXT && c.car == 3, "the CAR page ended with %d, car %d", r, c.car);
    {
        static uint32_t bpal[256];
        static uint8_t bkg[S2G_PIC_BYTES];
        CHECK(s2gfx_bkk("BKG_2L.BKK", bkg, bpal), "BKG_2L.BKK");
        const int n = 5;
        int32_t slot[5][3];
        const struct ref_cam cam = { 0, 4000, 600, -0.39269908 };
        const int ticks[3] = { 100, 230, 380 };
        for (int shot = 0; shot < 3; shot++) {
            const int chosen = shot ? 2 : 1;                                /* the key turned the ring after frame 110: the first shot has car 2 chosen, the rest car 3 */
            for (int i = 0; i < n; i++) {
                const double zz = i == 0 ? 2800.0 : 3000.0, a = -(2.0 * M_PI / n) * i;
                slot[i][0] = (int32_t)(zz * sin(a));
                slot[i][1] = 0;
                slot[i][2] = (int32_t)(zz * cos(a));
            }
            int32_t at[5][3], from[5][3];
            int32_t turned[5][3];
            for (int turn = 0; turn < 2; turn++) {                          /* the slots as the page has them: turned round as many times as the chosen car's number */
                const int k = turn ? 2 : 1;
                for (int i = 0; i < n; i++) memcpy(turned[i], slot[(i - k % n + n) % n], sizeof turned[i]);
                if (turn == 0) memcpy(from, turned, sizeof from);
                else memcpy(at, turned, sizeof at);
            }
            struct ref_item it[5];
            for (int i = 0; i < n; i++) {
                int a = 0;
                for (int f = 0; f <= ticks[shot]; f++) {
                    const int me = i == (f <= 110 ? 1 : 2);
                    a = (int)((double)a + 0.02 * (me ? 512.0 : 128.0)) & 0x7FF;
                }
                int32_t p[3];
                for (int k = 0; k < 3; k++) {
                    int32_t v = from[i][k];
                    if (shot == 0) { p[k] = v; continue; }
                    double target = at[i][k];
                    int row = 0x46;
                    if (shot == 2) {                                        /* the second state: the chosen car forward and up, the others gone down and back */
                        const int me = i == chosen;
                        target = k == 0 ? 0.0 : at[i][k] + (me ? 200 : k == 1 ? -0x4B0 : -0xFA0);
                        row = me ? 0x5A : 0x46;
                        v = glide_to(from[i][k], at[i][k], 0x46);
                        v = glide_to(v, target, row);
                        p[k] = v;
                        continue;
                    }
                    p[k] = glide_to(v, target, row);
                }
                it[i] = (struct ref_item){ r_car_mesh[i], r_car_mlb[i], { p[0], p[1], p[2] }, 0, a, 0, 1 };
            }
            ref_draw(ref_fb, bkg, it, n, &cam);
            int d = frame_diff(cap[2 + shot], ref_fb, 50, 150);
            char what[32];
            snprintf(what, sizeof what, "car3d_%d", shot);
            rgb_of(prefix, what, cap[2 + shot], bpal);
            snprintf(what, sizeof what, "car3d_%d_ref", shot);
            rgb_of(prefix, what, ref_fb, bpal);
            printf("pages3d: CAR page, shot %d (tick %d, %s): %d of %d pixels differ from the independent reference (rows 50 to 150); %u pieces, %u faces a frame\n", shot, ticks[shot],
                   shot == 0 ? "the ring at rest" : shot == 1 ? "after RIGHT" : "the second state", d, 100 * 320, ref_pieces, ref_faces);
            CHECK(d <= 150, "CAR page shot %d: %d pixels differ from the reference", shot, d);
        }
    }
}

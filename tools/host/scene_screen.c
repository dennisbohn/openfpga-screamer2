/*
 * The ARCADE title's cars on the PC (firmware/s2arcscene.c, the title of s2arcade.c): included by ui_screen.c, which has the stand-ins of the hardware (the swap, the scripted buttons, the
 * sounds) and pages3d_screen.c's geometry unit model. What is tested here is the page with the real files: the title run by the page runner for 837 frames (the first 812 frames, a
 * take is 201, and the 25 of the fade out after ENTER), every frame's take, the four cars' places and headings and the camera against tools/ui_scene_ref.py (a second reading of
 * the page's rules, in Python and double precision: places cut to whole units equal within 1, headings within 1 of 2048, the camera exact), the camera and the first frame's
 * places against the numbers the running original has (tools/s2/dosbox_scene.py: tools/scene.sh compares every frame of its recording; the constants below are what it recorded), the
 * negative control (the same comparison one frame late must fail), the takes and their lengths, what the geometry unit is handed in a whole round of the four takes (the title's cost),
 * and the title without its cars (a team that has none: the picture and the prompt as before).
 */
#include "../s2arcscene.h"

#define SCENE_FRAMES    837
#define SCENE_ROUND     (4 * 201)

static struct { int take, a1[4], cam_a0, cam_a1; int32_t car[4][3], cam[3]; } scene_rec[SCENE_FRAMES + 2];
static int scene_nrec;

static void scene_probe(void)
{
    if (calls < 1 || calls > SCENE_FRAMES) return;
    typeof(scene_rec[0]) *r = &scene_rec[calls - 1];
    for (int i = 0; i < 4; i++) s2arcscene_car(i, r->car[i], &r->a1[i]);
    s2arcscene_camera(r->cam, &r->cam_a0, &r->cam_a1);
    r->take = s2arcscene_take();
    scene_nrec = (int)calls;
}

struct ref_state { int take, car[4][4], cam[3], a0, a1; };

static int scene_ref(const char *ref, struct ref_state *out, int max)
{
    char path[512];
    snprintf(path, sizeof path, "%s/scene_states.txt", ref);
    FILE *f = fopen(path, "r");
    int n = 0, frame;
    while (f && n < max) {
        struct ref_state *s = &out[n];
        if (fscanf(f, "%d %d", &frame, &s->take) != 2) break;
        for (int i = 0; i < 4; i++)
            if (fscanf(f, "%d %d %d %d", &s->car[i][0], &s->car[i][1], &s->car[i][2], &s->car[i][3]) != 4) return -1;
        if (fscanf(f, "%d %d %d %d %d", &s->cam[0], &s->cam[1], &s->cam[2], &s->a0, &s->a1) != 5) return -1;
        n++;
    }
    if (f) fclose(f);
    return n;
}

static int near_angle(int a, int b)
{
    const int d = (a - b) & 0x7FF;
    return d <= 1 || d >= 0x7FF;
}

/* frames of the run against the reference's, `late` frames later (the negative control): how many differ, and how many are not exactly the reference's */
static void scene_compare(const struct ref_state *ref, int nref, int late, int *bad, int *inexact)
{
    *bad = *inexact = 0;
    for (int f = 0; f < scene_nrec; f++) {
        const struct ref_state *s = &ref[(f + late) % SCENE_ROUND];
        int ok = scene_rec[f].take == s->take, exact = 1;
        for (int i = 0; i < 4; i++) {
            for (int k = 0; k < 3; k++) {
                const int d = scene_rec[f].car[i][k] - s->car[i][k];
                ok &= d >= -1 && d <= 1;
                exact &= d == 0;
            }
            ok &= near_angle(scene_rec[f].a1[i], s->car[i][3]);
            exact &= scene_rec[f].a1[i] == s->car[i][3];
        }
        for (int k = 0; k < 3; k++) ok &= scene_rec[f].cam[k] == s->cam[k];
        ok &= scene_rec[f].cam_a0 == s->a0 && scene_rec[f].cam_a1 == s->a1;
        *bad += !ok;
        *inexact += ok && !exact;
    }
    (void)nref;
}

static void check_scene(const char *ref, const char *prefix)
{
    static struct ref_state states[SCENE_ROUND + 8];
    const int nref = scene_ref(ref, states, SCENE_ROUND + 8);
    CHECK(nref >= SCENE_ROUND, "scene_states.txt has %d frames, %d are needed", nref, SCENE_ROUND);
    if (nref < SCENE_ROUND) return;
    static uint32_t pal[256];
    CHECK(s2gfx_bkk("ARCADE_L.BKK", s2gfx_pic(0), pal), "ARCADE_L.BKK");

    /* the page: 812 frames of a title, ENTER at the 813th, the 25 frames of its fade out; the cars drive on all through it */
    s2save_reset();
    struct s2_menu_cfg c = { .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1 };
    char script[SCENE_FRAMES + 8];
    keys_at(script, sizeof script, "812:B");
    begin_run(script, prefix);
    front = 0;
    scene_nrec = 0;
    frame_probe = scene_probe;
    const int r = s2arcade_page(S2ARC_TITLE, &c);
    frame_probe = 0;
    scene_probe();
    CHECK(r == S2ARC_NEXT && calls >= SCENE_FRAMES - 1 && calls <= SCENE_FRAMES + 1, "the title ended with %d after %u frames (ENTER at frame 812, a fade of 25)", r, calls);
    CHECK(scene_nrec >= SCENE_FRAMES - 1, "%d frames recorded of %d", scene_nrec, SCENE_FRAMES);

    /* against the reference: every frame of the run (frame f is the round's frame f mod 804) */
    int bad, inexact;
    scene_compare(states, nref, 0, &bad, &inexact);
    CHECK(!bad, "%d of %d frames differ from the reference by more than 1 (a place or a heading) or in the take or the camera", bad, scene_nrec);
    int control, control_inexact;
    scene_compare(states, nref, 1, &control, &control_inexact);
    CHECK(control > scene_nrec / 2, "the negative control (the reference one frame later) differs in only %d of %d frames: the comparison cannot tell", control, scene_nrec);

    /* the takes: 0, 1, 2, 3 and round, 201 frames each */
    int takes_ok = 1, n_takes = 0;
    for (int f = 0; f < scene_nrec; f++) {
        takes_ok &= scene_rec[f].take == (f / 201) % 4;
        if (f && scene_rec[f].take != scene_rec[f - 1].take) n_takes++;
    }
    CHECK(takes_ok && n_takes == 4, "the takes do not go 0, 1, 2, 3, 0 in 201 frames each (%d changes in %d frames)", n_takes, scene_nrec);

    /* what the running original has (tools/s2/dosbox_scene.py, 12,479 rows of its memory: tools/scene.sh): the camera is the same in all four takes, and the first frame's places;
       the camera's a1 is 1023 where double precision alone gives 1024 (the program's own floats: the reference has them too) */
    int cam_same = 1;
    for (int f = 0; f < scene_nrec; f++)
        cam_same &= scene_rec[f].cam[0] == -2013 && scene_rec[f].cam[1] == 1084 && scene_rec[f].cam[2] == -3934 && scene_rec[f].cam_a0 == 1982 && scene_rec[f].cam_a1 == 1023;
    CHECK(cam_same, "the camera is not the original's: (%d, %d, %d) a0 %d a1 %d at frame 0", scene_rec[0].cam[0], scene_rec[0].cam[1], scene_rec[0].cam[2], scene_rec[0].cam_a0, scene_rec[0].cam_a1);
    /* the recorded frame k = 0, t = 0.965 (first row of the original's recording, 2 x 193 hundredths after the page began): cars at (-3495, 0, -5307) heading 1882 etc. */
    const struct { int32_t x, y, z; int a1; } orig[4] = { { -3495, 0, -5307, 1882 }, { -4385, 217, -3904, 1770 }, { -1663, 217, -8554, 46 }, { -1035, 217, -6216, 2037 } };
    int orig_ok = 1;
    for (int i = 0; i < 4; i++)
        orig_ok &= scene_rec[193].car[i][0] == orig[i].x && scene_rec[193].car[i][1] == orig[i].y && scene_rec[193].car[i][2] == orig[i].z && scene_rec[193].a1[i] == orig[i].a1;
    CHECK(orig_ok, "frame 193 is not the original's (car 0 at %d, %d, %d heading %d)", scene_rec[193].car[0][0], scene_rec[193].car[0][1], scene_rec[193].car[0][2], scene_rec[193].a1[0]);

    /* the title's cost: a whole round of the four takes drawn (the picture, the cars): what the geometry unit is handed, a frame */
    unsigned long pieces = 0, faces = 0, vertices = 0, pixels = 0;
    unsigned max_pieces = 0, max_faces = 0, max_pixels = 0, visible = 0, blank = 0;
    s2arcscene_frame(0);
    for (int f = 0; f < SCENE_ROUND; f++) {
        if (f) s2arcscene_frame((uint32_t)f);
        struct blit_list *l = s2gfx_begin_on(0);
        s2gfx_picture(l, s2gfx_pic(0));
        pieces_drawn = faces_drawn = 0;
        pixels_drawn = vertices_drawn = 0;
        s2arcscene_draw(l);
        s2gfx_flush();
        pieces += pieces_drawn;
        faces += faces_drawn;
        vertices += vertices_drawn;
        pixels += pixels_drawn;
        max_pieces = pieces_drawn > max_pieces ? pieces_drawn : max_pieces;
        max_faces = faces_drawn > max_faces ? faces_drawn : max_faces;
        max_pixels = pixels_drawn > max_pixels ? pixels_drawn : max_pixels;
        int differ = 0;
        for (int i = 0; i < S2G_PIC_BYTES && !differ; i++) differ = FB(0)[i] != s2gfx_pic(0)[i];
        visible += differ;
        blank += !differ;
        if (f % 201 == 0) CHECK(!differ, "frame %d (a take's first): a car is in view at the start of its path", f);
    }
    printf("scene: the title's cars over four takes (%d frames): the geometry unit is handed %.1f pieces, %.1f faces, %.1f vertices and fills %.0f pixels a frame on average, "
           "%u pieces, %u faces and %u pixels at most; cars are in view in %u of the frames (none in %u)\n", SCENE_ROUND, (double)pieces / SCENE_ROUND, (double)faces / SCENE_ROUND,
           (double)vertices / SCENE_ROUND, (double)pixels / SCENE_ROUND, max_pieces, max_faces, max_pixels, visible, blank);
    CHECK(max_pieces <= 64 && max_faces <= 700, "a frame of the title hands the unit %u pieces and %u faces", max_pieces, max_faces);

    /* a team that has no cars to show: the loader says so and the title is the picture and the prompt as before */
    CHECK(!s2arcscene_load(s2gfx_zip(), 9, 4), "a team 9's cars were found");

    printf("scene: ARCADE title: %d frames of the page equal to the reference (cars within 1: in %d of them one is not exactly the reference's), the camera and the first frames the original's, takes of 201 frames, the negative "
           "control differs in %d\n", scene_nrec - bad, inexact, control);
}

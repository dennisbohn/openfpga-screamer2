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

/* ------------------------------------------------------------------------------------------------ the hardware's stand-ins */

int back_wait;
uint32_t draw_buf, draw_wait, back_clocks, back_cycles, tl_scene, frames_shown;
void (*hw_idle)(void);
static uint32_t pieces_drawn, faces_drawn;

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
    gu_draw(FB((GU_MODE >> 8) & 1), &clip, &v, (const uint8_t *)(uintptr_t)(SDRAM_BASE + GU_MLB), g, &st);
    pieces_drawn++;
    faces_drawn += st.faces;
}

/* ------------------------------------------------------------------------------------------------ the pages */

static void check_pages3d(const char *prefix)
{
    static char script[1200];
    s2save_reset();
    struct s2_menu_cfg c = { .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1 };
    memset(script, '.', sizeof script - 1);
    begin_run(script, prefix);
    shot_at(2, "team_a");
    shot_at(30, "team_b");
    shot_at(100, "team_c");
    front = 0;
    script[110] = 'B';
    script[140] = 'B';
    int r = s2_page_team(&c);
    printf("pages3d: the team page ended with %d after %u frames, %u pieces, %u faces\n", r, calls, pieces_drawn, faces_drawn);
}

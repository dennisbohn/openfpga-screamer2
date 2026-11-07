/*
 * The menus' music (s2menumusic.h)
 */
#include "s2menumusic.h"
#include "music.h"
#include "s2save.h"
#include "s2sound.h"

#define MENU_TRACK  2                       /* the CD track of the menu's song (GAME.DAT: the first audio track; the races' are 3 .. 9) */

/* MUSIC ON is 0 in the settings page's list (0x128a44); music.c asks music_on */
static void setting(void)
{
    music_on = s2save_get(S2S_MUSIC) == 0;
}

void s2mm_page(void)
{
    if (s2snd_racing()) return;
    setting();
    if (music_on && !music_active()) music_cd(MENU_TRACK);
}

void s2mm_song(void)
{
    setting();
    music_cd(MENU_TRACK);                   /* (stops what plays; plays nothing when MUSIC is OFF) */
}

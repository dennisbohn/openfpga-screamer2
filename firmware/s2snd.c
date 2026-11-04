/*
 * Screamer 2's sound rules (s2snd.h)
 */
#include "s2snd.h"
#include "s2params.h"
#include "dat.h"

int s2snd_cd_tracks(const uint8_t *game_dat, uint32_t size, int out[7])
{
    static uint8_t text[2048];
    struct dat_entry e;
    uint32_t w[7];
    if(!dat_find(game_dat, size, "MISC.DTR", &e) || e.size > sizeof text) return 0;
    uint32_t n = dat_extract(game_dat, size, &e, text, sizeof text);
    if(!n || s2_dtr_words(text, n, "CD_TRACKS", w, 7) != 7) return 0;
    for(int i = 0; i < 7; i++) out[i] = (int)w[i];
    return 1;
}

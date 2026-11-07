/*
 * The menus' music: CD track 2, the song the original plays between the races (docs/screamer2/menus.md "The menu's music").
 *
 * What STARTL.EXE does (read with tools/dis.sh -f; the CD calls are 0x4d784 play(track, repeat), 0x4d908 stop, 0x4d990 volume, the
 * MUSIC setting is [0x128a44], 0 for ON): the song is `music_cd(2)`, repeated, in these places and no others, all only with MUSIC ON
 * and a CD: the end of the attract sequence (0x3cc1e), the program's menu loop each time it begins again after a race (0x38951: every
 * kind of race comes back through it), the championship's desk scene after it is loaded (0x3bd29, the song stopped at its start,
 * 0x3bc46: it begins again), and the MUSIC row of GAME SETTINGS (0x42462 ON, 0x42451 OFF). It is stopped where the attract
 * sequence and a race begin (0x3ca3a, 0x386bd, the race's own set-up 0x2ebc4, which plays the track's song) and at the end of
 * a race (0x3156c). In between nothing touches the CD: no page starts, changes or fades it (the song goes on through page changes and
 * loads, the drive plays it by itself and the poll of the page loop, 0x4d800, starts it again when it ends).
 *
 * Here the song is the ring's (music.c) and the CPU keeps it, so it differs in two things. It is not started at an event but
 * wherever a page begins outside a race and no song is playing (s2mm_page(), from the page runner): that is the same places, since a
 * page follows each of them, and it starts the song where the page is up instead of in front of a load. And a long load holds it
 * (slot.c asks music.c before every blocking read): the song stops for the load and goes on from where it was when the page is up,
 * where the CD plays through.
 */
#pragma once

/* A page begins (the page runner): the menu's song starts if MUSIC is ON, no song plays or waits, and no race is on (the pause menu
   and the end screen are pages of a race, which has its own song). */
void s2mm_page(void);
/* The song from its start, even if it plays; silence when MUSIC is OFF. The desk scene (after its load) and the MUSIC row of GAME
   SETTINGS (after the value changed) call it. */
void s2mm_song(void);

/*
 * The LOADING screen (STARTL.EXE 0x34ede, docs/screamer2/menus.md): the track's LOAD_nL picture, its name's banner (sprite 0x13 + n - 1)
 * at the top, LOADING... at the bottom and the track's BEST TIME and BEST LAP lines. It is put up before the track is built and stays:
 * the display is driven by the hardware from the front buffer and its palette, so the CPU can be busy for seconds (the load takes 3 to
 * 5 s on the Pocket, `T1 load` in the debug log) and the picture does not change. Once the track is ready it fades out.
 */
#pragma once
#include <stdint.h>

/* A record as the save will keep it (the original's best-time lists, 15 rows a track and number of laps: time in seconds, 3599.999 for
   an empty row, and the name of the driver, "MIL" when none was entered) */
struct s2_record { float time; char name[4]; };

/* The track's best total time over `laps` laps (1, 3, 5, 10) and its best lap, for the loading screen: the first rows of the arcade
   lists of the save (s2save.h). A track nobody has driven has 3599.999 and MIL, which the screen shows as --:--:-- MIL, as the original
   does. */
void s2_best_get(int track, int laps, struct s2_record *total, struct s2_record *lap);
/* The place of a race of `laps` laps (1, 3, 5, 10) among the save's lists of totals: 0 .. 3 */
int s2_laps_list(int laps);

/* "01:23:45" (minutes, seconds, hundredths, p needs 9 bytes) as the original prints a time (0x37e1e); a time that is not above 0 or
   is 3599.9 and over (an empty row) is --:--:--. Returns the end of the text. */
char *s2_time_text(char *p, float t);
/* A row of a best-times list as the end screen and the HI SCORES page set it (0x42d99): fixed pitch (the byte 1), the row's number (1 .. 15, a space in front
   of a one-digit one), ". ", the name, six spaces, the time: "\x01 1. MIL      01:23:45". p needs 40 bytes. */
struct s2save_row;
void s2_row_text(char *p, int row, const struct s2save_row *r);
/* The same and the name behind it, as the original prints a record (0x37e1e, "%s %s"); p needs 16 bytes. */
void s2_record_text(char *p, const struct s2_record *r);

/* The screen for track 1 .. 7 and a race of `laps` laps, shown at once; the palette of its picture (stays valid), or 0 if the picture,
   the sprites or the fonts are not there (the caller then shows its own text). */
const uint32_t *s2load_show(int track, int laps);
/* The track is ready: the screen fades out (0.4 s). Needs the palette s2load_show() returned. */
void s2load_fade(void);

/*
 * The game's files and the screen that lists them (files.h)
 */
#include "files.h"
#include "hw.h"
#include "console.h"
#include "dlog.h"
#include "slot.h"
#include "zip.h"

#define ZIP_SLOT    3           /* core/data.json: SCREAMER2.zip */

/* The files by group. A group is a row of the screen: a label, then one short name per file;
   the file's path in the ZIP is pre + name + post, matched on the end of the stored path. */
static const struct group {
    uint8_t row, must;          /* the screen's row; 1 if a race cannot run without them */
    const char *label, *pre, *post, *items;
    const uint8_t *slots;       /* the items are not in the ZIP but in these data slots (the music) */
} groups[] = {
    /* what a race at DETAIL HIGH in daylight reads (docs/screamer2/comparison.md, "Slots needed") */
    {  4, 1, "DATA/",         "DATA/",        ".DAT",     "GAME GEN LOCAL SOUND", 0 },
    {  5, 1, "DATA/MAPn",     "DATA/MAP",     ".DAT",     "1 2 3 4 5 6 7", 0 },
    {  6, 1, "GFX/",          "GFX/",         ".DAT",     "GAME1 GRAF1", 0 },
    {  7, 1, "GFX/MAPnH01",   "GFX/MAP",      "H01.DAT",  "1 2 3 4 5 6 7", 0 },
    {  8, 1, "GFX/CARSt01",   "GFX/CARS",     "01.DAT",   "1 2 3 4", 0 },
    /* the rest: the CD music, the night set (tracks 5 and 7 have none), reflections, menu rooms, scenes */
    { 11, 0, "Music (CD)",    0, 0,                     "GAME.GOG GAME.DAT", (const uint8_t[]){ 22, 21 } },
    { 12, 0, "GFX/MAPnH11",   "GFX/MAP",      "H11.DAT",  "1 2 3 4 6", 0 },
    { 13, 0, "GFX/CARSt11",   "GFX/CARS",     "11.DAT",   "1 2 3 4", 0 },
    { 14, 0, "GFX/ENVPn01",   "GFX/ENVP",     "01.DAT",   "1 2 3 4 5 6 7", 0 },
    { 15, 0, "GFX/ENVPn11",   "GFX/ENVP",     "11.DAT",   "1 2 3 4 6", 0 },
    { 16, 0, "GFX/ENVMt1",    "GFX/ENVM",     "1.DAT",    "1 2 3 4", 0 },
    { 17, 0, "GFX/MENUt1",    "GFX/MENU",     "1.DAT",    "1 2 3 4", 0 },
    { 18, 0, "GFX/",          "GFX/",         "1.DAT",    "LOGO MAPS PLATES", 0 },
    { 19, 0, "ANIMS/SCENE0t1", "ANIMS/SCENE0", "1.DAT",   "1 2 3 4", 0 },
    { 20, 0, "ANIMS/SCCARt1", "ANIMS/SCCAR",  "1.DAT",    "1 2 3 4", 0 },
};
#define NGROUPS     (int)(sizeof groups / sizeof groups[0])
#define MAX_ITEMS   80
#define ITEM_COL    16          /* where the names start */
#define LOG_NAMES   12          /* missing files the log names (the screen shows all) */

/* what files_load_check() reads whole: small ones, then the biggest the first race needs */
static const char *const probe[] = {
    "DATA/GAME.DAT", "DATA/GEN.DAT", "DATA/LOCAL.DAT", "DATA/MAP1.DAT", "GFX/GAME1.DAT", "DATA/SOUND.DAT", "GFX/MAP1H01.DAT",
};

enum { ST_MISSING, ST_OK, ST_DEFLATED, ST_METHOD };     /* not there; stored; deflated (slow to load); a method we cannot read */

/* the screen's palette: entries 0 to 4 */
#define BLACK   0
#define WHITE   1
#define GREEN   2
#define RED     3
#define GREY    4

static struct zip zip;          /* 110 KB of .bss (zip.h) */
static int zip_err;             /* zip_open's result */
static int needed, others;      /* files a race needs / the other ones that are missing or cannot be read */
static int found, unsupported;  /* files looked for that are in the ZIP, of them with a method we cannot read */
static int req_stored, req_deflated;    /* the needed ones in the ZIP by method */
static char first[48];          /* the first needed file that is missing */
static uint8_t state[MAX_ITEMS];        /* per file, in the order of the groups */

static int load_state;          /* the read test: 0 not run, 1 done, 2 failed */
static uint32_t load_kb, load_ms;
static char load_name[48];
static int load_err;

/* The next name of a list: copied to tok, returns where the rest starts, 0 at the end */
static const char *next_item(const char *s, char tok[16])
{
    while (*s == ' ') s++;
    if (!*s) return 0;
    int i = 0;
    for (; *s && *s != ' '; s++)
        if (i < 15) tok[i++] = *s;
    tok[i] = 0;
    return s;
}

/* A file's name for the log: its path in the ZIP, or the name in the SD card's folder (music) */
static char *path_of(const struct group *g, const char *tok, char path[48])
{
    char *p = path;
    if (g->pre) p = fmt_str(p, g->pre);
    p = fmt_str(p, tok);
    if (g->post) fmt_str(p, g->post);
    return path;
}

/* What is wrong with the ZIP itself, in two lines of the screen; 0 if nothing */
static const char *const *zip_text(void)
{
    static const char *const text[][2] = {
        [ZIP_NO_FILE]  = { "SCREAMER2.zip not found",          "Make it as the README describes" },
        [ZIP_READ]     = { "SCREAMER2.zip could not be read",  "see debug.txt in the Saves folder" },
        [ZIP_NOT_ZIP]  = { "SCREAMER2.zip is not a ZIP file",  "(no end of archive found)" },
        [ZIP_ZIP64]    = { "SCREAMER2.zip uses ZIP64",         "zip only the folders DATA, GFX, ANIMS" },
        [ZIP_TOO_MANY] = { "SCREAMER2.zip has too many files", "zip only the folders DATA, GFX, ANIMS" },
        [ZIP_BAD]      = { "SCREAMER2.zip is damaged",         "Make it again, see the README" },
    };
    static const char *const method_text[2] = { "SCREAMER2.zip: unsupported compression,", "repack with Store (see README)" };
    static const char *const empty_text[2] = { "SCREAMER2.zip: no game files in it", "It needs the folders DATA, GFX, ANIMS" };
    if (zip_err) return text[zip_err < ZIP_BAD ? zip_err : ZIP_BAD];
    if (unsupported) return method_text;
    if (!found) return empty_text;
    return 0;
}

/* The notice for a ZIP with deflated files: shown before anything is loaded, and while it is */
static const char *const slow_text[3] = { "SCREAMER2.zip is compressed: loading", "is slow. Repack with Store for fast", "loading (see README)." };

int files_check(uint8_t *scratch, uint32_t scratch_size)
{
    zip_err = zip_open(&zip, ZIP_SLOT, scratch, scratch_size);
    needed = others = found = unsupported = req_stored = req_deflated = 0;
    first[0] = 0;
    int idx = 0, missing_needed = 0;
    char tok[16], path[48];
    for (int i = 0; i < NGROUPS; i++) {
        const struct group *g = &groups[i];
        const char *s = g->items;
        for (int k = 0; (s = next_item(s, tok)) && idx < MAX_ITEMS; k++, idx++) {
            int st = ST_MISSING;
            path_of(g, tok, path);
            if (g->slots) {
                if (slot_size(g->slots[k])) st = ST_OK;
            } else if (!zip_err) {
                const struct zip_entry *e = zip_find(&zip, path);
                if (e) {
                    found++;
                    st = e->method == 0 ? ST_OK : e->method == 8 ? ST_DEFLATED : ST_METHOD;
                    unsupported += st == ST_METHOD;
                }
            }
            state[idx] = (uint8_t)st;
            if (g->must) {
                req_stored += st == ST_OK;
                req_deflated += st == ST_DEFLATED;
            }
            if (st == ST_OK || st == ST_DEFLATED) continue;
            if (g->must) {
                missing_needed += st == ST_MISSING;
                if (!needed++) fmt_str(first, path);
            } else {
                others++;
            }
        }
    }

    const char *const *bad = zip_text();
    dlog_str(zip_err ? bad[0] : unsupported ? "SCREAMER2.zip: unsupported compression, repack with Store"
                   : bad ? bad[0] : "SCREAMER2.zip");
    if (!zip_err) {
        dlog_str(bad ? " (" : ": ");
        dlog_dec(zip.n);
        dlog_str(" files");
        if (bad) dlog_char(')');
    }
    dlog_char(10);
    dlog_str("zip: needed files: ");
    dlog_dec(req_stored);
    dlog_str(" stored, ");
    dlog_dec(req_deflated);
    dlog_str(" deflated, ");
    dlog_dec(needed - missing_needed);
    dlog_str(" unsupported method, ");
    dlog_dec(missing_needed);
    dlog_str(" missing\n");
    if (req_deflated) dlog_str("zip: compressed, loading is slow: repack with Store for fast loading\n");
    dlog_str("game files: ");
    dlog_dec(needed);
    dlog_str(" missing");
    if (needed + others && !zip_err) {
        dlog_char(':');
        idx = 0;
        for (int i = 0, shown = 0; i < NGROUPS; i++) {
            const char *s = groups[i].items;
            for (; (s = next_item(s, tok)) && idx < MAX_ITEMS; idx++)
                if (state[idx] != ST_OK && state[idx] != ST_DEFLATED && shown++ < LOG_NAMES) {
                    dlog_char(' ');
                    dlog_str(path_of(&groups[i], tok, path));
                }
        }
        if (needed + others > LOG_NAMES) {
            dlog_str(" and ");
            dlog_dec(needed + others - LOG_NAMES);
            dlog_str(" more");
        }
    }
    dlog_char('\n');
    return needed;
}

static const char *reason(int r)
{
    return r == ZIP_READ ? "could not be read" : r == ZIP_CRC ? "CRC-32 differs" : r == ZIP_INFLATE ? "bad compressed data"
         : r == ZIP_METHOD ? "unsupported compression" : r == ZIP_RANGE ? "does not fit" : "damaged";
}

void files_load_check(uint8_t *dest, uint32_t max, uint32_t limit)
{
    if (needed || zip_err) return;
    uint32_t t_all = rdcycle(), bytes = 0;
    int n = 0;
    for (int i = 0; i < (int)(sizeof probe / sizeof probe[0]); i++) {
        const struct zip_entry *e = zip_find(&zip, probe[i]);
        if (!e || e->size > limit) continue;
        uint32_t t0 = rdcycle();
        int r = zip_read(&zip, e, dest, max);
        uint32_t cycles = rdcycle() - t0;
        dlog_str("zip: ");
        dlog_str(probe[i]);
        dlog_str(" method ");
        dlog_dec(e->method);
        dlog_char(' ');
        dlog_dec((int32_t)e->size);
        dlog_str(" bytes in ");
        dlog_dec((int32_t)(cycles / (CPU_HZ / 1000)));
        dlog_str(" ms (");
        dlog_dec((int32_t)cycles);
        dlog_str(" cycles)");
        if (r) {
            dlog_str(": ");
            dlog_str(reason(r));
        }
        dlog_char('\n');
        if (r) {
            load_state = 2;
            load_err = r;
            fmt_str(load_name, probe[i]);
            return;
        }
        bytes += e->size;
        n++;
    }
    load_state = 1;
    load_kb = bytes / 1024;
    load_ms = (rdcycle() - t_all) / (CPU_HZ / 1000);
    dlog_str("zip: read test: ");
    dlog_dec(n);
    dlog_str(" files, ");
    dlog_dec((int32_t)bytes);
    dlog_str(" bytes in ");
    dlog_dec((int32_t)load_ms);
    dlog_str(" ms\n");
}

void files_draw(volatile uint8_t *fb, int failed)
{
    static const uint32_t pal[5] = { 0x000000, 0xFFFFFF, 0x40D070, 0xF04848, 0x909090 };
    for (int i = 0; i < 5; i++) VID_PAL[i] = pal[i];
    fb_fill(fb, 0, 0, FB_W, FB_H, BLACK);
    fb_fill(fb, 0, 0, FB_W, 8, WHITE);
    con_text(fb, 1, 0, "SCREAMER 2 - GAME FILES", BLACK, WHITE);

    const char *const *bad = zip_text();
    if (bad) {
        con_text(fb, 1, 1, bad[0], RED, BLACK);
        con_text(fb, 1, 2, bad[1], WHITE, BLACK);
    } else {
        char n[12];
        fmt_str(fmt_udec(n, (uint32_t)zip.n), " files");
        con_text(fb, 1, 1, "SCREAMER2.zip", WHITE, BLACK);
        con_text(fb, 15, 1, "[OK]", GREEN, BLACK);
        con_text(fb, 20, 1, n, GREY, BLACK);
    }

    if (!zip_err) {
        con_text(fb, 1, 3, "Needed for a race", WHITE, BLACK);
        int idx = 0, music_missing = 0;
        char tok[16];
        for (int i = 0; i < NGROUPS; i++) {
            const struct group *g = &groups[i];
            con_text(fb, 1, g->row, g->label, g->must ? WHITE : GREY, BLACK);
            int col = ITEM_COL;
            for (const char *s = g->items; (s = next_item(s, tok)) && idx < MAX_ITEMS; idx++) {
                int st = state[idx];
                if (g->slots && st != ST_OK) music_missing = 1;
                col = con_text(fb, col, g->row, tok, st == ST_OK || st == ST_DEFLATED ? GREEN : g->must || st == ST_METHOD ? RED : GREY, BLACK) + 1;
            }
        }
        /* Screamer 2 has no MIDI songs: without the CD files there is no music at all */
        con_text(fb, 1, 10, music_missing ? "Optional. Without Music (CD): no music" : "Optional (grey: not there, fine)", GREY, BLACK);
    }

    /* rows 21 to 24: the state, then what is missing or why the read test failed, then the hint or the notice */
    char line[64];
    if (needed) {
        fmt_str(fmt_udec(line, (uint32_t)needed), needed == 1 ? " needed file missing" : " needed files missing");
        con_text(fb, 1, 21, line, RED, BLACK);
        if (!bad) {                         /* the ZIP is fine: name the first of the missing */
            char *p = fmt_str(fmt_str(line, "Missing: "), first);
            if (needed > 1) fmt_udec(fmt_str(p, " +"), (uint32_t)needed - 1);
            con_text(fb, 1, 22, line, RED, BLACK);
        }
    } else if (load_state == 2) {
        con_text(fb, 1, 21, "read test failed", RED, BLACK);
        fmt_str(fmt_str(fmt_str(line, load_name), ": "), reason(load_err));
        con_text(fb, 1, 22, line, RED, BLACK);
    } else if (load_state == 1) {
        char *p = fmt_udec(fmt_str(line, "ready, read test: "), load_kb);
        fmt_str(fmt_udec(fmt_str(p, " KB in "), load_ms), " ms");
        con_text(fb, 1, 21, line, GREEN, BLACK);
    } else {
        con_text(fb, 1, 21, "ready", GREEN, BLACK);
    }
    if (failed) {
        con_text(fb, 1, 23, "A file could not be read:", WHITE, BLACK);
        con_text(fb, 1, 24, "see debug.txt in the Saves folder", WHITE, BLACK);
    } else if (needed) {
        con_text(fb, 1, 23, "Copy the game files to the SD card:", WHITE, BLACK);
        con_text(fb, 1, 24, "Assets/screamer2/common/ (see README)", WHITE, BLACK);
    } else if (req_deflated && load_state != 2) {
        for (int i = 0; i < 3; i++) con_text(fb, 1, 22 + i, slow_text[i], WHITE, BLACK);
    } else if (load_state == 1) {
        con_text(fb, 1, 23, "Starting the viewer (A or B: now)", WHITE, BLACK);
    }
}

int files_ready(void)
{
    return !needed && !zip_err && load_state == 1;
}

struct zip *files_zip(void)
{
    return &zip;
}

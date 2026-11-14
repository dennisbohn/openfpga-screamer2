/*
 * The files screen (firmware/files.c) on the PC: the same code that runs on the Pocket, with a
 * data slot read that serves a ZIP from a file, drawn into a raw RGB frame for tools/rgb2png.py.
 * The SDRAM and the registers are mapped at their addresses as in tools/s2/render_s2.c, so the
 * palette the screen writes can be read back. The debug log goes to stdout.
 *
 * Usage: files_screen <SCREAMER2.zip or -> <out.rgb> [music] [load]
 *   music: GAME.GOG and GAME.DAT are there;  load: the read test ran before the picture
 */
#define _GNU_SOURCE
#include "../files.h"
#include "../dlog.h"
#include "../hw.h"
#include "../slot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define SCRATCH     ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D60000))
#define SCRATCH_MAX (PROG_BASE - (SDRAM_BASE + 0x2D60000))

static FILE *zf;
static uint32_t zsize;
static int music, load;

uint32_t slot_size(uint32_t id)
{
    return id == 3 ? zsize : (id == 21 || id == 22) && music ? 1 : 0;
}

/* zip.c's buffers (struct zip is in the .bss here, not in the SDRAM): the address handed to slot_read
   for a pointer outside the SDRAM stands for the last such pointer (-DZIP_ADDR in tools/files_screens.sh) */
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

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <zip or -> <out.rgb> [music]\n", argv[0]); return 2; }
    int fd = memfd_create("sdram", 0);
    if (fd < 0 || ftruncate(fd, SDRAM_SIZE)
        || mmap((void *)(uintptr_t)SDRAM_BASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED
        || mmap((void *)0xF0000000u, 0x4000000u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        perror("map");
        return 1;
    }
    for (int i = 3; i < argc; i++) {
        music |= !strcmp(argv[i], "music");
        load |= !strcmp(argv[i], "load");
    }
    if (strcmp(argv[1], "-") && (zf = fopen(argv[1], "rb"))) {
        fseek(zf, 0, SEEK_END);
        zsize = (uint32_t)ftell(zf);
    }
    dlog_init();
    int missing = files_check(SCRATCH, SCRATCH_MAX);
    if (load) files_load_check((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x1000000), 0x800000, 0xFFFFFFFFu);
    files_draw(FB(0), 0);
    printf("%s", (const char *)(uintptr_t)DLOG_BASE + DLOG_HEAD);

    FILE *o = fopen(argv[2], "wb");
    const volatile uint8_t *fb = FB(0);
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint32_t c = VID_PAL[fb[i]];
        uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
    printf("needed files missing: %d\n", missing);
    return 0;
}

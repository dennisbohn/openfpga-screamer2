/*
 * Host test of the ZIP reader (firmware/zip.c), run by tools/check_zip.sh: zip.c is compiled
 * unchanged with a stand-in for the data slot read that holds to the APF's rules (a read starts
 * at a multiple of 4, has a length that is a multiple of 4 or the rest of the file, and never
 * reaches past the file) and works on the real SCREAMER2.zip and on the cases tools/check_zip.py
 * made. Each case lists what must be found and what the bytes must be (size, CRC-32); the real
 * archive is checked against what Python's zipfile says, every file whole and in pieces. At the
 * end 30000 damaged copies of a small archive go through open, find and read: no read may break
 * the rules, no byte outside the buffers may change.
 *
 * Usage: check_zip <folder of tools/check_zip.py> <the real SCREAMER2.zip>
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/crc32.h"
#include "../firmware/slot.h"

#define SLOT        3
#define ARENA_SIZE  0xA00000u       /* the "SDRAM": scratch, the index, two buffers */
#define Z_AT        0x20000u
#define DEST_AT     0x40000u        /* the buffer of a whole file, up to 8 MB */
#define DEST_MAX    0x7F0000u
#define DEST2_AT    0x850000u       /* the buffer of a piece, up to 1 MB */
#define PIECE_MAX   0x100000u
#define CANARY      64
#define JUNK        0xA5

static uint8_t *arena;
static int fails;

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL: ");
    vprintf(fmt, ap);
    printf("\n");
    exit(1);
}

static uint32_t arena_addr(const void *p)
{
    if ((const uint8_t *)p < arena || (const uint8_t *)p >= arena + ARENA_SIZE) die("a buffer outside the SDRAM");
    return (uint32_t)((const uint8_t *)p - arena);
}
#define ZIP_ADDR(p) arena_addr(p)
#include "../firmware/zip.c"

/* the data slot: a file on disk or in memory */
static FILE *src_file;
static const uint8_t *src_mem;
static uint64_t src_size;
static uint32_t n_reads;
static uint64_t n_bytes;

uint32_t slot_size(uint32_t id)
{
    return id == SLOT ? (uint32_t)src_size : 0;
}

int slot_read(uint32_t id, uint32_t off, uint32_t addr, uint32_t len)
{
    if (id != SLOT) die("read of slot %u", id);
    if (off & 3) die("read at the unaligned offset %u", off);
    uint64_t end = len == SLOT_REST ? src_size : (uint64_t)off + len;
    if (len != SLOT_REST && (len & 3 || len == 0)) die("read of %u bytes", len);
    if (off >= src_size || end > src_size) die("read of %u + %u reaches past the file's end (%llu)", off, len, (unsigned long long)src_size);
    uint64_t n = end - off, padded = (n + 3) & ~3ull;
    if (addr + padded > ARENA_SIZE) die("read to 0x%x + %llu is outside the SDRAM", addr, (unsigned long long)padded);
    if (src_mem) memcpy(arena + addr, src_mem + off, n);
    else if (fseek(src_file, (long)off, SEEK_SET) || fread(arena + addr, 1, n, src_file) != n) die("cannot read the file");
    memset(arena + addr + n, 0, padded - n);
    n_reads++;
    n_bytes += n;
    return 0;
}

static void canary(uint32_t at, uint32_t n)
{
    memset(arena + at, JUNK, n);
}

static int canary_ok(uint32_t at, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (arena[at + i] != JUNK) return 0;
    return 1;
}

#define Z ((struct zip *)(arena + Z_AT))

/* One entry through zip_read, whole and in three pieces, against size and crc */
static void check_entry(const char *name, const struct zip_entry *e, uint32_t size, uint32_t crc)
{
    if (e->size != size) { printf("FAIL: %s: size %u, expected %u\n", name, e->size, size); fails++; return; }
    if (size > DEST_MAX) { printf("FAIL: %s: too big for this test\n", name); fails++; return; }
    uint32_t max = size + ZIP_SLACK, room = (max + 3) & ~3u;
    canary(DEST_AT - CANARY, CANARY);
    canary(DEST_AT + room, CANARY);
    int r = zip_read(Z, e, arena + DEST_AT, max);
    if (r) { printf("FAIL: %s: zip_read %d\n", name, r); fails++; return; }
    if (crc32(arena + DEST_AT, arena + DEST_AT + size) != crc) { printf("FAIL: %s: CRC-32 differs\n", name); fails++; return; }
    if (!canary_ok(DEST_AT - CANARY, CANARY) || !canary_ok(DEST_AT + room, CANARY)) { printf("FAIL: %s: a byte outside the buffer changed\n", name); fails++; return; }
    if (size && zip_read(Z, e, arena + DEST_AT, max - 1) != ZIP_RANGE) { printf("FAIL: %s: a buffer one byte short was accepted\n", name); fails++; return; }

    uint32_t piece = size / 2 < PIECE_MAX ? size / 2 : PIECE_MAX, tail = size < 5 ? size : 5;
    const uint32_t from[3] = { 0, size / 3 | 1, size - tail }, len[3] = { size < 7 ? size : 7, piece, tail };
    for (int k = 0; k < 3; k++) {
        if (from[k] > size || len[k] > size - from[k]) continue;
        canary(DEST2_AT, len[k] + ZIP_SLACK + CANARY);
        r = zip_read_range(Z, e, from[k], len[k], arena + DEST2_AT, len[k] + ZIP_SLACK);
        if (r || memcmp(arena + DEST2_AT, arena + DEST_AT + from[k], len[k]) != 0 ||
            !canary_ok(DEST2_AT + ((len[k] + ZIP_SLACK + 3) & ~3u), CANARY - 4)) {
            printf("FAIL: %s: piece %u + %u (result %d)\n", name, from[k], len[k], r);
            fails++;
            return;
        }
    }
    if (zip_read_range(Z, e, size, 1, arena + DEST2_AT, 64) != ZIP_RANGE) { printf("FAIL: %s: a read past the end was accepted\n", name); fails++; }
}

static int run_case(const char *dir, const char *file, const char *manifest, int want, int want_n, const char *real)
{
    char path[1024], line[512];
    int before = fails, real_zip = strcmp(file, "REAL") == 0;
    if (real_zip) snprintf(path, sizeof path, "%s", real);
    else snprintf(path, sizeof path, "%s/%s", dir, file);
    static uint8_t *mem;
    free(mem);
    mem = 0;
    src_mem = 0;
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot read %s", path);
    fseek(f, 0, SEEK_END);
    src_size = (uint64_t)ftell(f);
    if (real_zip) {
        src_file = f;
    } else {
        mem = malloc((size_t)src_size + 1);
        fseek(f, 0, SEEK_SET);
        if (fread(mem, 1, (size_t)src_size, f) != src_size) die("cannot read %s", path);
        fclose(f);
        src_file = 0;
        src_mem = mem;
    }
    canary(ZIP_SCRATCH, CANARY);
    n_reads = 0;
    n_bytes = 0;
    int r = zip_open(Z, SLOT, arena, ZIP_SCRATCH);
    printf("%-15s %10llu bytes: zip_open %d, %d files, %u slot reads of %llu bytes\n", real_zip ? "SCREAMER2.zip" : file,
           (unsigned long long)src_size, r, Z->n, n_reads, (unsigned long long)n_bytes);
    if (r != want || (r == 0 && Z->n != want_n)) {
        printf("FAIL: %s: expected result %d with %d files\n", file, want, want_n);
        fails++;
    }
    if (!canary_ok(ZIP_SCRATCH, CANARY)) { printf("FAIL: %s: zip_open wrote behind its scratch\n", file); fails++; }

    FILE *m = fopen((snprintf(path, sizeof path, "%s/%s", dir, manifest), path), "r");
    if (!m) die("cannot read %s", path);
    int checked = 0;
    while (r == 0 && fgets(line, sizeof line, m)) {
        char *verb = strtok(line, "\t\n"), *name = strtok(0, "\t\n"), *size = strtok(0, "\t\n"), *crc = strtok(0, "\t\n");
        const struct zip_entry *e = zip_find(Z, name);
        if (!strcmp(verb, "none")) {
            if (e) { printf("FAIL: %s: found, but must not be\n", name); fails++; }
        } else if (!e) {
            printf("FAIL: %s: not found\n", name);
            fails++;
        } else if (!strcmp(verb, "packed")) {
            if (e->method == 0 || zip_read(Z, e, arena + DEST_AT, 1 << 20) != ZIP_COMPRESSED) { printf("FAIL: %s: not reported as compressed\n", name); fails++; }
        } else {
            check_entry(name, e, (uint32_t)strtoul(size, 0, 10), (uint32_t)strtoul(crc, 0, 16));
        }
        checked++;
    }
    fclose(m);
    if (src_file) fclose(src_file);
    src_file = 0;
    if (r == 0) printf("  %d names checked%s\n", checked, fails == before ? ", all match" : "");
    return fails != before;
}

/* 30000 damaged copies of the small archive */
static uint32_t rng = 12345;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void fuzz(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/fuzz.zip", dir);
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot read %s", path);
    static uint8_t orig[1 << 17], mem[1 << 17];
    size_t size = fread(orig, 1, sizeof orig, f);
    fclose(f);
    int opened = 0, bad = 0, reads = 0;
    for (int it = 0; it < 30000; it++) {
        memcpy(mem, orig, size);
        uint32_t len = (uint32_t)size;
        for (int k = 1 + (int)(rnd() % 4); k > 0; k--) {
            uint32_t at = rnd() % 4 < 3 ? len - 1 - rnd() % (len < 1500 ? len : 1500) : rnd() % len;
            mem[at] = (uint8_t)rnd();
        }
        if (rnd() % 10 == 0) len = rnd() % len;
        src_mem = mem;
        src_file = 0;
        src_size = len;
        canary(ZIP_SCRATCH, CANARY);
        int r = zip_open(Z, SLOT, arena, ZIP_SCRATCH);
        if (!canary_ok(ZIP_SCRATCH, CANARY)) die("fuzz %d: zip_open wrote behind its scratch", it);
        if (r) { bad++; continue; }
        opened++;
        for (int k = 0; k < Z->n; k++) {
            const struct zip_entry *e = &Z->e[k];
            const struct zip_entry *g = zip_find(Z, e->name);
            if (!g) die("fuzz %d: %s not found", it, e->name);
            if (e->method) {                    /* the size of a compressed file is not checked: not readable */
                if (zip_read(Z, e, arena + DEST_AT, 64) != ZIP_COMPRESSED) die("fuzz %d: %s: a compressed file was read", it, e->name);
                continue;
            }
            if (e->size > 0x40000) die("fuzz %d: size %u passed the checks", it, e->size);
            uint32_t from = e->size ? rnd() % e->size : 0, n = e->size - from;
            canary(DEST_AT + ((e->size + ZIP_SLACK + 3) & ~3u), CANARY);
            if (zip_read(Z, e, arena + DEST_AT, e->size + ZIP_SLACK) == 0) reads++;
            if (zip_read_range(Z, e, from, n, arena + DEST_AT, n + ZIP_SLACK) == 0) reads++;
            if (!canary_ok(DEST_AT + ((e->size + ZIP_SLACK + 3) & ~3u), CANARY)) die("fuzz %d: %s: a byte outside the buffer changed", it, e->name);
        }
    }
    printf("fuzz: 30000 damaged archives, %d opened, %d refused, %d entries read; no read broke the rules\n", opened, bad, reads);
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s <folder of check_zip.py> <SCREAMER2.zip>\n", argv[0]); return 2; }
    arena = calloc(ARENA_SIZE, 1);
    if (!arena) return 2;
    char path[1024], line[1024];
    snprintf(path, sizeof path, "%s/cases.txt", argv[1]);
    FILE *c = fopen(path, "r");
    if (!c) die("cannot read %s", path);
    while (fgets(line, sizeof line, c)) {
        char *file = strtok(line, "\t\n"), *man = strtok(0, "\t\n"), *want = strtok(0, "\t\n"), *n = strtok(0, "\t\n");
        run_case(argv[1], file, man, atoi(want), atoi(n), argv[2]);
    }
    fclose(c);

    /* a scratch that is too small is refused, not overrun */
    src_mem = (const uint8_t *)line;
    src_size = 1000;
    if (zip_open(Z, SLOT, arena, 100) != ZIP_BAD) { printf("FAIL: a scratch of 100 bytes was accepted\n"); fails++; }
    if (zip_open(Z, SLOT + 1, arena, ZIP_SCRATCH) != ZIP_NO_FILE) { printf("FAIL: an empty slot was not reported\n"); fails++; }

    fuzz(argv[1]);
    printf(fails ? "FAIL: %d problems\n" : "PASS: zip.c reads the real archive and refuses the broken ones\n", fails);
    return fails != 0;
}

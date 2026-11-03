/*
 * A reader for ONE ZIP file in a data slot: SCREAMER2.zip, the original DATA, GFX and ANIMS
 * folders of the game (core/data.json, slot 3). Entries may be stored (method 0, fast) or
 * deflated (method 8, slow); every other method is listed but refused.
 *
 * zip_open() reads the end of the archive and its central directory once and keeps a compact
 * index (name, local header offset, sizes, method, CRC-32) of up to ZIP_MAX_ENTRIES files; the
 * sizes and offsets come from the central directory only, because ZIPs made by Windows have
 * sizes 0 in the local headers (data descriptor). zip_find() matches a name on the END of the
 * stored path, ignoring case, so "DATA/GAME.DAT" finds "DATA/GAME.DAT", "./DATA/GAME.DAT" and
 * "Screamer 2/DATA/game.dat".
 *
 * Offsets in the file are bytes. A data slot read wants a multiple of 4 for the offset and the
 * length (slot.h), so a stored entry is read from the word before its first byte and the bytes
 * are moved down; that overwrites up to ZIP_SLACK bytes behind the data. A deflated entry is
 * inflated straight into the destination buffer (which also serves as the 32 KB window), its
 * compressed bytes streamed through a buffer of ZIP_CHUNK bytes read from the slot one chunk at
 * a time; the CRC-32 of the result is checked against the directory's.
 *
 * RAM: struct zip is about 110 KB (static, in the SDRAM with the program's .bss, never on the
 * stack: a data slot read goes to the SDRAM): the index 43 KB, the input chunk 64 KB, the
 * inflate's tables 5 KB. Reading the directory in zip_open() borrows a scratch area of
 * ZIP_SCRATCH bytes that the caller lends for that call. Not reentrant.
 */
#pragma once
#include <stdint.h>

#define ZIP_MAX_ENTRIES 512
#define ZIP_NAME        64          /* characters of a path that are kept: its end */
#define ZIP_SCRATCH     0x12000     /* the scratch zip_open() needs: the last 64 KB + 22 of the file */
#define ZIP_SLACK       8           /* bytes a read needs behind the data it asks for */
#define ZIP_CHUNK       0x10000     /* compressed bytes per slot read while inflating */

enum {
    ZIP_OK = 0,
    ZIP_NO_FILE,        /* the slot has no file */
    ZIP_READ,           /* the data slot read failed */
    ZIP_NOT_ZIP,        /* no end-of-archive record in the last 64 KB */
    ZIP_ZIP64,          /* ZIP64 (more than 65534 files or 4 GB): not supported */
    ZIP_TOO_MANY,       /* more than ZIP_MAX_ENTRIES entries, or a directory above ZIP_SCRATCH */
    ZIP_BAD,            /* the directory or a local header is damaged or inconsistent */
    ZIP_METHOD,         /* zip_read: not stored or deflated (BZip2, LZMA, Deflate64, ...) */
    ZIP_RANGE,          /* zip_read: past the entry's end, dest too small, or a piece of a deflated entry */
    ZIP_CRC,            /* zip_read: the CRC-32 of the inflated bytes is not the directory's */
    ZIP_INFLATE         /* zip_read: the deflate stream is damaged, cut off, or not the entry's size */
};

struct zip_entry {
    uint32_t offset;                /* of the local header */
    uint32_t size, csize;           /* bytes uncompressed, bytes in the ZIP */
    uint32_t crc;                   /* CRC-32 of the uncompressed bytes */
    uint16_t method;                /* 0 stored, 8 deflated; anything else cannot be read */
    uint8_t len;                    /* characters in name */
    char name[ZIP_NAME + 1];        /* the end of the path, '/' for separators, NUL at the end */
};

/* The inflate's state and tables (zip.c) */
struct zip_inf {
    const uint8_t *in, *in_end;     /* the compressed bytes in the chunk not yet taken */
    uint32_t pos, start, end;       /* file offsets: next chunk (a multiple of 4), the stream's first byte and end */
    uint32_t bitbuf;
    int bitcnt, pad, err, fixed;    /* bits in bitbuf, of them zeros past the end, a read error, fixed tables built */
    uint16_t lcount[16], lsym[288], dcount[16], dsym[32], ccount[16], csym[19];
    uint16_t lfast[1 << 10], dfast[1 << 8];
    uint8_t lens[320];
};

struct zip {
    uint32_t slot, size;            /* the data slot and its file's size */
    uint32_t cd_off;                /* where the central directory starts: the entries' data ends before it */
    int n;                          /* files in the index (folders are not indexed) */
    struct zip_entry e[ZIP_MAX_ENTRIES];
    uint32_t in[ZIP_CHUNK / 4];     /* the input chunk; also a local header, read here */
    struct zip_inf inf;
};

/* Index the ZIP in data slot slot. scratch: ZIP_SCRATCH bytes of the SDRAM, free afterwards.
   Returns ZIP_OK or the reason; z->n counts what was indexed. */
int zip_open(struct zip *z, uint32_t slot, uint8_t *scratch, uint32_t scratch_size);

/* The entry whose path ends with name (compared without case, on a '/' border), or 0 */
const struct zip_entry *zip_find(const struct zip *z, const char *name);

/* The whole entry to dest (4-byte aligned, max bytes of room, at least e->size + ZIP_SLACK).
   ZIP_OK, or ZIP_METHOD, ZIP_RANGE, ZIP_BAD, ZIP_READ, ZIP_CRC, ZIP_INFLATE. dest is only
   written up to e->size bytes (and ZIP_SLACK more for a stored entry). */
int zip_read(struct zip *z, const struct zip_entry *e, uint8_t *dest, uint32_t max);

/* Bytes from .. from + len of a stored entry to dest (same room). A deflated entry can only be
   read whole (from 0, len e->size): anything else is ZIP_RANGE. */
int zip_read_range(struct zip *z, const struct zip_entry *e, uint32_t from, uint32_t len,
                   uint8_t *dest, uint32_t max);

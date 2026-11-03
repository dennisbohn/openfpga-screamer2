/*
 * A reader for ONE uncompressed ("Store") ZIP file in a data slot: SCREAMER2.zip, the original
 * DATA, GFX and ANIMS folders of the game (core/data.json, slot 3).
 *
 * zip_open() reads the end of the archive and its central directory once and keeps a compact
 * index (name, local header offset, size, method) of up to ZIP_MAX_ENTRIES files; the sizes and
 * offsets come from the central directory only, because ZIPs made by Windows have sizes 0 in the
 * local headers (data descriptor). zip_find() matches a name on the END of the stored path,
 * ignoring case, so "DATA/GAME.DAT" finds "DATA/GAME.DAT", "./DATA/GAME.DAT" and
 * "Screamer 2/DATA/game.dat". Compressed entries are listed (method != 0) but never unpacked.
 *
 * Offsets in the file are bytes. A data slot read wants a multiple of 4 for the offset and the
 * length (slot.h), so a read starts at the word before an entry's first byte and moves the bytes
 * down; it overwrites up to ZIP_SLACK bytes behind the data it returns.
 *
 * RAM: struct zip is about 39 KB (static, in the SDRAM with the program's .bss, never on the
 * stack: the APF writes into it); the directory and the search of the end are done in a scratch
 * area of ZIP_SCRATCH bytes that the caller lends for the call of zip_open().
 */
#pragma once
#include <stdint.h>

#define ZIP_MAX_ENTRIES 512
#define ZIP_NAME        64          /* characters of a path that are kept: its end */
#define ZIP_SCRATCH     0x12000     /* the scratch zip_open() needs: the last 64 KB + 22 of the file */
#define ZIP_SLACK       8           /* bytes a read needs behind the data it asks for */

enum {
    ZIP_OK = 0,
    ZIP_NO_FILE,        /* the slot has no file */
    ZIP_READ,           /* the data slot read failed */
    ZIP_NOT_ZIP,        /* no end-of-archive record in the last 64 KB */
    ZIP_ZIP64,          /* ZIP64 (more than 65534 files or 4 GB): not supported */
    ZIP_TOO_MANY,       /* more than ZIP_MAX_ENTRIES entries, or a directory above ZIP_SCRATCH */
    ZIP_BAD,            /* the directory or a local header is damaged or inconsistent */
    ZIP_COMPRESSED,     /* zip_read: the entry is compressed (method != 0) */
    ZIP_RANGE           /* zip_read: past the entry's end, or dest too small */
};

struct zip_entry {
    uint32_t offset;                /* of the local header */
    uint32_t size;                  /* bytes (uncompressed) */
    uint16_t method;                /* 0 = stored; anything else cannot be read */
    uint8_t len;                    /* characters in name */
    char name[ZIP_NAME + 1];        /* the end of the path, '/' for separators, NUL at the end */
};

struct zip {
    uint32_t slot, size;            /* the data slot and its file's size */
    int n;                          /* files in the index (folders are not indexed) */
    uint32_t hdr[10];               /* a local header, read here (a slot read goes to the SDRAM, not the stack) */
    struct zip_entry e[ZIP_MAX_ENTRIES];
};

/* Index the ZIP in data slot slot. scratch: ZIP_SCRATCH bytes of the SDRAM, free afterwards.
   Returns ZIP_OK or the reason; z->n counts what was indexed. */
int zip_open(struct zip *z, uint32_t slot, uint8_t *scratch, uint32_t scratch_size);

/* The entry whose path ends with name (compared without case, on a '/' border), or 0 */
const struct zip_entry *zip_find(const struct zip *z, const char *name);

/* Bytes from .. from + len of entry e to dest (4-byte aligned, max bytes of room, at least
   len + ZIP_SLACK). ZIP_OK, ZIP_COMPRESSED, ZIP_RANGE, ZIP_BAD or ZIP_READ. */
int zip_read_range(struct zip *z, const struct zip_entry *e, uint32_t from, uint32_t len,
                   uint8_t *dest, uint32_t max);

/* The whole entry to dest (4-byte aligned, max at least e->size + ZIP_SLACK) */
int zip_read(struct zip *z, const struct zip_entry *e, uint8_t *dest, uint32_t max);

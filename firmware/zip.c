/*
 * The reader for the game's ZIP in a data slot (zip.h)
 */
#include "zip.h"
#include "slot.h"

#ifndef ZIP_ADDR                        /* the bridge address of a buffer: the CPU's own for the SDRAM */
#define ZIP_ADDR(p) ((uint32_t)(uintptr_t)(p))
#endif

typedef uint32_t __attribute__((may_alias)) word_t;

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

/* Bytes off .. off + len of the slot's file to dst[0 ..], dst 4-byte aligned with len +
   ZIP_SLACK bytes of room. The slot read wants whole words at word offsets, so the words around
   the bytes are read (the file's last 1-3 bytes as the rest of the file) and the bytes moved
   down; up to 3 bytes after them are junk. */
static int pull(const struct zip *z, uint32_t off, uint32_t len, uint8_t *dst)
{
    if (off > z->size || len > z->size - off) return ZIP_RANGE;
    if (!len) return ZIP_OK;
    uint32_t a = off & ~3u, end = off + len, body = z->size & ~3u;
    uint32_t b = end > body ? body : (end + 3) & ~3u;
    if (a < b && slot_read(z->slot, a, ZIP_ADDR(dst), b - a)) return ZIP_READ;
    if (end > body && slot_read(z->slot, body, ZIP_ADDR(dst + (body - a)), SLOT_REST)) return ZIP_READ;
    uint32_t sh = 8 * (off - a);
    if (sh) {
        word_t *w = (word_t *)dst;
        for (uint32_t i = 0, n = (len + 3) / 4; i < n; i++)
            w[i] = w[i] >> sh | w[i + 1] << (32 - sh);          /* little endian, as the CPU reads the file */
    }
    return ZIP_OK;
}

int zip_open(struct zip *z, uint32_t slot, uint8_t *scratch, uint32_t scratch_size)
{
    z->slot = slot;
    z->n = 0;
    z->size = slot_size(slot);
    if (!z->size) return ZIP_NO_FILE;
    if (z->size < 22) return ZIP_NOT_ZIP;

    /* the end-of-archive record: 22 bytes plus a comment of up to 64 KB, the last thing in the file */
    uint32_t tail = z->size < 22 + 65535 ? z->size : 22 + 65535;
    if (scratch_size < tail + ZIP_SLACK) return ZIP_BAD;        /* the caller lent too little */
    int r = pull(z, z->size - tail, tail, scratch);
    if (r) return r;
    int at = -1;
    for (int i = (int)tail - 22; i >= 0; i--)
        if (le32(scratch + i) == 0x06054b50u && (uint32_t)i + 22 + le16(scratch + i + 20) <= tail) {
            at = i;
            break;
        }
    if (at < 0) return ZIP_NOT_ZIP;
    const uint8_t *q = scratch + at;
    uint32_t entries = le16(q + 10), cd_size = le32(q + 12), cd_off = le32(q + 16);
    if (entries == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu) return ZIP_ZIP64;
    uint32_t eocd = z->size - tail + (uint32_t)at;
    if (le16(q + 4) || le16(q + 6) || cd_off > eocd || cd_size > eocd - cd_off) return ZIP_BAD;
    if (entries > ZIP_MAX_ENTRIES || cd_size > scratch_size - ZIP_SLACK) return ZIP_TOO_MANY;
    r = pull(z, cd_off, cd_size, scratch);          /* the end record is not needed any more */
    if (r) return r;

    uint32_t p = 0;
    for (uint32_t k = 0; k < entries; k++) {
        const uint8_t *c = scratch + p;
        if (cd_size - p < 46 || le32(c) != 0x02014b50u) return ZIP_BAD;
        uint32_t method = le16(c + 10), csize = le32(c + 20), usize = le32(c + 24);
        uint32_t nlen = le16(c + 28), skip = 46 + nlen + le16(c + 30) + le16(c + 32), lho = le32(c + 42);
        if (skip > cd_size - p) return ZIP_BAD;
        p += skip;
        const char *name = (const char *)c + 46;
        if (!nlen || name[nlen - 1] == '/') continue;           /* a folder */
        if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu || lho == 0xFFFFFFFFu) return ZIP_ZIP64;
        /* the local header and the data of a stored file lie before the directory */
        if (lho > cd_off || cd_off - lho < 30 || (method == 0 && (csize != usize || usize > cd_off - lho - 30)))
            return ZIP_BAD;
        struct zip_entry *e = &z->e[z->n++];
        e->offset = lho;
        e->size = usize;
        e->method = (uint16_t)method;
        e->len = (uint8_t)(nlen < ZIP_NAME ? nlen : ZIP_NAME);
        name += nlen - e->len;                                  /* the end of the path */
        for (int i = 0; i < e->len; i++)                        /* a backslash is what PowerShell's Compress-Archive writes */
            e->name[i] = name[i] == '\\' ? '/' : name[i] ? name[i] : '?';
        e->name[e->len] = 0;
    }
    return ZIP_OK;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

const struct zip_entry *zip_find(const struct zip *z, const char *name)
{
    int n = 0;
    while (name[n]) n++;
    for (int k = 0; k < z->n; k++) {
        const struct zip_entry *e = &z->e[k];
        if (e->len < n) continue;
        const char *t = e->name + e->len - n;
        int i = 0;
        while (i < n && lower(t[i]) == lower(name[i])) i++;
        if (i == n && (e->len == n || t[-1] == '/')) return e;
    }
    return 0;
}

int zip_read_range(struct zip *z, const struct zip_entry *e, uint32_t from, uint32_t len,
                   uint8_t *dest, uint32_t max)
{
    if (e->method) return ZIP_COMPRESSED;
    if (from > e->size || len > e->size - from || max < ZIP_SLACK || len > max - ZIP_SLACK) return ZIP_RANGE;
    if (!len) return ZIP_OK;
    int r = pull(z, e->offset, 30, (uint8_t *)z->hdr);          /* the local header, to find the data */
    if (r) return r;
    const uint8_t *h = (const uint8_t *)z->hdr;
    uint32_t start = e->offset + 30 + le16(h + 26) + le16(h + 28);
    if (le32(h) != 0x04034b50u || start < e->offset || start > z->size || e->size > z->size - start)
        return ZIP_BAD;
    return pull(z, start + from, len, dest);
}

int zip_read(struct zip *z, const struct zip_entry *e, uint8_t *dest, uint32_t max)
{
    return zip_read_range(z, e, 0, e->size, dest, max);
}

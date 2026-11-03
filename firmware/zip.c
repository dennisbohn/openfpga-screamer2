/*
 * The reader for the game's ZIP in a data slot (zip.h), with its inflate (RFC 1951)
 */
#include "zip.h"
#include "slot.h"
#include "crc32.h"
#include <string.h>

#ifndef ZIP_ADDR                        /* the bridge address of a buffer: the CPU's own for the SDRAM */
#define ZIP_ADDR(p) ((uint32_t)(uintptr_t)(p))
#endif

typedef uint32_t __attribute__((may_alias)) word_t;

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

/* ---- reading bytes of the file ---- */

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

/* ---- the directory ---- */

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
    z->cd_off = cd_off;

    uint32_t p = 0;
    for (uint32_t k = 0; k < entries; k++) {
        const uint8_t *c = scratch + p;
        if (cd_size - p < 46 || le32(c) != 0x02014b50u) return ZIP_BAD;
        uint32_t method = le16(c + 10), crc = le32(c + 16), csize = le32(c + 20), usize = le32(c + 24);
        uint32_t nlen = le16(c + 28), skip = 46 + nlen + le16(c + 30) + le16(c + 32), lho = le32(c + 42);
        if (skip > cd_size - p) return ZIP_BAD;
        p += skip;
        const char *name = (const char *)c + 46;
        if (!nlen || name[nlen - 1] == '/') continue;           /* a folder */
        if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu || lho == 0xFFFFFFFFu) return ZIP_ZIP64;
        /* the local header and the bytes in the ZIP lie before the directory */
        if (lho > cd_off || cd_off - lho < 30 || csize > cd_off - lho - 30 || (method == 0 && csize != usize))
            return ZIP_BAD;
        struct zip_entry *e = &z->e[z->n++];
        e->offset = lho;
        e->size = usize;
        e->csize = csize;
        e->crc = crc;
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

/* ---- inflate ---- */

#define LBITS   10                      /* bits the lookup table of the literal/length codes takes at once */
#define DBITS   8                       /* and of the distance codes; longer codes take the slow way */

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_ext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dist_ext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static const uint8_t clen_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* The next chunk of the compressed bytes, a multiple of 4 from the slot (the first one starts
   at the word before the stream); 0 at the end or after a failed read (inf.err) */
static int next_chunk(struct zip *z)
{
    struct zip_inf *s = &z->inf;
    while (s->pos < s->end) {
        uint32_t n = ((s->end + 3) & ~3u) - s->pos, at = s->pos;
        if (n > ZIP_CHUNK) n = ZIP_CHUNK;
        if (slot_read(z->slot, at, ZIP_ADDR(z->in), n)) {
            s->err = ZIP_READ;
            return 0;
        }
        const uint8_t *buf = (const uint8_t *)z->in;
        s->pos = at + n;
        s->in = buf + (at < s->start ? s->start - at : 0);
        s->in_end = buf + (s->end - at < n ? s->end - at : n);
        if (s->in < s->in_end) return 1;
    }
    return 0;
}

/* Make at least 25 bits available; past the end of the stream zeros, counted in pad */
static void refill(struct zip *z)
{
    struct zip_inf *s = &z->inf;
    while (s->bitcnt <= 24) {
        uint32_t b = 0;
        if (s->in < s->in_end || next_chunk(z)) b = *s->in++;
        else s->pad += 8;
        s->bitbuf |= b << s->bitcnt;
        s->bitcnt += 8;
    }
}

static uint32_t getbits(struct zip *z, int n)           /* n <= 16 and refill() done */
{
    struct zip_inf *s = &z->inf;
    uint32_t v = s->bitbuf & ((1u << n) - 1);
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return v;
}

/* The canonical code of len[0 .. n): count of codes per length, symbols in code order and, with
   fbits, a table of 1 << fbits entries (symbol << 4 | length) for the codes up to fbits long, indexed
   by the next bits of the stream. Returns the code space left over: 0 complete, < 0 oversubscribed. */
static int build(uint16_t *count, uint16_t *sym, uint16_t *fast, int fbits, const uint8_t *len, int n)
{
    uint16_t offs[16], next[16];
    for (int l = 0; l < 16; l++) count[l] = 0;
    for (int i = 0; i < n; i++) count[len[i]]++;
    int left = 1;
    for (int l = 1; l < 16; l++) {
        left = 2 * left - count[l];
        if (left < 0) return left;
    }
    uint32_t code = 0;
    offs[1] = 0;
    for (int l = 1; l < 16; l++) {
        code = (code + (l > 1 ? count[l - 1] : 0)) << 1;
        next[l] = (uint16_t)code;
        if (l < 15) offs[l + 1] = (uint16_t)(offs[l] + count[l]);
    }
    if (fast) memset(fast, 0, sizeof(uint16_t) << fbits);
    for (int i = 0; i < n; i++) {
        int l = len[i];
        if (!l) continue;
        sym[offs[l]++] = (uint16_t)i;
        uint32_t c = next[l]++;
        if (l > fbits || !fast) continue;
        uint32_t r = 0;                                         /* the code's bits in stream order: reversed */
        for (int k = 0; k < l; k++) r |= (c >> k & 1) << (l - 1 - k);
        for (uint32_t j = r; j < 1u << fbits; j += 1u << l) fast[j] = (uint16_t)(i << 4 | l);
    }
    return left;
}

/* A code that may be incomplete only if it is one code of one bit (a single distance, or none) */
static int usable(int left, const uint16_t *count, int n)
{
    return left == 0 || (left > 0 && n - count[0] == count[1]);
}

/* The symbol of the code at the low end of the bit buffer the slow way, bit by bit; -1 if none */
static int slow(uint32_t *bb, int *bc, const uint16_t *count, const uint16_t *sym)
{
    int code = 0, first = 0, index = 0;
    uint32_t b = *bb;
    for (int l = 1; l < 16; l++) {
        code |= (int)(b & 1);
        b >>= 1;
        int c = count[l];
        if (code - c < first) {
            *bb >>= l;
            *bc -= l;
            return sym[index + (code - first)];
        }
        index += c;
        first += c;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int fixed_tables(struct zip_inf *s)
{
    if (s->fixed) return 0;
    uint8_t *l = s->lens;
    int i = 0;
    for (; i < 144; i++) l[i] = 8;
    for (; i < 256; i++) l[i] = 9;
    for (; i < 280; i++) l[i] = 7;
    for (; i < 288; i++) l[i] = 8;
    build(s->lcount, s->lsym, s->lfast, LBITS, l, 288);
    for (i = 0; i < 32; i++) l[i] = 5;                          /* 30 and 31 are refused when they come */
    build(s->dcount, s->dsym, s->dfast, DBITS, l, 32);
    s->fixed = 1;
    return 0;
}

static int dynamic_tables(struct zip *z)
{
    struct zip_inf *s = &z->inf;
    uint8_t cl[19];
    refill(z);
    int nlen = (int)getbits(z, 5) + 257, ndist = (int)getbits(z, 5) + 1, ncode = (int)getbits(z, 4) + 4;
    if (nlen > 286 || ndist > 30) return ZIP_INFLATE;
    for (int i = 0; i < 19; i++) cl[i] = 0;
    for (int i = 0; i < ncode; i++) {
        refill(z);
        cl[clen_order[i]] = (uint8_t)getbits(z, 3);
    }
    if (build(s->ccount, s->csym, 0, 0, cl, 19) != 0) return ZIP_INFLATE;   /* must be complete */
    int index = 0;
    while (index < nlen + ndist) {
        refill(z);
        int sy = slow(&s->bitbuf, &s->bitcnt, s->ccount, s->csym);
        if (sy < 0) return ZIP_INFLATE;
        if (sy < 16) {
            s->lens[index++] = (uint8_t)sy;
            continue;
        }
        int rep, v = 0;
        if (sy == 16) {
            if (!index) return ZIP_INFLATE;
            v = s->lens[index - 1];
            rep = 3 + (int)getbits(z, 2);
        } else if (sy == 17) {
            rep = 3 + (int)getbits(z, 3);
        } else {
            rep = 11 + (int)getbits(z, 7);
        }
        if (index + rep > nlen + ndist) return ZIP_INFLATE;
        while (rep--) s->lens[index++] = (uint8_t)v;
    }
    if (!s->lens[256]) return ZIP_INFLATE;                      /* no end-of-block code */
    s->fixed = 0;
    int e = build(s->lcount, s->lsym, s->lfast, LBITS, s->lens, nlen);
    if (e < 0 || !usable(e, s->lcount, nlen)) return ZIP_INFLATE;
    e = build(s->dcount, s->dsym, s->dfast, DBITS, s->lens + nlen, ndist);
    if (e < 0 || !usable(e, s->dcount, ndist)) return ZIP_INFLATE;
    return 0;
}

/* A stored block: to a byte border, LEN and NLEN, then LEN bytes, first those in the bit buffer */
static int stored_block(struct zip *z, uint8_t *out, uint32_t *outpos, uint32_t outmax)
{
    struct zip_inf *s = &z->inf;
    s->bitbuf >>= s->bitcnt & 7;
    s->bitcnt -= s->bitcnt & 7;
    refill(z);
    uint32_t len = getbits(z, 16), nlen = getbits(z, 16);
    if (len != (~nlen & 0xFFFFu) || len > outmax - *outpos) return ZIP_INFLATE;
    uint8_t *o = out + *outpos;
    *outpos += len;
    while (len && s->bitcnt >= 8) {
        *o++ = (uint8_t)s->bitbuf;
        s->bitbuf >>= 8;
        s->bitcnt -= 8;
        len--;
    }
    if (s->pad > s->bitcnt) return ZIP_INFLATE;                 /* the bytes ran into the zeros past the end */
    while (len) {
        if (s->in >= s->in_end && !next_chunk(z)) return ZIP_INFLATE;
        uint32_t n = (uint32_t)(s->in_end - s->in);
        if (n > len) n = len;
        memcpy(o, s->in, n);
        o += n;
        s->in += n;
        len -= n;
    }
    return 0;
}

/* The symbols of a block with the tables built; bit buffer and input pointers in locals */
static int codes(struct zip *z, uint8_t *out, uint32_t *outpos, uint32_t outmax)
{
    struct zip_inf *s = &z->inf;
    const uint8_t *in = s->in, *in_end = s->in_end;
    uint32_t bb = s->bitbuf, o = *outpos;
    int bc = s->bitcnt, pad = s->pad, r = 0;
#define FILL() while (bc <= 24) { \
        uint32_t b_ = 0; \
        if (in < in_end) b_ = *in++; \
        else if (next_chunk(z)) { in = s->in; in_end = s->in_end; b_ = *in++; } \
        else pad += 8; \
        bb |= b_ << bc; \
        bc += 8; }
#define DECODE(count, sym, fast, fbits, v) do { \
        unsigned e_ = fast[bb & ((1u << fbits) - 1)]; \
        if (e_) { bb >>= e_ & 15; bc -= (int)(e_ & 15); v = (int)(e_ >> 4); } \
        else v = slow(&bb, &bc, count, sym); } while (0)
    for (;;) {
        FILL();
        if (bc < pad) { r = ZIP_INFLATE; break; }               /* the last bits were not in the stream */
        int sy;
        DECODE(s->lcount, s->lsym, s->lfast, LBITS, sy);
        if (sy < 256) {
            if (sy < 0 || o >= outmax) { r = ZIP_INFLATE; break; }
            out[o++] = (uint8_t)sy;
            continue;
        }
        if (sy == 256) break;
        sy -= 257;
        if (sy >= 29) { r = ZIP_INFLATE; break; }
        uint32_t len = len_base[sy] + (bb & ((1u << len_ext[sy]) - 1));
        bb >>= len_ext[sy];
        bc -= len_ext[sy];
        FILL();
        int ds;
        DECODE(s->dcount, s->dsym, s->dfast, DBITS, ds);
        if (ds < 0 || ds >= 30) { r = ZIP_INFLATE; break; }
        FILL();
        uint32_t dist = dist_base[ds] + (bb & ((1u << dist_ext[ds]) - 1));
        bb >>= dist_ext[ds];
        bc -= dist_ext[ds];
        if (dist > o || len > outmax - o) { r = ZIP_INFLATE; break; }
        const uint8_t *src = out + o - dist;
        uint8_t *dst = out + o;
        o += len;
        while (len--) *dst++ = *src++;                          /* forward: a match may overlap itself */
    }
    if (!r && bc < pad) r = ZIP_INFLATE;
    s->in = in;
    s->in_end = in_end;
    s->bitbuf = bb;
    s->bitcnt = bc;
    s->pad = pad;
    *outpos = o;
    return r;
#undef FILL
#undef DECODE
}

/* The deflated entry e, whose compressed bytes start at file offset start, into dest */
static int inflate_entry(struct zip *z, const struct zip_entry *e, uint32_t start, uint8_t *dest)
{
    struct zip_inf *s = &z->inf;
    s->pos = start & ~3u;
    s->start = start;
    s->end = start + e->csize;
    s->in = s->in_end = (const uint8_t *)z->in;
    s->bitbuf = 0;
    s->bitcnt = s->pad = s->err = s->fixed = 0;
    uint32_t outpos = 0;
    int last, r = 0;
    do {
        refill(z);
        last = (int)getbits(z, 1);
        int type = (int)getbits(z, 2);
        if (type == 0) r = stored_block(z, dest, &outpos, e->size);
        else if (type == 3) r = ZIP_INFLATE;
        else if ((r = type == 1 ? fixed_tables(s) : dynamic_tables(z)) == 0) r = codes(z, dest, &outpos, e->size);
        if (s->err) return s->err;
        if (r) return r;
        if (s->bitcnt < s->pad) return ZIP_INFLATE;
    } while (!last);
    if (outpos != e->size) return ZIP_INFLATE;
    return crc32(dest, dest + e->size) == e->crc ? ZIP_OK : ZIP_CRC;
}

/* ---- reading an entry ---- */

int zip_read_range(struct zip *z, const struct zip_entry *e, uint32_t from, uint32_t len,
                   uint8_t *dest, uint32_t max)
{
    if (e->method != 0 && e->method != 8) return ZIP_METHOD;
    if (from > e->size || len > e->size - from || max < ZIP_SLACK || len > max - ZIP_SLACK) return ZIP_RANGE;
    if (e->method == 8 && (from || len != e->size)) return ZIP_RANGE;
    if (!len) return ZIP_OK;
    int r = pull(z, e->offset, 30, (uint8_t *)z->in);           /* the local header, to find the data */
    if (r) return r;
    const uint8_t *h = (const uint8_t *)z->in;
    uint32_t start = e->offset + 30 + le16(h + 26) + le16(h + 28);
    if (le32(h) != 0x04034b50u || start < e->offset || start > z->cd_off || e->csize > z->cd_off - start)
        return ZIP_BAD;
    return e->method ? inflate_entry(z, e, start, dest) : pull(z, start + from, len, dest);
}

int zip_read(struct zip *z, const struct zip_entry *e, uint8_t *dest, uint32_t max)
{
    return zip_read_range(z, e, 0, e->size, dest, max);
}

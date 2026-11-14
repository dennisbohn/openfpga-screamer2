/*
 * A .PKC picture as boxes (tb_hud.h). The file:
 *   .PKC       320 x 200: int32 n < 0 skips -n pixels (see-through), n >= 0 is followed by n
 *              pixels; the picture ends at its last pixel or the file's last whole int32
 * A picture becomes boxes as make_track.py's pkc() finds them: the rows with pixels (a byte
 * not 0) make bands of adjacent rows, top to bottom; a band's columns with pixels make boxes,
 * left to right, split where more than 8 empty columns lie between.
 */
#include "tb_hud.h"
#include <string.h>

#define PKC_W   320             /* a .PKC picture's size */
#define PKC_H   200

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    wr16(p, v);
    wr16(p + 2, v >> 16);
}

/* where the section goes: the next image's table entry and pixels */
struct sink { uint8_t *out; uint32_t cap, entry, at; };

/* the table entry of an image w x h at (x, y); its pixels, or NULL if they do not fit or the
   numbers do not fit the entry */
static uint8_t *image(struct sink *s, int32_t w, int32_t h, int32_t x, int32_t y)
{
    if(w < 0 || w > 0xFFFF || h < 0 || h > 0xFFFF || x < -0x8000 || x > 0x7FFF || y < -0x8000 || y > 0x7FFF)
        return NULL;
    uint32_t n = (uint32_t)w * (uint32_t)h;
    if(n > s->cap - s->at) return NULL;
    uint8_t *e = s->out + s->entry, *px = s->out + s->at;
    wr32(e, s->at);
    wr16(e + 4, (uint32_t)w);
    wr16(e + 6, (uint32_t)h);
    wr16(e + 8, (uint32_t)x);
    wr16(e + 10, (uint32_t)y);
    s->entry += 12;
    s->at += n;
    return px;
}

/* A .PKC picture's pixels in pieces of one row, in the order of the file */
struct walk { const uint8_t *d, *src; uint32_t size, p, at, left; };

/* the next piece: 1 with its row, column, length and bytes; 0 at the picture's end; -1 if a
   run passes the file's end. Runs past the picture's last pixel are cut off there. */
static int next(struct walk *k, uint32_t *y, uint32_t *x, uint32_t *n, const uint8_t **src)
{
    while(!k->left) {
        if(k->size - k->p < 4 || k->at >= PKC_W * PKC_H) return 0;
        uint32_t v = rd32(k->d + k->p);
        k->p += 4;
        if(v & 0x80000000u) {
            k->at += 0u - v;
            continue;
        }
        if(v > k->size - k->p) return -1;
        k->src = k->d + k->p;
        k->p += v;
        k->left = v < PKC_W * PKC_H - k->at ? v : PKC_W * PKC_H - k->at;
    }
    *y = k->at / PKC_W;
    *x = k->at % PKC_W;
    *n = PKC_W - *x < k->left ? PKC_W - *x : k->left;
    *src = k->src;
    k->src += *n;
    k->at += *n;
    k->left -= *n;
    return 1;
}

#define SET(b, i)   ((b)[(i) >> 5] |= 1u << ((i) & 31))
#define GET(b, i)   ((b)[(i) >> 5] >> ((i) & 31) & 1)

/* box x0..x1 of the band y0..y1 (exclusive): its entry and the picture's bytes in it */
static int box(const uint8_t *d, uint32_t size, struct sink *s, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1)
{
    uint32_t w = x1 - x0;
    uint8_t *px = image(s, (int32_t)w, (int32_t)(y1 - y0), (int32_t)x0, (int32_t)y0);
    if(!px) return 0;
    memset(px, 0, w * (y1 - y0));
    struct walk k = { d, NULL, size, 0, 0, 0 };
    uint32_t y, x, n;
    const uint8_t *src;
    while(next(&k, &y, &x, &n, &src) > 0 && y < y1) {
        uint32_t a = x > x0 ? x : x0, b = x + n < x1 ? x + n : x1;
        if(y >= y0 && a < b) memcpy(px + (y - y0) * w + a - x0, src + a - x, b - a);
    }
    return 1;
}

/* the boxes of a .PKC picture; with s, their entries and pixels. Returns their number, -1 if
   the file is malformed or they do not fit. */
static int32_t pkc(const uint8_t *d, uint32_t size, struct sink *s)
{
    uint32_t rows[(PKC_H + 31) / 32] = {0};
    struct walk k = { d, NULL, size, 0, 0, 0 };
    uint32_t y, x, n;
    const uint8_t *src;
    int e;
    while((e = next(&k, &y, &x, &n, &src)) > 0)
        for(uint32_t i = 0; i < n; i++)
            if(src[i]) {
                SET(rows, y);
                break;
            }
    if(e < 0) return -1;
    int32_t count = 0;
    for(uint32_t y0 = 0, y1; y0 < PKC_H; y0 = y1) {
        for(y1 = y0; y1 < PKC_H && GET(rows, y1); y1++) ;
        if(y1 == y0) {
            y1++;
            continue;
        }
        uint32_t cols[PKC_W / 32] = {0};
        k = (struct walk){ d, NULL, size, 0, 0, 0 };
        while(next(&k, &y, &x, &n, &src) > 0 && y < y1)
            if(y >= y0)
                for(uint32_t i = 0; i < n; i++)
                    if(src[i]) SET(cols, x + i);
        int32_t x0 = -1, prev = -1;
        for(int32_t c = 0; c <= PKC_W; c++) {
            if(c < PKC_W && !GET(cols, c)) continue;
            if(x0 >= 0 && (c == PKC_W || c - prev > 9)) {
                if(s && !box(d, size, s, (uint32_t)x0, (uint32_t)prev + 1, y0, y1)) return -1;
                count++;
                x0 = -1;
            }
            if(x0 < 0) x0 = c;
            prev = c;
        }
    }
    return count;
}

uint32_t tb_hud_picture(const uint8_t *d, uint32_t size, uint8_t *out, uint32_t cap)
{
    int32_t n = pkc(d, size, NULL);
    if(n < 0 || cap < 4 || 4 + 12 * (uint64_t)n > cap) return 0;
    struct sink s = { out, cap, 4, 4 + 12 * (uint32_t)n };
    if(pkc(d, size, &s) != n) return 0;
    wr32(out, (uint32_t)n);
    return s.at;
}


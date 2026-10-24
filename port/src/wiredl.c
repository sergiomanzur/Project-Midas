/**
 * wiredl.c - 8-byte N64 display lists on a 64-bit host.
 *
 * A host Gfx is 16 bytes (two pointer-width words), but every display list
 * that comes straight out of the cart (font, rareware logo, gun barrel,
 * model sub-lists) is an 8-byte command stream. Walking one with the host
 * stride misreads every other command (a matrix pointer comes out as 0x61,
 * etc.).
 *
 * Ranges that hold wire-format lists are registered here (the ROM data
 * segment and each model image); gfx_run_dl() asks whether the pointer it
 * was handed is inside one, and if so expands that list into a host Gfx
 * array once (cached). Nested G_DL targets are left as wire references and
 * are expanded the same way when the walker recurses into them.
 */
#include <stdint.h>
#include <string.h>
#include <ultra64.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIRE_MAX_RANGES 512  /* room DLs are registered per streamed room */
#define WIRE_CACHE_SIZE 1024

static struct {
    const u8 *lo;
    const u8 *hi;
} s_ranges[WIRE_MAX_RANGES];
static int s_rangeCount;

static struct {
    const void *src;
    Gfx *dst;
} s_cache[WIRE_CACHE_SIZE];

void portWireDLRegister(const void *lo, const void *hi)
{
    if (lo == NULL || hi == NULL || hi <= lo) {
        return;
    }
    if (s_rangeCount >= WIRE_MAX_RANGES) {
        return;
    }
    s_ranges[s_rangeCount].lo = (const u8 *) lo;
    s_ranges[s_rangeCount].hi = (const u8 *) hi;
    s_rangeCount++;
}

/* The menu buffer gets reused (model load -> gunbarrel/logo scratch), so an
 * old model registration must not keep classifying the new contents as an
 * 8-byte command stream. Drop every range that intersects [lo, hi). */
void portWireDLUnregister(const void *lo, const void *hi)
{
    const u8 *a = (const u8 *) lo;
    const u8 *b = (const u8 *) hi;
    int i;

    if (a == NULL || b == NULL || b <= a) {
        return;
    }
    for (i = 0; i < s_rangeCount; ) {
        if (s_ranges[i].lo < b && s_ranges[i].hi > a) {
            s_ranges[i] = s_ranges[--s_rangeCount];
        } else {
            i++;
        }
    }
}

int portIsWireDL(const void *p)
{
    const u8 *q = (const u8 *) p;
    int i;

    if (q == NULL) {
        return 0;
    }
    for (i = 0; i < s_rangeCount; i++) {
        if (q >= s_ranges[i].lo && q < s_ranges[i].hi) {
            {
                extern void portConLogf(const char *fmt, ...);
                static int said;
                if (!said && (uintptr_t) q >= 0x501b2000u && (uintptr_t) q < 0x501b4000u) {
                    said = 1;
                    portConLogf("ge: wirehit %p range %p..%p (%d ranges)\n",
                            p, (const void *) s_ranges[i].lo, (const void *) s_ranges[i].hi, s_rangeCount);
                }
            }
            return 1;
        }
    }
    return 0;
}

Gfx *portWireDLExpand(const void *p)
{
    const u32 *wire = (const u32 *) p;
    u32 n = 0;
    u32 i;
    Gfx *dst;
    int slot;

    for (slot = 0; slot < WIRE_CACHE_SIZE; slot++) {
        if (s_cache[slot].src == p) {
            return s_cache[slot].dst;
        }
    }

    /* command count: 8-byte commands up to and including ENDDL. GE builds
     * against the plain F3DEX GBI (G_ENDDL = 0xB8) while some pre-converted
     * cart data carries the F3DEX2 value 0xDF, so accept either. */
    for (;;) {
        u8 op = (u8) (wire[n * 2] >> 24);
        if (op == 0xB8u || op == 0xDFu) {
            break;
        }
        n++;
        if (n > 0x8000) {
            return NULL;
        }
    }
    n++;

    {
        extern void *portAllocHigh(u32 size);
        dst = (Gfx *) portAllocHigh(n * (u32) sizeof(Gfx));
    }
    if (dst == NULL) {
        return NULL;
    }
    {
        extern void portConLogf(const char *fmt, ...);
        static int said;
        if (said < 8) {
            said++;
            portConLogf("ge: expand src=%p n=%u w=%08x %08x %08x %08x %08x %08x\n", p, n,
                    (unsigned) wire[0], (unsigned) wire[1], (unsigned) wire[2],
                    (unsigned) wire[3], (unsigned) wire[4], (unsigned) wire[5]);
        }
    }

    for (i = 0; i < n; i++) {
        dst[i].words.w0 = wire[i * 2];
        dst[i].words.w1 = wire[i * 2 + 1];
    }

    for (slot = 0; slot < WIRE_CACHE_SIZE; slot++) {
        if (s_cache[slot].src == NULL) {
            s_cache[slot].src = p;
            s_cache[slot].dst = dst;
            break;
        }
    }

    return dst;
}

#ifdef __cplusplus
}
#endif


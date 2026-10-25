/**
 * bank64.c - sound bank / sequence table conversion for 64-bit hosts.
 *
 * The N64 .ctl bank images and the Rare music table are 32-bit structure
 * trees (4-byte offsets that alBnkfNew / musicSeqFileNew overwrite with 4-byte
 * pointers in place). That cannot work on an LP64 host: the library structs
 * (ALBank, ALInstrument, ALSound, ALWaveTable...) hold 8-byte pointers and are
 * bigger than the wire records, so the pointer-patching walk overran every
 * record. This file builds the host structs out of the big-endian wire image
 * instead.
 *
 * Everything is allocated from the low arena (portAllocHigh): the audio
 * microcode commands (Acmd) carry 32-bit addresses (aLoadADPCM(book),
 * aSetLoop(loop), the sample data...), so anything the mixer reads by address
 * must sit below 4 GB. The sample tables are relocated there by
 * portAudioRelocate (segments.c).
 */
#include <stdint.h>
#include "platform_info.h" /* PORT_LP64 */

#ifdef PORT_LP64

#include <stdint.h>
#include <string.h>
#include <ultra64.h>
#include <PR/libaudio.h>
#include "music.h"

extern void *portAllocHigh(u32 size);
extern void portConLogf(const char *fmt, ...);

static const u8 *s_ctl;
static u32 s_ctlSize;
static u8 *s_tbl;
static u32 s_tblSize;
static s32 s_bad;

static u32 rd32(u32 off)
{
    const u8 *p = s_ctl + off;

    if (off + 4 > s_ctlSize) {
        s_bad++;
        return 0;
    }
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | (u32) p[3];
}

static u16 rd16(u32 off)
{
    const u8 *p = s_ctl + off;

    if (off + 2 > s_ctlSize) {
        s_bad++;
        return 0;
    }
    return (u16) (((u32) p[0] << 8) | p[1]);
}

static u8 rd8(u32 off)
{
    if (off + 1 > s_ctlSize) {
        s_bad++;
        return 0;
    }
    return s_ctl[off];
}

static void *lowAlloc(u32 size)
{
    void *p = portAllocHigh((size + 15u) & ~15u);

    if (p == NULL) {
        s_bad++;
        return NULL;
    }
    memset(p, 0, size);
    return p;
}

/* offset -> converted object, so records shared in the wire image stay shared
 * (a 128-slot instrument array usually points at a handful of instruments) */
#define MEMO_SIZE 16384

static struct {
    u32 key;
    void *val;
} s_memo[MEMO_SIZE];

static u32 memoKey(u32 off, u32 kind)
{
    return (off << 3) | kind | 0x80000000u; /* never 0 */
}

static void *memoGet(u32 off, u32 kind)
{
    u32 k = memoKey(off, kind);
    u32 i = (k * 2654435761u) % MEMO_SIZE;
    u32 n;

    for (n = 0; n < MEMO_SIZE; n++, i = (i + 1) % MEMO_SIZE) {
        if (s_memo[i].key == k) {
            return s_memo[i].val;
        }
        if (s_memo[i].key == 0) {
            return NULL;
        }
    }
    return NULL;
}

static void memoPut(u32 off, u32 kind, void *val)
{
    u32 k = memoKey(off, kind);
    u32 i = (k * 2654435761u) % MEMO_SIZE;
    u32 n;

    for (n = 0; n < MEMO_SIZE; n++, i = (i + 1) % MEMO_SIZE) {
        if (s_memo[i].key == 0 || s_memo[i].key == k) {
            s_memo[i].key = k;
            s_memo[i].val = val;
            return;
        }
    }
    s_bad++;
}

enum { K_INST = 1, K_SOUND, K_WAVE, K_ENV, K_KEY, K_BOOK, K_LOOP };

#define VISIT_MAX 4096
static const u8 *s_visitedBase[VISIT_MAX];
static s32 s_visitedCount;

static s32 visitBase(const u8 *base)
{
    s32 i;

    for (i = 0; i < s_visitedCount; i++) {
        if (s_visitedBase[i] == base) {
            return 0;
        }
    }
    if (s_visitedCount < VISIT_MAX) {
        s_visitedBase[s_visitedCount++] = base;
    }
    return 1;
}

static ALEnvelope *convEnv(u32 off)
{
    ALEnvelope *e;

    if (off == 0 || off + 14 > s_ctlSize) {
        return NULL;
    }
    e = (ALEnvelope *) memoGet(off, K_ENV);
    if (e != NULL) {
        return e;
    }
    e = (ALEnvelope *) lowAlloc(sizeof(ALEnvelope));
    if (e == NULL) {
        return NULL;
    }
    e->attackTime = (s32) rd32(off + 0);
    e->decayTime = (s32) rd32(off + 4);
    e->releaseTime = (s32) rd32(off + 8);
    e->attackVolume = rd8(off + 12);
    e->decayVolume = rd8(off + 13);
    memoPut(off, K_ENV, e);
    return e;
}

static ALKeyMap *convKey(u32 off)
{
    ALKeyMap *k;

    if (off == 0 || off + 6 > s_ctlSize) {
        return NULL;
    }
    k = (ALKeyMap *) memoGet(off, K_KEY);
    if (k != NULL) {
        return k;
    }
    k = (ALKeyMap *) lowAlloc(sizeof(ALKeyMap));
    if (k == NULL) {
        return NULL;
    }
    k->velocityMin = rd8(off + 0);
    k->velocityMax = rd8(off + 1);
    k->keyMin = rd8(off + 2);
    k->keyMax = rd8(off + 3);
    k->keyBase = rd8(off + 4);
    k->detune = (s8) rd8(off + 5);
    memoPut(off, K_KEY, k);
    return k;
}

static ALADPCMBook *convBook(u32 off)
{
    ALADPCMBook *b;
    s32 order, npred, n, count;

    if (off == 0 || off + 8 > s_ctlSize) {
        return NULL;
    }
    b = (ALADPCMBook *) memoGet(off, K_BOOK);
    if (b != NULL) {
        return b;
    }
    order = (s32) rd32(off);
    npred = (s32) rd32(off + 4);
    count = order * npred * 8;
    if (order < 0 || npred < 0 || count < 0 || count > 4096 || off + 8 + (u32) count * 2 > s_ctlSize) {
        s_bad++;
        return NULL;
    }
    b = (ALADPCMBook *) lowAlloc((u32) (sizeof(ALADPCMBook) + (size_t) count * sizeof(s16)));
    if (b == NULL) {
        return NULL;
    }
    b->order = order;
    b->npredictors = npred;
    for (n = 0; n < count; n++) {
        b->book[n] = (s16) rd16(off + 8 + (u32) n * 2);
    }
    memoPut(off, K_BOOK, b);
    return b;
}

static ALADPCMloop *convLoop(u32 off)
{
    ALADPCMloop *l;
    s32 n;

    if (off == 0 || off + 12 + 32 > s_ctlSize) {
        return NULL;
    }
    l = (ALADPCMloop *) memoGet(off, K_LOOP);
    if (l != NULL) {
        return l;
    }
    l = (ALADPCMloop *) lowAlloc(sizeof(ALADPCMloop));
    if (l == NULL) {
        return NULL;
    }
    l->start = rd32(off);
    l->end = rd32(off + 4);
    l->count = rd32(off + 8);
    for (n = 0; n < 16; n++) {
        ((s16 *) l->state)[n] = (s16) rd16(off + 12 + (u32) n * 2);
    }
    memoPut(off, K_LOOP, l);
    return l;
}

static ALRawLoop *convRawLoop(u32 off)
{
    ALRawLoop *l;

    if (off == 0 || off + 12 > s_ctlSize) {
        return NULL;
    }
    l = (ALRawLoop *) memoGet(off, K_LOOP);
    if (l != NULL) {
        return l;
    }
    l = (ALRawLoop *) lowAlloc(sizeof(ALRawLoop));
    if (l == NULL) {
        return NULL;
    }
    l->start = rd32(off);
    l->end = rd32(off + 4);
    l->count = rd32(off + 8);
    memoPut(off, K_LOOP, l);
    return l;
}

static ALWaveTable *convWave(u32 off)
{
    ALWaveTable *w;
    u32 base;

    if (off == 0 || off + 16 > s_ctlSize) {
        return NULL;
    }
    w = (ALWaveTable *) memoGet(off, K_WAVE);
    if (w != NULL) {
        return w;
    }
    w = (ALWaveTable *) lowAlloc(sizeof(ALWaveTable));
    if (w == NULL) {
        return NULL;
    }
    memoPut(off, K_WAVE, w);

    base = rd32(off + 0);
    w->len = (s32) rd32(off + 4);
    w->type = rd8(off + 8);
    w->flags = 1; /* already converted (what the in-place patcher marked) */
    if (s_tblSize != 0 && (base > s_tblSize || (u32) w->len > s_tblSize - base)) {
        s_bad++;
        w->base = s_tbl;
        w->len = 0;
    } else {
        w->base = s_tbl + base;
    }

    if (w->type == AL_ADPCM_WAVE) {
        w->waveInfo.adpcmWave.loop = convLoop(rd32(off + 12));
        w->waveInfo.adpcmWave.book = convBook(rd32(off + 16));
    } else if (w->type == AL_RAW16_WAVE) {
        w->waveInfo.rawWave.loop = convRawLoop(rd32(off + 12));

        /* RAW16 samples are big-endian s16 in the table but the mixer plays
         * native s16, so swap them once */
        if (w->len > 1 && visitBase(w->base)) {
            s16 *smp = (s16 *) w->base;
            s32 ns = w->len >> 1;
            s32 k;

            for (k = 0; k < ns; k++) {
                u16 v = (u16) smp[k];

                smp[k] = (s16) (u16) ((v >> 8) | (v << 8));
            }
        }
    }
    return w;
}

static ALSound *convSound(u32 off)
{
    ALSound *s;

    if (off == 0 || off + 15 > s_ctlSize) {
        return NULL;
    }
    s = (ALSound *) memoGet(off, K_SOUND);
    if (s != NULL) {
        return s;
    }
    s = (ALSound *) lowAlloc(sizeof(ALSound));
    if (s == NULL) {
        return NULL;
    }
    memoPut(off, K_SOUND, s);
    s->envelope = convEnv(rd32(off + 0));
    s->keyMap = convKey(rd32(off + 4));
    s->wavetable = convWave(rd32(off + 8));
    s->samplePan = (ALPan) rd8(off + 12);
    s->sampleVolume = rd8(off + 13);
    s->flags = 1;
    return s;
}

static ALInstrument *convInst(u32 off)
{
    ALInstrument *inst;
    s32 count, i;

    if (off == 0 || off + 16 > s_ctlSize) {
        return NULL;
    }
    inst = (ALInstrument *) memoGet(off, K_INST);
    if (inst != NULL) {
        return inst;
    }
    count = (s16) rd16(off + 14);
    if (count < 0 || count > 2048) {
        s_bad++;
        return NULL;
    }
    inst = (ALInstrument *) lowAlloc((u32) (sizeof(ALInstrument) + (size_t) (count > 0 ? count - 1 : 0) * sizeof(ALSound *)));
    if (inst == NULL) {
        return NULL;
    }
    memoPut(off, K_INST, inst);
    inst->volume = rd8(off + 0);
    inst->pan = (ALPan) rd8(off + 1);
    inst->priority = rd8(off + 2);
    inst->flags = 1;
    inst->tremType = rd8(off + 4);
    inst->tremRate = rd8(off + 5);
    inst->tremDepth = rd8(off + 6);
    inst->tremDelay = rd8(off + 7);
    inst->vibType = rd8(off + 8);
    inst->vibRate = rd8(off + 9);
    inst->vibDepth = rd8(off + 10);
    inst->vibDelay = rd8(off + 11);
    inst->bendRange = (s16) rd16(off + 12);
    inst->soundCount = (s16) count;
    for (i = 0; i < count; i++) {
        inst->soundArray[i] = convSound(rd32(off + 16 + (u32) i * 4));
    }
    return inst;
}

static ALBank *convBank(u32 off)
{
    ALBank *bank;
    s32 count, i;

    if (off == 0 || off + 12 > s_ctlSize) {
        return NULL;
    }
    count = (s16) rd16(off);
    if (count < 0 || count > 1024) {
        s_bad++;
        return NULL;
    }
    bank = (ALBank *) lowAlloc((u32) (sizeof(ALBank) + (size_t) (count > 0 ? count - 1 : 0) * sizeof(ALInstrument *)));
    if (bank == NULL) {
        return NULL;
    }
    bank->instCount = (s16) count;
    bank->flags = 1;
    bank->sampleRate = (s32) rd32(off + 4);
    bank->percussion = convInst(rd32(off + 8));
    for (i = 0; i < count; i++) {
        bank->instArray[i] = convInst(rd32(off + 12 + (u32) i * 4));
    }
    return bank;
}

/**
 * Convert a big-endian ctl image. Returns the first bank (all GE code uses
 * bankArray[0]) or NULL when the image is not a bank file.
 */
ALBank *portBank64Load(const u8 *ctl, u32 ctlSize, u8 *tbl, u32 tblSize, const char *name)
{
    ALBank *bank = NULL;
    u32 revision, bankCount;

    s_ctl = ctl;
    s_ctlSize = ctlSize;
    s_tbl = tbl;
    s_tblSize = tblSize;
    s_bad = 0;
    memset(s_memo, 0, sizeof(s_memo));
    s_visitedCount = 0;

    if (ctl == NULL || ctlSize < 8) {
        portConLogf("ge: bank64 %s: no ctl image\n", name);
        return NULL;
    }
    revision = rd16(0);
    bankCount = rd16(2);
    if (revision != AL_BANK_VERSION || bankCount == 0) {
        portConLogf("ge: bank64 %s: not a bank file (rev %04x banks %u)\n", name, (unsigned) revision, (unsigned) bankCount);
        return NULL;
    }
    bank = convBank(rd32(4));
    if (bank != NULL) { /* census: proves every sound resolved */
        s32 ii, ss, total = 0, nullSound = 0, nullWave = 0, nullEnv = 0, nullKey = 0, nullBook = 0, adpcm = 0, raw = 0;

        for (ii = 0; ii < bank->instCount; ii++) {
            ALInstrument *in = bank->instArray[ii];

            if (in == NULL) {
                continue;
            }
            for (ss = 0; ss < in->soundCount; ss++) {
                ALSound *so = in->soundArray[ss];

                total++;
                if (so == NULL) { nullSound++; continue; }
                if (so->wavetable == NULL) { nullWave++; }
                else if (so->wavetable->type == AL_ADPCM_WAVE) { adpcm++; if (so->wavetable->waveInfo.adpcmWave.book == NULL) nullBook++; }
                else { raw++; }
                if (so->envelope == NULL) { nullEnv++; }
                if (so->keyMap == NULL) { nullKey++; }
            }
        }
        portConLogf("ge: bank64 %s census: sounds %d null %d, wave null %d (adpcm %d raw %d, book null %d), env null %d, key null %d\n", name,
                    (int) total, (int) nullSound, (int) nullWave, (int) adpcm, (int) raw, (int) nullBook, (int) nullEnv, (int) nullKey);
    }
    portConLogf("ge: bank64 %s: bank %p insts=%d rate=%d sounds0=%d (%d bad references)\n", name, (void *) bank,
                bank != NULL ? (int) bank->instCount : -1, bank != NULL ? (int) bank->sampleRate : -1,
                (bank != NULL && bank->instCount > 0 && bank->instArray[0] != NULL) ? (int) bank->instArray[0]->soundCount : -1,
                (int) s_bad);
    return bank;
}

/**
 * Convert the Rare music table (wire: u16 count, u16 pad, then count 8-byte
 * entries {u32 offset, u16 uncompressed_len, u16 len}, all big-endian) into
 * host RareALSeqData entries whose address is the sample-table pointer.
 */
RareALSeqBankFile *portSeqTable64(const u8 *sbk, u32 sbkSize, u8 *base)
{
    RareALSeqBankFile *t;
    u32 count, i;

    if (sbk == NULL || sbkSize < 4) {
        return NULL;
    }
    count = ((u32) sbk[0] << 8) | sbk[1];
    if (count == 0 || count > 1024 || 4 + count * 8 > sbkSize) {
        portConLogf("ge: seqtable64: bad count %u\n", (unsigned) count);
        return NULL;
    }
    t = (RareALSeqBankFile *) lowAlloc((u32) (sizeof(RareALSeqBankFile) + (size_t) count * sizeof(RareALSeqData)));
    if (t == NULL) {
        return NULL;
    }
    t->seqCount = (u16) count;
    for (i = 0; i < count; i++) {
        const u8 *e = sbk + 4 + i * 8;
        u32 off = ((u32) e[0] << 24) | ((u32) e[1] << 16) | ((u32) e[2] << 8) | e[3];

        t->seqArray[i].address = base + off;
        t->seqArray[i].uncompressed_len = (u16) (((u32) e[4] << 8) | e[5]);
        t->seqArray[i].len = (u16) (((u32) e[6] << 8) | e[7]);
    }
    portConLogf("ge: seqtable64: %u sequences\n", (unsigned) count);
    return t;
}

#endif /* PORT_LP64 */

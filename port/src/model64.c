/**
 * model64.c - 64-bit model image conversion (arm64 / x86_64 hosts).
 *
 * The N64 model file is a 32-bit structure graph:
 *   - switches: numSwitches 4-byte 0x05000000-based node references
 *   - textures: numtextures ModelFileTextures records (no pointers)
 *   - nodes:    24-byte ModelNode records (opcode + five 4-byte references)
 *   - rodata:   per-node records whose pointer fields are also 4-byte offsets
 *   - raw data: vertices, display lists, images (left in place)
 *
 * The game's compiled structs use real pointers, so the wire image cannot be
 * walked in place on a 64-bit host (modelPromoteNodeOffsetsToPointers used to
 * do that and crashed on the first model). This file builds a parallel host
 * graph: every reachable ModelNode and its union ModelRoData are allocated
 * from MEMPOOL_STAGE, links and rodata pointers are resolved to host
 * addresses, and RootNode/Switches are repointed. Data the game treats as
 * opaque bytes (vertices, Gfx lists, images) stays in the wire buffer and is
 * referenced by BaseAddr/Vertices/Primary/... as before.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ultra64.h>
#include <bondtypes.h>
#include "memp.h"
#include "port_model64.h"

extern void portConLogf(const char *fmt, ...);

#define WIRE_BASE 0x05000000u

/* The texture pass converts the node display lists (sub_GAME_7F0762E0) right
 * after this conversion; it needs the model image base to resolve the
 * 0x05000000 references of nested G_DL targets. */
static u8 *s_lastBase;
u8 *portModelLastBase(void)
{
    return s_lastBase;
}

#define R8(p, o)   (*(const u8 *) ((const u8 *) (p) + (o)))
#define R16(p, o)  (*(const u16 *) ((const u8 *) (p) + (o)))
#define R32(p, o)  (*(const u32 *) ((const u8 *) (p) + (o)))
#define RF32(p, o) (*(const f32 *) ((const u8 *) (p) + (o)))

typedef struct {
    u8 *base;
    u32 size;
    u32 *offs;          /* wire file offsets of collected nodes */
    s32 count;
    s32 cap;
    ModelNode *nodes;   /* host node array, parallel to offs */
    union ModelRoData *rodata;
    const char *name;
    s32 bad;
} ModelConv;

static int is_wire(u32 v)
{
    return (v >> 24) == 0x05;
}

static u32 wire_off(u32 v)
{
    return v - WIRE_BASE;
}

/* Raw data pointer (vertices, Gfx lists, images, ...). */
static void *raw_ptr(ModelConv *c, u32 v)
{
    if (!is_wire(v)) {
        return NULL;
    }
    {
        u32 off = wire_off(v);
        if (off >= c->size) {
            c->bad++;
            return NULL;
        }
        return c->base + off;
    }
}

static s32 find_off(ModelConv *c, u32 off)
{
    s32 i;
    for (i = 0; i < c->count; i++) {
        if (c->offs[i] == off) {
            return i;
        }
    }
    return -1;
}

static ModelNode *find_node(ModelConv *c, u32 v)
{
    s32 i;
    if (!is_wire(v)) {
        return NULL;
    }
    i = find_off(c, wire_off(v));
    return (i >= 0) ? &c->nodes[i] : NULL;
}

static void push_off(ModelConv *c, u32 off)
{
    u32 v;
    const u8 *w;
    u32 data, dop;
    int op;

    if (off + 24 > c->size) {
        return;
    }
    if (find_off(c, off) >= 0) {
        return;
    }
    if (c->count == c->cap) {
        s32 ncap = (c->cap == 0) ? 64 : c->cap * 2;
        u32 *n = (u32 *) realloc(c->offs, (size_t) ncap * sizeof(u32));
        if (n == NULL) {
            c->bad++;
            return;
        }
        c->offs = n;
        c->cap = ncap;
    }
    c->offs[c->count++] = off;

    w = c->base + off;
    op = R16(w, 0) & 0xff;
    data = R32(w, 4);
    dop = is_wire(data) ? wire_off(data) : 0xFFFFFFFFu;

    /* node links */
    v = R32(w, 8);  if (is_wire(v)) push_off(c, wire_off(v));   /* Parent */
    v = R32(w, 0x0C); if (is_wire(v)) push_off(c, wire_off(v)); /* Next   */
    v = R32(w, 0x10); if (is_wire(v)) push_off(c, wire_off(v)); /* Prev   */
    v = R32(w, 0x14); if (is_wire(v)) push_off(c, wire_off(v)); /* Child  */

    /* rodata fields that name another node (see modelPromote...) */
    if (dop != 0xFFFFFFFFu && dop + 0x24 <= c->size) {
        const u8 *d = c->base + dop;
        switch (op) {
        case 1:
            v = R32(d, 0x04); if (is_wire(v)) push_off(c, wire_off(v));
            break;
        case 2:
        case 3:
            v = R32(d, 0x14); if (is_wire(v)) push_off(c, wire_off(v));
            break;
        case 7:
            v = R32(d, 0x00); if (is_wire(v)) push_off(c, wire_off(v));
            v = R32(d, 0x04); if (is_wire(v)) push_off(c, wire_off(v));
            break;
        case 8:
            v = R32(d, 0x08); if (is_wire(v)) push_off(c, wire_off(v));
            break;
        case 9:
            v = R32(d, 0x18); if (is_wire(v)) push_off(c, wire_off(v));
            v = R32(d, 0x1C); if (is_wire(v)) push_off(c, wire_off(v));
            break;
        case 0x0D:
        case 0x11:
            v = R32(d, 0x14); if (is_wire(v)) push_off(c, wire_off(v));
            break;
        case 0x12:
            v = R32(d, 0x00); if (is_wire(v)) push_off(c, wire_off(v));
            break;
        default:
            break;
        }
    }
}

static void conv_rodata(ModelConv *c, union ModelRoData *d, const u8 *w, int op)
{
    u8 *base = c->base;

    switch (op) {
    case 1: /* Header */
        d->Header.AnimPart = R16(w, 0x00);
        d->Header.MatrixIndex = (s16) R16(w, 0x02);
        d->Header.FirstGroupNode = find_node(c, R32(w, 0x04));
        d->Header.Group1 = R16(w, 0x08);
        d->Header.Group2 = R16(w, 0x0A);
        d->Header.RwDataIndex = R16(w, 0x0C);
        d->Header.reserved = R16(w, 0x0E);
        break;

    case 2:
    case 3: /* Group */
        memcpy(&d->Group.Origin, w + 0x00, 12);
        d->Group.JointID = R16(w, 0x0C);
        d->Group.MatrixID0 = (s16) R16(w, 0x0E);
        d->Group.MatrixID1 = (s16) R16(w, 0x10);
        d->Group.MatrixID2 = (s16) R16(w, 0x12);
        d->Group.ChildGroupNode = find_node(c, R32(w, 0x14));
        d->Group.BoundingVolumeRadius = RF32(w, 0x18);
        break;

    case 4: /* Display list */
        d->DisplayList.Primary = (Gfx *) raw_ptr(c, R32(w, 0x00));
        d->DisplayList.Secondary = (Gfx *) raw_ptr(c, R32(w, 0x04));
        d->DisplayList.BaseAddr = base;
        d->DisplayList.Vertices = (Vertex *) raw_ptr(c, R32(w, 0x0C));
        d->DisplayList.numVertices = R16(w, 0x10);
        d->DisplayList.ModelType = (s8) R8(w, 0x12);
        break;

    case 5: /* Op05 */
        d->Op05.NumChildren = (s32) R32(w, 0x00);
        d->Op05.Children = (struct ModelRoData_Child *) raw_ptr(c, R32(w, 0x04));
        d->Op05.Vertices = (struct Vertex *) raw_ptr(c, R32(w, 0x08));
        d->Op05.Images = (struct sImageTableEntry *) raw_ptr(c, R32(w, 0x0C));
        memcpy(d->Op05.Data, w + 0x10, sizeof(d->Op05.Data));
        d->Op05.unk1A0 = R32(w, 0x1A0);
        d->Op05.BaseAddr = base;
        break;

    case 6: /* Op06 */
        d->Op06.unk00 = R32(w, 0x00);
        d->Op06.unk04 = R32(w, 0x04);
        d->Op06.unk08 = R32(w, 0x08);
        d->Op06.unk0C = R32(w, 0x0C);
        d->Op06.unk10 = R32(w, 0x10);
        d->Op06.BaseAddr = base;
        break;

    case 7: /* Op07 */
        d->Op07.unk00 = find_node(c, R32(w, 0x00));
        d->Op07.unk04 = find_node(c, R32(w, 0x04));
        d->Op07.NumChildren = (s32) R32(w, 0x08);
        d->Op07.Children = (struct ModelRoData_Child *) raw_ptr(c, R32(w, 0x0C));
        d->Op07.Vertices = (struct Vertex *) raw_ptr(c, R32(w, 0x10));
        d->Op07.Images = (struct sImageTableEntry *) raw_ptr(c, R32(w, 0x14));
        memcpy(d->Op07.Data, w + 0x18, sizeof(d->Op07.Data));
        d->Op07.unk1A8 = R16(w, 0x1A8);
        d->Op07.RwDataIndex = R16(w, 0x1AA);
        d->Op07.BaseAddr = base;
        break;

    case 8: /* LOD */
        d->LOD.MinDistance = RF32(w, 0x00);
        d->LOD.MaxDistance = RF32(w, 0x04);
        d->LOD.Affects = find_node(c, R32(w, 0x08));
        d->LOD.RwDataIndex = R16(w, 0x0C);
        d->LOD.reserved = R16(w, 0x0E);
        break;

    case 9: /* BSP */
        memcpy(&d->BSP.Point, w + 0x00, 12);
        memcpy(&d->BSP.Vector, w + 0x0C, 12);
        d->BSP.leftChild = find_node(c, R32(w, 0x18));
        d->BSP.rightChild = find_node(c, R32(w, 0x1C));
        d->BSP.reserved = (s16) R16(w, 0x20);
        d->BSP.RwDataIndex = R16(w, 0x22);
        break;

    case 0x0A: /* Bounding box */
        d->BoundingBox.ModelNumber = R32(w, 0x00);
        memcpy(&d->BoundingBox.Bounds, w + 0x04, sizeof(d->BoundingBox.Bounds));
        break;

    case 0x0B: /* Op11 */
        memcpy(d->Op11.unk0c, w + 0x00, sizeof(d->Op11.unk0c));
        d->Op11.BoundingVolumeRadius = RF32(w, 0x40);
        d->Op11.RwDataIndex = R16(w, 0x44);
        d->Op11.unk46 = R16(w, 0x46);
        d->Op11.BaseAddr = base;
        break;

    case 0x0C: /* Gunfire */
        memcpy(&d->Gunfire.Offset, w + 0x00, 12);
        memcpy(&d->Gunfire.Size, w + 0x0C, 12);
        d->Gunfire.Image = raw_ptr(c, R32(w, 0x18));
        d->Gunfire.Scale = RF32(w, 0x1C);
        d->Gunfire.RwDataIndex = R16(w, 0x20);
        d->Gunfire.reserved = R16(w, 0x22);
        d->Gunfire.BaseAddr = (u32) (uintptr_t) base;
        break;

    case 0x0D: /* Shadow */
        memcpy(&d->Shadow.pos, w + 0x00, sizeof(d->Shadow.pos));
        memcpy(&d->Shadow.size, w + 0x08, sizeof(d->Shadow.size));
        d->Shadow.image = raw_ptr(c, R32(w, 0x10));
        d->Shadow.HeaderNode = find_node(c, R32(w, 0x14));
        d->Shadow.Scale = RF32(w, 0x18);
        d->Shadow.BaseAddr = base;
        break;

    case 0x0E: /* Op14 */
        memcpy(&d->Op14, w, sizeof(d->Op14));
        break;

    case 0x0F: /* Interlinkage */
        memcpy(&d->Interlinkage, w, sizeof(d->Interlinkage));
        break;

    case 0x10: /* Op16 */
        memcpy(&d->Op16, w, sizeof(d->Op16));
        break;

    case 0x11: /* Op17 (not a member of the union; still a rodata record) */
    {
        struct ModelRoData_Op17Record *r = (struct ModelRoData_Op17Record *) (void *) d;
        r->hitpart = (s32) R32(w, 0x00);
        r->radiusSq = RF32(w, 0x04);
        memcpy(&r->pos, w + 0x08, 12);
        r->othernode = find_node(c, R32(w, 0x14));
        r->scale1 = RF32(w, 0x18);
        r->scale2 = RF32(w, 0x1C);
        break;
    }

    case 0x12: /* Switch */
        d->Switch.Controls = find_node(c, R32(w, 0x00));
        d->Switch.RwDataIndex = R16(w, 0x04);
        d->Switch.reserved = R16(w, 0x06);
        break;

    case 0x15: /* GroupSimple */
        memcpy(&d->GroupSimple, w, sizeof(d->GroupSimple));
        break;

    case 0x16: /* DisplayListPrimary */
        d->DisplayListPrimary.numVertices = (s32) R32(w, 0x00);
        d->DisplayListPrimary.Vertices = (Vertex *) raw_ptr(c, R32(w, 0x04));
        d->DisplayListPrimary.Primary = (Gfx *) raw_ptr(c, R32(w, 0x08));
        d->DisplayListPrimary.BaseAddr = base;
        break;

    case 0x17: /* Head placeholder */
        d->HeadPlaceholder.RwDataIndex = R16(w, 0x00);
        break;

    case 0x18: /* Display list + collision */
        d->DisplayListCollisions.Primary = (Gfx *) raw_ptr(c, R32(w, 0x00));
        d->DisplayListCollisions.Secondary = (Gfx *) raw_ptr(c, R32(w, 0x04));
        d->DisplayListCollisions.Vertices = (Vertex *) raw_ptr(c, R32(w, 0x08));
        d->DisplayListCollisions.numVertices = (s16) R16(w, 0x0C);
        d->DisplayListCollisions.numCollisionVertices = (s16) R16(w, 0x0E);
        d->DisplayListCollisions.CollisionVertices = (Vertex *) raw_ptr(c, R32(w, 0x10));
        d->DisplayListCollisions.PointUsage = (s16 *) raw_ptr(c, R32(w, 0x14));
        d->DisplayListCollisions.ModelType = (s16) R16(w, 0x18);
        d->DisplayListCollisions.RwDataIndex = R16(w, 0x1A);
        d->DisplayListCollisions.BaseAddr = base;
#ifdef PORT_LP64
        d->DisplayListCollisions.CollisionRelatedNodes = NULL;
        {
            /* A Vertex is the 16-byte Vtx, so the collision vertices' related-node word
             * (+0x8) cannot hold a host pointer: resolve it once into a side table. The
             * generator emitted it as two s16, so on little-endian its halves are swapped
             * (and so is the s16 related index at +0xC); normalise in place, once. */
            s32 ncv = d->DisplayListCollisions.numCollisionVertices;
            u8 *cv = (u8 *) d->DisplayListCollisions.CollisionVertices;

            if (ncv > 0 && cv != NULL) {
                ModelNode **tab = (ModelNode **) mempAllocBytesInBank((u32) ncv * (u32) sizeof(ModelNode *), MEMPOOL_STAGE);

                if (tab != NULL) {
                    s32 k;

                    memset(tab, 0, (size_t) ncv * sizeof(ModelNode *));
                    for (k = 0; k < ncv; k++) {
                        u8 *vv = cv + (size_t) k * 16;
                        u32 wv;

                        memcpy(&wv, vv + 8, 4);
                        if (wv != 0 && !is_wire(wv)) {
                            u32 sw = (wv << 16) | (wv >> 16);

                            if (is_wire(sw)) {
                                u16 ri;

                                wv = sw;
                                memcpy(vv + 8, &wv, 4);
                                memcpy(&ri, vv + 12, 2);
                                ri = (u16) ((ri >> 8) | (ri << 8));
                                memcpy(vv + 12, &ri, 2);
                            }
                        }
                        tab[k] = is_wire(wv) ? find_node(c, wv) : NULL;
                    }
                    d->DisplayListCollisions.CollisionRelatedNodes = tab;
                }
            }
        }
#endif
        {
            static int said;
            if (said < 6 && c->name != NULL && strncmp(c->name, "Chead", 5) == 0) {
                said++;
                portConLogf("ge: m64 dlcol %s at +%04x: %08x %08x %08x %08x %08x %08x %08x %08x\n",
                        c->name, (unsigned) ((const u8 *) w - c->base),
                        (unsigned) R32(w, 0x00), (unsigned) R32(w, 0x04), (unsigned) R32(w, 0x08),
                        (unsigned) R32(w, 0x0C), (unsigned) R32(w, 0x10), (unsigned) R32(w, 0x14),
                        (unsigned) R32(w, 0x18), (unsigned) R32(w, 0x1C));
            }
        }
        break;

    default:
        break;
    }
}

void portModelToHost(struct ModelFileHeader *hdr, u8 *base, u32 size, const char *name)
{
    ModelConv c;
    s32 nsw, ntex, i;
    u32 root_off;
    size_t nodes_bytes, rodata_bytes, sw_bytes, total;
    u8 *mem;
    ModelNode **hsw;

    memset(&c, 0, sizeof(c));
    c.base = base;
    c.size = size;
    c.name = name;
    s_lastBase = base;
    {
        extern void portWireDLRegister(const void *lo, const void *hi);
        extern void portWireDLUnregister(const void *lo, const void *hi);
        portWireDLUnregister(base, base + size);
        portWireDLRegister(base, base + size);
        {
            static int said;
            if (said < 20 && (uintptr_t) base < 0x501c0000u) {
                said++;
                portConLogf("ge: m64 reg %s %p..%p\n", name, (void *) base, (void *) (base + size));
            }
        }
    }

    nsw = (s16) hdr->numSwitches;
    ntex = (s16) hdr->numtextures;
    if (nsw < 0) nsw = 0;
    if (ntex < 0) ntex = 0;

    root_off = (u32) ((u8 *) hdr->RootNode - base);
    {
        static int said;
        if (!said && name != NULL && strncmp(name, "Chead", 5) == 0) {
            int k;
            said = 1;
            portConLogf("ge: m64 head %s base=%p nsw=%d ntex=%d rootsz=%d root=%u\n",
                    name, (void *) base, (int) hdr->numSwitches, (int) hdr->numtextures,
                    (int) sizeof(ModelFileTextures), (unsigned) root_off);
            for (k = 0; k < 16; k++) {
                portConLogf("ge: m64 head w[%02x]=%08x %08x %08x %08x\n", k * 16,
                        (unsigned) R32(base, k * 16 + 0), (unsigned) R32(base, k * 16 + 4),
                        (unsigned) R32(base, k * 16 + 8), (unsigned) R32(base, k * 16 + 0xC));
            }
        }
    }
    push_off(&c, root_off);
    for (i = 0; i < nsw; i++) {
        u32 v = R32(base, (u32) i * 4);
        if (is_wire(v)) {
            push_off(&c, wire_off(v));
        }
    }

    if (c.count == 0) {
        free(c.offs);
        portConLogf("ge: model %s: no reachable nodes\n", name);
        return;
    }

    portConLogf("ge: m64: %s nsw=%d ntex=%d root=%u nodes=%d op0=%02x l0=%08x l1=%08x l2=%08x l3=%08x\n",
            name, (int) nsw, (int) ntex, (unsigned) root_off, (int) c.count,
            (unsigned) (R16(base + c.offs[0], 0) & 0xff),
            (unsigned) R32(base + c.offs[0], 8), (unsigned) R32(base + c.offs[0], 0x0C),
            (unsigned) R32(base + c.offs[0], 0x10), (unsigned) R32(base + c.offs[0], 0x14));

    nodes_bytes = (size_t) c.count * sizeof(ModelNode);
    rodata_bytes = (size_t) c.count * sizeof(union ModelRoData);
    sw_bytes = (size_t) nsw * sizeof(ModelNode *);
    total = (nodes_bytes + 7) & ~(size_t) 7;
    total += rodata_bytes;
    total = (total + 7) & ~(size_t) 7;
    total += sw_bytes;

    mem = (u8 *) mempAllocBytesInBank((u32) total + 16, MEMPOOL_STAGE);
    if (mem == NULL) {
        free(c.offs);
        portConLogf("ge: model %s: no stage memory for the host graph (%u bytes)\n",
                name, (unsigned) total);
        return;
    }
    memset(mem, 0, total + 16);
    c.nodes = (ModelNode *) mem;
    c.rodata = (union ModelRoData *) (mem + ((nodes_bytes + 7) & ~(size_t) 7));
    hsw = (ModelNode **) (mem + ((nodes_bytes + 7) & ~(size_t) 7) + rodata_bytes);
    hsw = (ModelNode **) (((uintptr_t) hsw + 7) & ~(uintptr_t) 7);
    /* second pass: fill host nodes and rodata */
    for (i = 0; i < c.count; i++) {
        const u8 *w = base + c.offs[i];
        ModelNode *hn = &c.nodes[i];
        int op = R16(w, 0) & 0xff;
        u32 data = R32(w, 4);

        hn->Opcode = (u16) R16(w, 0);
        if (is_wire(data) && wire_off(data) + 4 <= size) {
            hn->Data = &c.rodata[i];
            conv_rodata(&c, &c.rodata[i], base + wire_off(data), op);
        } else {
            hn->Data = NULL;
        }
        hn->Parent = find_node(&c, R32(w, 0x08));
        hn->Next = find_node(&c, R32(w, 0x0C));
        hn->Prev = find_node(&c, R32(w, 0x10));
        hn->Child = find_node(&c, R32(w, 0x14));
    }

    /* switches: wire entry -> host node */
    for (i = 0; i < nsw; i++) {
        u32 v = R32(base, (u32) i * 4);
        hsw[i] = is_wire(v) ? find_node(&c, v) : NULL;
    }

    hdr->RootNode = find_node(&c, root_off);
    if (hdr->RootNode == NULL) {
        hdr->RootNode = &c.nodes[0];
    }
    hdr->Switches = hsw;

    if (c.bad != 0) {
        portConLogf("ge: model %s: %d out-of-range references during host conversion\n", name, (int) c.bad);
    }
    free(c.offs);
}

/**
 * lp64_setup.c - expand a ROM-served setup's prop definitions to the 64-bit host layout.
 *
 * The prop definitions of a setup file (doors, props, pickups, guards, ...) are records in the
 * N64 layout: every pointer field is 4 bytes. On a 64-bit host the same C structs are bigger and
 * their fields sit at different offsets, so the game cannot read the file's records in place.
 * Before the stage's objects are created, prop.c hands the (already byte-order-fixed) records here
 * and gets back a fresh array in the host layout; from then on sizepropdef() steps by the host
 * stride and every consumer sees ordinary host structs.
 *
 * The per-type field map is generated from the compiler's own struct layouts
 * (scripts/port/gen_lp64_setup.py -> port/generated/lp64_setup_ops.h), so it cannot drift from
 * src/bondtypes.h by hand. A pointer slot keeps its 32-bit value zero-extended: a few carry setup
 * file offsets that the loader promotes later; the runtime pointers are all zero in the file.
 */
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include "platform_info.h"

#ifdef PORT_LP64

#include "../generated/lp64_setup_ops.h"

extern void portConLogf(const char *fmt, ...);

#define LP_PROPDEF_END 48 /* PROPDEF_END (bondconstants.h) */

/* the header word is {u16 scale; u8 state; u8 type}: the type is the top byte of the word */
static u32 lpType(const u32 *rec)
{
    return ((const u8 *) rec)[3];
}

/** Size of one record of `type`, in 32-bit words, in the wire (host == 0) or host layout; 0 if the
 *  type is not one the retail setups use. */
u32 portSetupPropdefWords(u32 type, s32 host)
{
    if (type >= LP_SETUP_TYPE_COUNT)
    {
        return 0;
    }

    return host ? kLpSetupTypes[type].hostWords : kLpSetupTypes[type].wireWords;
}

/** Expand `wire` (records up to and including PROPDEF_END) into a freshly allocated host-layout
 *  array. The previous expansion is released. Returns NULL when `wire` is NULL. */
u32 *portSetupExpandPropdefs(const u32 *wire)
{
    static u32 *s_Host;
    u32 hostWords = 0;
    u32 wireWords = 0;
    u32 *out;

    if (wire == NULL)
    {
        return NULL;
    }

    for (;;)
    {
        u32 type = lpType(wire + wireWords);
        const struct LpSetupType *t;

        if (type >= LP_SETUP_TYPE_COUNT)
        {
            portConLogf("ge: lp64 setup: unknown propdef type %u at word %u - stopped\n",
                        (unsigned) type, (unsigned) wireWords);
            type = LP_PROPDEF_END;
            hostWords += 2;
            break;
        }

        t = &kLpSetupTypes[type];
        wireWords += t->wireWords;
        hostWords += t->hostWords;

        if (type == LP_PROPDEF_END)
        {
            break;
        }
    }

    free(s_Host);
    s_Host = (u32 *) calloc(hostWords + 2, sizeof(u32));

    if (s_Host == NULL)
    {
        portConLogf("ge: lp64 setup: out of memory expanding %u prop words\n", (unsigned) hostWords);
        return NULL;
    }

    out = s_Host;
    wireWords = 0;
    hostWords = 0;

    for (;;)
    {
        u32 type = lpType(wire + wireWords);
        const struct LpSetupType *t;
        const u8 *src;
        u8 *dst;
        u32 i;

        if (type >= LP_SETUP_TYPE_COUNT)
        {
            ((u8 *) (out + hostWords))[3] = LP_PROPDEF_END;
            break;
        }

        t = &kLpSetupTypes[type];
        src = (const u8 *) (wire + wireWords);
        dst = (u8 *) (out + hostWords);

        for (i = 0; i < t->numOps; i++)
        {
            const struct LpSetupOp *op = &kLpSetupOps[t->firstOp + i];

            if (op->ptr)
            {
                u32 v;
                u64 widened;

                memcpy(&v, src + op->wire, 4);
                widened = v;
                memcpy(dst + op->host, &widened, 8);
            }
            else
            {
                memcpy(dst + op->host, src + op->wire, op->len);
            }
        }

        wireWords += t->wireWords;
        hostWords += t->hostWords;

        if (type == LP_PROPDEF_END)
        {
            break;
        }
    }

    return out;
}

#endif /* PORT_LP64 */

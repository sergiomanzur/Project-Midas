#include <ultra64.h>
#include <ramrom.h>
#include <memp.h>
#include "image_bank.h"

#ifndef TARGET_N64
#include <stdlib.h> /* texReset pristine-bank snapshot */
#include <string.h>
#endif

// bss
//8008D0A0
u8* img_curpos;
//8008D0A4
u32 img_curdatatable;
//8008D0A8
s32 img_bitcount;
//8008D0AC
s32 dword_CODE_bss_8008D0AC;
//8008D0B0;
s32 globalbank_rdram_offset;
//8008D0B4;
s32 *pGlobalimagetable;
//8008D0B8;
struct sImageTableEntry *genericimage;
//8008D0BC
struct sImageTableEntry *impactimages;
//8008D0C0
struct sImageTableEntry *explosion_smokeimages;
//8008D0C4
struct sImageTableEntry *scattered_explosions;
//8008D0C8
struct sImageTableEntry *flareimage1;
//8008D0CC
struct sImageTableEntry *flareimage2;
//8008D0D0
struct sImageTableEntry *flareimage3;
//8008D0D4
struct sImageTableEntry *flareimage4;
//8008D0D8
struct sImageTableEntry *flareimage5;
//8008D0DC
struct sImageTableEntry *ammo9mmimage;
//8008D0E0
struct sImageTableEntry *rifleammoimage;
//8008D0E4
struct sImageTableEntry *shotgunammoimage;
//8008D0E8
struct sImageTableEntry *knifeammoimage;
//8008D0EC
struct sImageTableEntry *glaunchammoimage;
//8008D0F0
struct sImageTableEntry *rocketammoimage;
//8008D0F4
struct sImageTableEntry *genericmineammoimage;
//8008D0F8
struct sImageTableEntry *grenadeammoimage;
//8008D0FC
struct sImageTableEntry *magnumammoimage;
//8008D100
struct sImageTableEntry *goldengunammoimage;
//8008D104
struct sImageTableEntry *remotemineammoimage;
//8008D108
struct sImageTableEntry *timedmineammoimage;
//8008D10C
struct sImageTableEntry *proxmineammoimage;
//8008D110
struct sImageTableEntry *tankammoimage;
//8008D114;
struct sImageTableEntry *crosshairimage;
//8008D118
struct sImageTableEntry *betacrosshairimage;
//8008D11C
struct sImageTableEntry *glassoverlayimage;
//8008D120
struct sImageTableEntry *monitorimages;
//8008D124
struct sImageTableEntry *skywaterimages;
//8008D128
struct sImageTableEntry *mainfolderimages;
//8008D12C
struct sImageTableEntry *mpradarimages;
//8008D130
struct sImageTableEntry *mpcharselimages;
//8008D134
struct sImageTableEntry *mpstageselimages;


extern u8* _GlobalimagetableSegmentRomStart;


void texSetBitstring(uintptr_t pos) {
    img_curpos = (u8 *) pos;
    img_curdatatable = 0;
    img_bitcount = 0;
}



u32 texReadBits(s32 bitCount)
{
    if (img_bitcount < bitCount)
    {
        do
        {
            img_curdatatable = (*img_curpos | (img_curdatatable << 8));
            img_curpos++;
            img_bitcount = img_bitcount + 8;
        } while (img_bitcount < bitCount);
    }
    
    img_bitcount -= bitCount;
    return (img_curdatatable >> img_bitcount) & ((1 << bitCount) - 1);
}



extern u32* _GlobalimagetableSegmentStart;
extern u32* _GlobalimagetableSegmentEnd;
extern void* s_genericimage;
extern void* s_impactimages;
extern void* s_explosion_smokeimages;
extern void* s_scattered_explosions;
extern void* s_flareimage1;
extern void* s_flareimage2;
extern void* s_flareimage3;
extern void* s_flareimage4;
extern void* s_flareimage5;
extern void* s_ammo9mmimage;
extern void* s_rifleammoimage;
extern void* s_shotgunammoimage;
extern void* s_knifeammoimage;
extern void* s_glammoimage;
extern void* s_rocketammoimage;
extern void* s_genericmineammoimage;
extern void* s_grenadeammoimage;
extern void* s_magnumammoimage;
extern void* s_goldengunammoimage;
extern void* s_remotemineammoimage;
extern void* s_timedmineammoimage;
extern void* s_proxmineammoimage;
extern void* s_tankammoimage;
extern void* s_crosshairimage;
extern void* s_betacrosshairimage;
extern void* s_glassoverlayimage;
extern void* s_monitorimages;
extern void* s_skywaterimages;
extern void* s_mainfolderimages;
extern void* s_mpradarimages;
extern void* s_mpcharselimages;
extern void* s_mpstageselimages;

extern Gfx* globalDL_0x000;
extern Gfx* globalDL_0x078;
extern Gfx* globalDL_0x120;
extern Gfx* globalDL_0x1c8;
extern Gfx* globalDL_0x270;
extern Gfx* globalDL_0x318;
extern Gfx* globalDL_0x3c0;
extern Gfx* globalDL_0x468;
extern Gfx* globalDL_0x510;
extern Gfx* globalDL_0x5b8;
extern Gfx* globalDL_0x660;
extern Gfx* globalDL_0x708;
extern Gfx* globalDL_0x7b0;
extern Gfx* globalDL_0x858;
extern Gfx* globalDL_0x900;
extern Gfx* globalDL_0x9a8;
extern Gfx* globalDL_0xa50;

void texReset(void)
{
    u32 size;
    s32 i;

#ifndef TARGET_N64
    /* The PC texture bank is compiled in and texLoad/texSelect overwrite its ids
     * with pointers in place, so snapshot the pristine bytes on first call and
     * restore them on every later stage load (otherwise the second load reads
     * pointers as ids and crashes). The bounds rely on oddtextures.c being one
     * TU built -fno-toplevel-reorder, contiguous from s_mpstageselimages to
     * globalDL_0x000 (+0x78). */
    {
        extern void *s_mpstageselimages;
        extern Gfx *globalDL_0x000;
        static u8 *pristine;
        u8 *p1 = (u8 *) &s_mpstageselimages;
        u8 *p2 = (u8 *) &globalDL_0x000 + 0x78;
        u8 *lo = (p1 < p2) ? p1 : (u8 *) &globalDL_0x000;
        u8 *hi = (p1 < p2) ? p2 : (u8 *) &s_mpstageselimages + 0x78;
        size_t sz = (size_t) (hi - lo);

        if (hi > lo && sz < 0x200000) {
            if (pristine == NULL) {
                pristine = malloc(sz);
                if (pristine != NULL) {
                    memcpy(pristine, lo, sz);
                }
            } else {
                memcpy(lo, pristine, sz);
            }
        }
    }

    /* Stage boundary = the moment pool addresses start getting recycled
     * with new pixels; fast3d's address-keyed texture cache must drop its
     * entries or stale GL textures surface (post-mission menu cursor drew the
     * COPY icon). See gfx_texture_cache_clear_c. */
    {
        extern void gfx_texture_cache_clear_c(void);
        gfx_texture_cache_clear_c();
    }
#endif

    size = (u32) ((uintptr_t) &_GlobalimagetableSegmentEnd - (uintptr_t) &_GlobalimagetableSegmentStart);
    pGlobalimagetable = mempAllocBytesInBank(size + 0x1000, MEMPOOL_STAGE);
    pGlobalimagetable = (s32 *) (((uintptr_t) pGlobalimagetable + 0xFFFu) & ~(uintptr_t) 0xFFFu);

    romCopy(pGlobalimagetable, &_GlobalimagetableSegmentRomStart, size);

#ifdef TARGET_N64
    globalbank_rdram_offset = (u32)pGlobalimagetable + 0xFE000000;
#else
    /* PORT: oddtextures.c is compiled in, so &s_*image and &globalDL_* below
     * are already real addresses -- no segment rebase. Keep them pointer-width:
     * a (u32) cast truncates on a 64-bit host and the dereference dies. */
    globalbank_rdram_offset = 0;
#endif
#define PORT_IMG_ADDR(sym) ((void *) (uintptr_t) ((uintptr_t) &(sym) + (uintptr_t) globalbank_rdram_offset))
#define PORT_DL_ADDR(sym)  ((Gfx *) (uintptr_t) ((uintptr_t) &(sym) + (uintptr_t) globalbank_rdram_offset))
    genericimage = PORT_IMG_ADDR(s_genericimage);
    impactimages = PORT_IMG_ADDR(s_impactimages);
    explosion_smokeimages = PORT_IMG_ADDR(s_explosion_smokeimages);
    scattered_explosions = PORT_IMG_ADDR(s_scattered_explosions);
    flareimage1 = PORT_IMG_ADDR(s_flareimage1);
    flareimage2 = PORT_IMG_ADDR(s_flareimage2);
    flareimage3 = PORT_IMG_ADDR(s_flareimage3);
    flareimage4 = PORT_IMG_ADDR(s_flareimage4);
    flareimage5 = PORT_IMG_ADDR(s_flareimage5);
    ammo9mmimage = PORT_IMG_ADDR(s_ammo9mmimage);
    rifleammoimage = PORT_IMG_ADDR(s_rifleammoimage);
    shotgunammoimage = PORT_IMG_ADDR(s_shotgunammoimage);
    knifeammoimage = PORT_IMG_ADDR(s_knifeammoimage);
    glaunchammoimage = PORT_IMG_ADDR(s_glammoimage);
    rocketammoimage = PORT_IMG_ADDR(s_rocketammoimage);
    genericmineammoimage = PORT_IMG_ADDR(s_genericmineammoimage);
    grenadeammoimage = PORT_IMG_ADDR(s_grenadeammoimage);
    magnumammoimage = PORT_IMG_ADDR(s_magnumammoimage);
    goldengunammoimage = PORT_IMG_ADDR(s_goldengunammoimage);
    remotemineammoimage = PORT_IMG_ADDR(s_remotemineammoimage);
    timedmineammoimage = PORT_IMG_ADDR(s_timedmineammoimage);
    proxmineammoimage = PORT_IMG_ADDR(s_proxmineammoimage);
    tankammoimage = PORT_IMG_ADDR(s_tankammoimage);
    crosshairimage = PORT_IMG_ADDR(s_crosshairimage);
    betacrosshairimage = PORT_IMG_ADDR(s_betacrosshairimage);
    glassoverlayimage = PORT_IMG_ADDR(s_glassoverlayimage);
    monitorimages = PORT_IMG_ADDR(s_monitorimages);
    skywaterimages = PORT_IMG_ADDR(s_skywaterimages);
    mainfolderimages = PORT_IMG_ADDR(s_mainfolderimages);
    mpradarimages = PORT_IMG_ADDR(s_mpradarimages);
    mpcharselimages = PORT_IMG_ADDR(s_mpcharselimages);
    mpstageselimages = PORT_IMG_ADDR(s_mpstageselimages);

    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x000), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x078), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x120), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x1c8), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x270), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x318), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x3c0), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x468), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x510), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x5b8), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x660), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x708), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x7b0), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x858), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x900), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0x9a8), 0);
    texLoadFromDisplayList(PORT_DL_ADDR(globalDL_0xa50), 0);

    texLoad(genericimage, 0);

    for (i=0; i < 6; i++)
    {
        texLoad(&explosion_smokeimages[i], 0);
    }

    for (i=0; i < 5; i++)
    {
        texLoad(&scattered_explosions[i], 0);
    }
}

#ifndef _INTRO_LOGOS_H_
#define _INTRO_LOGOS_H_
#include <ultra64.h>

extern s32 barrelDisplayListPtr;
extern Gfx *gunbarrelgfxListPointer;
extern Mtx *matrixBufferRareLogo0;
extern Mtx *matrixBufferGunbarrel0;
extern Mtx *matrixBufferRareLogo1;
extern Mtx *matrixBufferRareLogo2;
extern Mtx *matrixBufferGunbarrel1;
extern Mtx *matrixBufferIntroBackdrop;
extern Mtx *matrixBufferIntroBond;
extern f32 x;
extern f32 y;
extern f32 titleTransitionX;
extern f32 titleTransitionY;
extern s16 word_CODE_bss_80069584;
extern s32 dword_CODE_bss_80069588;
extern s32 dword_CODE_bss_8006958C;
extern uintptr_t virtualaddress;
extern s32 gunbarrelTimer;

extern u32 D_8002A7D0;

/* PORT: prototypes matter on 64-bit hosts - an implicit declaration would
 * pass these pointer arguments through int and truncate them. */
void setupRarewareLogoData(void *address, s32 size);
void initializeGunBarrelIntro(u8 *gfxBuffer, s32 bufferSize);

Gfx *titleRenderFolderMenuBackground(Gfx *gdl, s32 xOffset, struct FolderSelectColour *topColour, struct FolderSelectColour *bottomColour);
#endif

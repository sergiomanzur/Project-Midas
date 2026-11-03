#ifndef GBIEX_COMPAT_H
#define GBIEX_COMPAT_H

/*
 * PD-fast3d extension opcodes/bits that GE's gbi.h does not define.
 * Values are verbatim from perfect_dark src/include/gbiex.h (and PD's
 * PR/gbi.h for G_TRI4/G_RDPHALF_CONT). The EXT band 0x21-0x45 does not
 * collide with anything GE emits ({01,03,04,06} u [B1,C0] u [E4,FF] -
 * docs/port/ge-microcode.md), so the interpreter keeps these features.
 *
 * These are only ever emitted by port-side code (video.c / future EXT
 * users), never by game display lists.
 */

/* PD PR/gbi.h immediates (GE's plain-F3D gbi.h lacks both) */
#ifndef G_TRI4
#define G_TRI4                       0xb1 /* (G_IMMFIRST-14); GE emits it, decode is bit-identical */
#endif
#ifndef G_RDPHALF_CONT
#define G_RDPHALF_CONT               0xb2 /* (G_IMMFIRST-13); never emitted by GE */
#endif

/* PD gbiex.h EXT opcode band */
#define G_SETFB_EXT                  0x21
#define G_SETTIMG_FB_EXT             0x23
#define G_INVALTEXCACHE_EXT          0x34
#define G_TEXRECT_WIDE_EXT           0x37
#define G_FILLRECT_WIDE_EXT          0x38
#define G_SETGRAYSCALE_EXT           0x39
#define G_EXTRAGEOMETRYMODE_EXT      0x3a
#define G_SETINTENSITY_EXT           0x40
#define G_COPYFB_EXT                 0x41
#define G_IMAGERECT_EXT              0x42
#define G_RDPFLUSH_EXT               0x43
#define G_CLEAR_DEPTH_EXT            0x44
#define G_SETSUBPIXELOFFSET_EXT      0x45
#define G_DXLU_NEWVIEW_EXT           0x46 /* GE: player world render begins */
#define G_DXLU_WORLDEND_EXT          0x47 /* GE: world done - flush sorted XLU before the gun */
#define G_MIRRORPILLAR_EXT           0x48 /* GE: w1!=0 - mirror this overlay into the wide side bands */
#define G_DXLU_GROUP_EXT             0x49 /* GE: w1=1 begin sort-group (keyed at mean depth), w1=2 begin (drawn last), w1=3 begin room unit (mode 1; off unless GE_XLU_ROOMUNITS=1), w1=0 end */
#define G_CONSOLE2D_EXT              0x4c /* GE: w1 = w<<16 | h - the console's own 2D space (full window width, no 4:3 pin, window resolution); w1 = 0 ends it. 0x4b is the Coop branch's G_SUBSCREEN_EXT */
#define G_PORTNEARCLIP_EXT           0x4d /* GE: w1=1 - draw what follows with GL depth clamp off (first-person gun: Adreno turns near-plane-straddling polys into screen-sized triangles under clamp); w1=0 restores */
#define G_PORTCACHE_EXT              0x4a /* GE: the NEXT G_DL is a room list (w1 = 1<<16 | room) or a model part list (w1 = 2<<16 | vertex segment) for the room / part cache (roc_hooks.c) */

/* PD gbiex.h extra-geometry-mode bits (rsp.extra_geometry_mode) */
#define G_ASPECT_LEFT_EXT            0x00000010
#define G_ASPECT_RIGHT_EXT           0x00000020
#define G_ASPECT_WIDE_EXT            0x00000040
#define G_ASPECT_CENTER_EXT          (G_ASPECT_LEFT_EXT | G_ASPECT_RIGHT_EXT)
#define G_ASPECT_MODE_EXT            (G_ASPECT_CENTER_EXT | G_ASPECT_WIDE_EXT)
#define G_NO_CLIPPING_EXT            0x00000100
#define G_MODULATE_EXT               0x00000200

/* PD gbiex.h texture filter extension (othermode_h TEXTFILT band) */
#define G_TF_BLUR_EXT                (1 << G_MDSFT_TEXTFILT)

#endif /* GBIEX_COMPAT_H */

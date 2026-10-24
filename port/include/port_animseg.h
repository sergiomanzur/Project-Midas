#ifndef PORT_ANIMSEG_H
#define PORT_ANIMSEG_H

#include <ultra64.h>

/*
 * The two animation segments are allocated in the low host arena by
 * port/src/animdata.c instead of living in .bss.
 *
 * The game stores their addresses in 32-bit fields: expand_ani_table_entries
 * (initanitable.c) adds &_animation_entriesSegmentRomStart into s32 animation
 * data, and the streamer later romCopy's from the result. A .bss array is high
 * on a 64-bit host, and the truncated/sign-extended address is a wild pointer
 * (on arm64 this crashed in alloc_load_expand_ani_table). The low arena keeps
 * the whole segment below the 2 GB line, where the 32-bit store is exact.
 */
extern u8 *port_animation_dataSegmentRomStart;
extern u8 *port_animation_entriesSegmentRomStart;

#endif /* PORT_ANIMSEG_H */

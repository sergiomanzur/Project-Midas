#ifndef PORT_MODEL64_H
#define PORT_MODEL64_H

#include <ultra64.h>
#include <bondtypes.h>

/*
 * Convert a ROM model image into a host pointer graph (64-bit hosts only).
 *
 * The model file holds 24-byte ModelNode records and rodata records whose
 * "pointers" are 32-bit 0x05000000-based offsets; the game's own structs use
 * real pointers, so on a 64-bit host the wire image cannot be walked in
 * place. This rebuilds every reachable node and its rodata as host structs
 * (allocated from MEMPOOL_STAGE, so a stage reset frees them), points the
 * links at the host copies, and leaves vertex/DL/texture data in the wire
 * buffer (BaseAddr, Vertices, Primary, ... point into it).
 *
 * `hdr` must already carry numSwitches/numtextures/RootNode as the vanilla
 * loader set them; on return RootNode and Switches point at host nodes.
 */
void portModelToHost(struct ModelFileHeader *hdr, u8 *base, u32 size, const char *name);

/* Image base of the most recently converted model (for the DL converter). */
u8 *portModelLastBase(void);

#endif /* PORT_MODEL64_H */

#ifndef PORT_WIREDL_H
#define PORT_WIREDL_H

#include <ultra64.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register an address range whose display lists are 8-byte N64 commands
 * (the ROM data segment, a model image, ...). */
void portWireDLRegister(const void *lo, const void *hi);

/* Drop every registered range that intersects [lo, hi) - call when a buffer
 * is repurposed (a model area reused as gunbarrel/logo scratch). */
void portWireDLUnregister(const void *lo, const void *hi);

/* Is this pointer inside a registered wire-format range? */
int portIsWireDL(const void *p);

/* Expand a wire list into a cached host Gfx array (NULL on failure). */
Gfx *portWireDLExpand(const void *p);

#ifdef __cplusplus
}
#endif

#endif /* PORT_WIREDL_H */

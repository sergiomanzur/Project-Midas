#include <ultra64.h>
#include "chrobjdata.h"

void init_player_gait_object(void) {
  /* PORT: this was `(int)&player_gait_hdr`, which truncates the pointer on a
   * 64-bit host and made every Bond model walk fault. */
  player_gait_object_header.RootNode = (ModelNode *)&player_gait_hdr;
  return;
}


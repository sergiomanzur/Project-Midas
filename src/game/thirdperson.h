#ifndef _GAME_THIRDPERSON_H_
#define _GAME_THIRDPERSON_H_

#include <ultra64.h>
#include <bondtypes.h>

/* Third-person camera (midas.ini [display] camera_view, set in the Gameplay menu).
 * 0 First Person, 1 Close, 2 Shoulder, 3 Far. The gameplay camera is moved back from the
 * first-person eye along the view, so the crosshair at the centre of the screen keeps pointing at
 * what a shot from the eye hits. */
s32 thirdPersonActive(void);
void thirdPersonSpPrepareCinematic(void);
void thirdPersonSpTick(void);
void thirdPersonAdjustCamera(coord3d *pos, coord3d *look, coord3d *up);

#endif

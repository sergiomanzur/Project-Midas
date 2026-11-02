#ifndef _GAME_MPRULES_H_
#define _GAME_MPRULES_H_

#include <ultra64.h>
#include <bondconstants.h>
#include "front.h"
#include "player.h"

/* Which rule set a match plays by is a property of the game mode, not of how
 * many human viewports there are: one human against Simulants is multiplayer.
 * In stock play gamemode == GAMEMODE_MULTI always means two or more humans, so
 * these read exactly like the count checks they replace. getPlayerCount() stays
 * the right test for anything about viewports (split-screen layout). */
#define MP_RULES() (getPlayerCount() >= 2 || gamemode == GAMEMODE_MULTI)
#define SP_RULES() (getPlayerCount() == 1 && gamemode != GAMEMODE_MULTI)

#endif

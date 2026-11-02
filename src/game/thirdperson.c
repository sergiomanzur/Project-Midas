#include <ultra64.h>
#include <bondtypes.h>
#include <math.h>
#include "bondview.h"
#include "player.h"
#include "stan.h"
#include "chr.h"
#include "front.h"
#include "thirdperson.h"

extern s32 portCameraView(void);
extern void sub_GAME_7F09B398(enum GUNHAND hand);

/* distance behind the eye, shift to the right, lift (world units; Bond is about 180 tall) per view */
static const struct
{
    f32 boom;
    f32 side;
    f32 lift;
} tpViews[] = {
    {0.0f, 0.0f, 0.0f},     /* 0 First Person (unused) */
    {170.0f, 0.0f, 12.0f},  /* 1 Close */
    {200.0f, 28.0f, 8.0f},  /* 2 Shoulder */
    {330.0f, 0.0f, 20.0f},  /* 3 Far */
};

/* Single player: the third-person body exists only in gameplay. Solo normally drops Bond's body
 * after the intro (first person has none), so it is created here and retired around cutscenes,
 * which expect the stock disposable body. */
static s32 g_tpSpBodyLive;

static s32 tpIsSolo(void)
{
    return getPlayerCount() == 1 && gamemode != GAMEMODE_MULTI;
}

s32 thirdPersonActive(void)
{
    if (portCameraView() <= 0)
    {
        return 0;
    }

    if (!tpIsSolo())
    {
        return 1; /* multiplayer players always have a body */
    }

    return g_tpSpBodyLive && g_CurrentPlayer != NULL && g_CurrentPlayer->prop != NULL
        && g_CurrentPlayer->prop->chr != NULL && g_CurrentPlayer->bodyModel != NULL;
}

/* A cutscene is starting: give back the TP body so solo_char_load builds the stock one. */
void thirdPersonSpPrepareCinematic(void)
{
    if (!g_tpSpBodyLive)
    {
        return;
    }

    g_tpSpBodyLive = 0;

    if (g_CurrentPlayer->prop->chr != NULL)
    {
        chrpropCleanupForThirdPersonToggle(g_CurrentPlayer->prop);
        g_CurrentPlayer->prop->chr = NULL;
        g_CurrentPlayer->bodyModel = NULL;
        bondviewUpdatePlayerRoom(g_CurrentPlayer);
    }
}

/* Once per gameplay tick: raise the solo body when the camera option is on, drop it when off. */
void thirdPersonSpTick(void)
{
    if (!tpIsSolo() || g_CameraMode != CAMERAMODE_FP || g_CurrentPlayer->bonddead)
    {
        return;
    }

    if (portCameraView() > 0)
    {
        if (!g_tpSpBodyLive && g_CurrentPlayer->prop->chr == NULL)
        {
            solo_char_load(); /* loads like an MP body while the camera is first-person gameplay */
            g_tpSpBodyLive = g_CurrentPlayer->prop->chr != NULL;

            if (g_tpSpBodyLive)
            {
                /* put the weapons Bond is holding into the body's hands */
                sub_GAME_7F09B398(GUNRIGHT);
                sub_GAME_7F09B398(GUNLEFT);
            }

            bondviewUpdatePlayerRoom(g_CurrentPlayer);
        }
    }
    else if (g_tpSpBodyLive)
    {
        g_tpSpBodyLive = 0;

        if (g_CurrentPlayer->prop->chr != NULL)
        {
            chrpropCleanupForThirdPersonToggle(g_CurrentPlayer->prop);
            g_CurrentPlayer->prop->chr = NULL;
            g_CurrentPlayer->bodyModel = NULL;
            g_bondviewForceDisarm = 1;
            bondviewUpdatePlayerRoom(g_CurrentPlayer);
        }
    }
}

static f32 tpLen(const coord3d *v)
{
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

void thirdPersonAdjustCamera(coord3d *pos, coord3d *look, coord3d *up)
{
    const s32 view = portCameraView();
    coord3d fwd;
    coord3d upn;
    coord3d right;
    coord3d eye;
    coord3d delta;
    StandTile *tile;
    f32 len;
    f32 lo;
    f32 hi;
    f32 floory;
    s32 i;

    if (view <= 0 || view > 3)
    {
        return;
    }

    len = tpLen(look);
    if (len < 0.0001f)
    {
        return;
    }

    fwd.x = look->x / len;
    fwd.y = look->y / len;
    fwd.z = look->z / len;

    len = tpLen(up);
    if (len < 0.0001f)
    {
        return;
    }

    upn.x = up->x / len;
    upn.y = up->y / len;
    upn.z = up->z / len;

    /* right = fwd x up */
    right.x = fwd.y * upn.z - fwd.z * upn.y;
    right.y = fwd.z * upn.x - fwd.x * upn.z;
    right.z = fwd.x * upn.y - fwd.y * upn.x;
    len = tpLen(&right);
    if (len > 0.0001f)
    {
        right.x /= len;
        right.y /= len;
        right.z /= len;
    }

    eye = *pos;
    delta.x = -fwd.x * tpViews[view].boom + right.x * tpViews[view].side + upn.x * tpViews[view].lift;
    delta.y = -fwd.y * tpViews[view].boom + right.y * tpViews[view].side + upn.y * tpViews[view].lift;
    delta.z = -fwd.z * tpViews[view].boom + right.z * tpViews[view].side + upn.z * tpViews[view].lift;

    /* Pull the camera in until the floor plan lets it reach the eye without leaving the level (a
     * wall behind the player). */
    tile = g_CurrentPlayer->field_488.current_tile_ptr;
    lo = 0.0f;
    hi = 1.0f;

    if (tile != NULL)
    {
        StandTile *probe = tile;

        if (walkTilesBetweenPoints_NoCallback(&probe, eye.x, eye.z, eye.x + delta.x, eye.z + delta.z) == 0)
        {
            for (i = 0; i < 7; i++)
            {
                f32 mid = (lo + hi) * 0.5f;

                probe = tile;

                if (walkTilesBetweenPoints_NoCallback(&probe, eye.x, eye.z, eye.x + delta.x * mid, eye.z + delta.z * mid) != 0)
                {
                    lo = mid;
                }
                else
                {
                    hi = mid;
                }
            }
        }
        else
        {
            lo = 1.0f;
        }
    }
    else
    {
        lo = 1.0f;
    }

    /* stay clear of the wall it stopped at */
    if (lo < 1.0f)
    {
        lo *= 0.9f;
    }

    pos->x = eye.x + delta.x * lo;
    pos->y = eye.y + delta.y * lo;
    pos->z = eye.z + delta.z * lo;

    floory = g_CurrentPlayer->field_70 + 18.0f;
    if (pos->y < floory)
    {
        pos->y = floory;
    }
}

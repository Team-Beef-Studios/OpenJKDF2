#include "stdVR_Scope.h"

#ifdef PLATFORM_VR

#include "stdVR.h"
#include "stdVR_Types.h"
#include "stdVR_WeaponWheel.h"
#include "stdVR_Map3D.h"
#include "stdVR_TwoHanded.h"
#include "stdPlatform.h"
#include "General/stdMath.h"
#include "Primitives/rdVector.h"
#include "Engine/sithCamera.h"
#include "Gameplay/sithInventory.h"
#include "Gameplay/sithPlayer.h"
#include "Cog/sithCogExec.h"
#include "Platform/std3D.h"
#include "World/jkPlayer.h"
#include "Main/Main.h"
#include "jk.h"

#include <math.h>

// JKXR: enter under g_vrScopeEngageDist (0.25), leave over 0.26.
#define STDVR_SCOPE_RELEASE_MARGIN (0.01f)

// JKXR: the off-hand sits below the line of sight, so aim at a point this far above it.
#define STDVR_SCOPE_OFFHAND_LIFT (0.12f)

#define STDVR_SCOPE_ZOOM_RAMP_SECS (0.15f)
#define STDVR_SCOPE_STICK_THRESHOLD (0.7f)
#define STDVR_SCOPE_MOVE_SCALE (0.33f)
#define STDVR_SCOPE_MASK_DIAMETER (0.9f)

// The cog activates these goggles with IR mode of their own; never strip IR from them.
#define STDVR_SCOPE_BIN_IR_GOGGLES_A (41)
#define STDVR_SCOPE_BIN_IR_GOGGLES_B (23)

static int stdVR_scopeEngaged = 0;
static rdVector3 stdVR_scopeAimDir;
static float stdVR_scopeZoomBlend = 0.0f;
static uint32_t stdVR_scopeLastViewMs = 0;
static rdMatrix34 stdVR_scopeWorldView;
static int stdVR_scopeWorldViewValid = 0;

void stdVR_Scope_Reset(void)
{
    stdVR_scopeEngaged = 0;
    stdVR_scopeZoomBlend = 0.0f;
    stdVR_scopeWorldViewValid = 0;
    stdPalEffects_vrSuppressedRequest = -1;
}

int stdVR_Scope_IsWeaponSelected(void)
{
    if (!Main_bMotsCompat || !stdVR_motionConfig.bMotionAimEnabled) return 0;
    if (!sithPlayer_pLocalPlayer || !sithPlayer_pLocalPlayerThing) return 0;
    if (sithPlayer_pLocalPlayerThing->actorParams.health <= 0.0) return 0;
    return sithPlayer_pLocalPlayer->curWeapon == SITHBIN_MOTS_STORMTROOPER_SCOPE;
}

int stdVR_Scope_IsEngaged(void)
{
    return stdVR_scopeEngaged;
}

int stdVR_Scope_ShouldHideEffects(void)
{
    if (!stdVR_Scope_IsWeaponSelected() || stdVR_scopeEngaged) return 0;
    if (sithInventory_GetActivate(sithPlayer_pLocalPlayerThing, STDVR_SCOPE_BIN_IR_GOGGLES_A)
        || sithInventory_GetActivate(sithPlayer_pLocalPlayerThing, STDVR_SCOPE_BIN_IR_GOGGLES_B)) {
        return 0;
    }
    return 1;
}

// The scope cog's red view is newColorEffect(1,0,0, 0,0,0, 0,0,0, 1). It is the only request
// with that exact shape, so it can be found without hooking the cog.
static int stdVR_Scope_FindColorEffect(void)
{
    for (int i = 0; i < 32; i++) {
        stdPalEffectRequest* pReq = &stdPalEffects_aEffects[i];
        if (!pReq->isValid) continue;
        if (pReq->effect.filter.x && !pReq->effect.filter.y && !pReq->effect.filter.z
            && pReq->effect.tint.x == 0.0 && pReq->effect.tint.y == 0.0 && pReq->effect.tint.z == 0.0
            && !pReq->effect.add.x && !pReq->effect.add.y && !pReq->effect.add.z) {
            return i;
        }
    }
    return -1;
}

static void stdVR_Scope_SetEngaged(int bEngaged)
{
    if (stdVR_scopeEngaged == bEngaged) return;
    stdVR_scopeEngaged = bEngaged;

    int dominant = stdVR_GetDominantHand();
    stdVR_TriggerHaptic(dominant, 0.25f, 0.05f, 120.0f);
    stdVR_TriggerHaptic(1 - dominant, 0.25f, 0.05f, 120.0f);

    if (bEngaged) {
        // Start from where the head looks so the scoped view eases onto the barrel.
        rdVector_Copy3(&stdVR_scopeAimDir, &stdVR_clientInfo.hmdPoseMatrix.lvec);
        stdVR_scopeZoomBlend = 0.0f;
        stdVR_scopeLastViewMs = stdPlatform_GetTimeMsec();
    }
    else {
        stdVR_scopeWorldViewValid = 0;
    }
}

void stdVR_Scope_UpdateInput(void)
{
    if (!stdVR_bEnabled || !stdVR_IsSessionRunning() || !stdVR_Scope_IsWeaponSelected()) {
        stdVR_Scope_SetEngaged(0);
        stdPalEffects_vrSuppressedRequest = -1;
        return;
    }

    stdVR_ControllerState* pDom = stdVR_GetDominantController();
    stdVR_ControllerState* pOff = stdVR_GetOffhandController();
    int bTracking = pDom && pOff && pDom->bTracking && pOff->bTracking;
    int bBlocked = stdVR_WeaponWheel_IsActive() || stdVR_Map3D_IsVisible();

    int bWant = 0;
    if (stdVR_TwoHanded_IsStabilised() && bTracking && !bBlocked) {
        rdVector3 toEye;
        rdVector_Sub3(&toEye, &pDom->position, &stdVR_clientInfo.hmdPosition);
        float threshold = jkPlayer_vrScopeEngageDist;
        if (stdVR_scopeEngaged) threshold += STDVR_SCOPE_RELEASE_MARGIN;
        bWant = rdVector_Len3(&toEye) < threshold;
    }
    stdVR_Scope_SetEngaged(bWant);

    stdPalEffects_vrSuppressedRequest = stdVR_scopeEngaged ? -1 : stdVR_Scope_FindColorEffect();
}

void stdVR_Scope_UpdateView(void)
{
    if (!stdVR_scopeEngaged) return;

    uint32_t now = stdPlatform_GetTimeMsec();
    float dt = (float)(now - stdVR_scopeLastViewMs) * 0.001f;
    stdVR_scopeLastViewMs = now;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;

    // JKXR: aim from the eye through a point just above the (averaged) supporting hand.
    rdVector3 target;
    stdVR_TwoHanded_GetOffhandAverage(&target);
    target.z += STDVR_SCOPE_OFFHAND_LIFT;
    rdVector_Sub3(&target, &target, &stdVR_clientInfo.hmdPosition);
    if (rdVector_Normalize3Acc(&target) < 0.0001f) return;

    // Frame-rate independent exponential smoothing; the smooth factor is "per 72Hz frame".
    float smooth = stdMath_Clamp(jkPlayer_vrScopeAimSmooth, 0.0f, 0.95f);
    float follow = 1.0f - powf(smooth, dt * 72.0f);
    rdVector3 delta;
    rdVector_Sub3(&delta, &target, &stdVR_scopeAimDir);
    rdVector_MultAcc3(&stdVR_scopeAimDir, &delta, follow);
    rdVector_Normalize3Acc(&stdVR_scopeAimDir);

    stdVR_scopeZoomBlend += dt / STDVR_SCOPE_ZOOM_RAMP_SECS;
    if (stdVR_scopeZoomBlend > 1.0f) stdVR_scopeZoomBlend = 1.0f;
}

float stdVR_Scope_GetZoom(void)
{
    if (!stdVR_scopeEngaged) return 1.0f;

    // zoomFov holds the cog's current (animated) magnification.
    float cogZoom = sithCamera_cameras[0].zoomFov;
    if (cogZoom < 1.0f) cogZoom = 1.0f;
    return 1.0f + (cogZoom - 1.0f) * stdVR_scopeZoomBlend;
}

int stdVR_Scope_GetLocalRotation(rdMatrix34* pOut)
{
    if (!stdVR_scopeEngaged || !pOut) return 0;

    // Yaw/pitch from the barrel, roll from the head, so tilting the rifle does not roll the world.
    rdVector3 l, r, u;
    rdVector_Copy3(&l, &stdVR_scopeAimDir);
    rdVector_Copy3(&r, &stdVR_clientInfo.hmdPoseMatrix.rvec);
    rdVector_MultAcc3(&r, &l, -rdVector_Dot3(&r, &l));
    if (rdVector_Normalize3Acc(&r) < 0.0001f) {
        rdVector3 up = {0.0f, 0.0f, 1.0f};
        rdVector_Cross3(&r, &l, &up);
        if (rdVector_Normalize3Acc(&r) < 0.0001f) return 0;
    }
    rdVector_Cross3(&u, &r, &l);

    rdMatrix_Identity34(pOut);
    rdVector_Copy3(&pOut->rvec, &r);
    rdVector_Copy3(&pOut->lvec, &l);
    rdVector_Copy3(&pOut->uvec, &u);
    return 1;
}

void stdVR_Scope_SetWorldView(const rdMatrix34* pView)
{
    rdMatrix_Copy34(&stdVR_scopeWorldView, pView);
    stdVR_scopeWorldViewValid = 1;
}

int stdVR_Scope_GetWorldView(rdMatrix34* pOut)
{
    if (!stdVR_scopeEngaged || !stdVR_scopeWorldViewValid || !pOut) return 0;
    rdMatrix_Copy34(pOut, &stdVR_scopeWorldView);
    return 1;
}

// The cog's manual mode captures jump (bit 0) and crouch (bit 1) to step the zoom.
int stdVR_Scope_IsZoomStickActive(void)
{
    return stdVR_scopeEngaged && sithCog_pActionCog && (sithCog_actionCogIdk & 0x3) == 0x3;
}

int stdVR_Scope_IsZoomInHeld(void)
{
    return stdVR_Scope_IsZoomStickActive() && stdVR_clientInfo.analogTurn[1] > STDVR_SCOPE_STICK_THRESHOLD;
}

int stdVR_Scope_IsZoomOutHeld(void)
{
    return stdVR_Scope_IsZoomStickActive() && stdVR_clientInfo.analogTurn[1] < -STDVR_SCOPE_STICK_THRESHOLD;
}

float stdVR_Scope_GetMoveScale(void)
{
    return stdVR_scopeEngaged ? STDVR_SCOPE_MOVE_SCALE : 1.0f;
}

void stdVR_Scope_DrawMask(int fboWidth, int fboHeight)
{
    if (!stdVR_scopeEngaged) return;

    // Straight ahead sits off the buffer's vertical centre when the frustum has more FOV below
    // than above. The HUD offset only corrects horizontally, so place the eyepiece here.
    float centreNdcY = 0.0f;
    for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
        float up = stdVR_clientInfo.eyes[eye].fovUp;
        float down = stdVR_clientInfo.eyes[eye].fovDown;
        if (up + down > 0.0001f) centreNdcY += (down - up) / (up + down);
    }
    centreNdcY /= (float)STDVR_EYE_COUNT;

    // The scoped image is mono, so put the eyepiece at infinity: no convergence between eyes.
    stdVR_SetHudOffsetForDepth(1000.0f);
    std3D_DrawVRScopeMask(fboWidth, fboHeight, STDVR_SCOPE_MASK_DIAMETER, centreNdcY);
    stdVR_SetHudOffsetForDepth(jkPlayer_vrHudDepth);
}

#endif // PLATFORM_VR

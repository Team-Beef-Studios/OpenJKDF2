#include "stdVR_TwoHanded.h"

#ifdef PLATFORM_VR

#include "stdVR.h"
#include "stdVR_Types.h"
#include "stdVR_WeaponWheel.h"
#include "stdVR_Map3D.h"
#include "General/stdMath.h"
#include "Primitives/rdVector.h"
#include "Primitives/rdMatrix.h"
#include "Gameplay/sithPlayer.h"
#include "jk.h"

#include <math.h>

// JKXR: the grab only takes when the off-hand sits along the barrel, within ~53 degrees.
#define STDVR_TWOHANDED_GRAB_DOT (0.6f)

#define STDVR_TWOHANDED_AVG_SAMPLES (5)

static int stdVR_twoHandedStabilised = 0;
static int stdVR_twoHandedWeapon = -1;
static rdVector3 stdVR_aTwoHandedOffhandHistory[STDVR_TWOHANDED_AVG_SAMPLES];
static int stdVR_twoHandedHistoryCount = 0;

void stdVR_TwoHanded_Reset(void)
{
    stdVR_twoHandedStabilised = 0;
    stdVR_twoHandedWeapon = -1;
    stdVR_twoHandedHistoryCount = 0;
}

int stdVR_TwoHanded_IsWeaponTwoHanded(int binIdx)
{
    switch (binIdx)
    {
        case SITHBIN_STORMTROOPER_RIFLE:
        case SITHBIN_TUSKEN_PROD:
        case SITHBIN_REPEATER:
        case SITHBIN_RAIL_DETONATOR:
        case SITHBIN_CONCUSSION_RIFLE:
        case SITHBIN_MOTS_STORMTROOPER_RIFLE:
        case SITHBIN_MOTS_REPEATER:
        case SITHBIN_MOTS_RAIL_DETONATOR:
        case SITHBIN_MOTS_CONCUSSION_RIFLE:
        case SITHBIN_MOTS_STORMTROOPER_SCOPE:
        case SITHBIN_MOTS_TUSKEN_PROD:
        case SITHBIN_MOTS_RAIL_SEEKER:
        case SITHBIN_MOTS_CARBO_GUN:
            return 1;
        default:
            return 0;
    }
}

static int stdVR_TwoHanded_GetCurWeapon(void)
{
    if (!sithPlayer_pLocalPlayer || !sithPlayer_pLocalPlayerThing) return -1;
    if (sithPlayer_pLocalPlayerThing->actorParams.health <= 0.0) return -1;
    return sithPlayer_pLocalPlayer->curWeapon;
}

// The direction a shot leaves the weapon hand, in tracking space - the same pitch adjustment as
// stdVR_GetControllerWorldMatrix.
static void stdVR_TwoHanded_GetAimForward(const rdMatrix34* pPose, rdVector3* pOut)
{
    rdMatrix34 pitchRot, aim;
    rdVector3 pitchAngles = { -90.0f + stdVR_motionConfig.weaponPitchAdjust, 0.0f, 0.0f };
    rdMatrix_BuildRotate34(&pitchRot, &pitchAngles);
    rdMatrix_Multiply34(&aim, pPose, &pitchRot);
    rdVector_Copy3(pOut, &aim.lvec);
    rdVector_Normalize3Acc(pOut);
}

void stdVR_TwoHanded_UpdateInput(void)
{
    int weap = stdVR_TwoHanded_GetCurWeapon();
    if (!stdVR_bEnabled || !stdVR_IsSessionRunning() || !stdVR_motionConfig.bMotionAimEnabled
        || !stdVR_TwoHanded_IsWeaponTwoHanded(weap) || weap != stdVR_twoHandedWeapon) {
        stdVR_twoHandedStabilised = 0;
    }
    stdVR_twoHandedWeapon = weap;
    if (!stdVR_TwoHanded_IsWeaponTwoHanded(weap)) return;

    int dominant = stdVR_GetDominantHand();
    int offhand = 1 - dominant;
    uint32_t btnGripOffhand = (offhand == STDVR_CONTROLLER_RIGHT) ? STDVR_BTN_GRIP_R : STDVR_BTN_GRIP_L;

    if (!(stdVR_clientInfo.buttonState & btnGripOffhand)) {
        stdVR_twoHandedStabilised = 0;
        return;
    }

    // Decided once, on the press: a grip that went down anywhere else belongs to the wheel.
    if (!(stdVR_clientInfo.buttonPressed & btnGripOffhand)) return;
    if (stdVR_WeaponWheel_IsActive() || stdVR_Map3D_IsVisible()) return;

    stdVR_ControllerState* pDom = stdVR_GetController(dominant);
    stdVR_ControllerState* pOff = stdVR_GetController(offhand);
    if (!pDom || !pOff || !pDom->bTracking || !pOff->bTracking) return;

    rdVector3 forward, toOffhand;
    stdVR_TwoHanded_GetAimForward(&pDom->poseMatrix, &forward);
    rdVector_Sub3(&toOffhand, &pOff->position, &pDom->position);
    if (rdVector_Normalize3Acc(&toOffhand) < 0.0001f) return;

    if (rdVector_Dot3(&forward, &toOffhand) > STDVR_TWOHANDED_GRAB_DOT) {
        stdVR_twoHandedStabilised = 1;
        stdVR_twoHandedHistoryCount = 0;
        stdVR_TriggerHaptic(offhand, 0.3f, 0.05f, 120.0f);
    }
}

// Rotates v about the unit axis by the angle whose cosine/sine are given (Rodrigues).
static void stdVR_TwoHanded_Rotate(rdVector3* v, const rdVector3* axis, float c, float s)
{
    rdVector3 cross, out;
    rdVector_Cross3(&cross, axis, v);
    float d = rdVector_Dot3(axis, v);
    out.x = v->x * c + cross.x * s + axis->x * d * (1.0f - c);
    out.y = v->y * c + cross.y * s + axis->y * d * (1.0f - c);
    out.z = v->z * c + cross.z * s + axis->z * d * (1.0f - c);
    *v = out;
}

void stdVR_TwoHanded_ApplyToPose(void)
{
    if (!stdVR_twoHandedStabilised) return;

    stdVR_ControllerState* pDom = stdVR_GetDominantController();
    stdVR_ControllerState* pOff = stdVR_GetOffhandController();
    if (!pDom || !pOff || !pDom->bTracking || !pOff->bTracking) return;

    for (int i = STDVR_TWOHANDED_AVG_SAMPLES - 1; i > 0; i--) {
        stdVR_aTwoHandedOffhandHistory[i] = stdVR_aTwoHandedOffhandHistory[i - 1];
    }
    stdVR_aTwoHandedOffhandHistory[0] = pOff->position;
    if (stdVR_twoHandedHistoryCount < STDVR_TWOHANDED_AVG_SAMPLES) stdVR_twoHandedHistoryCount++;

    // Smallest rotation that swings the aim onto the off-hand. It keeps the wrist's own roll,
    // so the gun does not twist while the barrel follows the supporting hand.
    rdVector3 from, to, axis;
    stdVR_TwoHanded_GetAimForward(&pDom->poseMatrix, &from);
    rdVector_Sub3(&to, &pOff->position, &pDom->position);
    if (rdVector_Normalize3Acc(&to) < 0.0001f) return;

    rdVector_Cross3(&axis, &from, &to);
    float s = rdVector_Normalize3Acc(&axis);
    float c = rdVector_Dot3(&from, &to);
    if (s < 0.0001f) return;

    stdVR_TwoHanded_Rotate(&pDom->poseMatrix.rvec, &axis, c, s);
    stdVR_TwoHanded_Rotate(&pDom->poseMatrix.lvec, &axis, c, s);
    stdVR_TwoHanded_Rotate(&pDom->poseMatrix.uvec, &axis, c, s);
}

int stdVR_TwoHanded_IsStabilised(void)
{
    return stdVR_twoHandedStabilised;
}

int stdVR_TwoHanded_OwnsOffhandGrip(void)
{
    return stdVR_twoHandedStabilised;
}

void stdVR_TwoHanded_GetOffhandAverage(rdVector3* pOut)
{
    stdVR_ControllerState* pOff = stdVR_GetOffhandController();
    if (!stdVR_twoHandedHistoryCount) {
        if (pOff) *pOut = pOff->position;
        return;
    }

    rdVector_Zero3(pOut);
    for (int i = 0; i < stdVR_twoHandedHistoryCount; i++) {
        rdVector_Add3Acc(pOut, &stdVR_aTwoHandedOffhandHistory[i]);
    }
    rdVector_Scale3Acc(pOut, 1.0f / (float)stdVR_twoHandedHistoryCount);
}

#endif // PLATFORM_VR

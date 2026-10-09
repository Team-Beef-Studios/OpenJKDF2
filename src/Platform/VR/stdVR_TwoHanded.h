#ifndef STDVR_TWOHANDED_H
#define STDVR_TWOHANDED_H

// Added: two-handed long guns, as in JKXR. Pressing the off-hand grip with that hand in front of
// the weapon hand grabs the weapon: it then aims from the weapon hand towards the off-hand, and
// the off-hand model locks onto the barrel. Any other off-hand grip press still opens the wheel.

#include "types.h"

#ifdef PLATFORM_VR

#ifdef __cplusplus
extern "C" {
#endif

void stdVR_TwoHanded_Reset(void);

int stdVR_TwoHanded_IsWeaponTwoHanded(int binIdx);

// Input tick. Must run before stdVR_WeaponWheel_Update so a grab never opens the wheel.
void stdVR_TwoHanded_UpdateInput(void);

// Right after the controller poses are located: turns the weapon hand's pose to point at the
// off-hand, so firing, the weapon model and the laser all follow without further changes.
void stdVR_TwoHanded_ApplyToPose(void);

int stdVR_TwoHanded_IsStabilised(void);
int stdVR_TwoHanded_OwnsOffhandGrip(void);

// Off-hand position (tracking space) averaged over the last few frames.
void stdVR_TwoHanded_GetOffhandAverage(rdVector3* pOut);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_VR

#endif // STDVR_TWOHANDED_H

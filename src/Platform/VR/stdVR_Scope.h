#ifndef STDVR_SCOPE_H
#define STDVR_SCOPE_H

// Added: VR handling for the MotS Rifle Scope (SITHBIN_MOTS_STORMTROOPER_SCOPE). The original
// weapon is a full-screen zoom with no POV model; in VR the rifle is grabbed with both hands
// (stdVR_TwoHanded) and the zoom only starts when the scope is raised to the eye.

#include "types.h"
#include "Primitives/rdMatrix.h"

#ifdef PLATFORM_VR

#ifdef __cplusplus
extern "C" {
#endif

void stdVR_Scope_Reset(void);

// Input tick, after stdVR_TwoHanded_UpdateInput.
void stdVR_Scope_UpdateInput(void);

// Render tick, before stdVR_SetMultiViewMatrices: smooths the aim and ramps the zoom.
void stdVR_Scope_UpdateView(void);

int stdVR_Scope_IsWeaponSelected(void);
int stdVR_Scope_IsEngaged(void);

// Cog effects (red filter, IR mode) are only shown while the scope is at the eye.
int stdVR_Scope_ShouldHideEffects(void);

// Current magnification applied to the eye projection (1.0 when not engaged).
float stdVR_Scope_GetZoom(void);

// Tracking-space rotation of the scoped view (scale zeroed). Returns 0 when not engaged.
int stdVR_Scope_GetLocalRotation(rdMatrix34* pOut);

// World-space scoped view (rotation + eye position), captured when the frame's camera is built.
// Used to fire and to aim the cog's auto-zoom probe along what the player sees.
void stdVR_Scope_SetWorldView(const rdMatrix34* pView);
int stdVR_Scope_GetWorldView(rdMatrix34* pOut);

// While engaged in the cog's manual zoom mode, the turn stick steps the zoom instead of crouching.
int stdVR_Scope_IsZoomStickActive(void);
int stdVR_Scope_IsZoomInHeld(void);
int stdVR_Scope_IsZoomOutHeld(void);

float stdVR_Scope_GetMoveScale(void);

void stdVR_Scope_DrawMask(int fboWidth, int fboHeight);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_VR

#endif // STDVR_SCOPE_H

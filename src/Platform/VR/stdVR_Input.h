#ifndef _STDVR_INPUT_H
#define _STDVR_INPUT_H

// Added: VR controller input mapping header

#ifdef PLATFORM_VR

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Map VR controller input to game actions
void stdVR_Input_MapToGame(void);

// Snap turn handling
void stdVR_Input_ProcessSnapTurn(void);

// Get movement direction based on config
void stdVR_Input_GetMovementDirection(float* pMoveX, float* pMoveY);
// Stick-click button masks, following the swap-thumbsticks option
uint32_t stdVR_Input_GetMoveStickButton(void);
uint32_t stdVR_Input_GetTurnStickButton(void);

// Get snap turn angle (returns 0 if no snap turn pending, otherwise the angle)
int stdVR_Input_GetSnapTurnAngle(void);

// Get smooth turn speed in degrees/second
float stdVR_Input_GetSmoothTurnSpeed(void);

// Button state queries
int stdVR_Input_IsButtonDown(uint32_t button);
int stdVR_Input_IsButtonPressed(uint32_t button);
int stdVR_Input_IsButtonReleased(uint32_t button);

// Analog input queries
float stdVR_Input_GetTrigger(int hand);
float stdVR_Input_GetGrip(int hand);

// Menu button state (short press = menu/escape, long press = recenter)
int stdVR_Input_IsMenuPressed(void);

// Run toggle (off-hand thumbstick click to toggle)
int stdVR_Input_IsRunToggled(void);
void stdVR_Input_ResetRunToggle(void);

// Weapon switching via dominant hand thumbstick up/down flick
int stdVR_Input_IsNextWeaponTriggered(void);
int stdVR_Input_IsPrevWeaponTriggered(void);

// Crouch toggle via right thumbstick down
int stdVR_Input_IsCrouchToggled(void);
// Added: turn stick held up/down while in water (swim up/down)
int stdVR_Input_IsSwimUpHeld(void);
int stdVR_Input_IsSwimDownHeld(void);
void stdVR_Input_ResetCrouchToggle(void);

// Melee punch: dominant hand thrust forward (fists are punch-activated, not trigger-fired)
int stdVR_Input_IsPunchActive(void);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_VR

#endif // _STDVR_INPUT_H

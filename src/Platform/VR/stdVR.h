#ifndef _STDVR_H
#define _STDVR_H

// Added: VR/OpenXR support public interface

#include "types.h"
#include "Platform/VR/stdVR_Types.h"
#include "Platform/VR/stdVR_WeaponOffsets.h"
#include "Platform/VR/stdVR_AlignmentTool.h"
#include "Platform/VR/stdVR_Map3D.h"
#include "Platform/VR/stdVR_WeaponWheel.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PLATFORM_VR

// Global VR state
extern int stdVR_bEnabled;              // Is VR enabled
extern int stdVR_bInitted;              // Is VR module initialized
extern stdVR_ClientInfo stdVR_clientInfo;
extern stdVR_Config stdVR_config;
extern stdVR_MotionConfig stdVR_motionConfig;

// Lifecycle functions
int stdVR_Startup(void);
void stdVR_Shutdown(void);

// Session management
int stdVR_CreateSession(void* pGLContext);
void stdVR_DestroySession(void);
int stdVR_IsSessionRunning(void);
int stdVR_IsExitRequested(void);        // Runtime asked the app to quit (universal menu)
flex_t stdVR_GetEyeOffsetWorld(void);   // Half the eye separation, in game units (0 when VR is off)

// Face-button roles. Lower pair (A/X) follows the movement stick, upper pair (B/Y) the weapon
// hand. Use these rather than the STDVR_BTN_* constants directly - see stdVR.c for why.
uint32_t stdVR_GetJumpButton(void);
uint32_t stdVR_GetActivateButton(void);
uint32_t stdVR_GetAltFireButton(void);
uint32_t stdVR_GetMenuButton(void);
int stdVR_GetButtonHand(uint32_t btn);
void stdVR_PollEvents(void);            // Poll OpenXR events (call every frame)

// Frame timing (match OpenXR frame cadence)
// Call order: WaitFrame -> UpdateTracking -> BeginFrame -> [render] -> EndFrame
int stdVR_WaitFrame(void);              // xrWaitFrame - blocks until optimal render time
int stdVR_BeginFrame(void);             // xrBeginFrame - start frame for rendering
int stdVR_EndFrame(void);               // xrEndFrame - submit rendered frame to compositor
void stdVR_SubmitEmptyFrame(void);      // Submit frame with no layers (for menus/loading)
int stdVR_IsFramePending(void);         // Check if WaitFrame called but EndFrame not yet
void stdVR_KeepAlive(void);             // Call during loading to prevent headset blackout

// Per-eye rendering
int stdVR_PrepareEyeBuffer(int eye);    // Acquire and bind eye swapchain image
int stdVR_FinishEyeBuffer(int eye);     // Release eye swapchain image
int stdVR_GetCurrentEyeFBO(int eye);    // Get the FBO for the current eye (0 if not active)
int stdVR_GetCurrentEye(void);          // Get which eye is currently being rendered (-1 if none)

// MultiView rendering (single-pass stereo for Quest VR)
int stdVR_IsMultiViewSupported(void);   // Check if MultiView is available and enabled
int stdVR_PrepareMultiViewBuffer(void); // Acquire and bind MultiView swapchain (both eyes)
int stdVR_FinishMultiViewBuffer(void);  // Release MultiView swapchain
int stdVR_GetMultiViewFBO(void);        // Get the MultiView FBO (0 if not active)
void stdVR_SetMultiViewMatrices(float zNear, float zFar);  // Upload both eye view/proj matrices to UBOs
void stdVR_SetHudOffsetForDepth(float depthMeters);        // Set baked-HUD per-eye shift for a virtual depth

// Desktop mirror (debug aid): blit the last-rendered VR buffer to the SDL window.
// Caller does the SDL_GL_SwapWindow afterwards. See stdVR_mirrorFlip in stdVR_OpenXR.h.
void stdVR_MirrorToWindow(int windowWidth, int windowHeight);

// Tracking
void stdVR_UpdateTracking(void);        // Update HMD and controller poses
void stdVR_GetHMDPose(rdVector3* pPosition, rdVector3* pOrientation);
void stdVR_GetControllerPose(int hand, rdVector3* pPosition, rdVector3* pOrientation);

// View matrices
void stdVR_GetEyeViewMatrix(int eye, rdMatrix34* pOut);
void stdVR_GetEyeProjection(int eye, float* pOut16);
void stdVR_GetEyeProjectionMatrix44(int eye, float* pOut16, float zNear, float zFar);

// Input
void stdVR_UpdateInput(void);           // Poll controller input state
void stdVR_MapInputToGame(void);        // Map VR input to game actions

// Haptics
void stdVR_TriggerHaptic(int hand, float amplitude, float duration, float frequency);
void stdVR_StopHaptic(int hand);

// On-screen instructional prompts. These go through the engine's message log (jkDev), so
// they queue, expire and get baked into the VR HUD alongside the game's own messages.
// Prompt ids index a per-profile "already shown" bitmask, so a prompt taught once stays
// taught. Keep the list under 32 entries. Renumbering is still free right now (nothing has
// shipped, so no profiles exist in the wild) but stops being so at the first public release -
// from then on the mask is saved to the player profile, and reordering would re-show or wrongly
// suppress prompts on existing saves. Retire ids in place rather than renumbering after that.
enum
{
    STDVR_PROMPT_SABER_SWING = 0,   // equipped the lightsaber
    STDVR_PROMPT_FISTS_PUNCH = 1,   // equipped the fists
    STDVR_PROMPT_CROUCH      = 2,   // gameplay start
    STDVR_PROMPT_STAND       = 3,   // once crouched
    STDVR_PROMPT_RUN         = 4,   // once stood back up
    STDVR_PROMPT_WEAPON_WHEEL= 5,   // once run has been toggled
    STDVR_PROMPT_FORCE_WHEEL = 6,   // first force power acquired
    STDVR_PROMPT_FORCE_USE   = 7,   // once the force wheel has been opened
    STDVR_PROMPT_ALT_FIRE    = 8,   // equipped a weapon that has a secondary fire
    STDVR_PROMPT_JUMP        = 9,   // basics tail, after the intro chain
    STDVR_PROMPT_ACTIVATE    = 10,
    STDVR_PROMPT_HOLOMAP     = 11,
    STDVR_PROMPT_ITEMS       = 12,  // usable inventory item, once the intro chain has finished
    STDVR_PROMPT_DEATH_LOAD  = 13,  // first death
    STDVR_PROMPT_HOLOMAP_GRAB= 14,  // holomap opened for the first time
    STDVR_PROMPT_HOLOMAP_CLOSE= 15, // follows the grab prompt, while the map is still open
    STDVR_PROMPT_COUNT              // <= 32
};

#define STDVR_PROMPT_DWELL_MS (9000)

void stdVR_ShowPrompt(const wchar_t* pText);
void stdVR_ShowPromptOnce(int promptId, const wchar_t* pText);
int stdVR_WasPromptShown(int promptId);
void stdVR_ResetPromptsShown(void);
// Write the taught-prompt bitmask to the profile. Only safe outside gameplay - see the note
// on the implementation.
void stdVR_FlushPromptsShown(void);

// Display refresh rate. The runtime must support XR_FB_display_refresh_rate. Quest supports
// it. When stdVR_GetRefreshRateCount() returns 0 the feature is absent and the VR Options
// menu hides the setting.
int stdVR_GetRefreshRateCount(void);
float stdVR_GetRefreshRateByIndex(int idx);
float stdVR_GetCurrentRefreshRate(void);
int stdVR_ApplyRefreshRate(float hz);   // Returns 1 if the runtime accepted the rate

// Utility
void stdVR_RecenterView(void);
// 6DoF head-driven body movement: per-frame horizontal head delta (tracking space, metres).
void stdVR_ConsumeHeadDelta(float* outDx, float* outDy);
void stdVR_ResetHeadDelta(void);
void stdVR_GetRecommendedRenderSize(int* pWidth, int* pHeight);
const char* stdVR_GetRuntimeName(void);

// Screen layer mode (for menus/cinematics - renders as 2D quad in 3D space)
int stdVR_UseScreenLayer(void);         // Check if screen layer should be used
void stdVR_SetScreenLayerMode(int bEnable);  // Enable/disable screen layer mode
void stdVR_UpdateScreenLayerSnap(void); // Update snap position when entering screen mode
float stdVR_GetScreenLayerDistance(void);    // Get screen distance from player

// VR Menu cursor (for interacting with menus via controller pointing)
void stdVR_UpdateMenuCursor(void);      // Update cursor position from controller angles
void stdVR_GetMenuCursorPos(int* pX, int* pY);  // Get cursor screen position
int stdVR_IsMenuCursorActive(void);     // Is cursor active (screen layer mode)
int stdVR_GetMenuTriggerPressed(void);  // Was trigger pressed this frame
int stdVR_GetMenuTriggerReleased(void); // Was trigger released this frame

// Helper function to combine game camera with VR eye offset
void stdVR_CombineCameraWithEye(const rdMatrix34* pGameCamera, int eye, rdMatrix34* pOut);

// Helper function to combine game camera with HMD center pose (for MultiView)
void stdVR_CombineCameraWithHMD(const rdMatrix34* pGameCamera, rdMatrix34* pOut);

// Added: Get/set current eye view matrix (for weapon rendering in VR)
void stdVR_SetCurrentEyeViewMatrix(const rdMatrix34* pMat);
int stdVR_GetCurrentEyeViewMatrix(rdMatrix34* pOut);
void stdVR_ClearCurrentEyeViewMatrix(void);

// Motion controls helper functions
int stdVR_GetDominantHand(void);
stdVR_ControllerState* stdVR_GetController(int hand);
stdVR_ControllerState* stdVR_GetDominantController(void);
stdVR_ControllerState* stdVR_GetOffhandController(void);
void stdVR_ControllerToWorld(int hand, rdVector3* pWorldPos, int useOffsets);
void stdVR_GetControllerAimDirection(int hand, rdVector3* pDirection);
void stdVR_GetControllerWorldMatrix(int hand, rdMatrix34* pMatrix);
int stdVR_GetSaberWorldMatrix(int hand, rdMatrix34* pMatrix);
int stdVR_IsSwingTriggered(void);
float stdVR_GetSwingSpeed(void);
int stdVR_GetControllerViewMatrix(int hand, rdMatrix34* pViewMat);
int stdVR_GetControllerViewMatrixRaw(int hand, rdMatrix34* pViewMat);

// In-world weapon crosshair (laser dot at aim point)
void stdVR_DrawWeaponCrosshair(void);

// Debug visualization
void stdVR_DrawDebugControllerAxes(int hand);

// Alignment aid: draw the controller axes as world rays. Forward is green and shows the
// direction a projectile takes. Lengths are in game units.
#define STDVR_AXIS_RAY_FORWARD_LEN (0.60f)
#define STDVR_AXIS_RAY_SIDE_LEN    (0.12f)
void stdVR_DrawControllerAxisRays(int hand);

// Settings sync functions (to/from jkPlayer settings)
void stdVR_SyncConfigFromJkPlayer(void);
void stdVR_SyncConfigToJkPlayer(void);

#else // !PLATFORM_VR

// Stub implementations when VR is not compiled in
#define stdVR_bEnabled 0
#define stdVR_bInitted 0

static inline int stdVR_Startup(void) { return 0; }
static inline void stdVR_Shutdown(void) {}
static inline int stdVR_CreateSession(void* ctx) { (void)ctx; return 0; }
static inline void stdVR_DestroySession(void) {}
static inline int stdVR_IsSessionRunning(void) { return 0; }
static inline int stdVR_IsExitRequested(void) { return 0; }
static inline flex_t stdVR_GetEyeOffsetWorld(void) { return 0.0; }
static inline uint32_t stdVR_GetJumpButton(void) { return 0; }
static inline uint32_t stdVR_GetActivateButton(void) { return 0; }
static inline uint32_t stdVR_GetAltFireButton(void) { return 0; }
static inline uint32_t stdVR_GetMenuButton(void) { return 0; }
static inline int stdVR_GetButtonHand(uint32_t btn) { (void)btn; return 0; }
static inline void stdVR_PollEvents(void) {}
static inline int stdVR_WaitFrame(void) { return 0; }
static inline int stdVR_BeginFrame(void) { return 0; }
static inline int stdVR_EndFrame(void) { return 0; }
static inline int stdVR_PrepareEyeBuffer(int eye) { (void)eye; return 0; }
static inline int stdVR_FinishEyeBuffer(int eye) { (void)eye; return 0; }
static inline int stdVR_GetCurrentEyeFBO(int eye) { (void)eye; return 0; }
static inline int stdVR_GetCurrentEye(void) { return -1; }
static inline int stdVR_IsMultiViewSupported(void) { return 0; }
static inline int stdVR_PrepareMultiViewBuffer(void) { return 0; }
static inline int stdVR_FinishMultiViewBuffer(void) { return 0; }
static inline int stdVR_GetMultiViewFBO(void) { return 0; }
static inline void stdVR_SetMultiViewMatrices(float zNear, float zFar) { (void)zNear; (void)zFar; }
static inline void stdVR_SetHudOffsetForDepth(float depthMeters) { (void)depthMeters; }
static inline void stdVR_UpdateTracking(void) {}
static inline void stdVR_UpdateInput(void) {}
static inline void stdVR_MapInputToGame(void) {}
static inline void stdVR_KeepAlive(void) {}
static inline void stdVR_SetCurrentEyeViewMatrix(const void* pMat) { (void)pMat; }
static inline int stdVR_GetCurrentEyeViewMatrix(void* pOut) { (void)pOut; return 0; }
static inline void stdVR_ClearCurrentEyeViewMatrix(void) {}

#endif // PLATFORM_VR

#ifdef __cplusplus
}
#endif

#endif // _STDVR_H

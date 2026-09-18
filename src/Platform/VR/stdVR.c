#include "stdVR.h"

// Added: VR support implementation

#ifdef PLATFORM_VR

#include "Platform/VR/stdVR_OpenXR.h"
#include "Platform/VR/stdVR_Input.h"
#include "Platform/VR/stdVR_WeaponOffsets.h"
#include "Platform/VR/stdVR_AlignmentTool.h"
#include "Primitives/rdVector.h"
#include "General/stdMath.h"
#include "Primitives/rdMatrix.h"
#include "Primitives/rdPrimit3.h"
#include "Platform/std3D.h"
#include "Engine/sithCollision.h"
#include "World/sithThing.h"
#include "World/sithTemplate.h"
#include "Main/jkMain.h"
#include "Main/jkDev.h"
#include "World/jkPlayer.h"
#include "stdPlatform.h"

#include <string.h>
#include <math.h>

// For debug drawing
#include "SDL2_helper.h"

// Global VR state
int stdVR_bEnabled = 0;
int stdVR_bInitted = 0;
stdVR_ClientInfo stdVR_clientInfo;
stdVR_Config stdVR_config;
stdVR_MotionConfig stdVR_motionConfig;

// Debug mode for shader testing (0=normal, 1=solid, 2=UV, 3=depth, 4=vertex color)
int std3D_vrDebugMode = 0;

// Weapon crosshair
static sithThing* stdVR_pCrosshairThing = NULL;

// Frame state tracking
static int stdVR_bPendingScreenLayerResnap = 0;  // Re-place the menu after a recenter
static int stdVR_bFramePending = 0;  // WaitFrame called but EndFrame not yet
static int stdVR_bFrameInProgress = 0;  // BeginFrame called but EndFrame not yet

// Added: Current combined camera+eye view matrix (for weapon rendering)
static rdMatrix34 stdVR_currentEyeViewMat;
static int stdVR_currentEyeViewMatValid = 0;

// Added: Store game camera separately (without VR applied) for controller matrix calculation
static rdMatrix34 stdVR_currentGameCamera;
static int stdVR_currentGameCameraValid = 0;

// Default configuration
static void stdVR_InitDefaultConfig(void)
{
    memset(&stdVR_config, 0, sizeof(stdVR_config));

    stdVR_config.moveDirection = STDVR_MOVE_CONTROLLER;
    stdVR_config.turnMode = STDVR_TURN_SNAP;
    stdVR_config.snapTurnAngle = 45;
    stdVR_config.smoothTurnSpeed = 120.0f;
    stdVR_config.walkSpeedScale = 1.0f;
    stdVR_config.sixDoFScale = 1.0f;
    stdVR_config.worldScale = 0.09f;  // IPD/roomscale multiplier - reduced for less physical movement
    stdVR_config.heightOffset = 0.0f;
    stdVR_config.fixedHeightAdjustment = 0.11f;
    stdVR_config.dominantHand = STDVR_CONTROLLER_RIGHT;
    stdVR_config.bSwapSticks = 0;
    stdVR_config.supersampling = 1.0f;

    // Initialize motion controls config
    memset(&stdVR_motionConfig, 0, sizeof(stdVR_motionConfig));

    stdVR_motionConfig.weaponVelocityTrigger = 2.0f;    // m/s for melee attack
    stdVR_motionConfig.saberVelocityTrigger = 2.5f;     // m/s for saber attack
    stdVR_motionConfig.forceVelocityTrigger = 1.5f;     // m/s for force gesture
    stdVR_motionConfig.forceDistanceTrigger = 0.3f;     // meters for push/pull
    stdVR_motionConfig.weaponPitchAdjust = 0.0f;      // degrees - rotate weapon to point forward
    stdVR_motionConfig.saberPitchAdjust = 0.0f;       // degrees
    stdVR_motionConfig.weaponOffsetX = -0.3f;            // meters - left/right
    stdVR_motionConfig.weaponOffsetY = 0.1f;           // meters - forward (negative = back toward player)
    stdVR_motionConfig.weaponOffsetZ = -0.15f;           // meters - up/down (positive = up)
    stdVR_motionConfig.weaponModelScale = 0.8f;         // VR weapon model size multiplier
    stdVR_motionConfig.fireOffsetX = 0.3f;              // meters - fire position right offset (~1 foot)
    stdVR_motionConfig.fireOffsetY = 0.0f;              // meters - fire position forward offset (~1 foot)
    stdVR_motionConfig.fireOffsetZ = -0.3f;             // meters - fire position up offset (~6 inches)
    stdVR_motionConfig.bMotionAimEnabled = 1;           // Enable controller aiming by default
    stdVR_motionConfig.bMotionSaberEnabled = 1;         // Enable swing-to-attack
    stdVR_motionConfig.bMotionForceEnabled = 0;         // Disable force gestures for now
    stdVR_motionConfig.bTwoHandedEnabled = 0;           // Disable two-handed for now
    stdVR_motionConfig.bDisablePovAnims = 1;            // Disable weapon recoil animations by default
    stdVR_motionConfig.positionSmoothingSamples = 3;
    stdVR_motionConfig.velocitySmoothingFactor = 0.5f;
}

int stdVR_Startup(void)
{
    if (stdVR_bInitted) {
        return 1;
    }

    stdPlatform_Printf("stdVR: Starting up VR subsystem...\n");

    memset(&stdVR_clientInfo, 0, sizeof(stdVR_clientInfo));
    stdVR_InitDefaultConfig();

    // Initialize OpenXR
    if (!stdVR_OpenXR_Init()) {
        stdPlatform_Printf("stdVR: Failed to initialize OpenXR\n");
        return 0;
    }

    stdVR_bInitted = 1;

    // Sync settings from jkPlayer (except enabled state - we handle that separately)
    stdVR_SyncConfigFromJkPlayer();

    // Initialize per-weapon offset system
    stdVR_WeaponOffsets_Startup();

    // Initialize 3D map system
    stdVR_Map3D_Startup();

#ifdef VR_WEAPON_ALIGNMENT_TOOL
    // Initialize alignment tool
    stdVR_AlignmentTool_Startup();
#endif

    // VR should be enabled by default when OpenXR initializes successfully
    // Only disable if the user explicitly set jkPlayer_vrEnabled = 0 in config
    // For now, always enable since we successfully initialized
    stdVR_bEnabled = 1;

    stdPlatform_Printf("stdVR: VR subsystem initialized successfully\n");
    stdPlatform_Printf("stdVR: Runtime: %s\n", stdVR_GetRuntimeName());
    stdPlatform_Printf("stdVR: VR enabled: %d\n", stdVR_bEnabled);

    return 1;
}

void stdVR_Shutdown(void)
{
    if (!stdVR_bInitted) {
        return;
    }

    stdPlatform_Printf("stdVR: Shutting down VR subsystem...\n");

    // Shutdown 3D map system
    stdVR_Map3D_Shutdown();

    // Shutdown per-weapon offset system
    stdVR_WeaponOffsets_Shutdown();

    stdVR_DestroySession();
    stdVR_OpenXR_Shutdown();

    stdVR_bEnabled = 0;
    stdVR_bInitted = 0;

    stdPlatform_Printf("stdVR: VR subsystem shutdown complete\n");
}

int stdVR_CreateSession(void* pGLContext)
{
    if (!stdVR_bInitted) {
        return 0;
    }

    int result = stdVR_OpenXR_CreateSession(pGLContext);

    // Auto-recenter view when session starts so player isn't offset from character
    if (result) {
        stdPlatform_Printf("stdVR: Session created, recentering view...\n");
        stdVR_RecenterView();
    }

    return result;
}

void stdVR_DestroySession(void)
{
    if (!stdVR_bInitted) {
        return;
    }

    stdVR_OpenXR_DestroySession();
    stdVR_bFramePending = 0;
    stdVR_bFrameInProgress = 0;
}

int stdVR_IsSessionRunning(void)
{
    return stdVR_clientInfo.bSessionRunning;
}

// Added: lateral distance an eye sits from the centre view, in GAME units. The CPU culls
// surfaces and adjoins from the centre point, but MultiView renders from eyes offset either
// side of it, so a plane the centre is just behind can still be visible to one eye. Callers
// widen their facing tests by this much.
flex_t stdVR_GetEyeOffsetWorld(void)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return 0.0;
    }

    float dx = stdVR_clientInfo.eyes[1].viewMatrix.scale.x - stdVR_clientInfo.eyes[0].viewMatrix.scale.x;
    float dy = stdVR_clientInfo.eyes[1].viewMatrix.scale.y - stdVR_clientInfo.eyes[0].viewMatrix.scale.y;
    float dz = stdVR_clientInfo.eyes[1].viewMatrix.scale.z - stdVR_clientInfo.eyes[0].viewMatrix.scale.z;
    float ipd = sqrtf(dx * dx + dy * dy + dz * dz);

    float worldScale = stdVR_config.worldScale;
    if (worldScale <= 0.0f) worldScale = 1.0f;

    // The raw half-IPD is the exact geometric bound, but worldScale shrinks it to almost
    // nothing in game units, so add the tunable slack from engine_config.h on top.
    return (flex_t)(ipd * 0.5f * worldScale) + (flex_t)VR_CULL_FACING_MARGIN;
}

// Face-button roles. Both pairs move, but for different reasons, and every call site must
// agree or the tutorial prompts teach the wrong button.
//
// Lower pair (A right / X left) follows the MOVEMENT stick: jump must not share a thumb with
// movement, or you cannot do both at once.
//
// Upper pair (B right / Y left) follows the WEAPON hand: you do not alt-fire with your off
// hand. The menu takes whichever upper button alt-fire did not. The menu is still always
// reachable from the headset's own menu button, so it does not need a fixed letter.
uint32_t stdVR_GetJumpButton(void)
{
    return stdVR_config.bSwapSticks ? STDVR_BTN_X : STDVR_BTN_A;
}

uint32_t stdVR_GetActivateButton(void)
{
    return stdVR_config.bSwapSticks ? STDVR_BTN_A : STDVR_BTN_X;
}

uint32_t stdVR_GetAltFireButton(void)
{
    return (stdVR_config.dominantHand == STDVR_CONTROLLER_LEFT) ? STDVR_BTN_Y : STDVR_BTN_B;
}

uint32_t stdVR_GetMenuButton(void)
{
    return (stdVR_config.dominantHand == STDVR_CONTROLLER_LEFT) ? STDVR_BTN_B : STDVR_BTN_Y;
}

// The controller a face button lives on, for haptics.
int stdVR_GetButtonHand(uint32_t btn)
{
    return (btn == STDVR_BTN_X || btn == STDVR_BTN_Y) ? STDVR_CONTROLLER_LEFT : STDVR_CONTROLLER_RIGHT;
}

int stdVR_IsExitRequested(void)
{
    return stdVR_bInitted ? stdVR_OpenXR_IsExitRequested() : 0;
}

void stdVR_PollEvents(void)
{
    if (!stdVR_bInitted) {
        return;
    }

    stdVR_OpenXR_PollEvents();
}

// External VR_Log from stdVR_OpenXR.cpp
extern void VR_Log(const char* fmt, ...);

static int vrWaitFrameCallCount = 0;

int stdVR_WaitFrame(void)
{
    if (!stdVR_bEnabled) {
        return 0;
    }

    vrWaitFrameCallCount++;

    // Always poll events to allow session state transitions
    stdVR_OpenXR_PollEvents();

    if (!stdVR_clientInfo.bSessionRunning) {
        return 0;
    }

    // If we already have a pending frame that wasn't completed, submit an empty frame
    // This can happen during loading screens or state transitions
    if (stdVR_bFramePending) {
        if (vrWaitFrameCallCount <= 5) {
            VR_Log("stdVR: WaitFrame #%d - previous frame pending, submitting empty\n", vrWaitFrameCallCount);
        }
        stdVR_SubmitEmptyFrame();
        if (stdVR_bFramePending || stdVR_bFrameInProgress) {
            if (vrWaitFrameCallCount <= 5) {
                VR_Log("stdVR: WaitFrame #%d aborted - frame still pending/in-progress\n", vrWaitFrameCallCount);
            }
            return 0;
        }
    }

    int result = stdVR_OpenXR_WaitFrame();
    if (result) {
        stdVR_bFramePending = 1;  // Mark that we need to complete this frame
    }

    if (vrWaitFrameCallCount <= 5) {
        VR_Log("stdVR: WaitFrame #%d done, pending=%d\n", vrWaitFrameCallCount, stdVR_bFramePending);
    }

    return result;
}

int stdVR_BeginFrame(void)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return 0;
    }

    if (stdVR_OpenXR_BeginFrame()) {
        stdVR_bFrameInProgress = 1;
        return 1;
    }
    return 0;
}

int stdVR_EndFrame(void)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return 0;
    }

    // Don't call EndFrame if no frame is in progress (prevents duplicate calls)
    if (!stdVR_bFrameInProgress) {
        return 0;
    }

    int result = stdVR_OpenXR_EndFrame();
    if (result) {
        stdVR_bFramePending = 0;  // Frame completed
        stdVR_bFrameInProgress = 0;
    }
    return result;
}

void stdVR_SubmitEmptyFrame(void)
{
    static int emptyFrameCount = 0;
    static int emptyFrameSkipCount = 0;

    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning || !stdVR_bFramePending) {
        emptyFrameSkipCount++;
        // Only log first skip
        if (emptyFrameSkipCount == 1) {
            VR_Log("stdVR: SubmitEmptyFrame skipped (enabled=%d, running=%d, pending=%d)\n",
                stdVR_bEnabled, stdVR_clientInfo.bSessionRunning, stdVR_bFramePending);
        }
        return;
    }

    emptyFrameCount++;
    // Only log first few empty frames
    if (emptyFrameCount <= 3) {
        VR_Log("stdVR: Submitting empty frame #%d\n", emptyFrameCount);
    }

    // Submit a frame with no rendered content (just begin/end)
    if (stdVR_bFrameInProgress) {
        // A frame is already in progress; end it with no layers.
        if (stdVR_OpenXR_EndFrameEmpty()) {
            stdVR_bFramePending = 0;
            stdVR_bFrameInProgress = 0;
        }
        return;
    }

    if (stdVR_BeginFrame()) {
        // Don't render anything, just end the frame with no layers
        if (stdVR_OpenXR_EndFrameEmpty()) {
            stdVR_bFramePending = 0;
            stdVR_bFrameInProgress = 0;
        }
    }
}

int stdVR_IsFramePending(void)
{
    return stdVR_bFramePending;
}

// Called during long operations (loading, etc.) to keep VR headset from going black
void stdVR_KeepAlive(void)
{
    static int keepAliveCount = 0;

    if (!stdVR_bEnabled || !stdVR_bInitted) {
        return;
    }

    keepAliveCount++;

    // Poll events to keep session state machine running
    stdVR_OpenXR_PollEvents();

    if (!stdVR_clientInfo.bSessionRunning) {
        return;
    }

    // Don't submit empty frames during loading — they show as black flashes
    // interleaved with the loading screen quad layer. The normal frame loop in
    // Window.c keeps submitting loading screen frames, and event polling above
    // keeps the session state machine alive.
}

int stdVR_PrepareEyeBuffer(int eye)
{
    static int prepareWrapperCount[2] = {0, 0};
    if (eye >= 0 && eye < 2) prepareWrapperCount[eye]++;

    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        // Only log first block per eye
        if (prepareWrapperCount[eye >= 0 && eye < 2 ? eye : 0] == 1) {
            VR_Log("stdVR_PrepareEyeBuffer(%d) BLOCKED: enabled=%d, running=%d\n",
                eye, stdVR_bEnabled, stdVR_clientInfo.bSessionRunning);
        }
        return 0;
    }

    if (eye < 0 || eye >= STDVR_EYE_COUNT) {
        VR_Log("stdVR_PrepareEyeBuffer(%d) BLOCKED: eye out of range\n", eye);
        return 0;
    }

    // Only log first 2 calls per eye (one frame)
    if (prepareWrapperCount[eye] <= 2) {
        VR_Log("stdVR_PrepareEyeBuffer(%d) #%d\n", eye, prepareWrapperCount[eye]);
    }
    return stdVR_OpenXR_PrepareEyeBuffer(eye);
}

int stdVR_FinishEyeBuffer(int eye)
{
    static int finishWrapperCount[2] = {0, 0};
    if (eye >= 0 && eye < 2) finishWrapperCount[eye]++;

    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        // Only log first block per eye
        if (finishWrapperCount[eye >= 0 && eye < 2 ? eye : 0] == 1) {
            VR_Log("stdVR_FinishEyeBuffer(%d) BLOCKED: enabled=%d, running=%d\n",
                eye, stdVR_bEnabled, stdVR_clientInfo.bSessionRunning);
        }
        return 0;
    }

    if (eye < 0 || eye >= STDVR_EYE_COUNT) {
        VR_Log("stdVR_FinishEyeBuffer(%d) BLOCKED: eye out of range\n", eye);
        return 0;
    }

    // Only log first 2 calls per eye (one frame)
    if (finishWrapperCount[eye] <= 2) {
        VR_Log("stdVR_FinishEyeBuffer(%d) #%d\n", eye, finishWrapperCount[eye]);
    }
    return stdVR_OpenXR_FinishEyeBuffer(eye);
}

int stdVR_GetCurrentEyeFBO(int eye)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return 0;
    }

    if (eye < 0 || eye >= STDVR_EYE_COUNT) {
        return 0;
    }

    return stdVR_OpenXR_GetCurrentEyeFBO(eye);
}

int stdVR_GetCurrentEye(void)
{
    // Don't check bEnabled - the OpenXR layer manages the actual state
    // The extern variable may be set even when the wrapper thinks VR is disabled
    return stdVR_OpenXR_GetCurrentEye();
}

// ============================================================================
// MultiView Buffer Functions - Single-pass stereo rendering for Quest VR
// ============================================================================

int stdVR_IsMultiViewSupported(void)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return 0;
    }
    return stdVR_OpenXR_IsMultiViewSupported();
}

int stdVR_PrepareMultiViewBuffer(void)
{
    static int mvPrepareWrapperCount = 0;
    mvPrepareWrapperCount++;

    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        if (mvPrepareWrapperCount == 1) {
            VR_Log("stdVR_PrepareMultiViewBuffer BLOCKED: enabled=%d, running=%d\n",
                stdVR_bEnabled, stdVR_clientInfo.bSessionRunning);
        }
        return 0;
    }

    return stdVR_OpenXR_PrepareMultiViewBuffer();
}

int stdVR_FinishMultiViewBuffer(void)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return 0;
    }
    return stdVR_OpenXR_FinishMultiViewBuffer();
}

int stdVR_GetMultiViewFBO(void)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return 0;
    }
    return stdVR_OpenXR_GetMultiViewFBO();
}

// Mirror the last-rendered VR buffer to the desktop window (debug aid). The caller is
// responsible for the subsequent SDL_GL_SwapWindow.
void stdVR_MirrorToWindow(int windowWidth, int windowHeight)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return;
    }
    stdVR_OpenXR_MirrorToWindow(windowWidth, windowHeight);
}

// Compute and upload both eye view/projection matrices for MultiView rendering
// Uses symmetric IPD offset for proper stereo
void stdVR_SetMultiViewMatrices(float zNear, float zFar)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return;
    }

    float viewMatrices[32];  // 2 x 4x4 matrices (column-major)
    float projMatrices[32];  // 2 x 4x4 matrices (column-major)

    float worldScale = stdVR_config.worldScale;
    if (worldScale <= 0.0f) {
        worldScale = 1.0f;
    }

    // Proper per-eye GPU projection (OVR_multiview). The scene is CPU-rendered ONCE to the CENTER
    // (HMD) view as view-space geometry (sithCamera_SetVRViewMultiView + rdCamera_bGpuProjection);
    // the GPU vertex shader applies these per-eye matrices selected by gl_ViewID_OVR.
    //
    // For each eye we upload:
    //   u_viewMatrices[eye] = A * (InvertOrtho34(eyePose) * hmdPose), translation * worldScale
    //   u_projMatrices[eye] = real asymmetric OpenXR per-eye frustum (stdVR_GetEyeProjectionMatrix44)
    // where A is the engine->GL axis swap (engine view space is X=right, Y=forward, Z=up; GL is
    // X=right, Y=up, -Z=forward). M_eye = InvertOrtho34(eyePose)*hmdPose maps a point from the
    // center-view space the geometry was rendered in into this eye's view space; it is independent
    // of the game camera (it cancels). This replaces the old union-render + shader post-warp/remap
    // and is correct on both desktop and Adreno (no post-projection magnify), so no platform gate.
    for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
        rdMatrix34 invEye, mEye;
        rdMatrix_InvertOrtho34(&invEye, &stdVR_clientInfo.eyes[eye].viewMatrix);
        rdMatrix_Multiply34(&mEye, &invEye, &stdVR_clientInfo.hmdPoseMatrix);
        // Physical eye-from-center offset is in meters; scale into game units like the combine path.
        mEye.scale.x *= worldScale;
        mEye.scale.y *= worldScale;
        mEye.scale.z *= worldScale;

        // Convert the engine-space transform M_eye into a GL column-major view matrix, applying the
        // engine->GL axis swap A (GL rows = engine rvec, uvec, -lvec; see stdVR_Map3D.c). Columns of
        // M_eye are rvec (in.x), lvec (in.y), uvec (in.z); translation is scale.
        float* V = &viewMatrices[eye * 16];
        V[0]  =  mEye.rvec.x;  V[4]  =  mEye.lvec.x;  V[8]  =  mEye.uvec.x;  V[12] =  mEye.scale.x;
        V[1]  =  mEye.rvec.z;  V[5]  =  mEye.lvec.z;  V[9]  =  mEye.uvec.z;  V[13] =  mEye.scale.z;
        V[2]  = -mEye.rvec.y;  V[6]  = -mEye.lvec.y;  V[10] = -mEye.uvec.y;  V[14] = -mEye.scale.y;
        V[3]  =  0.0f;         V[7]  =  0.0f;         V[11] =  0.0f;         V[15] =  1.0f;

        // Real asymmetric per-eye projection from the OpenXR FOV tangents.
        stdVR_GetEyeProjectionMatrix44(eye, &projMatrices[eye * 16], zNear, zFar);
    }

    // Per-eye HUD offset (forward-centering + convergence) for the baked 2D HUD, at the menu depth.
    extern float jkPlayer_vrHudDepth;
    stdVR_SetHudOffsetForDepth(jkPlayer_vrHudDepth);

    // Upload to std3D UBOs (ViewMatrices + ProjectionMatrices, consumed by default_v.glsl /
    // crosshair_v.glsl via gl_ViewID_OVR).
    extern void std3D_UpdateMultiViewMatrices(float* viewMatrices, float* projMatrices);
    std3D_UpdateMultiViewMatrices(viewMatrices, projMatrices);
}

// Compute and upload the per-eye baked-HUD horizontal shift (native-eye NDC) for a given virtual
// depth: forward-centering (-nativeCenter/nativeWidth, puts the HUD at the binocular straight-
// ahead, correcting asymmetric FOV) + convergence (+/- IPD/(d*nativeWidth) to sit at distance d).
// The eye buffer holds each eye's NATIVE frustum (the GPU projects per eye), so the HUD is laid
// out in native-eye NDC. Used for the HUD (menu depth) and overridden for the weapon/force wheel.
void stdVR_SetHudOffsetForDepth(float depthMeters)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return;
    }
    // Altered: measure the eye separation as the length of the full 3D vector between the eye
    // positions, not the difference of their world X. The separation vector rotates with the
    // head, so an X-only reading gives IPD*|cos(yaw)| - correct facing along X, but collapsing
    // to zero at 90 degrees, which zeroed the convergence term and left the HUD with no stereo
    // and apparently at infinity. The magnitude is rotation-invariant.
    float ipdX = stdVR_clientInfo.eyes[1].viewMatrix.scale.x - stdVR_clientInfo.eyes[0].viewMatrix.scale.x;
    float ipdY = stdVR_clientInfo.eyes[1].viewMatrix.scale.y - stdVR_clientInfo.eyes[0].viewMatrix.scale.y;
    float ipdZ = stdVR_clientInfo.eyes[1].viewMatrix.scale.z - stdVR_clientInfo.eyes[0].viewMatrix.scale.z;
    float ipdAbs = sqrtf(ipdX * ipdX + ipdY * ipdY + ipdZ * ipdZ);

    // The baked 2D HUD is laid out in each eye's NATIVE-eye NDC (the eye buffer now always holds
    // that eye's real per-eye frustum - the GPU projects per eye, so there is no union/remap and
    // no platform difference). Per-eye shift = forward-centering (-nativeCenter/nativeWidth, places
    // the HUD at the binocular straight-ahead) + depth convergence (+/- IPD/(d*nativeWidth)).
    float off[2] = { 0.0f, 0.0f };
    for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
        float nativeWidth  = stdVR_clientInfo.eyes[eye].fovRight + stdVR_clientInfo.eyes[eye].fovLeft;
        float nativeCenter = stdVR_clientInfo.eyes[eye].fovRight - stdVR_clientInfo.eyes[eye].fovLeft;
        if (nativeWidth > 0.0001f) {
            float forwardCenter = -nativeCenter / nativeWidth;
            float convergeNDC = (depthMeters > 0.01f) ? (ipdAbs / (depthMeters * nativeWidth)) : 0.0f;
            off[eye] = forwardCenter + ((eye == 0) ? convergeNDC : -convergeNDC);
        }
    }
    extern void std3D_SetVRHudOffset(float eye0, float eye1);
    std3D_SetVRHudOffset(off[0], off[1]);
}


void stdVR_UpdateTracking(void)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return;
    }

    stdVR_OpenXR_UpdateTracking();

    if (stdVR_bPendingScreenLayerResnap) {
        stdVR_bPendingScreenLayerResnap = 0;
        stdVR_UpdateScreenLayerSnap();
    }

    // Update move direction based on config
    if (stdVR_config.moveDirection == STDVR_MOVE_HEAD) {
        rdVector_Copy3(&stdVR_clientInfo.moveForward, &stdVR_clientInfo.hmdPoseMatrix.lvec);
        stdVR_clientInfo.moveYaw = stdVR_clientInfo.hmdOrientation.y;
    } else {
        // Steer with the hand actually holding the movement stick, not the weapon hand: you push
        // the stick with one thumb and expect to travel where that hand points. The move stick is
        // the left one unless Swap Thumbsticks is on, independent of handedness.
        int hand = stdVR_config.bSwapSticks ? STDVR_CONTROLLER_RIGHT : STDVR_CONTROLLER_LEFT;
        if (stdVR_clientInfo.controllers[hand].bTracking) {
            rdVector_Copy3(&stdVR_clientInfo.moveForward, &stdVR_clientInfo.controllers[hand].poseMatrix.lvec);
            stdVR_clientInfo.moveYaw = stdVR_clientInfo.controllers[hand].orientation.y;
        } else {
            // Fallback to head
            rdVector_Copy3(&stdVR_clientInfo.moveForward, &stdVR_clientInfo.hmdPoseMatrix.lvec);
            stdVR_clientInfo.moveYaw = stdVR_clientInfo.hmdOrientation.y;
        }
    }
}

void stdVR_GetHMDPose(rdVector3* pPosition, rdVector3* pOrientation)
{
    if (pPosition) {
        rdVector_Copy3(pPosition, &stdVR_clientInfo.hmdPosition);
    }
    if (pOrientation) {
        rdVector_Copy3(pOrientation, &stdVR_clientInfo.hmdOrientation);
    }
}

void stdVR_GetControllerPose(int hand, rdVector3* pPosition, rdVector3* pOrientation)
{
    if (hand < 0 || hand >= STDVR_CONTROLLER_COUNT) {
        return;
    }

    if (pPosition) {
        rdVector_Copy3(pPosition, &stdVR_clientInfo.controllers[hand].position);
    }
    if (pOrientation) {
        rdVector_Copy3(pOrientation, &stdVR_clientInfo.controllers[hand].orientation);
    }
}

void stdVR_GetEyeViewMatrix(int eye, rdMatrix34* pOut)
{
    if (!pOut || eye < 0 || eye >= STDVR_EYE_COUNT) {
        return;
    }

    rdMatrix_Copy34(pOut, &stdVR_clientInfo.eyes[eye].viewMatrix);
}

void stdVR_GetEyeProjection(int eye, float* pOut16)
{
    if (!pOut16 || eye < 0 || eye >= STDVR_EYE_COUNT) {
        return;
    }

    memcpy(pOut16, stdVR_clientInfo.eyes[eye].projectionMatrix, 16 * sizeof(float));
}

void stdVR_GetEyeProjectionMatrix44(int eye, float* pOut16, float zNear, float zFar)
{
    if (!pOut16 || eye < 0 || eye >= STDVR_EYE_COUNT) {
        return;
    }

    stdVR_EyeView* pEye = &stdVR_clientInfo.eyes[eye];

    // Build asymmetric projection matrix from FOV tangents
    float left = -pEye->fovLeft * zNear;
    float right = pEye->fovRight * zNear;
    float bottom = -pEye->fovDown * zNear;
    float top = pEye->fovUp * zNear;

    float width = right - left;
    float height = top - bottom;

    // Column-major 4x4 perspective matrix
    memset(pOut16, 0, 16 * sizeof(float));
    pOut16[0] = 2.0f * zNear / width;
    pOut16[5] = 2.0f * zNear / height;
    pOut16[8] = (right + left) / width;
    pOut16[9] = (top + bottom) / height;
    pOut16[10] = -(zFar + zNear) / (zFar - zNear);
    pOut16[11] = -1.0f;
    pOut16[14] = -2.0f * zFar * zNear / (zFar - zNear);
    pOut16[15] = 0.0f;
}

void stdVR_UpdateInput(void)
{
    if (!stdVR_bEnabled || !stdVR_clientInfo.bSessionRunning) {
        return;
    }

    stdVR_OpenXR_UpdateInput();
}

void stdVR_MapInputToGame(void)
{
    if (!stdVR_bEnabled) {
        return;
    }

    stdVR_Input_MapToGame();
}

int stdVR_GetRefreshRateCount(void)
{
    return stdVR_clientInfo.numRefreshRates;
}

float stdVR_GetRefreshRateByIndex(int idx)
{
    if (idx < 0 || idx >= stdVR_clientInfo.numRefreshRates) {
        return 0.0f;
    }

    return stdVR_clientInfo.aRefreshRates[idx];
}

float stdVR_GetCurrentRefreshRate(void)
{
    return stdVR_clientInfo.currentRefreshRate;
}

int stdVR_ApplyRefreshRate(float hz)
{
    if (!stdVR_bEnabled || hz <= 0.0f) {
        return 0;
    }

    return stdVR_OpenXR_RequestRefreshRate(hz);
}

void stdVR_TriggerHaptic(int hand, float amplitude, float duration, float frequency)
{
    if (!stdVR_bEnabled || hand < 0 || hand >= STDVR_CONTROLLER_COUNT) {
        return;
    }

    stdVR_OpenXR_TriggerHaptic(hand, amplitude, duration, frequency);
}

void stdVR_StopHaptic(int hand)
{
    if (!stdVR_bEnabled || hand < 0 || hand >= STDVR_CONTROLLER_COUNT) {
        return;
    }

    stdVR_OpenXR_StopHaptic(hand);
}

// Added: instructional prompts on the HUD. jkDev owns the queue, expiry and drawing; this
// just gives VR text a longer dwell than a gameplay message and, for the "once" variant,
// remembers in the player profile that the lesson has been taught.
static int stdVR_promptsShownDirty = 0;

void stdVR_ShowPrompt(const wchar_t* pText)
{
    if (!pText || !stdVR_bEnabled) {
        return;
    }

    jkDev_PrintUniStringTimed(pText, STDVR_PROMPT_DWELL_MS);
}

void stdVR_ShowPromptOnce(int promptId, const wchar_t* pText)
{
    if (!pText || !stdVR_bEnabled) {
        return;
    }
    if (promptId < 0 || promptId >= STDVR_PROMPT_COUNT) {
        return;
    }

    uint32_t mask = 1u << promptId;
    if ((uint32_t)jkPlayer_vrPromptsShown & mask) {
        return;
    }

    jkPlayer_vrPromptsShown |= (int)mask;
    stdVR_promptsShownDirty = 1;
    stdVR_ShowPrompt(pText);
}

// Persist the taught-prompt bitmask. MUST NOT be called from a gameplay tick: WriteConf
// drives the single global stdConffile object, and sithWorld_Load pumps VR frames between
// level sections (the "VR KeepAlive" path), so writing from there closes the conffile that
// the loader is reading and the next section faults on a dead handle. Call it only at a
// boundary where no load can be in flight - leaving gameplay.
void stdVR_FlushPromptsShown(void)
{
    if (!stdVR_promptsShownDirty) {
        return;
    }

    stdVR_promptsShownDirty = 0;
    jkPlayer_WriteConf(jkPlayer_playerShortName);
}

int stdVR_WasPromptShown(int promptId)
{
    if (promptId < 0 || promptId >= STDVR_PROMPT_COUNT) {
        return 1;  // unknown id: treat as taught so a bad call cannot spam
    }

    return ((uint32_t)jkPlayer_vrPromptsShown & (1u << promptId)) != 0;
}

void stdVR_ResetPromptsShown(void)
{
    jkPlayer_vrPromptsShown = 0;
}

// 6DoF head-driven body movement: per-frame horizontal head delta (tracking space, metres).
// We track the previous sample and emit the per-frame delta; the physics code feeds
// this delta into collision movement so the body follows the head.
static int   stdVR_headDeltaValid = 0;
static float stdVR_prevHeadX = 0.0f;
static float stdVR_prevHeadY = 0.0f;

// Reset the head-delta reference so the next frame re-seeds prev and emits a zero delta.
// Must be called whenever the tracking origin is re-based (recenter / session start) so we
// don't inject a huge spurious movement from the position discontinuity.
void stdVR_ResetHeadDelta(void)
{
    stdVR_headDeltaValid = 0;
}

// Return this frame's horizontal head movement (tracking space, metres; x=right, y=forward)
// and advance the stored reference. Returns (0,0) on the first valid frame after a reset.
void stdVR_ConsumeHeadDelta(float* outDx, float* outDy)
{
    float headX = stdVR_clientInfo.hmdPosition.x;
    float headY = stdVR_clientInfo.hmdPosition.y;

    float dx = 0.0f, dy = 0.0f;
    if (stdVR_headDeltaValid) {
        dx = headX - stdVR_prevHeadX;
        dy = headY - stdVR_prevHeadY;
    }
    stdVR_prevHeadX = headX;
    stdVR_prevHeadY = headY;
    stdVR_headDeltaValid = 1;

    if (outDx) *outDx = dx;
    if (outDy) *outDy = dy;
}

void stdVR_RecenterView(void)
{
    if (!stdVR_bEnabled) {
        return;
    }

    stdVR_OpenXR_RecenterView();

    // The tracking origin just moved; drop the head-delta reference so we don't lurch the body.
    stdVR_ResetHeadDelta();

    // Added: an open menu was placed against the old origin, so re-place it once the next
    // tracking update has caught up with the new one.
    if (stdVR_clientInfo.bUseScreenLayer) {
        stdVR_bPendingScreenLayerResnap = 1;
    }
}

void stdVR_GetRecommendedRenderSize(int* pWidth, int* pHeight)
{
    if (pWidth) {
        *pWidth = stdVR_clientInfo.renderWidth;
    }
    if (pHeight) {
        *pHeight = stdVR_clientInfo.renderHeight;
    }
}

const char* stdVR_GetRuntimeName(void)
{
    return stdVR_OpenXR_GetRuntimeName();
}

void stdVR_CombineCameraWithEye(const rdMatrix34* pGameCamera, int eye, rdMatrix34* pOut)
{
    static int combineCallCount = 0;
    combineCallCount++;

    if (!pGameCamera || !pOut || eye < 0 || eye >= STDVR_EYE_COUNT) {
        return;
    }

    // Store game camera for controller matrix calculations
    rdMatrix_Copy34(&stdVR_currentGameCamera, pGameCamera);
    stdVR_currentGameCameraValid = 1;

    float worldScale = stdVR_config.worldScale;
    if (worldScale <= 0.0f) {
        worldScale = 1.0f;
    }

    // Get the per-eye pose (complete transform in VR tracking space, already in JKDF2 coords)
    rdMatrix34 eyePose;
    rdMatrix_Copy34(&eyePose, &stdVR_clientInfo.eyes[eye].viewMatrix);

    // Get the HMD center pose for reference
    rdMatrix34 hmdPose;
    rdMatrix_Copy34(&hmdPose, &stdVR_clientInfo.hmdPoseMatrix);

    // Log for debugging (first few frames only)
    if (combineCallCount <= 4) {
        extern void VR_Log(const char* fmt, ...);
        VR_Log("stdVR_CombineCameraWithEye: eye %d, worldScale=%.2f\n", eye, worldScale);
        VR_Log("  gameCamera pos=(%.3f, %.3f, %.3f)\n",
            pGameCamera->scale.x, pGameCamera->scale.y, pGameCamera->scale.z);
        VR_Log("  hmdPose pos=(%.4f, %.4f, %.4f)\n",
            hmdPose.scale.x, hmdPose.scale.y, hmdPose.scale.z);
    }

    // === BUILD THE COMBINED VIEW MATRIX ===
    //
    // The view matrix combines:
    // 1. Game camera orientation (body direction from controller turning)
    // 2. VR head orientation (head rotation relative to body)
    // 3. Game camera position (player's location in game world)
    // 4. HMD position offset (6DOF head movement, scaled to game units)
    // 5. Per-eye offset (IPD for stereo)
    //
    // We multiply: gameCamera * vrHeadPose * eyeOffset
    // This makes controller turning rotate the body, and VR adds head movement on top.

    // First, compute the VR head rotation relative to tracking origin
    // The eyePose contains full orientation - we use this for head rotation
    // We need to multiply game camera rotation by VR head rotation

    // Build combined orientation: game camera rotation * VR head rotation
    // This means: first apply VR head rotation, then apply game body rotation
    // Result: looking left in VR while body faces north = looking northwest
    rdMatrix34 combined;
    rdMatrix_Multiply34(&combined, pGameCamera, &eyePose);

    // Now handle position:
    //
    // HMD position is in meters relative to tracking origin.
    // We do NOT scale HMD position by worldScale - that would push the camera
    // way outside the level. The game camera position already places us correctly
    // in the game world.
    //
    // We only use HMD position for:
    // 1. Small head movements (leaning, ducking) - these are already in meters
    // 2. IPD offset for stereo separation
    //
    // The worldScale parameter is for adjusting perceived object sizes (IPD-related).

    // HMD position offset for 6DOF head movement (roomscale)
    // Scale by worldScale to control how much physical movement affects in-game position
    // This also reduces the "strafing" effect when turning your head (pivot offset)
    rdVector3 hmdOffset;
    hmdOffset.x = hmdPose.scale.x * worldScale;
    hmdOffset.y = hmdPose.scale.y * worldScale;
    hmdOffset.z = hmdPose.scale.z * worldScale;

    // Apply height offset if configured (in meters, then scaled)
    if (stdVR_config.heightOffset != 0.0f) {
        hmdOffset.z += stdVR_config.heightOffset * worldScale;
    }

    // Calculate per-eye offset from HMD center (IPD) - this stays in meters
    // IPD is typically ~0.063 meters, worldScale adjusts how this translates to game units
    rdVector3 ipdOffset;
    ipdOffset.x = (eyePose.scale.x - hmdPose.scale.x) * worldScale;
    ipdOffset.y = (eyePose.scale.y - hmdPose.scale.y) * worldScale;
    ipdOffset.z = (eyePose.scale.z - hmdPose.scale.z) * worldScale;

    // 6DoF Model A: when head-driven body movement is active, the player BODY follows the
    // head laterally (see sithPhysics_VRApplyHeadMovement), so the camera must NOT also add
    // the horizontal head offset (that would double-count and let the view drift through
    // walls). Keep only the vertical component so physical crouch/duck still lowers the view.
    if (stdVR_config.sixDoFScale > 0.0f) {
        hmdOffset.x = 0.0f;
        hmdOffset.y = 0.0f;
    }

    // Transform HMD offset by game camera orientation (body direction)
    // This makes physical movement relative to where your body is facing
    rdVector3 hmdOffsetWorld;
    rdMatrix_TransformVector34(&hmdOffsetWorld, &hmdOffset, pGameCamera);

    // Transform IPD offset by game camera orientation only (not combined)
    // The ipdOffset already includes head rotation from OpenXR (eye positions move with head roll),
    // so we only need to apply body rotation to convert from tracking space to world space.
    // Using 'combined' here would double-apply head roll, breaking stereo when tilting head.
    rdVector3 ipdOffsetWorld;
    rdMatrix_TransformVector34(&ipdOffsetWorld, &ipdOffset, pGameCamera);

    // Final position = game camera position + HMD offset (in world) + IPD offset (in world)
    combined.scale.x = pGameCamera->scale.x + hmdOffsetWorld.x + ipdOffsetWorld.x;
    combined.scale.y = pGameCamera->scale.y + hmdOffsetWorld.y + ipdOffsetWorld.y;
    combined.scale.z = pGameCamera->scale.z + hmdOffsetWorld.z + ipdOffsetWorld.z;

    if (combineCallCount <= 4) {
        extern void VR_Log(const char* fmt, ...);
        VR_Log("  hmdOffset (meters)=(%.4f, %.4f, %.4f)\n", hmdOffset.x, hmdOffset.y, hmdOffset.z);
        VR_Log("  ipdOffset (scaled)=(%.5f, %.5f, %.5f)\n", ipdOffset.x, ipdOffset.y, ipdOffset.z);
        VR_Log("  final pos=(%.3f, %.3f, %.3f)\n", combined.scale.x, combined.scale.y, combined.scale.z);
    }

    rdMatrix_Copy34(pOut, &combined);
}

// Combine game camera with HMD center pose (no eye offset, for MultiView center)
void stdVR_CombineCameraWithHMD(const rdMatrix34* pGameCamera, rdMatrix34* pOut)
{
    if (!pGameCamera || !pOut) return;

    // Store game camera for controller matrix calculations
    rdMatrix_Copy34(&stdVR_currentGameCamera, pGameCamera);
    stdVR_currentGameCameraValid = 1;

    float worldScale = stdVR_config.worldScale;
    if (worldScale <= 0.0f) {
        worldScale = 1.0f;
    }

    // Get HMD center pose (no IPD offset)
    rdMatrix34 hmdPose;
    rdMatrix_Copy34(&hmdPose, &stdVR_clientInfo.hmdPoseMatrix);

    // Multiply: gameCamera * hmdPose (head rotation without eye separation)
    rdMatrix34 combined;
    rdMatrix_Multiply34(&combined, pGameCamera, &hmdPose);

    // Apply world scale to HMD offset
    rdVector3 hmdOffset;
    hmdOffset.x = hmdPose.scale.x * worldScale;
    hmdOffset.y = hmdPose.scale.y * worldScale;
    hmdOffset.z = hmdPose.scale.z * worldScale;

    // Apply height offset if configured
    if (stdVR_config.heightOffset != 0.0f) {
        hmdOffset.z += stdVR_config.heightOffset * worldScale;
    }

    hmdOffset.z += stdVR_config.fixedHeightAdjustment * worldScale;

    // 6DoF Model A: body follows the head laterally, so drop the horizontal camera float
    // (keep vertical for crouch/duck). See the matching note in stdVR_CombineCameraWithEye.
    if (stdVR_config.sixDoFScale > 0.0f) {
        hmdOffset.x = 0.0f;
        hmdOffset.y = 0.0f;
    }

    // Transform by game camera orientation
    rdVector3 hmdOffsetWorld;
    rdMatrix_TransformVector34(&hmdOffsetWorld, &hmdOffset, pGameCamera);

    // Final position = game position + HMD offset (no IPD)
    combined.scale.x = pGameCamera->scale.x + hmdOffsetWorld.x;
    combined.scale.y = pGameCamera->scale.y + hmdOffsetWorld.y;
    combined.scale.z = pGameCamera->scale.z + hmdOffsetWorld.z;

    rdMatrix_Copy34(pOut, &combined);
}

// Added: Set the current eye view matrix (called from sithCamera_SetVRView)
void stdVR_SetCurrentEyeViewMatrix(const rdMatrix34* pMat)
{
    if (pMat) {
        rdMatrix_Copy34(&stdVR_currentEyeViewMat, pMat);
        stdVR_currentEyeViewMatValid = 1;
    } else {
        stdVR_currentEyeViewMatValid = 0;
    }
}

// Added: Get the current combined camera+eye view matrix for this eye
// Returns 1 if valid, 0 if not (should fall back to base camera)
int stdVR_GetCurrentEyeViewMatrix(rdMatrix34* pOut)
{
    if (!pOut) return 0;
    if (!stdVR_currentEyeViewMatValid) return 0;

    rdMatrix_Copy34(pOut, &stdVR_currentEyeViewMat);
    return 1;
}

// Added: Clear the current eye view matrix (called when not rendering an eye)
// Note: We do NOT clear stdVR_currentGameCameraValid here because the game camera
// is needed for fire position calculation which happens during game logic BEFORE
// the next render pass sets a new camera. The last known game camera position is
// much better than falling back to player->position for fire origin.
void stdVR_ClearCurrentEyeViewMatrix(void)
{
    stdVR_currentEyeViewMatValid = 0;
    // Keep stdVR_currentGameCameraValid = 1 so fire position uses last known camera
}

// ============================================================================
// Motion Controls Helper Functions
// ============================================================================

// Get the dominant hand controller index
int stdVR_GetDominantHand(void)
{
    return stdVR_config.dominantHand;
}

// Get controller state by hand index
stdVR_ControllerState* stdVR_GetController(int hand)
{
    if (hand < 0 || hand >= STDVR_CONTROLLER_COUNT) {
        return NULL;
    }
    return &stdVR_clientInfo.controllers[hand];
}

// Get dominant hand controller
stdVR_ControllerState* stdVR_GetDominantController(void)
{
    return stdVR_GetController(stdVR_config.dominantHand);
}

// Get off-hand controller
stdVR_ControllerState* stdVR_GetOffhandController(void)
{
    return stdVR_GetController(1 - stdVR_config.dominantHand);
}

// Added: build the per-weapon alignment angles for one hand. The weapon visual and the fire
// origin both need these and used to carry separate copies, which drifted apart.
//
// The left grip frame is the right frame mirrored across local X (OpenXR grip +X is the palm
// normal). Under that mirror a rotation about X keeps its sign and rotations about the other
// two axes flip, so pitch stays and yaw and roll negate. Degrees.
static void stdVR_BuildWeaponRotationAdjust(int hand, const stdVR_WeaponOffset* pOffset,
                                            int bApplyOffset, int bApplyGlobalPitch,
                                            rdVector3* pAnglesOut)
{
    float pitchAdjust = ((bApplyOffset && pOffset) ? pOffset->pitchAdjust : 0.f)
                      + (bApplyGlobalPitch ? stdVR_motionConfig.weaponPitchAdjust : 0.f);
    float yawAdjust = (bApplyOffset && pOffset) ? pOffset->yawAdjust : 0.f;
    float rollAdjust = (bApplyOffset && pOffset) ? pOffset->rollAdjust : 0.f;

    if (hand == STDVR_CONTROLLER_LEFT) {
        yawAdjust = -yawAdjust;
        rollAdjust = -rollAdjust;
    }

    pAnglesOut->x = pitchAdjust;
    pAnglesOut->y = yawAdjust;
    pAnglesOut->z = rollAdjust;
}

// Transform controller position to game world coordinates
// Takes controller position (in VR tracking space) and outputs world position
// This must match the position calculation in stdVR_GetControllerViewMatrix for
// the fire position to align with the rendered weapon.
void stdVR_ControllerToWorld(int hand, rdVector3* pWorldPos, int useOffsets)
{
    if (!pWorldPos || hand < 0 || hand >= STDVR_CONTROLLER_COUNT) {
        return;
    }

    stdVR_ControllerState* pCtrl = &stdVR_clientInfo.controllers[hand];
    if (!pCtrl->bTracking) {
        return;
    }

    // Use game camera if available (matches weapon visual calculation exactly)
    // Fall back to player thing if game camera not available
    const rdMatrix34* pCamera = NULL;
    rdVector3 cameraPos;

    if (stdVR_currentGameCameraValid) {
        pCamera = &stdVR_currentGameCamera;
        cameraPos.x = pCamera->scale.x;
        cameraPos.y = pCamera->scale.y;
        cameraPos.z = pCamera->scale.z;
    } else {
        // Fallback to player orientation
        extern sithThing* sithPlayer_pLocalPlayerThing;
        if (!sithPlayer_pLocalPlayerThing) {
            return;
        }
        pCamera = &sithPlayer_pLocalPlayerThing->lookOrientation;
        cameraPos = sithPlayer_pLocalPlayerThing->position;
    }

    float worldScale = stdVR_config.worldScale;
    if (worldScale <= 0.0f) worldScale = 1.0f;

    rdVector3 ctrlPos = pCtrl->position;
    rdVector3 hmdPos = stdVR_clientInfo.hmdPosition;

    // Controller offset from HMD in tracking space (meters)
    rdVector3 offset;
    offset.x = ctrlPos.x - hmdPos.x;
    offset.y = ctrlPos.y - hmdPos.y;
    offset.z = ctrlPos.z - hmdPos.z;

    // Rotation that positions the muzzle offset. Altered: the per-weapon alignment angles now
    // apply ONLY when the caller asks for the per-weapon offsets (useOffsets), which means the
    // weapon VISUAL. The fire path calls this with useOffsets = 0, so aligning a weapon model
    // no longer moves the point that projectiles come from. Only the global controller pitch
    // adjustment affects the fire path.
    stdVR_WeaponOffset* pWeaponOffset = stdVR_GetCurrentWeaponOffset();
    rdVector3 pitchAngles;
    stdVR_BuildWeaponRotationAdjust(hand, pWeaponOffset, useOffsets, 1, &pitchAngles);

    rdMatrix34 adjustedPose;
    if (pitchAngles.x != 0.0f || pitchAngles.y != 0.0f || pitchAngles.z != 0.0f) {
        rdMatrix34 pitchRot;
        rdMatrix_BuildRotate34(&pitchRot, &pitchAngles);
        rdMatrix_Multiply34(&adjustedPose, &pCtrl->poseMatrix, &pitchRot);
    } else {
        rdMatrix_Copy34(&adjustedPose, &pCtrl->poseMatrix);
    }

    // Apply weapon position offset in controller local space (for fire position)
    // Use per-weapon offsets if available, otherwise fall back to global config
    float weapOffX = useOffsets && pWeaponOffset ? pWeaponOffset->offsetX : stdVR_motionConfig.weaponOffsetX;
    float weapOffY = useOffsets && pWeaponOffset ? pWeaponOffset->offsetY : stdVR_motionConfig.weaponOffsetY;
    float weapOffZ = useOffsets && pWeaponOffset ? pWeaponOffset->offsetZ : stdVR_motionConfig.weaponOffsetZ;

    // Added: mirror palm-out X for left hand so fire position matches the
    // visible weapon. Must stay in sync with stdVR_GetControllerViewMatrixInternal.
    if (hand == STDVR_CONTROLLER_LEFT) {
        weapOffX = -weapOffX;
    }

    if (weapOffX != 0.0f || weapOffY != 0.0f || weapOffZ != 0.0f) {
        rdVector3 localOffset;
        localOffset.x = weapOffX;
        localOffset.y = weapOffY;
        localOffset.z = weapOffZ;

        // Transform local offset by ADJUSTED controller orientation (matches weapon visual)
        rdVector3 transformedOffset;
        rdMatrix_TransformVector34(&transformedOffset, &localOffset, &adjustedPose);

        offset.x += transformedOffset.x;
        offset.y += transformedOffset.y;
        offset.z += transformedOffset.z;
    }

    // Scale offset to game world units (same as GetControllerViewMatrix)
    rdVector3 scaledOffset;
    scaledOffset.x = offset.x * worldScale;
    scaledOffset.y = offset.y * worldScale;
    scaledOffset.z = offset.z * worldScale;

    // Transform offset by camera orientation (rotate into world space)
    rdVector3 worldOffset;
    rdMatrix_TransformVector34(&worldOffset, &scaledOffset, pCamera);

    // Also include HMD offset from tracking origin (same as GetControllerViewMatrix does)
    // This is critical - without this, the fire position is missing the roomscale/standing offset
    rdVector3 hmdOffset;
    hmdOffset.x = hmdPos.x * worldScale;
    hmdOffset.y = hmdPos.y * worldScale;
    hmdOffset.z = hmdPos.z * worldScale;

    // Altered: with head-driven body movement enabled, the player/body has
    // already absorbed lateral HMD movement. Keep vertical HMD offset for
    // crouch/height, but do not double-count lateral room-scale motion.
    if (stdVR_config.sixDoFScale > 0.0f) {
        hmdOffset.x = 0.0f;
        hmdOffset.y = 0.0f;
    }

    rdVector3 hmdOffsetWorld;
    rdMatrix_TransformVector34(&hmdOffsetWorld, &hmdOffset, pCamera);

    // Final position = camera position + HMD offset + controller offset (matches weapon visual)
    pWorldPos->x = cameraPos.x + hmdOffsetWorld.x + worldOffset.x;
    pWorldPos->y = cameraPos.y + hmdOffsetWorld.y + worldOffset.y;
    pWorldPos->z = cameraPos.z + hmdOffsetWorld.z + worldOffset.z;

    // Apply fire position offset in controller local space (so it moves with weapon aim)
    // Transform by combined body + controller orientation so offset rotates with weapon
    if (stdVR_motionConfig.fireOffsetX != 0.0f ||
        stdVR_motionConfig.fireOffsetY != 0.0f ||
        stdVR_motionConfig.fireOffsetZ != 0.0f) {
        rdVector3 fireOffset;
        // Added: mirror palm-out X for left hand (same reason as weapOffX above).
        float mirroredFireX = (hand == STDVR_CONTROLLER_LEFT)
                              ? -stdVR_motionConfig.fireOffsetX
                              : stdVR_motionConfig.fireOffsetX;
        fireOffset.x = mirroredFireX * worldScale;
        fireOffset.y = stdVR_motionConfig.fireOffsetY * worldScale;
        fireOffset.z = stdVR_motionConfig.fireOffsetZ * worldScale;

        // Build combined orientation: body rotation * controller rotation
        rdMatrix34 combinedOrientation;
        rdMatrix_Multiply34(&combinedOrientation, pCamera, &adjustedPose);

        rdVector3 fireOffsetWorld;
        rdMatrix_TransformVector34(&fireOffsetWorld, &fireOffset, &combinedOrientation);

        pWorldPos->x += fireOffsetWorld.x;
        pWorldPos->y += fireOffsetWorld.y;
        pWorldPos->z += fireOffsetWorld.z;
    }

    // Apply height offset
    pWorldPos->z += stdVR_config.heightOffset * worldScale;
    pWorldPos->z += stdVR_config.fixedHeightAdjustment * worldScale;

}

// Get controller world matrix (for weapon rendering)
void stdVR_GetControllerWorldMatrix(int hand, rdMatrix34* pMatrix)
{
    if (!pMatrix || hand < 0 || hand >= STDVR_CONTROLLER_COUNT) {
        return;
    }

    stdVR_ControllerState* pCtrl = &stdVR_clientInfo.controllers[hand];
    if (!pCtrl->bTracking) {
        rdMatrix_Identity34(pMatrix);
        return;
    }

    extern sithThing* sithPlayer_pLocalPlayerThing;
    if (!sithPlayer_pLocalPlayerThing) {
        rdMatrix_Identity34(pMatrix);
        return;
    }

    sithThing* player = sithPlayer_pLocalPlayerThing;

    // Apply pitch adjustment to controller pose if configured
    rdMatrix34 adjustedPose;
    rdMatrix34 pitchRot;
    rdVector3 pitchAngles = { -90.f + stdVR_motionConfig.weaponPitchAdjust, 0.0f, 0.0f };
    rdMatrix_BuildRotate34(&pitchRot, &pitchAngles);
    rdMatrix_Multiply34(&adjustedPose, &pCtrl->poseMatrix, &pitchRot);

    // Combine player body orientation with adjusted controller orientation
    rdMatrix_Multiply34(pMatrix, &player->lookOrientation, &adjustedPose);

    // Set position
    rdVector3 worldPos;
    stdVR_ControllerToWorld(hand, &worldPos, 1);
    pMatrix->scale.x = worldPos.x;
    pMatrix->scale.y = worldPos.y;
    pMatrix->scale.z = worldPos.z;
}

// Get saber blade world matrix for collision. Uses same orientation as visual rendering
// (stdVR_GetControllerViewMatrix) so collision direction matches the visible blade exactly.
// Unlike stdVR_GetControllerWorldMatrix, this does NOT apply the -90 pitch offset.
int stdVR_GetSaberWorldMatrix(int hand, rdMatrix34* pMatrix)
{
    // Use the same function that renders the weapon visual
    // This ensures collision direction matches the blade direction exactly
    if (!stdVR_GetControllerViewMatrix(hand, pMatrix))
        return 0;

    // Normalize orientation vectors (visual rendering may have scale applied)
    rdVector_Normalize3Acc(&pMatrix->rvec);
    rdVector_Normalize3Acc(&pMatrix->lvec);
    rdVector_Normalize3Acc(&pMatrix->uvec);
    return 1;
}

void stdVR_GetControllerAimDirection(int hand, rdVector3* pDirection)
{
    rdMatrix34 worldMat;
    if (stdVR_GetSaberWorldMatrix(hand, &worldMat)) {
        rdVector_Copy3(pDirection, &worldMat.lvec);
    } else {
        rdVector_Zero3(pDirection);
        pDirection->y = 1.0f; // Default: forward
    }
}

// Check if a velocity-triggered attack occurred this frame for dominant hand
int stdVR_IsSwingTriggered(void)
{
    stdVR_ControllerState* pCtrl = stdVR_GetDominantController();
    if (!pCtrl || !pCtrl->bTracking) {
        return 0;
    }
    return pCtrl->motion.bVelocityTriggeredAttack && !pCtrl->motion.bVelocityTriggeredAttackLast;
}

// Get the swing speed of dominant hand (m/s)
float stdVR_GetSwingSpeed(void)
{
    stdVR_ControllerState* pCtrl = stdVR_GetDominantController();
    if (!pCtrl || !pCtrl->bTracking) {
        return 0.0f;
    }
    return pCtrl->motion.swingSpeed;
}

// Get controller pose as a view matrix (for rendering weapon at controller position)
// Returns 1 if successful, 0 if controller not tracking
//
// This builds a view matrix that positions AND orients the weapon according to the
// controller, combining game camera body orientation with controller orientation.
// Added: Internal helper for controller view matrix with optional weapon offsets
static int stdVR_GetControllerViewMatrixInternal(int hand, rdMatrix34* pViewMat, int bApplyWeaponOffset)
{
    if (!pViewMat || hand < 0 || hand >= STDVR_CONTROLLER_COUNT) {
        return 0;
    }

    stdVR_ControllerState* pCtrl = &stdVR_clientInfo.controllers[hand];
    if (!pCtrl->bTracking) {
        return 0;
    }

    // We need the game camera (body orientation/position) to combine with controller
    if (!stdVR_currentGameCameraValid) {
        return 0;
    }

    const rdMatrix34* pGameCamera = &stdVR_currentGameCamera;

    // World scale converts VR meters to game world units
    float worldScale = stdVR_config.worldScale;
    if (worldScale <= 0.0f) worldScale = 1.0f;

    // ==========================================
    // ORIENTATION: Combine game camera body rotation with controller orientation
    // ==========================================
    // Just like eye view: combined = gameCamera * vrPose
    // But for controller, we use controller pose instead of eye pose
    //
    // This makes the weapon point where the controller points, but rotated
    // into game world space by the body direction.

    // Use per-weapon offsets if available, otherwise fall back to global config
    stdVR_WeaponOffset* pWeaponOffset = bApplyWeaponOffset ? stdVR_GetCurrentWeaponOffset() : NULL;
    rdVector3 pitchAngles;
    stdVR_BuildWeaponRotationAdjust(hand, pWeaponOffset, bApplyWeaponOffset, bApplyWeaponOffset,
                                    &pitchAngles);

    // First apply the rotation adjustment to the controller pose if configured
    rdMatrix34 adjustedPose;
    if (pitchAngles.x != 0.0f || pitchAngles.y != 0.0f || pitchAngles.z != 0.0f) {
        rdMatrix34 pitchRot;
        rdMatrix_BuildRotate34(&pitchRot, &pitchAngles);

        // adjustedPose = controllerPose * rotation
        rdMatrix_Multiply34(&adjustedPose, &pCtrl->poseMatrix, &pitchRot);
    } else {
        rdMatrix_Copy34(&adjustedPose, &pCtrl->poseMatrix);
    }

    rdMatrix34 combined;
    rdMatrix_Multiply34(&combined, pGameCamera, &adjustedPose);

    // ==========================================
    // POSITION: Game camera position + controller offset from HMD
    // ==========================================
    // Controller position is in VR tracking space (meters, JKDF2 coords)
    // We need offset from HMD to controller, transformed by body orientation

    rdVector3 ctrlPos = pCtrl->position;
    rdVector3 hmdPos = stdVR_clientInfo.hmdPosition;

    // Offset from HMD to controller (in meters)
    rdVector3 offset;
    offset.x = ctrlPos.x - hmdPos.x;
    offset.y = ctrlPos.y - hmdPos.y;
    offset.z = ctrlPos.z - hmdPos.z;

    // Apply weapon position offset in controller local space (only for weapon hand)
    if (bApplyWeaponOffset) {
        float weapOffX = pWeaponOffset ? pWeaponOffset->offsetX : 0.f;
        float weapOffY = pWeaponOffset ? pWeaponOffset->offsetY : 0.f;
        float weapOffZ = pWeaponOffset ? pWeaponOffset->offsetZ : 0.f;

        // Added: mirror the palm-out (X) component for the left hand so weapon
        // offsets calibrated against the right hand align correctly. OpenXR grip
        // pose +X is "palm normal" — a body-mirrored axis — while +Y and +Z are
        // not mirrored, so only X needs flipping.
        if (hand == STDVR_CONTROLLER_LEFT) {
            weapOffX = -weapOffX;
        }

        if (weapOffX != 0.0f || weapOffY != 0.0f || weapOffZ != 0.0f) {
            rdVector3 localOffset;
            localOffset.x = weapOffX;
            localOffset.y = weapOffY;
            localOffset.z = weapOffZ;

            // Transform local offset by controller orientation
            rdVector3 transformedOffset;
            rdMatrix_TransformVector34(&transformedOffset, &localOffset, &adjustedPose);

            offset.x += transformedOffset.x;
            offset.y += transformedOffset.y;
            offset.z += transformedOffset.z;
        }
    }

    // Scale offset to game world units (1:1 with world scale)
    float weaponScale = worldScale;
    rdVector3 scaledOffset;
    scaledOffset.x = offset.x * weaponScale;
    scaledOffset.y = offset.y * weaponScale;
    scaledOffset.z = offset.z * weaponScale;

    // Transform offset by game camera orientation (rotate into world space)
    rdVector3 worldOffset;
    rdMatrix_TransformVector34(&worldOffset, &scaledOffset, pGameCamera);

    // Also include HMD offset from tracking origin (same as eye view does)
    rdVector3 hmdOffset;
    hmdOffset.x = hmdPos.x * worldScale;
    hmdOffset.y = hmdPos.y * worldScale;
    hmdOffset.z = hmdPos.z * worldScale;

    // Altered: match stdVR_CombineCameraWithEye. The body follows lateral HMD
    // movement when 6DoF is enabled, so controller weapons should be positioned
    // relative to the followed body, not body plus absolute HMD X/Y again.
    if (stdVR_config.sixDoFScale > 0.0f) {
        hmdOffset.x = 0.0f;
        hmdOffset.y = 0.0f;
    }

    rdVector3 hmdOffsetWorld;
    rdMatrix_TransformVector34(&hmdOffsetWorld, &hmdOffset, pGameCamera);

    // Final position = game camera position + HMD offset + controller offset
    combined.scale.x = pGameCamera->scale.x + hmdOffsetWorld.x + worldOffset.x;
    combined.scale.y = pGameCamera->scale.y + hmdOffsetWorld.y + worldOffset.y;
    combined.scale.z = pGameCamera->scale.z + hmdOffsetWorld.z + worldOffset.z;

    // Altered: Apply both fixed height adjustment and player height offset
    // so the weapon model matches the camera viewpoint height
    combined.scale.z += stdVR_config.fixedHeightAdjustment * worldScale;
    if (stdVR_config.heightOffset != 0.0f) {
        combined.scale.z += stdVR_config.heightOffset * worldScale;
    }

    rdMatrix_Copy34(pViewMat, &combined);

    return 1;
}

int stdVR_GetControllerViewMatrix(int hand, rdMatrix34* pViewMat)
{
    return stdVR_GetControllerViewMatrixInternal(hand, pViewMat, 1);
}

// Added: Raw controller view matrix without per-weapon pitch/position offsets.
// Used for off-hand rendering so it doesn't shift when changing weapons.
int stdVR_GetControllerViewMatrixRaw(int hand, rdMatrix34* pViewMat)
{
    return stdVR_GetControllerViewMatrixInternal(hand, pViewMat, 0);
}

// ============================================================================
// In-world weapon crosshair — casts a ray along the weapon aim direction
// and draws a small dot at the hit point
// ============================================================================
void stdVR_DrawWeaponCrosshair(void)
{
    extern sithThing* sithPlayer_pLocalPlayerThing;

    if (!stdVR_config.bWeaponCrosshair) return;
    if (!sithPlayer_pLocalPlayerThing) return;
    if (!sithPlayer_pLocalPlayerThing->sector) return;
    if (!stdVR_motionConfig.bMotionAimEnabled) return;

    // Don't show crosshair for fists or lightsaber
    extern sithPlayerInfo* sithPlayer_pLocalPlayer;
    if (sithPlayer_pLocalPlayer) {
        int weap = sithPlayer_pLocalPlayer->curWeapon;
        if (weap == SITHBIN_FISTS || weap == SITHBIN_LIGHTSABER
            || weap == SITHBIN_MOTS_FISTS || weap == SITHBIN_MOTS_LIGHTSABER) {
            // Destroy any existing crosshair thing
            if (stdVR_pCrosshairThing) {
                sithThing_Destroy(stdVR_pCrosshairThing);
                stdVR_pCrosshairThing = NULL;
            }
            return;
        }
    }

    sithThing* pPlayer = sithPlayer_pLocalPlayerThing;

    // Get fire origin and aim direction
    int hand = stdVR_GetDominantHand();
    stdVR_ControllerState* pCtrl = stdVR_GetDominantController();
    if (!pCtrl || !pCtrl->bTracking) return;

    rdVector3 firePos;
    stdVR_ControllerToWorld(hand, &firePos, 0);

    rdMatrix34 aimMat;
    stdVR_GetControllerWorldMatrix(hand, &aimMat);

    // Multi-sector raycast — step through adjoins (sector portals) to hit solid walls
    float maxDist = 50.0f;
    float hitDist = maxDist;
    rdVector3 rayOrigin;
    rdVector_Copy3(&rayOrigin, &firePos);
    sithSector* raySector = pPlayer->sector;
    float accumulated = 0.0f;

    for (int i = 0; i < 20 && raySector; i++) {
        float remaining = maxDist - accumulated;
        if (remaining <= 0.001f) break;

        sithCollision_SearchRadiusForThings(raySector, pPlayer, &rayOrigin, &aimMat.lvec, remaining, 0.0f, 0x1);
        sithCollisionSearchEntry* pHit = sithCollision_NextSearchResult();

        if (!pHit) {
            // No hit in this sector — ray exits into void
            sithCollision_SearchClose();
            break;
        }

        // Check if we hit an adjoin (sector portal)
        if (pHit->surface && pHit->surface->adjoin) {
            float d = pHit->distance;
            sithSector* nextSector = pHit->surface->adjoin->sector;
            sithCollision_SearchClose();

            // Advance ray origin past the portal
            accumulated += d + 0.001f;
            rayOrigin.x = firePos.x + aimMat.lvec.x * accumulated;
            rayOrigin.y = firePos.y + aimMat.lvec.y * accumulated;
            rayOrigin.z = firePos.z + aimMat.lvec.z * accumulated;
            raySector = nextSector;
            continue;
        }

        // Solid hit
        hitDist = accumulated + pHit->distance;
        sithCollision_SearchClose();
        break;
    }
    if (hitDist >= maxDist) return;

    // Hit position
    rdVector3 hitPos;
    hitPos.x = firePos.x + aimMat.lvec.x * (hitDist - 0.003f);
    hitPos.y = firePos.y + aimMat.lvec.y * (hitDist - 0.003f);
    hitPos.z = firePos.z + aimMat.lvec.z * (hitDist - 0.003f);

    // Find sector for hit position
    sithSector* hitSector = sithCollision_GetSectorLookAt(pPlayer->sector, &firePos, &hitPos, 0.0f);
    if (!hitSector) hitSector = pPlayer->sector;

    // Get the bolt template (once) — we only need its rdThing/model, not a full sithThing
    static sithThing* sBoltTemplate = NULL;
    static int sBoltSearched = 0;
    if (!sBoltSearched) {
        sBoltSearched = 1;
        sBoltTemplate = sithTemplate_GetEntryByName("+bryarbolt");
        if (!sBoltTemplate) sBoltTemplate = sithTemplate_GetEntryByName("+stlaser");
    }
    if (!sBoltTemplate) return;

    // Orient along aim direction and scale down
    rdMatrix34 xhairMat;
    rdMatrix_Copy34(&xhairMat, &aimMat);
    float modelScale = 0.6f;
    xhairMat.rvec.x *= modelScale; xhairMat.rvec.y *= modelScale; xhairMat.rvec.z *= modelScale;
    xhairMat.uvec.x *= modelScale; xhairMat.uvec.y *= modelScale; xhairMat.uvec.z *= modelScale;
    xhairMat.lvec.x *= modelScale; xhairMat.lvec.y *= modelScale; xhairMat.lvec.z *= modelScale;
    rdVector_Copy3(&xhairMat.scale, &hitPos);

    // Draw the model directly via rdThing_Draw — bypasses sithThing system entirely,
    // so no dynamic lighting, no physics, no collision, no weapon processing.
    rdThing_Draw(&sBoltTemplate->rdthing, &xhairMat);
}

// Added: alignment aid. Draws the controller's three axes as world-space rays, so a weapon
// model can be lined up against the direction the projectile takes. Forward is the long ray,
// because that is the one that matters for aiming.
//   forward (green)  = the aim direction, and the path of the projectile
//   right   (red)
//   up      (blue)
// This uses the aim matrix, so the rays show the FIRE direction. The per-weapon alignment
// angles do not move them. That is the point: align the model until the barrel lies along the
// green ray.
void stdVR_DrawControllerAxisRays(int hand)
{
    if (!stdVR_bEnabled || hand < 0 || hand >= STDVR_CONTROLLER_COUNT) {
        return;
    }

    stdVR_ControllerState* pCtrl = &stdVR_clientInfo.controllers[hand];
    if (!pCtrl->bTracking) {
        return;
    }

    rdMatrix34 aimMat;
    stdVR_GetControllerWorldMatrix(hand, &aimMat);

    rdVector3 origin = aimMat.scale;
    rdVector3 end;

    // Forward. Long, so it shows where the shot goes.
    rdVector_Copy3(&end, &origin);
    rdVector_MultAcc3(&end, &aimMat.lvec, STDVR_AXIS_RAY_FORWARD_LEN);
    std3D_DrawWorldLine(&origin, &end, 40, 255, 40, 220, 3.0f);

    // Right.
    rdVector_Copy3(&end, &origin);
    rdVector_MultAcc3(&end, &aimMat.rvec, STDVR_AXIS_RAY_SIDE_LEN);
    std3D_DrawWorldLine(&origin, &end, 255, 40, 40, 220, 3.0f);

    // Up.
    rdVector_Copy3(&end, &origin);
    rdVector_MultAcc3(&end, &aimMat.uvec, STDVR_AXIS_RAY_SIDE_LEN);
    std3D_DrawWorldLine(&origin, &end, 60, 120, 255, 220, 3.0f);
}

// Debug: Draw controller axes at given world position using immediate mode GL
// This draws RGB axes (X=Red, Y=Green, Z=Blue) to visualize controller orientation
void stdVR_DrawDebugControllerAxes(int hand)
{
    if (!stdVR_bEnabled) return;

    stdVR_ControllerState* pCtrl = &stdVR_clientInfo.controllers[hand];
    if (!pCtrl->bTracking) return;

    // Get controller world matrix
    rdMatrix34 ctrlWorld;
    stdVR_GetControllerWorldMatrix(hand, &ctrlWorld);

    // Save GL state
    GLint oldProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    GLboolean oldDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean oldBlend = glIsEnabled(GL_BLEND);

    // Use fixed function pipeline for debug draw (no shader)
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);  // Draw on top of everything
    glDisable(GL_BLEND);
    glLineWidth(3.0f);

    // Extract position and axes from matrix
    float px = ctrlWorld.scale.x;
    float py = ctrlWorld.scale.y;
    float pz = ctrlWorld.scale.z;

    // Axis length in world units
    float axisLen = 0.1f;

    // X axis (Red) - right vector
    float rx = ctrlWorld.rvec.x * axisLen;
    float ry = ctrlWorld.rvec.y * axisLen;
    float rz = ctrlWorld.rvec.z * axisLen;

    // Y axis (Green) - forward vector
    float fx = ctrlWorld.lvec.x * axisLen;
    float fy = ctrlWorld.lvec.y * axisLen;
    float fz = ctrlWorld.lvec.z * axisLen;

    // Z axis (Blue) - up vector
    float ux = ctrlWorld.uvec.x * axisLen;
    float uy = ctrlWorld.uvec.y * axisLen;
    float uz = ctrlWorld.uvec.z * axisLen;

    // Draw axes using deprecated immediate mode (simple debug viz)
#if defined(TARGET_ANDROID_NATIVE_GLES)
    // Skip debug axis rendering on native GLES (uses legacy GL immediate mode)
    // Native GLES3 doesn't support glBegin/glEnd - would need shader-based line drawing
    (void)px; (void)py; (void)pz;
    (void)rx; (void)ry; (void)rz;
    (void)fx; (void)fy; (void)fz;
    (void)ux; (void)uy; (void)uz;
#else
    // gl4es translates these to GLES on Android
    glBegin(GL_LINES);
    // X axis - Red
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f(px, py, pz);
    glVertex3f(px + rx, py + ry, pz + rz);
    // Y axis - Green
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f(px, py, pz);
    glVertex3f(px + fx, py + fy, pz + fz);
    // Z axis - Blue
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex3f(px, py, pz);
    glVertex3f(px + ux, py + uy, pz + uz);
    glEnd();
#endif

    // Restore GL state
    if (oldDepthTest) glEnable(GL_DEPTH_TEST);
    if (oldBlend) glEnable(GL_BLEND);
    glUseProgram(oldProgram);
}

// Sync jkPlayer VR settings to stdVR_config
void stdVR_SyncConfigFromJkPlayer(void)
{
    // Import from jkPlayer.c
    extern int jkPlayer_vrEnabled;
    extern int jkPlayer_vrSnapTurnAngle;


    extern int jkPlayer_vrWeaponPitchAdjust;
    extern int jkPlayer_vrSmoothTurnSpeed;
    extern float jkPlayer_vrWorldScale;
    extern float jkPlayer_vrHeightOffset;
    extern int jkPlayer_vrWeaponCrosshair;
    extern int jkPlayer_vrDominantHand;
    extern int jkPlayer_vrSwapSticks;
    extern int jkPlayer_vrMoveDirection;
    extern float jkPlayer_vrSupersampling;
    extern float jkPlayer_vr6DoFScale;

    // Don't let profile load disable VR if it's already running
    // The enabled state should only be changed through explicit user action
    // or at startup before VR is initialized
    if (!stdVR_bEnabled || !stdVR_IsSessionRunning()) {
        stdVR_bEnabled = jkPlayer_vrEnabled;
    }

    // Movement/turn settings
    stdVR_config.moveDirection = jkPlayer_vrMoveDirection; // 0=head, 1=controller
    if (jkPlayer_vrSnapTurnAngle > 0) {
        stdVR_config.turnMode = STDVR_TURN_SNAP;
        stdVR_config.snapTurnAngle = jkPlayer_vrSnapTurnAngle;
    } else {
        stdVR_config.turnMode = STDVR_TURN_SMOOTH;
    }
    stdVR_config.smoothTurnSpeed = (float)jkPlayer_vrSmoothTurnSpeed;

    // Scale and comfort
    stdVR_config.heightOffset = jkPlayer_vrHeightOffset;
    stdVR_config.bWeaponCrosshair = jkPlayer_vrWeaponCrosshair;

    // Handedness
    stdVR_config.dominantHand = jkPlayer_vrDominantHand;
    stdVR_config.bSwapSticks = jkPlayer_vrSwapSticks;

    // 6DoF head-driven body movement scale
    stdVR_config.sixDoFScale = jkPlayer_vr6DoFScale;

    // Quality
    stdVR_config.supersampling = jkPlayer_vrSupersampling;
    if (stdVR_config.supersampling <= 0.0f) {
        stdVR_config.supersampling = 1.0f;
    }
    // If the supersampling (VR render scale) changed, ask the OpenXR backend to rebuild the
    // MultiView swapchain at the new size. The request is a no-op until a session is running
    // (the initial swapchain is already built at the loaded value) and the rebuild itself is
    // deferred to between-frames inside the backend.
    {
        static float stdVR_lastAppliedSupersampling = -1.0f;
        if (stdVR_config.supersampling != stdVR_lastAppliedSupersampling) {
            stdVR_lastAppliedSupersampling = stdVR_config.supersampling;
            stdVR_OpenXR_RequestSupersampleRebuild();
        }
    }

    // Display refresh rate. Only ask the runtime when the value changed, because the profile
    // sync also runs during a level load.
    {
        extern float jkPlayer_vrRefreshRate;
        static float stdVR_lastAppliedRefreshRate = -1.0f;
        if (jkPlayer_vrRefreshRate > 0.0f && jkPlayer_vrRefreshRate != stdVR_lastAppliedRefreshRate) {
            stdVR_lastAppliedRefreshRate = jkPlayer_vrRefreshRate;
            stdVR_ApplyRefreshRate(jkPlayer_vrRefreshRate);
        }
    }

    //Motion Config
    stdVR_motionConfig.weaponPitchAdjust = (float)jkPlayer_vrWeaponPitchAdjust;
}

// Push stdVR_config settings back to jkPlayer
void stdVR_SyncConfigToJkPlayer(void)
{
    extern int jkPlayer_vrEnabled;
    extern int jkPlayer_vrSnapTurnAngle;
    extern int jkPlayer_vrSmoothTurnSpeed;
    extern int jkPlayer_vrWeaponPitchAdjust;
    extern float jkPlayer_vrWorldScale;
    extern float jkPlayer_vrHeightOffset;
    extern int jkPlayer_vrWeaponCrosshair;
    extern int jkPlayer_vrDominantHand;
    extern int jkPlayer_vrSwapSticks;
    extern int jkPlayer_vrMoveDirection;
    extern float jkPlayer_vrSupersampling;
    extern float jkPlayer_vr6DoFScale;

    jkPlayer_vrEnabled = stdVR_bEnabled;
    jkPlayer_vrMoveDirection = stdVR_config.moveDirection;
    if (stdVR_config.turnMode == STDVR_TURN_SNAP) {
        jkPlayer_vrSnapTurnAngle = stdVR_config.snapTurnAngle;
    } else {
        jkPlayer_vrSnapTurnAngle = 0;
    }
    jkPlayer_vrSmoothTurnSpeed = (int)stdVR_config.smoothTurnSpeed;
    jkPlayer_vrHeightOffset = stdVR_config.heightOffset;
    jkPlayer_vrWeaponCrosshair = stdVR_config.bWeaponCrosshair;
    jkPlayer_vrDominantHand = stdVR_config.dominantHand;
    jkPlayer_vrSwapSticks = stdVR_config.bSwapSticks;
    jkPlayer_vrSupersampling = stdVR_config.supersampling;
    jkPlayer_vr6DoFScale = stdVR_config.sixDoFScale;

    //Motion Config
    jkPlayer_vrWeaponPitchAdjust = (int)stdVR_motionConfig.weaponPitchAdjust;
}

// Screen layer mode functions (for menus/cinematics)
// Previous screen layer state for detecting transitions
static int stdVR_prevScreenLayerState = 0;

// Check if we should use screen layer mode (2D quad) instead of stereo projection
int stdVR_UseScreenLayer(void)
{
    // Import jkGame_isDDraw from jkGame.c
    // jkGame_isDDraw is 0 for menus/2D mode, 1 for 3D gameplay
    extern int jkGame_isDDraw;
    extern int jkGuiBuildMulti_bRendering;
    extern int jkSmack_GetCurrentGuiState(void);
#ifdef QUAKE_CONSOLE
    extern int jkQuakeConsole_bOpen;
#endif

    // Mirror JKXR-style logic: use screen layer when UI/cinematics/menus are active
    int guiState = jkSmack_GetCurrentGuiState();
    int inGameplay = (guiState == JK_GAMEMODE_GAMEPLAY);

    int shouldUseScreenLayer = (jkGame_isDDraw == 0) || !inGameplay || jkGuiBuildMulti_bRendering;
#ifdef QUAKE_CONSOLE
    if (jkQuakeConsole_bOpen) {
        shouldUseScreenLayer = 1;
    }
#endif

    // Update the client info
    stdVR_clientInfo.bUseScreenLayer = shouldUseScreenLayer;

    // Detect transition INTO screen layer mode - snap position/orientation
    // Only log transitions, not every frame
    static int f = 0;
    if (f++ < 120 || (shouldUseScreenLayer && !stdVR_prevScreenLayerState)) {
        stdVR_UpdateScreenLayerSnap();
        VR_Log("stdVR: Entering screen layer mode\n");
    }
    else if (!shouldUseScreenLayer && stdVR_prevScreenLayerState) {
        VR_Log("stdVR: Exiting screen layer mode (3D gameplay)\n");
    }

    stdVR_prevScreenLayerState = shouldUseScreenLayer;

    return shouldUseScreenLayer;
}

// Explicitly set screen layer mode
void stdVR_SetScreenLayerMode(int bEnable)
{
    if (bEnable && !stdVR_clientInfo.bUseScreenLayer) {
        stdVR_UpdateScreenLayerSnap();
    }
    stdVR_clientInfo.bUseScreenLayer = bEnable;
    stdVR_prevScreenLayerState = bEnable;
}

// Update the snap position/orientation when entering screen layer mode
void stdVR_UpdateScreenLayerSnap(void)
{
    // Store current HMD position and yaw for screen placement
    rdVector_Copy3(&stdVR_clientInfo.screenLayerSnapPos, &stdVR_clientInfo.hmdPosition);
    stdVR_clientInfo.screenLayerSnapYaw = stdVR_clientInfo.hmdOrientation.y;

    // Initialize screen layer parameters if not already set
    if (stdVR_clientInfo.screenLayerDistance <= 0.0f) {
        stdVR_clientInfo.screenLayerDistance = 4.0f;  // 4 meters away
    }
    if (stdVR_clientInfo.screenLayerWidth <= 0.0f) {
        stdVR_clientInfo.screenLayerWidth = 6.0f;     // 6 meters wide
    }
    if (stdVR_clientInfo.screenLayerHeight <= 0.0f) {
        stdVR_clientInfo.screenLayerHeight = 4.5f;    // 4.5 meters tall (4:3 aspect)
    }

    VR_Log("stdVR: Screen layer snap - pos(%.2f, %.2f, %.2f), yaw=%.1f, dist=%.1f\n",
        stdVR_clientInfo.screenLayerSnapPos.x,
        stdVR_clientInfo.screenLayerSnapPos.y,
        stdVR_clientInfo.screenLayerSnapPos.z,
        stdVR_clientInfo.screenLayerSnapYaw,
        stdVR_clientInfo.screenLayerDistance);
}

// Get the screen layer distance from player
float stdVR_GetScreenLayerDistance(void)
{
    if (stdVR_clientInfo.screenLayerDistance <= 0.0f) {
        return 4.0f;  // Default distance
    }
    return stdVR_clientInfo.screenLayerDistance;
}

// ============================================================================
// VR Menu Cursor - Controller-based pointing for menu interaction
// ============================================================================

// Previous trigger state for edge detection
static int stdVR_prevMenuTriggerDown = 0;
#define DEG2RAD( x )	((float)(x) * (float)(M_PI / 180.f))

// Update cursor position from controller angles
// Uses the right controller (or dominant hand) to aim at the screen
void stdVR_UpdateMenuCursor(void)
{
    // Only active in screen layer mode
    if (!stdVR_clientInfo.bUseScreenLayer) {
        stdVR_clientInfo.bMenuCursorActive = 0;
        stdVR_clientInfo.bMenuTriggerPressed = 0;
        stdVR_clientInfo.bMenuTriggerReleased = 0;
        return;
    }

    stdVR_clientInfo.bMenuCursorActive = 1;

    // Use dominant hand controller for menu pointing
    int controllerIndex = stdVR_config.dominantHand;
    stdVR_ControllerState* pController = &stdVR_clientInfo.controllers[controllerIndex];

    // Altered: cast the controller's aim ray at the menu panel instead of mapping raw
    // controller angles. The old map spread the whole panel width across 60 degrees while
    // the panel subtends about 74, and it compared the hand's yaw against the HEAD's snap
    // yaw, so the cursor never landed where the player aimed.
    {
        float yawRad = DEG2RAD(stdVR_clientInfo.screenLayerSnapYaw);
        float sinYaw = sinf(yawRad);
        float cosYaw = cosf(yawRad);

        // Panel basis in JKDF2 axes (x=right, y=forward, z=up), matching the quad layer.
        rdVector3 panelFwd = { -sinYaw, cosYaw, 0.0f };
        rdVector3 panelRight = { cosYaw, sinYaw, 0.0f };

        float distance = stdVR_clientInfo.screenLayerDistance;
        if (distance <= 0.0f) distance = 4.0f;
        float panelWidth = stdVR_clientInfo.screenLayerWidth;
        if (panelWidth <= 0.0f) panelWidth = 6.0f;
        float panelHeight = stdVR_clientInfo.screenLayerHeight;
        if (panelHeight <= 0.0f) panelHeight = 4.5f;

        rdVector3 panelCenter;
        panelCenter.x = stdVR_clientInfo.screenLayerSnapPos.x + panelFwd.x * distance;
        panelCenter.y = stdVR_clientInfo.screenLayerSnapPos.y + panelFwd.y * distance;
        panelCenter.z = stdVR_clientInfo.screenLayerSnapPos.z;

        // The grip pose needs the same -90 pitch the aim path uses before its Y axis points
        // where the player points.
        rdMatrix34 aimPose, pitchRot;
        rdVector3 pitchAngles = { -90.0f, 0.0f, 0.0f };
        rdMatrix_BuildRotate34(&pitchRot, &pitchAngles);
        rdMatrix_Multiply34(&aimPose, &pController->poseMatrix, &pitchRot);

        rdVector3 rayDir = aimPose.lvec;
        rdVector3 rayOrigin = pController->position;

        float denom = rdVector_Dot3(&rayDir, &panelFwd);
        if (denom > 0.001f) {
            rdVector3 toCenter;
            toCenter.x = panelCenter.x - rayOrigin.x;
            toCenter.y = panelCenter.y - rayOrigin.y;
            toCenter.z = panelCenter.z - rayOrigin.z;

            float t = rdVector_Dot3(&toCenter, &panelFwd) / denom;
            if (t > 0.0f) {
                rdVector3 rel;
                rel.x = rayOrigin.x + rayDir.x * t - panelCenter.x;
                rel.y = rayOrigin.y + rayDir.y * t - panelCenter.y;
                rel.z = rayOrigin.z + rayDir.z * t - panelCenter.z;

                float u = rdVector_Dot3(&rel, &panelRight) / panelWidth + 0.5f;
                float v = 0.5f - rel.z / panelHeight;

                stdVR_clientInfo.menuCursorX = stdMath_Clamp(u, 0.0f, 1.0f);
                stdVR_clientInfo.menuCursorY = stdMath_Clamp(v, 0.0f, 1.0f);
            }
        }
        // A ray that misses the panel keeps the last position, so the cursor does not jump.
    }

    // Convert to screen pixel coordinates (assuming 640x480 menu resolution)
    stdVR_clientInfo.menuCursorScreenX = stdMath_ClampInt((int)(stdVR_clientInfo.menuCursorX * 640.0f), 0, 639);
    stdVR_clientInfo.menuCursorScreenY = stdMath_ClampInt((int)(stdVR_clientInfo.menuCursorY * 480.0f), 0, 479);

    // Handle trigger input for "clicks"
    // Use the trigger from the same controller
    int triggerDown = 0;
    if (controllerIndex == STDVR_CONTROLLER_RIGHT) {
        triggerDown = (stdVR_clientInfo.triggerRight > 0.5f) ? 1 : 0;
    } else {
        triggerDown = (stdVR_clientInfo.triggerLeft > 0.5f) ? 1 : 0;
    }

    // Edge detection for press/release events
    stdVR_clientInfo.bMenuTriggerPressed = (triggerDown && !stdVR_prevMenuTriggerDown);
    stdVR_clientInfo.bMenuTriggerReleased = (!triggerDown && stdVR_prevMenuTriggerDown);
    stdVR_clientInfo.bMenuTriggerDown = triggerDown;

    stdVR_prevMenuTriggerDown = triggerDown;
    // Menu cursor logging removed - too verbose for normal operation
}

// Get cursor screen position
void stdVR_GetMenuCursorPos(int* pX, int* pY)
{
    if (pX) *pX = stdVR_clientInfo.menuCursorScreenX;
    if (pY) *pY = stdVR_clientInfo.menuCursorScreenY;
}

// Is cursor active (only in screen layer mode)
int stdVR_IsMenuCursorActive(void)
{
    return stdVR_clientInfo.bMenuCursorActive;
}

// Was trigger pressed this frame (for mouse down)
int stdVR_GetMenuTriggerPressed(void)
{
    return stdVR_clientInfo.bMenuTriggerPressed;
}

// Was trigger released this frame (for mouse up/click)
int stdVR_GetMenuTriggerReleased(void)
{
    return stdVR_clientInfo.bMenuTriggerReleased;
}

#endif // PLATFORM_VR

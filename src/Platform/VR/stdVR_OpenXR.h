#ifndef _STDVR_OPENXR_H
#define _STDVR_OPENXR_H

// Added: OpenXR platform layer internal header

#ifdef PLATFORM_VR

#ifdef __cplusplus
extern "C" {
#endif

// OpenXR initialization and shutdown
int stdVR_OpenXR_Init(void);
void stdVR_OpenXR_Shutdown(void);

// Session management
int stdVR_OpenXR_CreateSession(void* pGLContext);
void stdVR_OpenXR_DestroySession(void);

// Event polling (call every frame to advance session state)
void stdVR_OpenXR_PollEvents(void);

// Frame timing
int stdVR_OpenXR_WaitFrame(void);
int stdVR_OpenXR_BeginFrame(void);
int stdVR_OpenXR_EndFrame(void);
int stdVR_OpenXR_EndFrameEmpty(void);

// Per-eye rendering
int stdVR_OpenXR_PrepareEyeBuffer(int eye);
int stdVR_OpenXR_FinishEyeBuffer(int eye);
int stdVR_OpenXR_GetCurrentEyeFBO(int eye);
int stdVR_OpenXR_GetCurrentEye(void);

// MultiView rendering (single-pass stereo for Quest VR)
int stdVR_OpenXR_IsMultiViewSupported(void);
int stdVR_OpenXR_PrepareMultiViewBuffer(void);
int stdVR_OpenXR_FinishMultiViewBuffer(void);
int stdVR_OpenXR_GetMultiViewFBO(void);

// Request a rebuild of the MultiView swapchain (+ its FBOs/depth) at the current supersampling
// (VR render scale). Safe to call from any thread/path; the rebuild itself is deferred to
// between-frames (BeginFrame). No-op unless a MultiView session is running.
void stdVR_OpenXR_RequestSupersampleRebuild(void);

// Display refresh rate (XR_FB_display_refresh_rate). The rate list is empty when the runtime
// does not support the extension.
void stdVR_OpenXR_InitRefreshRates(void);
int stdVR_OpenXR_RequestRefreshRate(float hz);
void stdVR_OpenXR_GetStereoOffsets(float* leftOffset, float* rightOffset);

// Tracking
void stdVR_OpenXR_UpdateTracking(void);

// Input
void stdVR_OpenXR_UpdateInput(void);

// Haptics
void stdVR_OpenXR_TriggerHaptic(int hand, float amplitude, float duration, float frequency);
void stdVR_OpenXR_StopHaptic(int hand);

// Utility
void stdVR_OpenXR_RecenterView(void);
const char* stdVR_OpenXR_GetRuntimeName(void);
int stdVR_OpenXR_IsExitRequested(void);

// Desktop mirror: blit the last-rendered left-eye VR buffer to the SDL window default framebuffer.
// stdVR_mirrorFlip: 0=none,1=flipY,2=flipX,3=flipXY (cycled with F9).
extern int stdVR_mirrorFlip;
void stdVR_OpenXR_MirrorToWindow(int windowWidth, int windowHeight);
void stdVR_OpenXR_DumpEyeMirror(void);  // Debug: dump eye/projection buffer to vrtest_eye.ppm

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_VR

#endif // _STDVR_OPENXR_H

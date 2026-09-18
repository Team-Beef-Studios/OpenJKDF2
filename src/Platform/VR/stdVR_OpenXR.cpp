// Added: OpenXR platform layer implementation
// Adapted from JKXR patterns for OpenJKDF2

#ifdef PLATFORM_VR

// MULTIVIEW_ENABLED is now a global compile definition (see cmake_modules/plat_feat_vr.cmake)
// so that std3D.c and other translation units share the same multiview gate. Keep a local
// fallback define in case this file is ever built without that definition.
#ifndef MULTIVIEW_ENABLED
#define MULTIVIEW_ENABLED
#endif

#define STDVR_HOTPATH_DEBUG 0

// Include game headers first (they have correct Windows include order)
extern "C" {
#include "stdPlatform.h"
#include "Primitives/rdMatrix.h"
#include "Primitives/rdVector.h"
#include "Platform/std3D.h"  // Added: for VR FBO override
}

// Define graphics API before including OpenXR
#ifdef _WIN32
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#elif defined(__ANDROID__)
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#endif

// Include OpenGL headers
#ifdef __ANDROID__
#include <EGL/egl.h>
#include <unistd.h>  // For usleep
#include <android/log.h>  // For logcat output in VR_Log

#if defined(TARGET_ANDROID_NATIVE_GLES)
// Native GLES3 for Quest VR - no gl4es translation layer
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <GLES2/gl2ext.h>
#else
// Use gl4es which provides standard GL headers translating to GLES
#include <GL/gl.h>
#include <GL/glext.h>
// gl4es exports these functions but doesn't declare them in headers
extern "C" {
extern void glGenFramebuffers(GLsizei n, GLuint *framebuffers);
extern void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers);
extern void glBindFramebuffer(GLenum target, GLuint framebuffer);
extern void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
extern GLenum glCheckFramebufferStatus(GLenum target);
extern void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers);
extern void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers);
extern void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
extern void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
extern void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
extern GLuint glCreateShader(GLenum type);
extern void glDeleteShader(GLuint shader);
extern GLboolean glIsShader(GLuint shader);
extern GLboolean glIsProgram(GLuint program);
extern void glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
extern void glCompileShader(GLuint shader);
extern void glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
extern void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
extern GLuint glCreateProgram(void);
extern void glDeleteProgram(GLuint program);
extern void glAttachShader(GLuint program, GLuint shader);
extern void glLinkProgram(GLuint program);
extern void glGetProgramiv(GLuint program, GLenum pname, GLint *params);
extern void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
extern void glUseProgram(GLuint program);
extern GLint glGetUniformLocation(GLuint program, const GLchar *name);
extern GLint glGetAttribLocation(GLuint program, const GLchar *name);
extern void glUniform1i(GLint location, GLint v0);
extern void glUniform1f(GLint location, GLfloat v0);
extern void glUniform2f(GLint location, GLfloat v0, GLfloat v1);
extern void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
extern void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
extern void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
extern void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
extern void glEnableVertexAttribArray(GLuint index);
extern void glDisableVertexAttribArray(GLuint index);
extern void glGenBuffers(GLsizei n, GLuint *buffers);
extern void glDeleteBuffers(GLsizei n, const GLuint *buffers);
extern void glBindBuffer(GLenum target, GLuint buffer);
extern void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
extern void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
extern void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
extern void glGenVertexArrays(GLsizei n, GLuint *arrays);
extern void glDeleteVertexArrays(GLsizei n, const GLuint *arrays);
extern void glBindVertexArray(GLuint array);
extern void glDrawBuffers(GLsizei n, const GLenum *bufs);
extern void glActiveTexture(GLenum texture);
extern void glGenerateMipmap(GLenum target);
extern void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
extern void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
}
#endif // TARGET_ANDROID_NATIVE_GLES

// Ensure GL defines are available
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif

#else
#include <GL/glew.h>
#ifdef _WIN32
#include <GL/wglew.h>
#endif
#include <SDL.h>  // For SDL_ShowSimpleMessageBox (fatal VR init errors)
#endif

#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

#include "stdVR_OpenXR.h"
#include "stdVR.h"
#include "stdVR_Types.h"

// JNI header needed for OpenXR Android platform types
#ifdef __ANDROID__
#include <jni.h>
#include <SDL.h>  // For SDL_AndroidGetJNIEnv and SDL_AndroidGetActivity
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>  // For std::clamp
#include <vector>
#include <cstdio>
#include <cstdarg>

// VR log file for debugging (console may not be visible in VR)
static FILE* vrLogFile = nullptr;
static bool vrLogInitialized = false;

// Initialize VR logging immediately
static void VR_InitLog(void)
{
    if (vrLogInitialized) return;
    vrLogInitialized = true;

    // Try multiple locations for the log file
    const char* logPaths[] = {
        "vr_debug.log",  // Current directory (usually game dir)
        "E:/Github/OpenJKDF2/vr_debug.log",
        "C:/Users/ellio/AppData/Local/OpenJKDF2/vr_debug.log",
        NULL
    };

    for (int i = 0; logPaths[i] != NULL; i++) {
        vrLogFile = fopen(logPaths[i], "w");
        if (vrLogFile) {
            fprintf(vrLogFile, "VR log opened at: %s\n", logPaths[i]);
            break;
        }
    }

    if (vrLogFile) {
        fprintf(vrLogFile, "=== OpenJKDF2 VR Debug Log ===\n");
        fprintf(vrLogFile, "Log file created successfully\n\n");
        fflush(vrLogFile);
    }
}

// Set to 1 to enable verbose VR logging to console (performance impact!)
#define VR_VERBOSE_CONSOLE_LOGGING 0

extern "C" void VR_Log(const char* fmt, ...)
{
    // Ensure log is initialized
    if (!vrLogInitialized) {
        VR_InitLog();
    }

    va_list args;
    va_start(args, fmt);

#ifdef __ANDROID__
    // On Android, always log to logcat since file paths may not be writable
    __android_log_vprint(ANDROID_LOG_INFO, "stdVR_OpenXR", fmt, args);
#elif VR_VERBOSE_CONSOLE_LOGGING
    // Print to console (disabled by default for performance)
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    stdPlatform_Printf("%s", buffer);
#endif

    // Write to log file only (no console spam)
    if (vrLogFile) {
        va_list args2;
        va_start(args2, fmt);
        vfprintf(vrLogFile, fmt, args2);
        va_end(args2);
        fflush(vrLogFile); // Added: a killed session would otherwise lose the buffered tail
    }

    va_end(args);
}

// Forward declarations
static void PollEvents(void);

// OpenXR state
static XrInstance xrInstance = XR_NULL_HANDLE;
static XrSystemId xrSystemId = XR_NULL_SYSTEM_ID;
static XrSession xrSession = XR_NULL_HANDLE;
static XrSpace xrLocalSpace = XR_NULL_HANDLE;
static XrSpace xrStageSpace = XR_NULL_HANDLE;
static XrSpace xrViewSpace = XR_NULL_HANDLE;
static XrSessionState xrSessionState = XR_SESSION_STATE_UNKNOWN;
static bool xrSessionRunning = false;

// Added: set when the runtime tells the app to quit (the universal menu's Exit). Without it
// the main loop never learns, and Window.c keeps rebuilding the session once a second.
static bool xrExitRequested = false;

// Added: accumulated recenter offset of xrLocalSpace against the runtime's own LOCAL space.
static float xrRecenterYaw = 0.0f;                  // radians
static XrVector3f xrRecenterPos = { 0.0f, 0.0f, 0.0f };

// Platform-specific swapchain image type
#ifdef __ANDROID__
typedef XrSwapchainImageOpenGLESKHR XrSwapchainImageGL;
#define XR_TYPE_SWAPCHAIN_IMAGE_GL XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR
#else
typedef XrSwapchainImageOpenGLKHR XrSwapchainImageGL;
#define XR_TYPE_SWAPCHAIN_IMAGE_GL XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR
#endif

// Swapchain state
static XrSwapchain xrSwapchains[STDVR_EYE_COUNT] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
static std::vector<XrSwapchainImageGL> xrSwapchainImages[STDVR_EYE_COUNT];
static uint32_t xrSwapchainImageIndex[STDVR_EYE_COUNT] = { 0, 0 };
static XrSwapchain xrNullSwapchains[STDVR_EYE_COUNT] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
static std::vector<XrSwapchainImageGL> xrNullSwapchainImages[STDVR_EYE_COUNT];
static uint32_t xrNullSwapchainImageIndex[STDVR_EYE_COUNT] = { 0, 0 };
static GLuint vrNullFBO[STDVR_EYE_COUNT] = { 0, 0 };
static XrViewConfigurationView xrConfigViews[STDVR_EYE_COUNT];
static XrView xrViews[STDVR_EYE_COUNT];

// MultiView swapchain layer layout.
// Desktop PCVR follows the golden-source (RealRTCWXR/TBXR) VDXR-safe layout: a 3-layer
// array swapchain that skips layer 0 (renders to layers 1 and 2), which is required for
// robust behaviour under Virtual Desktop / VDXR streaming. Android/Quest keeps the standard
// 2-layer layout (layers 0 and 1) to match the shipping Quest build. This is a
// constant-level difference only; the code path is identical (gated on MULTIVIEW_ENABLED).
// Desktop PCVR uses the golden-source (RealRTCWXR/TBXR) 3-layer array swapchain that skips
// layer 0 (render to layers 1,2; submit imageArrayIndex eye+1). This is what the desktop
// Oculus runtime expects — the standard 2-layer/layer-0 layout renders the projection wrong
// per eye on Oculus. Android/Quest keeps the standard 2/0/0.
#if defined(TARGET_ANDROID_NATIVE_GLES)
#define VR_MV_ARRAY_SIZE      2
#define VR_MV_BASE_VIEW_INDEX 0
#define VR_MV_LAYER_OFFSET    0
#else
#define VR_MV_ARRAY_SIZE      3
#define VR_MV_BASE_VIEW_INDEX 1
#define VR_MV_LAYER_OFFSET    1
#endif

// MultiView state (GL_OVR_multiview2 for single-pass stereo rendering)
static bool vrMultiViewSupported = false;
static bool vrMultiViewEnabled = false;
static XrSwapchain xrMultiViewSwapchain = XR_NULL_HANDLE;
static std::vector<XrSwapchainImageGL> xrMultiViewSwapchainImages;
static uint32_t xrMultiViewSwapchainImageIndex = 0;
// Per-swapchain-image FBOs and depth textures (like RazeXR)
static std::vector<GLuint> vrMultiViewFBOs;
static std::vector<GLuint> vrMultiViewDepthTextures;
static bool vrMultiViewRenderActive = false;
// Actual pixel size the MultiView swapchain was created at (recommended * supersampling, clamped
// to the runtime max). Used for the per-frame viewport, the std3D target, and the submit rect, so
// they always match what the swapchain images really are (even after a runtime supersampling rebuild).
static int vrMultiViewWidth = 0;
static int vrMultiViewHeight = 0;
// Color format chosen at session start; cached so the MultiView swapchain can be rebuilt at runtime
// (on a supersampling change) without re-running format selection.
static int64_t vrMVSwapchainFormat = GL_RGBA8;
// Set when supersampling changes at runtime; consumed at the top of the next BeginFrame (between
// frames, no swapchain image acquired) to rebuild the MultiView swapchain at the new size.
static int vrPendingSupersampleRebuild = 0;

// MultiView swapchain build/teardown, factored out so both session init and the runtime
// supersampling rebuild share one implementation.
static void stdVR_OpenXR_GetMVRenderSize(int* outW, int* outH);
static bool stdVR_OpenXR_CreateMultiViewSwapchainResources(void);
static void stdVR_OpenXR_DestroyMultiViewSwapchainResources(void);
static void stdVR_OpenXR_RebuildMultiViewSwapchain(void);

// Desktop mirror: blit the last-rendered left-eye VR buffer to the SDL window so the screen
// shows what's in VR (the 3D scene; HUD/menu quad layers are composited by the runtime).
static GLuint vrMirrorFBO = 0;
// Mirror orientation (cycled at runtime with F9): 0 = no flip, 1 = flip Y, 2 = flip X,
// 3 = flip X+Y. Default 0 (no flip) — verified correct for this swapchain convention.
int stdVR_mirrorFlip = 0;
// Owned 2D copy of the last left-eye buffer, captured during Finish*Buffer WHILE the
// swapchain image is still acquired. The mirror blits THIS, never the live swapchain image
// (reading a released swapchain image intermittently hangs the GL driver / whole app).
static GLuint vrMirrorEyeTex = 0;
static int    vrMirrorEyeW = 0;
static int    vrMirrorEyeH = 0;
static bool   vrMirrorEyeValid = false;

// Copy a left-eye color source (2D texture, or one layer of a 2D array texture) into the
// owned vrMirrorEyeTex. Call only while the source swapchain image is still acquired.
static void stdVR_OpenXR_CaptureEyeMirror(GLuint srcTex, bool isArray, int layer, int w, int h)
{
    if (srcTex == 0 || w <= 0 || h <= 0) return;
    if (vrMirrorEyeTex == 0 || vrMirrorEyeW != w || vrMirrorEyeH != h) {
        if (vrMirrorEyeTex == 0) glGenTextures(1, &vrMirrorEyeTex);
        glBindTexture(GL_TEXTURE_2D, vrMirrorEyeTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        vrMirrorEyeW = w;
        vrMirrorEyeH = h;
    }
    if (vrMirrorFBO == 0) glGenFramebuffers(1, &vrMirrorFBO);
    GLint prevRead = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, vrMirrorFBO);
    if (isArray) {
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, srcTex, 0, layer);
    } else {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
    }
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindTexture(GL_TEXTURE_2D, vrMirrorEyeTex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
    vrMirrorEyeValid = true;
}

// MultiView function pointers (loaded dynamically)
#if defined(TARGET_ANDROID_NATIVE_GLES)
typedef void (GL_APIENTRY *PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(GLenum target, GLenum attachment,
    GLuint texture, GLint level, GLint baseViewIndex, GLsizei numViews);
typedef void (GL_APIENTRY *PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)(GLenum target, GLenum attachment,
    GLuint texture, GLint level, GLsizei samples, GLint baseViewIndex, GLsizei numViews);
static PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC glFramebufferTextureMultiviewOVR = nullptr;
static PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC glFramebufferTextureMultisampleMultiviewOVR = nullptr;
#endif

static int stdVR_OpenXR_WaitSwapchainImage(XrSwapchain swapchain, const char* label, int eye)
{
    static int waitSwapchainCount = 0;
    waitSwapchainCount++;
#if STDVR_HOTPATH_DEBUG
    if (waitSwapchainCount <= 20 || waitSwapchainCount % 100 == 0) {
        stdPlatform_Printf("stdVR_OpenXR_WaitSwapchainImage(%s, eye=%d) ENTRY #%d\n",
            label ? label : "swapchain", eye, waitSwapchainCount);
    }
#endif

    XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitInfo.timeout = 100000000; // 100 ms per attempt

    XrResult result = xrWaitSwapchainImage(swapchain, &waitInfo);
    int retryCount = 0;
    // CAP the retries: previously this looped forever on XR_TIMEOUT_EXPIRED, which hangs the
    // main thread ("Not Responding", frozen headset frame) if an image never becomes ready —
    // e.g. during the menu<->gameplay swapchain transition. Give up after ~1s total and drop
    // the frame; the caller releases the acquired image and the next frame retries, which lets
    // the app recover instead of deadlocking.
    const int maxRetries = 10;
    while (result == XR_TIMEOUT_EXPIRED && retryCount < maxRetries) {
        retryCount++;
        result = xrWaitSwapchainImage(swapchain, &waitInfo);
    }

#if STDVR_HOTPATH_DEBUG
    if (waitSwapchainCount <= 20 || waitSwapchainCount % 100 == 0) {
        stdPlatform_Printf("stdVR_OpenXR_WaitSwapchainImage: result=%d\n", result);
    }
#endif

    if (result == XR_TIMEOUT_EXPIRED) {
        VR_Log("stdVR_OpenXR: xrWaitSwapchainImage gave up after %d retries for %s eye %d (dropping frame)\n",
            retryCount, label ? label : "swapchain", eye);
        return 0;
    }

    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: xrWaitSwapchainImage failed for %s eye %d: error %d\n",
            label ? label : "swapchain", eye, result);
        return 0;
    }

    if (retryCount > 0) {
        VR_Log("stdVR_OpenXR: xrWaitSwapchainImage retried %d times for %s eye %d\n",
            retryCount, label ? label : "swapchain", eye);
    }

    return 1;
}

static void stdVR_OpenXR_ReleaseSwapchainImageForSwapchain(XrSwapchain swapchain, const char* label, int eye)
{
    XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XrResult result = xrReleaseSwapchainImage(swapchain, &releaseInfo);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: xrReleaseSwapchainImage failed for %s eye %d: error %d\n",
            label ? label : "swapchain", eye, result);
    }
}

static void stdVR_OpenXR_ReleaseSwapchainImage(int eye)
{
    stdVR_OpenXR_ReleaseSwapchainImageForSwapchain(xrSwapchains[eye], "main", eye);
}

// Track null FBO validation (only validate once)
static bool vrNullFBOValidated[STDVR_EYE_COUNT] = { false, false };

// Manual FBO/viewport state tracking to avoid glGetIntegerv (GPU stalls)
// Declared early so stdVR_OpenXR_ClearNullSwapchain can use them
static GLint trackedFBO = 0;
static GLint trackedViewport[4] = { 0, 0, 640, 480 };
static bool stateTrackingActive = false;

static int stdVR_OpenXR_ClearNullSwapchain(int eye)
{
    if (eye < 0 || eye >= STDVR_EYE_COUNT) {
        return 0;
    }
    if (xrNullSwapchains[eye] == XR_NULL_HANDLE || vrNullFBO[eye] == 0 || xrNullSwapchainImages[eye].empty()) {
        return 0;
    }

    XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrResult result = xrAcquireSwapchainImage(xrNullSwapchains[eye], &acquireInfo, &xrNullSwapchainImageIndex[eye]);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: xrAcquireSwapchainImage failed for null eye %d: error %d\n", eye, result);
        return 0;
    }

    if (!stdVR_OpenXR_WaitSwapchainImage(xrNullSwapchains[eye], "null", eye)) {
        stdVR_OpenXR_ReleaseSwapchainImageForSwapchain(xrNullSwapchains[eye], "null", eye);
        return 0;
    }

    // OPTIMIZATION: Use tracked state instead of GPU queries
    GLint savedFBO = stateTrackingActive ? trackedFBO : 0;
    GLint savedViewport[4];
    if (stateTrackingActive) {
        memcpy(savedViewport, trackedViewport, sizeof(savedViewport));
    } else {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFBO);
        glGetIntegerv(GL_VIEWPORT, savedViewport);
    }

    GLuint texture = xrNullSwapchainImages[eye][xrNullSwapchainImageIndex[eye]].image;
    glBindFramebuffer(GL_FRAMEBUFFER, vrNullFBO[eye]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    // OPTIMIZATION: Only validate FBO on first use
    if (!vrNullFBOValidated[eye]) {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            VR_Log("stdVR_OpenXR: Null FBO incomplete for eye %d, status 0x%x\n", eye, status);
            glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
            stdVR_OpenXR_ReleaseSwapchainImageForSwapchain(xrNullSwapchains[eye], "null", eye);
            return 0;
        }
        vrNullFBOValidated[eye] = true;
    }

    int width = xrConfigViews[eye].recommendedImageRectWidth;
    int height = xrConfigViews[eye].recommendedImageRectHeight;
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_FRAMEBUFFER_SRGB);

    // Detach the texture before releasing (mirrors main swapchain handling)
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
    glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);

    // Update tracked state
    if (stateTrackingActive) {
        trackedFBO = savedFBO;
        memcpy(trackedViewport, savedViewport, sizeof(trackedViewport));
    }

    stdVR_OpenXR_ReleaseSwapchainImageForSwapchain(xrNullSwapchains[eye], "null", eye);
    return 1;
}

// Frame state
static XrFrameState xrFrameState = {};
static bool xrFrameInProgress = false;
static int vrFrameCount = 0;  // Track frame count for debugging

// VR FBO state for rendering (per-swapchain-image, like JKXR)
static std::vector<GLuint> vrFBO[STDVR_EYE_COUNT];
static std::vector<GLuint> vrDepthTex[STDVR_EYE_COUNT];
static std::vector<bool> vrFBOValidated[STDVR_EYE_COUNT];  // Added: track which FBOs have been validated
static GLuint vrCurrentFBO[STDVR_EYE_COUNT] = { 0, 0 };
static GLint previousFBO = 0;
static GLint previousViewport[4] = { 0, 0, 0, 0 };

// Track current eye being rendered (for debugging)
// Using extern "C" so C code can reference this variable
extern "C" int stdVR_currentEye = -1;  // -1 = not in eye rendering, 0 = left, 1 = right

// Input state
static XrActionSet xrActionSet = XR_NULL_HANDLE;
static XrAction xrAimPoseAction = XR_NULL_HANDLE;
static XrAction xrGripPoseAction = XR_NULL_HANDLE;
static XrAction xrTriggerAction = XR_NULL_HANDLE;
static XrAction xrGripAction = XR_NULL_HANDLE;
static XrAction xrThumbstickAction = XR_NULL_HANDLE;
static XrAction xrThumbstickClickLAction = XR_NULL_HANDLE;
static XrAction xrThumbstickClickRAction = XR_NULL_HANDLE;
static XrAction xrButtonAAction = XR_NULL_HANDLE;
static XrAction xrButtonBAction = XR_NULL_HANDLE;
static XrAction xrButtonXAction = XR_NULL_HANDLE;
static XrAction xrButtonYAction = XR_NULL_HANDLE;
static XrAction xrMenuAction = XR_NULL_HANDLE;
static XrAction xrHapticAction = XR_NULL_HANDLE;

// Added: XR_FB_display_refresh_rate. The extension is optional, so the entry points come from
// xrGetInstanceProcAddr and stay null when the runtime does not have it.
static bool stdVR_bHasRefreshRateExt = false;
static PFN_xrEnumerateDisplayRefreshRatesFB stdVR_pfnEnumerateDisplayRefreshRates = nullptr;
static PFN_xrGetDisplayRefreshRateFB stdVR_pfnGetDisplayRefreshRate = nullptr;
static PFN_xrRequestDisplayRefreshRateFB stdVR_pfnRequestDisplayRefreshRate = nullptr;
static XrSpace xrAimControllerSpaces[STDVR_CONTROLLER_COUNT] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
static XrSpace xrGripControllerSpaces[STDVR_CONTROLLER_COUNT] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
static XrPath xrHandPaths[STDVR_CONTROLLER_COUNT];

// Runtime info
static char xrRuntimeName[XR_MAX_RUNTIME_NAME_SIZE] = "Unknown";

// Helper macros
#define XR_CHECK(call) \
    do { \
        XrResult result = (call); \
        if (XR_FAILED(result)) { \
            VR_Log("OpenXR error: %s returned %d at %s:%d\n", #call, result, __FILE__, __LINE__); \
            return 0; \
        } \
    } while (0)

#define XR_CHECK_VOID(call) \
    do { \
        XrResult result = (call); \
        if (XR_FAILED(result)) { \
            VR_Log("OpenXR error: %s returned %d at %s:%d\n", #call, result, __FILE__, __LINE__); \
            return; \
        } \
    } while (0)

// Convert XrQuaternionf to Euler angles (Pitch, Yaw, Roll in degrees)
// OpenXR coords: X=right, Y=up, Z=backward (forward = -Z)
// JKDF2 coords:  X=right, Y=forward, Z=up
//
// Uses vector-based extraction so angles are independent:
// - Pitch is based on forward vector's elevation (independent of yaw)
// - Yaw is based on forward vector's horizontal direction
// - Roll is based on up vector tilt
static void QuatToEuler(const XrQuaternionf* q, rdVector3* euler)
{
    const float RAD_TO_DEG = 180.0f / 3.14159265f;

    // Extract the forward vector (-Z in OpenXR) after rotation
    // forward = quaternion * (0, 0, -1)
    float fx = -2.0f * (q->x * q->z + q->w * q->y);
    float fy = 2.0f * (q->w * q->x - q->y * q->z);
    float fz = 2.0f * (q->x * q->x + q->y * q->y) - 1.0f;

    // Extract the up vector (+Y in OpenXR) after rotation
    // up = quaternion * (0, 1, 0)
    float ux = 2.0f * (q->x * q->y - q->w * q->z);
    float uy = 1.0f - 2.0f * (q->x * q->x + q->z * q->z);
    float uz = 2.0f * (q->y * q->z + q->w * q->x);

    // Pitch: angle of forward vector above/below horizontal
    // This is INDEPENDENT of yaw - purely how much you're looking up/down
    // fy is how much the forward vector points up (+) or down (-)
    // In OpenXR, fy > 0 means looking up
    float pitch_oxr = std::asin(std::clamp(fy, -1.0f, 1.0f));

    // Yaw: horizontal angle of forward vector in XZ plane
    // atan2(-fx, -fz) gives angle from -Z axis (forward) toward -X axis (left)
    float yaw_oxr = std::atan2(-fx, -fz);

    // Roll: tilt of up vector around the forward axis
    // Project up vector onto plane perpendicular to forward, measure angle from world up
    // When looking horizontally, roll = atan2(ux, uy)
    // But we need to account for pitch...

    // Compute expected up direction if there were no roll (just yaw and pitch)
    // After yaw and pitch, the "up" should still be in the plane containing world-up and forward
    float cosP = std::cos(pitch_oxr);
    float sinP = std::sin(pitch_oxr);
    float cosY = std::cos(yaw_oxr);
    float sinY = std::sin(yaw_oxr);

    // Expected up with no roll (derived from yaw-pitch rotation)
    float expectedUpX = sinY * sinP;
    float expectedUpY = cosP;
    float expectedUpZ = cosY * sinP;

    // Expected right with no roll
    float expectedRightX = cosY;
    float expectedRightY = 0.0f;
    float expectedRightZ = -sinY;

    // Roll is the angle between actual up and expected up, measured around forward axis
    // Project actual up onto expected up and expected right
    float upDotExpUp = ux * expectedUpX + uy * expectedUpY + uz * expectedUpZ;
    float upDotExpRight = ux * expectedRightX + uy * expectedRightY + uz * expectedRightZ;
    float roll_oxr = std::atan2(upDotExpRight, upDotExpUp);

    // Convert to JKDF2 coordinate system:
    // - JKDF2 Pitch: positive = looking UP. In OpenXR, pitch_oxr > 0 = looking up. Same convention!
    // - JKDF2 Yaw: positive = turn left. In OpenXR, yaw_oxr > 0 = turn left. Same convention!
    // - JKDF2 Roll: axis flip (JKDF2 Y = -OpenXR Z), so negate
    euler->x = pitch_oxr * RAD_TO_DEG;   // Pitch: same convention
    euler->y = yaw_oxr * RAD_TO_DEG;     // Yaw: full range [-180, +180]
    euler->z = -roll_oxr * RAD_TO_DEG;   // Roll: negated due to axis flip
}

// Convert XrPosef to rdMatrix34
// OpenXR coordinate system: +X=right, +Y=up, +Z=backward (toward user)
// JKDF2 coordinate system:  +X=right, +Y=forward, +Z=up
// Conversion: JKDF2.x = OpenXR.x, JKDF2.y = -OpenXR.z, JKDF2.z = OpenXR.y
static void PoseToMatrix(const XrPosef* pose, rdMatrix34* mat)
{
    // Convert quaternion to rotation matrix in OpenXR space first
    const XrQuaternionf* q = &pose->orientation;
    float xx = q->x * q->x;
    float yy = q->y * q->y;
    float zz = q->z * q->z;
    float xy = q->x * q->y;
    float xz = q->x * q->z;
    float yz = q->y * q->z;
    float wx = q->w * q->x;
    float wy = q->w * q->y;
    float wz = q->w * q->z;

    // OpenXR rotation matrix (column vectors: right, up, back)
    float xr_rvec_x = 1.0f - 2.0f * (yy + zz);
    float xr_rvec_y = 2.0f * (xy + wz);
    float xr_rvec_z = 2.0f * (xz - wy);

    float xr_uvec_x = 2.0f * (xy - wz);
    float xr_uvec_y = 1.0f - 2.0f * (xx + zz);
    float xr_uvec_z = 2.0f * (yz + wx);

    float xr_bvec_x = 2.0f * (xz + wy);
    float xr_bvec_y = 2.0f * (yz - wx);
    float xr_bvec_z = 1.0f - 2.0f * (xx + yy);

    // Convert to JKDF2 coordinate system
    // JKDF2 rvec (right) = OpenXR rvec with Y/Z swapped and Z negated
    mat->rvec.x = xr_rvec_x;
    mat->rvec.y = -xr_rvec_z;
    mat->rvec.z = xr_rvec_y;

    // JKDF2 lvec (forward) = OpenXR -bvec (negative backward = forward) with Y/Z swapped
    mat->lvec.x = -xr_bvec_x;
    mat->lvec.y = xr_bvec_z;
    mat->lvec.z = -xr_bvec_y;

    // JKDF2 uvec (up) = OpenXR uvec with Y/Z swapped and Z negated
    mat->uvec.x = xr_uvec_x;
    mat->uvec.y = -xr_uvec_z;
    mat->uvec.z = xr_uvec_y;

    // Convert position: JKDF2.x = OpenXR.x, JKDF2.y = -OpenXR.z, JKDF2.z = OpenXR.y
    mat->scale.x = pose->position.x;
    mat->scale.y = -pose->position.z;
    mat->scale.z = pose->position.y;
}

// Create OpenXR instance
extern "C" int stdVR_OpenXR_Init(void)
{
    // Initialize log file immediately
    VR_InitLog();
    VR_Log("stdVR_OpenXR: Initializing OpenXR...\n");
    VR_Log("stdVR_OpenXR: Build timestamp: %s %s\n", __DATE__, __TIME__);

#ifdef __ANDROID__
    // On Android, we MUST initialize the OpenXR loader with the Android context
    // before calling any other OpenXR functions
    VR_Log("stdVR_OpenXR: Initializing OpenXR loader for Android...\n");

    // Get the function pointer for xrInitializeLoaderKHR
    PFN_xrInitializeLoaderKHR initLoaderFunc = nullptr;
    XrResult loaderResult = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                             (PFN_xrVoidFunction*)&initLoaderFunc);

    if (XR_SUCCEEDED(loaderResult) && initLoaderFunc != nullptr) {
        // Get JavaVM and Activity from SDL
        // SDL_AndroidGetJNIEnv returns JNIEnv*, we need to get JavaVM from it
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        JavaVM* javaVM = nullptr;
        if (env != nullptr) {
            env->GetJavaVM(&javaVM);
        }
        jobject activity = (jobject)SDL_AndroidGetActivity();

        if (javaVM != nullptr && activity != nullptr) {
            XrLoaderInitInfoAndroidKHR loaderInitInfo = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
            loaderInitInfo.applicationVM = javaVM;
            loaderInitInfo.applicationContext = activity;

            loaderResult = initLoaderFunc((XrLoaderInitInfoBaseHeaderKHR*)&loaderInitInfo);
            if (XR_FAILED(loaderResult)) {
                VR_Log("stdVR_OpenXR: Failed to initialize OpenXR loader: %d\n", loaderResult);
                return 0;
            }
            VR_Log("stdVR_OpenXR: OpenXR loader initialized successfully\n");
        } else {
            VR_Log("stdVR_OpenXR: Failed to get Android context (JavaVM=%p, Activity=%p)\n", javaVM, activity);
            return 0;
        }
    } else {
        VR_Log("stdVR_OpenXR: xrInitializeLoaderKHR not available (result=%d)\n", loaderResult);
        // Continue anyway - some runtimes may not require this
    }
#endif

    // Get available extensions
    uint32_t extensionCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);

    std::vector<XrExtensionProperties> extensions(extensionCount, { XR_TYPE_EXTENSION_PROPERTIES });
    xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());

    // Check for OpenGL/OpenGL ES extension (platform-specific)
#ifdef __ANDROID__
    const char* requiredGfxExtension = XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
#else
    const char* requiredGfxExtension = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
#endif

    bool hasOpenGL = false;
    VR_Log("stdVR_OpenXR: Available extensions:\n");
    for (const auto& ext : extensions) {
        // Print graphics-related extensions for debugging
        if (strstr(ext.extensionName, "enable") || strstr(ext.extensionName, "graphics")) {
            stdPlatform_Printf("  - %s\n", ext.extensionName);
        }
        if (strcmp(ext.extensionName, requiredGfxExtension) == 0) {
            hasOpenGL = true;
        }
    }

    if (!hasOpenGL) {
        VR_Log("stdVR_OpenXR: ERROR - %s not supported by this runtime\n", requiredGfxExtension);
#ifdef __ANDROID__
        VR_Log("stdVR_OpenXR: OpenJKDF2 VR requires an OpenXR runtime with OpenGL ES support.\n");
        VR_Log("stdVR_OpenXR: Compatible runtimes:\n");
        stdPlatform_Printf("  - Meta Quest runtime\n");
        stdPlatform_Printf("  - Pico runtime\n");
#else
        VR_Log("stdVR_OpenXR: OpenJKDF2 VR requires an OpenXR runtime with OpenGL support.\n");
        VR_Log("stdVR_OpenXR: Compatible runtimes:\n");
        stdPlatform_Printf("  - SteamVR (recommended)\n");
        stdPlatform_Printf("  - Oculus runtime (with actual headset)\n");
        stdPlatform_Printf("  - Monado\n");
        VR_Log("stdVR_OpenXR: Note: Meta XR Simulator does not support OpenGL.\n");
#endif
        return 0;
    }

#ifdef __ANDROID__
    // Check for Android create instance extension (required for Pico and other Android runtimes)
    bool hasAndroidCreateInstance = false;
    bool hasBDControllerInteraction = false;
    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) == 0) {
            hasAndroidCreateInstance = true;
        }
        if (strcmp(ext.extensionName, XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME) == 0) {
            hasBDControllerInteraction = true;
        }
        if (strcmp(ext.extensionName, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME) == 0) {
            stdVR_bHasRefreshRateExt = true;
        }
    }
    VR_Log("stdVR_OpenXR: XR_FB_display_refresh_rate: %s\n", stdVR_bHasRefreshRateExt ? "YES" : "NO");
    VR_Log("stdVR_OpenXR: XR_KHR_android_create_instance: %s\n", hasAndroidCreateInstance ? "YES" : "NO");
    VR_Log("stdVR_OpenXR: XR_BD_controller_interaction: %s\n", hasBDControllerInteraction ? "YES" : "NO");
#endif

    // Create instance
    std::vector<const char*> enabledExtensions;
    enabledExtensions.push_back(requiredGfxExtension);
#ifdef __ANDROID__
    if (hasAndroidCreateInstance) {
        enabledExtensions.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
    }
    // Added: Enable Pico controller interaction profile extension (required for
    // bytedance/pico4_controller and bytedance/pico_neo3_controller profiles)
    if (hasBDControllerInteraction) {
        enabledExtensions.push_back(XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME);
    }
#endif
    // Added: display refresh rate control. The extension is optional. The menu option stays
    // hidden when the runtime does not report it.
    if (stdVR_bHasRefreshRateExt) {
        enabledExtensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    }

    XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    strcpy(createInfo.applicationInfo.applicationName, "OpenJKDF2");
    createInfo.applicationInfo.applicationVersion = 1;
    strcpy(createInfo.applicationInfo.engineName, "OpenJKDF2");
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    createInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
    createInfo.enabledExtensionNames = enabledExtensions.data();

#ifdef __ANDROID__
    // On Android, chain the Android create instance info (required for Pico and other runtimes)
    XrInstanceCreateInfoAndroidKHR androidCreateInfo = { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    if (hasAndroidCreateInstance) {
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        JavaVM* javaVM = nullptr;
        if (env != nullptr) {
            env->GetJavaVM(&javaVM);
        }
        jobject activity = (jobject)SDL_AndroidGetActivity();

        androidCreateInfo.applicationVM = javaVM;
        androidCreateInfo.applicationActivity = activity;
        createInfo.next = &androidCreateInfo;
        VR_Log("stdVR_OpenXR: Chaining XrInstanceCreateInfoAndroidKHR (VM=%p, Activity=%p)\n", javaVM, activity);
    }
#endif

    XrResult result = xrCreateInstance(&createInfo, &xrInstance);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: Failed to create instance (error %d)\n", result);
        VR_Log("stdVR_OpenXR: Make sure SteamVR or another compatible OpenXR runtime is running.\n");
        return 0;
    }

    // Get instance properties
    XrInstanceProperties instanceProps = { XR_TYPE_INSTANCE_PROPERTIES };
    xrGetInstanceProperties(xrInstance, &instanceProps);
    strncpy(xrRuntimeName, instanceProps.runtimeName, sizeof(xrRuntimeName) - 1);
    VR_Log("stdVR_OpenXR: Runtime: %s\n", xrRuntimeName);

    // Get system
    XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR_CHECK(xrGetSystem(xrInstance, &systemInfo, &xrSystemId));

    // Get view configuration
    uint32_t viewCount = 0;
    xrEnumerateViewConfigurationViews(xrInstance, xrSystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
    if (viewCount != STDVR_EYE_COUNT) {
        VR_Log("stdVR_OpenXR: Unexpected view count: %d\n", viewCount);
        return 0;
    }

    for (int i = 0; i < STDVR_EYE_COUNT; i++) {
        xrConfigViews[i] = { XR_TYPE_VIEW_CONFIGURATION_VIEW };
    }
    xrEnumerateViewConfigurationViews(xrInstance, xrSystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, STDVR_EYE_COUNT, &viewCount, xrConfigViews);

    // Store recommended render size
    stdVR_clientInfo.renderWidth = xrConfigViews[0].recommendedImageRectWidth;
    stdVR_clientInfo.renderHeight = xrConfigViews[0].recommendedImageRectHeight;
    VR_Log("stdVR_OpenXR: Recommended render size: %dx%d\n", stdVR_clientInfo.renderWidth, stdVR_clientInfo.renderHeight);
    std3D_SetVRTargetSize(stdVR_clientInfo.renderWidth, stdVR_clientInfo.renderHeight);

    return 1;
}

extern "C" void stdVR_OpenXR_Shutdown(void)
{
    VR_Log("stdVR_OpenXR: Shutting down...\n");
    // The MotS relaunch path in main.c re-enters the main loop, so a stale request would
    // quit the new run immediately.
    xrExitRequested = false;
    if (xrInstance != XR_NULL_HANDLE) {
        xrDestroyInstance(xrInstance);
        xrInstance = XR_NULL_HANDLE;
    }
    xrSystemId = XR_NULL_SYSTEM_ID;

    // Close log file
    if (vrLogFile) {
        VR_Log("stdVR_OpenXR: Shutdown complete, closing log\n");
        fclose(vrLogFile);
        vrLogFile = nullptr;
    }
}

static int CreateActionSet(void)
{
    // Create action set
    XrActionSetCreateInfo actionSetInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
    strcpy(actionSetInfo.actionSetName, "gameplay");
    strcpy(actionSetInfo.localizedActionSetName, "Gameplay");
    actionSetInfo.priority = 0;
    XR_CHECK(xrCreateActionSet(xrInstance, &actionSetInfo, &xrActionSet));

    // Get hand paths
    xrStringToPath(xrInstance, "/user/hand/left", &xrHandPaths[STDVR_CONTROLLER_LEFT]);
    xrStringToPath(xrInstance, "/user/hand/right", &xrHandPaths[STDVR_CONTROLLER_RIGHT]);

    // Create pose action
    XrActionCreateInfo actionInfo = { XR_TYPE_ACTION_CREATE_INFO };
    strcpy(actionInfo.actionName, "aim_pose");
    actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
    actionInfo.countSubactionPaths = STDVR_CONTROLLER_COUNT;
    actionInfo.subactionPaths = xrHandPaths;
    strcpy(actionInfo.localizedActionName, "Aim Pose");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrAimPoseAction));

    strcpy(actionInfo.actionName, "grip_pose");
    actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
    actionInfo.countSubactionPaths = STDVR_CONTROLLER_COUNT;
    actionInfo.subactionPaths = xrHandPaths;
    strcpy(actionInfo.localizedActionName, "Grip Pose");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrGripPoseAction));

    // Create trigger action
    strcpy(actionInfo.actionName, "trigger");
    actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
    strcpy(actionInfo.localizedActionName, "Trigger");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrTriggerAction));

    // Create grip action
    strcpy(actionInfo.actionName, "grip");
    actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
    strcpy(actionInfo.localizedActionName, "Grip");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrGripAction));

    // Create thumbstick action
    strcpy(actionInfo.actionName, "thumbstick");
    actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    strcpy(actionInfo.localizedActionName, "Thumbstick");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrThumbstickAction));

    // Create button actions (with subaction paths for Pico compatibility —
    // Pico's OpenXR runtime requires actions to be created with subaction paths
    // if their bindings reference hand-specific paths)
    actionInfo.countSubactionPaths = STDVR_CONTROLLER_COUNT;
    actionInfo.subactionPaths = xrHandPaths;
    actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;

    strcpy(actionInfo.actionName, "button_a");
    strcpy(actionInfo.localizedActionName, "A Button");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrButtonAAction));

    strcpy(actionInfo.actionName, "button_b");
    strcpy(actionInfo.localizedActionName, "B Button");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrButtonBAction));

    strcpy(actionInfo.actionName, "button_x");
    strcpy(actionInfo.localizedActionName, "X Button");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrButtonXAction));

    strcpy(actionInfo.actionName, "button_y");
    strcpy(actionInfo.localizedActionName, "Y Button");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrButtonYAction));

    strcpy(actionInfo.actionName, "menu");
    strcpy(actionInfo.localizedActionName, "Menu");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrMenuAction));

    strcpy(actionInfo.actionName, "thumbstick_click_l");
    strcpy(actionInfo.localizedActionName, "Left Thumbstick Click");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrThumbstickClickLAction));

    strcpy(actionInfo.actionName, "thumbstick_click_r");
    strcpy(actionInfo.localizedActionName, "Right Thumbstick Click");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrThumbstickClickRAction));

    // Create haptic action
    actionInfo.countSubactionPaths = STDVR_CONTROLLER_COUNT;
    actionInfo.subactionPaths = xrHandPaths;
    strcpy(actionInfo.actionName, "haptic");
    actionInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
    strcpy(actionInfo.localizedActionName, "Haptic");
    XR_CHECK(xrCreateAction(xrActionSet, &actionInfo, &xrHapticAction));

    // Suggest bindings for Oculus Touch
    XrPath oculusTouchPath;
    xrStringToPath(xrInstance, "/interaction_profiles/oculus/touch_controller", &oculusTouchPath);

    std::vector<XrActionSuggestedBinding> bindings;
    XrPath path;

    // Left controller
    xrStringToPath(xrInstance, "/user/hand/left/input/aim/pose", &path);
    bindings.push_back({ xrAimPoseAction, path });
    xrStringToPath(xrInstance, "/user/hand/left/input/grip/pose", &path);
    bindings.push_back({ xrGripPoseAction, path });
    xrStringToPath(xrInstance, "/user/hand/left/input/trigger/value", &path);
    bindings.push_back({ xrTriggerAction, path });
    xrStringToPath(xrInstance, "/user/hand/left/input/squeeze/value", &path);
    bindings.push_back({ xrGripAction, path });
    xrStringToPath(xrInstance, "/user/hand/left/input/thumbstick", &path);
    bindings.push_back({ xrThumbstickAction, path });
    xrStringToPath(xrInstance, "/user/hand/left/input/x/click", &path);
    bindings.push_back({ xrButtonXAction, path });
    xrStringToPath(xrInstance, "/user/hand/left/input/y/click", &path);
    bindings.push_back({ xrButtonYAction, path });
    xrStringToPath(xrInstance, "/user/hand/left/input/menu/click", &path);
    bindings.push_back({ xrMenuAction, path });
    xrStringToPath(xrInstance, "/user/hand/left/input/thumbstick/click", &path);
    bindings.push_back({ xrThumbstickClickLAction, path });
    xrStringToPath(xrInstance, "/user/hand/left/output/haptic", &path);
    bindings.push_back({ xrHapticAction, path });

    // Right controller
    xrStringToPath(xrInstance, "/user/hand/right/input/aim/pose", &path);
    bindings.push_back({ xrAimPoseAction, path });
    xrStringToPath(xrInstance, "/user/hand/right/input/grip/pose", &path);
    bindings.push_back({ xrGripPoseAction, path });
    xrStringToPath(xrInstance, "/user/hand/right/input/trigger/value", &path);
    bindings.push_back({ xrTriggerAction, path });
    xrStringToPath(xrInstance, "/user/hand/right/input/squeeze/value", &path);
    bindings.push_back({ xrGripAction, path });
    xrStringToPath(xrInstance, "/user/hand/right/input/thumbstick", &path);
    bindings.push_back({ xrThumbstickAction, path });
    xrStringToPath(xrInstance, "/user/hand/right/input/a/click", &path);
    bindings.push_back({ xrButtonAAction, path });
    xrStringToPath(xrInstance, "/user/hand/right/input/b/click", &path);
    bindings.push_back({ xrButtonBAction, path });
    xrStringToPath(xrInstance, "/user/hand/right/input/thumbstick/click", &path);
    bindings.push_back({ xrThumbstickClickRAction, path });
    xrStringToPath(xrInstance, "/user/hand/right/output/haptic", &path);
    bindings.push_back({ xrHapticAction, path });

    XrInteractionProfileSuggestedBinding suggestedBindings = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    suggestedBindings.interactionProfile = oculusTouchPath;
    suggestedBindings.suggestedBindings = bindings.data();
    suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
    {
        XrResult touchResult = xrSuggestInteractionProfileBindings(xrInstance, &suggestedBindings);
        VR_Log("stdVR_OpenXR: Oculus Touch bindings: %s (result %d, %d bindings)\n",
               XR_SUCCEEDED(touchResult) ? "OK" : "FAILED", (int)touchResult, (int)bindings.size());
    }

    // Valve Index Controller bindings
    XrPath indexPath;
    if (XR_SUCCEEDED(xrStringToPath(xrInstance, "/interaction_profiles/valve/index_controller", &indexPath))) {
        bindings.clear();

        // Left controller
        xrStringToPath(xrInstance, "/user/hand/left/input/aim/pose", &path);
        bindings.push_back({ xrAimPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/grip/pose", &path);
        bindings.push_back({ xrGripPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/trigger/value", &path);
        bindings.push_back({ xrTriggerAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/squeeze/value", &path);
        bindings.push_back({ xrGripAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/thumbstick", &path);
        bindings.push_back({ xrThumbstickAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/a/click", &path);  // Index has A on left
        bindings.push_back({ xrButtonXAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/b/click", &path);  // Index has B on left
        bindings.push_back({ xrButtonYAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/thumbstick/click", &path);
        bindings.push_back({ xrThumbstickClickLAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/output/haptic", &path);
        bindings.push_back({ xrHapticAction, path });

        // Right controller
        xrStringToPath(xrInstance, "/user/hand/right/input/aim/pose", &path);
        bindings.push_back({ xrAimPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/grip/pose", &path);
        bindings.push_back({ xrGripPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/trigger/value", &path);
        bindings.push_back({ xrTriggerAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/squeeze/value", &path);
        bindings.push_back({ xrGripAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/thumbstick", &path);
        bindings.push_back({ xrThumbstickAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/a/click", &path);
        bindings.push_back({ xrButtonAAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/b/click", &path);
        bindings.push_back({ xrButtonBAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/thumbstick/click", &path);
        bindings.push_back({ xrThumbstickClickRAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/output/haptic", &path);
        bindings.push_back({ xrHapticAction, path });

        suggestedBindings.interactionProfile = indexPath;
        suggestedBindings.suggestedBindings = bindings.data();
        suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
        xrSuggestInteractionProfileBindings(xrInstance, &suggestedBindings);
    }

    // HTC Vive Controller bindings
    XrPath vivePath;
    if (XR_SUCCEEDED(xrStringToPath(xrInstance, "/interaction_profiles/htc/vive_controller", &vivePath))) {
        bindings.clear();

        // Left controller - Vive uses trackpad instead of thumbstick
        xrStringToPath(xrInstance, "/user/hand/left/input/aim/pose", &path);
        bindings.push_back({ xrAimPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/grip/pose", &path);
        bindings.push_back({ xrGripPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/trigger/value", &path);
        bindings.push_back({ xrTriggerAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/squeeze/click", &path);  // Vive grip is a button
        bindings.push_back({ xrGripAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/trackpad", &path);  // Use trackpad as thumbstick
        bindings.push_back({ xrThumbstickAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/trackpad/click", &path);
        bindings.push_back({ xrButtonXAction, path });  // Trackpad click = X
        xrStringToPath(xrInstance, "/user/hand/left/input/menu/click", &path);
        bindings.push_back({ xrMenuAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/output/haptic", &path);
        bindings.push_back({ xrHapticAction, path });

        // Right controller
        xrStringToPath(xrInstance, "/user/hand/right/input/aim/pose", &path);
        bindings.push_back({ xrAimPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/grip/pose", &path);
        bindings.push_back({ xrGripPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/trigger/value", &path);
        bindings.push_back({ xrTriggerAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/squeeze/click", &path);
        bindings.push_back({ xrGripAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/trackpad", &path);
        bindings.push_back({ xrThumbstickAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/trackpad/click", &path);
        bindings.push_back({ xrButtonAAction, path });  // Trackpad click = A
        xrStringToPath(xrInstance, "/user/hand/right/input/menu/click", &path);
        bindings.push_back({ xrMenuAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/output/haptic", &path);
        bindings.push_back({ xrHapticAction, path });

        suggestedBindings.interactionProfile = vivePath;
        suggestedBindings.suggestedBindings = bindings.data();
        suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
        xrSuggestInteractionProfileBindings(xrInstance, &suggestedBindings);
    }

    // Pico 4 controller bindings (ByteDance/Pico)
    // Pico 4 controllers have same layout as Oculus Touch (A/B/X/Y, triggers, grips, thumbsticks)
    XrPath pico4Path;
    if (XR_SUCCEEDED(xrStringToPath(xrInstance, "/interaction_profiles/bytedance/pico4_controller", &pico4Path))) {
        bindings.clear();

        // Left controller
        xrStringToPath(xrInstance, "/user/hand/left/input/aim/pose", &path);
        bindings.push_back({ xrAimPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/grip/pose", &path);
        bindings.push_back({ xrGripPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/trigger/value", &path);
        bindings.push_back({ xrTriggerAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/squeeze/value", &path);
        bindings.push_back({ xrGripAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/thumbstick", &path);
        bindings.push_back({ xrThumbstickAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/x/click", &path);
        bindings.push_back({ xrButtonXAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/y/click", &path);
        bindings.push_back({ xrButtonYAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/menu/click", &path);
        bindings.push_back({ xrMenuAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/thumbstick/click", &path);
        bindings.push_back({ xrThumbstickClickLAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/output/haptic", &path);
        bindings.push_back({ xrHapticAction, path });

        // Right controller
        xrStringToPath(xrInstance, "/user/hand/right/input/aim/pose", &path);
        bindings.push_back({ xrAimPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/grip/pose", &path);
        bindings.push_back({ xrGripPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/trigger/value", &path);
        bindings.push_back({ xrTriggerAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/squeeze/value", &path);
        bindings.push_back({ xrGripAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/thumbstick", &path);
        bindings.push_back({ xrThumbstickAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/a/click", &path);
        bindings.push_back({ xrButtonAAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/b/click", &path);
        bindings.push_back({ xrButtonBAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/thumbstick/click", &path);
        bindings.push_back({ xrThumbstickClickRAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/output/haptic", &path);
        bindings.push_back({ xrHapticAction, path });

        suggestedBindings.interactionProfile = pico4Path;
        suggestedBindings.suggestedBindings = bindings.data();
        suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
        XrResult pico4Result = xrSuggestInteractionProfileBindings(xrInstance, &suggestedBindings);
        VR_Log("stdVR_OpenXR: Pico 4 bindings: %s (result %d, %d bindings)\n",
               XR_SUCCEEDED(pico4Result) ? "OK" : "FAILED", (int)pico4Result, (int)bindings.size());
        // XR_ERROR_PATH_UNSUPPORTED is expected on non-Pico devices
    }

    // Pico Neo3 controller bindings (for older Pico devices)
    XrPath picoNeo3Path;
    if (XR_SUCCEEDED(xrStringToPath(xrInstance, "/interaction_profiles/bytedance/pico_neo3_controller", &picoNeo3Path))) {
        bindings.clear();

        // Left controller
        xrStringToPath(xrInstance, "/user/hand/left/input/aim/pose", &path);
        bindings.push_back({ xrAimPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/grip/pose", &path);
        bindings.push_back({ xrGripPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/trigger/value", &path);
        bindings.push_back({ xrTriggerAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/squeeze/value", &path);
        bindings.push_back({ xrGripAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/thumbstick", &path);
        bindings.push_back({ xrThumbstickAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/x/click", &path);
        bindings.push_back({ xrButtonXAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/y/click", &path);
        bindings.push_back({ xrButtonYAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/menu/click", &path);
        bindings.push_back({ xrMenuAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/thumbstick/click", &path);
        bindings.push_back({ xrThumbstickClickLAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/output/haptic", &path);
        bindings.push_back({ xrHapticAction, path });

        // Right controller
        xrStringToPath(xrInstance, "/user/hand/right/input/aim/pose", &path);
        bindings.push_back({ xrAimPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/grip/pose", &path);
        bindings.push_back({ xrGripPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/trigger/value", &path);
        bindings.push_back({ xrTriggerAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/squeeze/value", &path);
        bindings.push_back({ xrGripAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/thumbstick", &path);
        bindings.push_back({ xrThumbstickAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/a/click", &path);
        bindings.push_back({ xrButtonAAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/b/click", &path);
        bindings.push_back({ xrButtonBAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/thumbstick/click", &path);
        bindings.push_back({ xrThumbstickClickRAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/output/haptic", &path);
        bindings.push_back({ xrHapticAction, path });

        suggestedBindings.interactionProfile = picoNeo3Path;
        suggestedBindings.suggestedBindings = bindings.data();
        suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
        XrResult neo3Result = xrSuggestInteractionProfileBindings(xrInstance, &suggestedBindings);
        if (XR_SUCCEEDED(neo3Result)) {
            VR_Log("stdVR_OpenXR: Pico Neo3 controller bindings registered\n");
        }
        // XR_ERROR_PATH_UNSUPPORTED is expected on non-Pico devices
    }

    // Simple controller fallback (Khronos simple controller)
    XrPath simplePath;
    if (XR_SUCCEEDED(xrStringToPath(xrInstance, "/interaction_profiles/khr/simple_controller", &simplePath))) {
        bindings.clear();

        // Left controller - only has select (trigger) and menu
        xrStringToPath(xrInstance, "/user/hand/left/input/aim/pose", &path);
        bindings.push_back({ xrAimPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/grip/pose", &path);
        bindings.push_back({ xrGripPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/select/click", &path);
        bindings.push_back({ xrTriggerAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/input/menu/click", &path);
        bindings.push_back({ xrMenuAction, path });
        xrStringToPath(xrInstance, "/user/hand/left/output/haptic", &path);
        bindings.push_back({ xrHapticAction, path });

        // Right controller
        xrStringToPath(xrInstance, "/user/hand/right/input/aim/pose", &path);
        bindings.push_back({ xrAimPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/grip/pose", &path);
        bindings.push_back({ xrGripPoseAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/select/click", &path);
        bindings.push_back({ xrTriggerAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/input/menu/click", &path);
        bindings.push_back({ xrMenuAction, path });
        xrStringToPath(xrInstance, "/user/hand/right/output/haptic", &path);
        bindings.push_back({ xrHapticAction, path });

        suggestedBindings.interactionProfile = simplePath;
        suggestedBindings.suggestedBindings = bindings.data();
        suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
        xrSuggestInteractionProfileBindings(xrInstance, &suggestedBindings);
    }

    VR_Log("stdVR_OpenXR: Controller bindings configured for Oculus Touch, Pico 4/Neo3, Valve Index, HTC Vive, and simple controllers\n");
    return 1;
}

static void DestroyActionSet(void)
{
    for (int i = 0; i < STDVR_CONTROLLER_COUNT; i++) {
        if (xrAimControllerSpaces[i] != XR_NULL_HANDLE) {
            xrDestroySpace(xrAimControllerSpaces[i]);
            xrAimControllerSpaces[i] = XR_NULL_HANDLE;
        }
    }

    for (int i = 0; i < STDVR_CONTROLLER_COUNT; i++) {
        if (xrGripControllerSpaces[i] != XR_NULL_HANDLE) {
            xrDestroySpace(xrGripControllerSpaces[i]);
            xrGripControllerSpaces[i] = XR_NULL_HANDLE;
        }
    }

    if (xrActionSet != XR_NULL_HANDLE) {
        xrDestroyActionSet(xrActionSet);
        xrActionSet = XR_NULL_HANDLE;
    }
}

extern "C" int stdVR_OpenXR_CreateSession(void* pGLContext)
{
    if (xrSession != XR_NULL_HANDLE) {
        return 1; // Already created
    }

    xrExitRequested = false;
    xrRecenterYaw = 0.0f;
    xrRecenterPos = { 0.0f, 0.0f, 0.0f };

    VR_Log("stdVR_OpenXR: Creating session...\n");

    // Check OpenGL/OpenGL ES requirements
#ifdef __ANDROID__
    XrGraphicsRequirementsOpenGLESKHR glReqs = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
    PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetOpenGLESGraphicsRequirementsKHR = nullptr;
    xrGetInstanceProcAddr(xrInstance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&xrGetOpenGLESGraphicsRequirementsKHR);
    if (xrGetOpenGLESGraphicsRequirementsKHR) {
        XrResult reqResult = xrGetOpenGLESGraphicsRequirementsKHR(xrInstance, xrSystemId, &glReqs);
        if (XR_SUCCEEDED(reqResult)) {
            VR_Log("stdVR_OpenXR: GLES requirements - min version: %d.%d.%d, max version: %d.%d.%d\n",
                XR_VERSION_MAJOR(glReqs.minApiVersionSupported),
                XR_VERSION_MINOR(glReqs.minApiVersionSupported),
                XR_VERSION_PATCH(glReqs.minApiVersionSupported),
                XR_VERSION_MAJOR(glReqs.maxApiVersionSupported),
                XR_VERSION_MINOR(glReqs.maxApiVersionSupported),
                XR_VERSION_PATCH(glReqs.maxApiVersionSupported));
        }
    }
#else
    XrGraphicsRequirementsOpenGLKHR glReqs = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR = nullptr;
    xrGetInstanceProcAddr(xrInstance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&xrGetOpenGLGraphicsRequirementsKHR);
    if (xrGetOpenGLGraphicsRequirementsKHR) {
        XrResult reqResult = xrGetOpenGLGraphicsRequirementsKHR(xrInstance, xrSystemId, &glReqs);
        if (XR_SUCCEEDED(reqResult)) {
            VR_Log("stdVR_OpenXR: GL requirements - min version: %d.%d.%d, max version: %d.%d.%d\n",
                XR_VERSION_MAJOR(glReqs.minApiVersionSupported),
                XR_VERSION_MINOR(glReqs.minApiVersionSupported),
                XR_VERSION_PATCH(glReqs.minApiVersionSupported),
                XR_VERSION_MAJOR(glReqs.maxApiVersionSupported),
                XR_VERSION_MINOR(glReqs.maxApiVersionSupported),
                XR_VERSION_PATCH(glReqs.maxApiVersionSupported));
        }
    }
#endif

    // Create session with graphics binding
    XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.systemId = xrSystemId;

#ifdef __ANDROID__
    // Android: Use OpenGL ES binding with EGL
    XrGraphicsBindingOpenGLESAndroidKHR glBinding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    glBinding.display = eglGetCurrentDisplay();
    glBinding.config = (EGLConfig)0;  // Not needed for most runtimes
    glBinding.context = eglGetCurrentContext();
    sessionInfo.next = &glBinding;
#else
    // Windows: Use OpenGL binding with WGL
    XrGraphicsBindingOpenGLWin32KHR glBinding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    glBinding.hDC = wglGetCurrentDC();
    glBinding.hGLRC = (HGLRC)pGLContext;
    sessionInfo.next = &glBinding;
#endif

    XrResult sessionResult = xrCreateSession(xrInstance, &sessionInfo, &xrSession);
    if (XR_FAILED(sessionResult)) {
        VR_Log("OpenXR error: xrCreateSession returned %d\n", sessionResult);
        return 0;
    }

    // From here on, if anything fails we need to clean up the session
    XrResult result;

    // Added: read the display refresh rates that the runtime offers. This needs a live session.
    stdVR_OpenXR_InitRefreshRates();

    // Store selected format for MultiView swapchain creation later
    // (declared here to avoid goto jumping over initialization)
    int64_t selectedFormat = GL_RGBA8;  // Fallback

    // Create reference spaces
    XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;

    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    result = xrCreateReferenceSpace(xrSession, &spaceInfo, &xrLocalSpace);
    if (XR_FAILED(result)) {
        VR_Log("OpenXR error: xrCreateReferenceSpace(LOCAL) returned %d\n", result);
        goto cleanup_session;
    }

    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    result = xrCreateReferenceSpace(xrSession, &spaceInfo, &xrViewSpace);
    if (XR_FAILED(result)) {
        VR_Log("OpenXR error: xrCreateReferenceSpace(VIEW) returned %d\n", result);
        goto cleanup_session;
    }

    // Try to create stage space, fall back to local if not available
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (XR_FAILED(xrCreateReferenceSpace(xrSession, &spaceInfo, &xrStageSpace))) {
        xrStageSpace = xrLocalSpace;
    }

    {
        // Enumerate supported swapchain formats
        uint32_t formatCount = 0;
        xrEnumerateSwapchainFormats(xrSession, 0, &formatCount, nullptr);
        std::vector<int64_t> formats(formatCount);
        xrEnumerateSwapchainFormats(xrSession, formatCount, &formatCount, formats.data());
        VR_Log("stdVR_OpenXR: Available swapchain formats:\n");
        for (uint32_t i = 0; i < formatCount; i++) {
            VR_Log("  - 0x%llX\n", (long long)formats[i]);
            // Prefer GL_SRGB8_ALPHA8 for correct color handling
            if (formats[i] == GL_SRGB8_ALPHA8) {
                selectedFormat = GL_SRGB8_ALPHA8;
            }
            // GL_RGBA8 is acceptable if sRGB not available
            else if (formats[i] == GL_RGBA8 && selectedFormat != GL_SRGB8_ALPHA8) {
                selectedFormat = GL_RGBA8;
            }
        }
        VR_Log("stdVR_OpenXR: Selected swapchain format: 0x%llX\n", (long long)selectedFormat);

        // Create swapchains
        for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
            XrSwapchainCreateInfo swapchainInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
            swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            swapchainInfo.format = selectedFormat;
            swapchainInfo.sampleCount = 1;
            swapchainInfo.width = xrConfigViews[eye].recommendedImageRectWidth;
            swapchainInfo.height = xrConfigViews[eye].recommendedImageRectHeight;
            swapchainInfo.faceCount = 1;
            swapchainInfo.arraySize = 1;
            swapchainInfo.mipCount = 1;

            result = xrCreateSwapchain(xrSession, &swapchainInfo, &xrSwapchains[eye]);
            if (XR_FAILED(result)) {
                VR_Log("OpenXR error: xrCreateSwapchain(%d) returned %d\n", eye, result);
                goto cleanup_session;
            }

            // Get swapchain images
            uint32_t imageCount = 0;
            xrEnumerateSwapchainImages(xrSwapchains[eye], 0, &imageCount, nullptr);
            xrSwapchainImages[eye].resize(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_GL });
            xrEnumerateSwapchainImages(xrSwapchains[eye], imageCount, &imageCount, (XrSwapchainImageBaseHeader*)xrSwapchainImages[eye].data());

            // Create a null swapchain per-eye for screen-layer projection
            result = xrCreateSwapchain(xrSession, &swapchainInfo, &xrNullSwapchains[eye]);
            if (XR_FAILED(result)) {
                VR_Log("OpenXR error: xrCreateSwapchain(null %d) returned %d\n", eye, result);
                goto cleanup_session;
            }

            uint32_t nullImageCount = 0;
            xrEnumerateSwapchainImages(xrNullSwapchains[eye], 0, &nullImageCount, nullptr);
            xrNullSwapchainImages[eye].resize(nullImageCount, { XR_TYPE_SWAPCHAIN_IMAGE_GL });
            xrEnumerateSwapchainImages(xrNullSwapchains[eye], nullImageCount, &nullImageCount, (XrSwapchainImageBaseHeader*)xrNullSwapchainImages[eye].data());
        }
    }

    // Initialize views
    for (int i = 0; i < STDVR_EYE_COUNT; i++) {
        xrViews[i] = { XR_TYPE_VIEW };
    }

    // Create per-swapchain-image FBOs and depth textures (mirrors JKXR approach)
    // OPTIMIZATION: Keep depth textures permanently attached, only swap color per-frame
    for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
        const uint32_t imageCount = (uint32_t)xrSwapchainImages[eye].size();
        const int width = xrConfigViews[eye].recommendedImageRectWidth;
        const int height = xrConfigViews[eye].recommendedImageRectHeight;

        vrFBO[eye].resize(imageCount);
        vrDepthTex[eye].resize(imageCount);
        vrFBOValidated[eye].resize(imageCount, false);  // Added: track validation state

        if (imageCount > 0) {
            glGenFramebuffers(imageCount, vrFBO[eye].data());
            glGenTextures(imageCount, vrDepthTex[eye].data());
        }

        for (uint32_t i = 0; i < imageCount; i++) {
            // Create depth texture for this swapchain image
            glBindTexture(GL_TEXTURE_2D, vrDepthTex[eye][i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#if defined(TARGET_ANDROID_NATIVE_GLES)
            // GLES3 doesn't have GL_DEPTH_COMPONENT32, use GL_DEPTH_COMPONENT32F
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
#else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32,
                width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
#endif
            glBindTexture(GL_TEXTURE_2D, 0);

            // Bind FBO and attach depth permanently (color is swapped per-frame)
            glBindFramebuffer(GL_FRAMEBUFFER, vrFBO[eye][i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                vrDepthTex[eye][i], 0);

            // Note: Color attachment validation happens on first use per swapchain image
            // We don't detach color here - it will be attached when PrepareEyeBuffer is called
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // Create a lightweight FBO for null swapchain clears
        glGenFramebuffers(1, &vrNullFBO[eye]);
    }

    VR_Log("stdVR_OpenXR: VR FBOs created - eye0 images=%zu, eye1 images=%zu, nullFBO[0]=%u, nullFBO[1]=%u\n",
        vrFBO[0].size(), vrFBO[1].size(), vrNullFBO[0], vrNullFBO[1]);


    // Check for MultiView extension support (single-pass stereo rendering)
    // Supported on: Meta Quest (Adreno 650/740), Pico 4/Neo3 (Adreno 650), other XR2-based
    // devices, and desktop GL (NVIDIA/Intel; AMD support varies).
#if defined(MULTIVIEW_ENABLED)
    {
#if defined(TARGET_ANDROID_NATIVE_GLES)
        const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
        bool hasMultiView2 = extensions && strstr(extensions, "GL_OVR_multiview2") != nullptr;
        bool hasMultiViewMSAA = extensions && strstr(extensions, "GL_OVR_multiview_multisampled_render_to_texture") != nullptr;

        VR_Log("stdVR_OpenXR: MultiView extension check:\n");
        VR_Log("  GL_OVR_multiview2: %s\n", hasMultiView2 ? "YES" : "NO");
        VR_Log("  GL_OVR_multiview_multisampled_render_to_texture: %s\n", hasMultiViewMSAA ? "YES" : "NO");

        if (hasMultiView2) {
            // GLES: load MultiView function pointers via eglGetProcAddress
            glFramebufferTextureMultiviewOVR = (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)
                eglGetProcAddress("glFramebufferTextureMultiviewOVR");
            glFramebufferTextureMultisampleMultiviewOVR = (PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)
                eglGetProcAddress("glFramebufferTextureMultisampleMultiviewOVR");

            if (glFramebufferTextureMultiviewOVR) {
                vrMultiViewSupported = true;
                VR_Log("stdVR_OpenXR: MultiView supported and function pointers loaded\n");
            } else {
                VR_Log("stdVR_OpenXR: MultiView extension present but function pointer load failed\n");
            }
        }
#else
        // Desktop GL: GLEW declares and loads the GL_OVR_multiview2 entry points during
        // glewInit(), so we use them directly (no manual SDL_GL_GetProcAddress needed).
        bool hasMultiView2 = (GLEW_OVR_multiview2 != 0) && (glFramebufferTextureMultiviewOVR != nullptr);

        VR_Log("stdVR_OpenXR: MultiView extension check (desktop/GLEW):\n");
        VR_Log("  GLEW_OVR_multiview2: %s\n", (GLEW_OVR_multiview2 != 0) ? "YES" : "NO");
        VR_Log("  glFramebufferTextureMultiviewOVR: %s\n", glFramebufferTextureMultiviewOVR ? "loaded" : "NULL");

        if (hasMultiView2) {
            vrMultiViewSupported = true;
            VR_Log("stdVR_OpenXR: MultiView supported (desktop GLEW)\n");
        }
#endif
    }

    // Create the MultiView swapchain + per-image FBOs/depth at the current (supersampled) render
    // size. Factored into a helper so the runtime supersampling rebuild reuses the same logic.
    if (vrMultiViewSupported) {
        vrMVSwapchainFormat = selectedFormat;
        if (!stdVR_OpenXR_CreateMultiViewSwapchainResources()) {
            // MultiView is the only VR gameplay path (per-eye gameplay rendering was removed). On
            // desktop the FATAL check below aborts; on Android/Quest/Pico MultiView is always
            // supported, so this is not expected to happen in practice.
            vrMultiViewSupported = false;
        }
    }

#if !defined(TARGET_ANDROID_NATIVE_GLES)
    // Desktop PCVR is MultiView-only (no per-eye fallback). If the GL/driver does not
    // expose GL_OVR_multiview2, fail loudly here rather than rendering incorrectly.
    // (On Android/GLES the per-eye path is retained as a safety net, so we don't abort.)
    if (!vrMultiViewEnabled) {
        VR_Log("stdVR_OpenXR: FATAL - GL_OVR_multiview2 is required for VR but is unavailable.\n");
        VR_Log("stdVR_OpenXR: Your GPU/driver must support OpenGL MultiView (GL_OVR_multiview2).\n");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenJKDF2 VR",
            "This GPU/driver does not support OpenGL MultiView (GL_OVR_multiview2), "
            "which is required for VR rendering.", nullptr);
        goto cleanup_session;
    }
#endif
#endif // MULTIVIEW_ENABLED

    // Create action set
    if (!CreateActionSet()) {
        VR_Log("stdVR_OpenXR: Failed to create action set\n");
        goto cleanup_session;
    }

    // Attach action set to session
    {
        XrSessionActionSetsAttachInfo attachInfo = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
        attachInfo.countActionSets = 1;
        attachInfo.actionSets = &xrActionSet;
        result = xrAttachSessionActionSets(xrSession, &attachInfo);
        if (XR_FAILED(result)) {
            VR_Log("OpenXR error: xrAttachSessionActionSets returned %d\n", result);
            goto cleanup_session;
        }
    }

    // Create controller spaces
    for (int i = 0; i < STDVR_CONTROLLER_COUNT; i++) {
        XrActionSpaceCreateInfo spaceCreateInfo = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        spaceCreateInfo.action = xrAimPoseAction;
        spaceCreateInfo.poseInActionSpace.orientation.w = 1.0f;
        spaceCreateInfo.subactionPath = xrHandPaths[i];
        result = xrCreateActionSpace(xrSession, &spaceCreateInfo, &xrAimControllerSpaces[i]);
        if (XR_FAILED(result)) {
            VR_Log("OpenXR error: xrCreateActionSpace(%d) returned %d\n", i, result);
            goto cleanup_session;
        }
    }

    for (int i = 0; i < STDVR_CONTROLLER_COUNT; i++) {
        XrActionSpaceCreateInfo spaceCreateInfo = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        spaceCreateInfo.action = xrGripPoseAction;
        spaceCreateInfo.poseInActionSpace.orientation.w = 1.0f;
        spaceCreateInfo.subactionPath = xrHandPaths[i];
        result = xrCreateActionSpace(xrSession, &spaceCreateInfo, &xrGripControllerSpaces[i]);
        if (XR_FAILED(result)) {
            VR_Log("OpenXR error: xrCreateActionSpace(%d) returned %d\n", i, result);
            goto cleanup_session;
        }
    }

    VR_Log("stdVR_OpenXR: Session created successfully\n");
    return 1;

cleanup_session:
    VR_Log("stdVR_OpenXR: Session creation failed, cleaning up\n");
    stdVR_OpenXR_DestroySession();
    return 0;
}

extern "C" void stdVR_OpenXR_DestroySession(void)
{
    VR_Log("stdVR_OpenXR: DestroySession called, session=%p, running=%d\n",
           (void*)xrSession, xrSessionRunning ? 1 : 0);

    // If session is running, request proper exit first
    if (xrSession != XR_NULL_HANDLE && xrSessionRunning) {
        VR_Log("stdVR_OpenXR: Requesting session exit...\n");
        XrResult result = xrRequestExitSession(xrSession);
        if (XR_SUCCEEDED(result)) {
            // Poll events to process the exit (STOPPING -> xrEndSession -> IDLE)
            // Give it a few attempts to complete the state machine
            for (int i = 0; i < 10 && xrSessionRunning; i++) {
                PollEvents();
                if (!xrSessionRunning) break;
                // Small sleep to allow runtime to process
#ifdef _WIN32
                Sleep(10);
#else
                usleep(10000);
#endif
            }
            VR_Log("stdVR_OpenXR: Session exit complete, running=%d\n", xrSessionRunning ? 1 : 0);
        } else {
            VR_Log("stdVR_OpenXR: xrRequestExitSession failed: %d\n", result);
        }
    }

    xrSessionRunning = false;
    stdVR_clientInfo.bSessionRunning = 0;

    DestroyActionSet();

    // Destroy VR FBOs and reset validation state
    for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
        if (!vrFBO[eye].empty()) {
            glDeleteFramebuffers((GLsizei)vrFBO[eye].size(), vrFBO[eye].data());
            vrFBO[eye].clear();
        }
        if (!vrDepthTex[eye].empty()) {
            glDeleteTextures((GLsizei)vrDepthTex[eye].size(), vrDepthTex[eye].data());
            vrDepthTex[eye].clear();
        }
        vrFBOValidated[eye].clear();  // Reset validation flags
        if (vrNullFBO[eye] != 0) {
            glDeleteFramebuffers(1, &vrNullFBO[eye]);
            vrNullFBO[eye] = 0;
        }
        vrNullFBOValidated[eye] = false;  // Reset null FBO validation
        vrCurrentFBO[eye] = 0;
    }
    stateTrackingActive = false;  // Reset state tracking

    for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
        if (xrSwapchains[eye] != XR_NULL_HANDLE) {
            xrDestroySwapchain(xrSwapchains[eye]);
            xrSwapchains[eye] = XR_NULL_HANDLE;
        }
        xrSwapchainImages[eye].clear();

        if (xrNullSwapchains[eye] != XR_NULL_HANDLE) {
            xrDestroySwapchain(xrNullSwapchains[eye]);
            xrNullSwapchains[eye] = XR_NULL_HANDLE;
        }
        xrNullSwapchainImages[eye].clear();
    }

    // Destroy MultiView resources (ungated so desktop frees them too)
    stdVR_OpenXR_DestroyMultiViewSwapchainResources();
    vrMultiViewEnabled = false;
    vrMultiViewWidth = 0;
    vrMultiViewHeight = 0;

    if (xrStageSpace != XR_NULL_HANDLE && xrStageSpace != xrLocalSpace) {
        xrDestroySpace(xrStageSpace);
    }
    xrStageSpace = XR_NULL_HANDLE;

    if (xrViewSpace != XR_NULL_HANDLE) {
        xrDestroySpace(xrViewSpace);
        xrViewSpace = XR_NULL_HANDLE;
    }

    if (xrLocalSpace != XR_NULL_HANDLE) {
        xrDestroySpace(xrLocalSpace);
        xrLocalSpace = XR_NULL_HANDLE;
    }

    if (xrSession != XR_NULL_HANDLE) {
        xrDestroySession(xrSession);
        xrSession = XR_NULL_HANDLE;
    }
}

static const char* SessionStateToString(XrSessionState state)
{
    switch (state) {
        case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
        case XR_SESSION_STATE_IDLE: return "IDLE";
        case XR_SESSION_STATE_READY: return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
        case XR_SESSION_STATE_STOPPING: return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING: return "EXITING";
        default: return "INVALID";
    }
}

static void HandleSessionStateChange(XrSessionState newState)
{
    VR_Log("stdVR_OpenXR: Session state change: %s -> %s\n",
        SessionStateToString(xrSessionState), SessionStateToString(newState));
    stdPlatform_Printf("stdVR_OpenXR: Session state change: %s -> %s\n",
        SessionStateToString(xrSessionState), SessionStateToString(newState));
    xrSessionState = newState;

    switch (newState) {
        case XR_SESSION_STATE_READY: {
            VR_Log("stdVR_OpenXR: Calling xrBeginSession...\n");
            stdPlatform_Printf("stdVR_OpenXR: Calling xrBeginSession...\n");
            XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
            beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            XrResult result = xrBeginSession(xrSession, &beginInfo);
            if (XR_SUCCEEDED(result)) {
                xrSessionRunning = true;
                stdVR_clientInfo.bSessionRunning = 1;
                VR_Log("stdVR_OpenXR: Session started successfully! VR rendering active.\n");
                stdPlatform_Printf("stdVR_OpenXR: xrBeginSession succeeded! bSessionRunning=1\n");
            } else {
                VR_Log("stdVR_OpenXR: xrBeginSession failed with error %d\n", result);
                stdPlatform_Printf("stdVR_OpenXR: xrBeginSession FAILED with error %d\n", result);
            }
            break;
        }
        case XR_SESSION_STATE_SYNCHRONIZED:
            VR_Log("stdVR_OpenXR: Session synchronized (waiting for visibility)\n");
            break;
        case XR_SESSION_STATE_VISIBLE:
            VR_Log("stdVR_OpenXR: Session visible (app in background)\n");
            break;
        case XR_SESSION_STATE_FOCUSED:
            VR_Log("stdVR_OpenXR: Session focused (app has input focus)\n");
            break;
        case XR_SESSION_STATE_STOPPING:
            VR_Log("stdVR_OpenXR: Session stopping, calling xrEndSession...\n");
            xrEndSession(xrSession);
            xrSessionRunning = false;
            stdVR_clientInfo.bSessionRunning = 0;
            VR_Log("stdVR_OpenXR: Session stopped\n");
            break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            VR_Log("stdVR_OpenXR: Session exiting or loss pending\n");
            xrSessionRunning = false;
            stdVR_clientInfo.bSessionRunning = 0;
            xrExitRequested = true; // Added: ask the main loop to shut down
            break;
        case XR_SESSION_STATE_IDLE:
            VR_Log("stdVR_OpenXR: Session idle\n");
            break;
        default:
            break;
    }
}

static void PollEvents(void)
{
    if (xrInstance == XR_NULL_HANDLE) {
        return;
    }

    XrEventDataBuffer eventData = { XR_TYPE_EVENT_DATA_BUFFER };
    XrResult result;
    int eventsThisFrame = 0;

    while ((result = xrPollEvent(xrInstance, &eventData)) == XR_SUCCESS) {
        eventsThisFrame++;
        switch (eventData.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                XrEventDataSessionStateChanged* stateChanged = (XrEventDataSessionStateChanged*)&eventData;
                HandleSessionStateChange(stateChanged->state);
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                VR_Log("stdVR_OpenXR: Instance loss pending\n");
                xrExitRequested = true; // Added: the instance is going away; quit rather than retry
                break;
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                VR_Log("stdVR_OpenXR: Interaction profile changed\n");
                break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                VR_Log("stdVR_OpenXR: Reference space change pending\n");
                break;
            default:
                VR_Log("stdVR_OpenXR: Unknown event type %d\n", eventData.type);
                break;
        }
        eventData = { XR_TYPE_EVENT_DATA_BUFFER };
    }

}

extern "C" void stdVR_OpenXR_PollEvents(void)
{
    if (xrInstance == XR_NULL_HANDLE) {
        return;
    }

    PollEvents();
}

// Compute the MultiView render size: the runtime's recommended per-view size scaled by the user
// supersampling (VR render scale) and clamped to the runtime's maximum supported image rect. Both
// eyes share eye 0's size (the MultiView swapchain is a single array image used by both eyes).
static void stdVR_OpenXR_GetMVRenderSize(int* outW, int* outH)
{
    float ss = stdVR_config.supersampling;
    if (ss <= 0.0f) ss = 1.0f;

    int baseW = (int)xrConfigViews[0].recommendedImageRectWidth;
    int baseH = (int)xrConfigViews[0].recommendedImageRectHeight;
    int maxW  = (int)xrConfigViews[0].maxImageRectWidth;
    int maxH  = (int)xrConfigViews[0].maxImageRectHeight;

    int w = (int)(baseW * ss + 0.5f);
    int h = (int)(baseH * ss + 0.5f);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (maxW > 0 && w > maxW) w = maxW;
    if (maxH > 0 && h > maxH) h = maxH;

    *outW = w;
    *outH = h;
}

// Destroy the MultiView swapchain + its per-image FBOs/depth textures. Ungated (used on both
// desktop and Android) so the runtime rebuild works everywhere. Does NOT clear vrMultiViewEnabled
// (the caller decides: a rebuild re-sets it; full session teardown clears it separately).
static void stdVR_OpenXR_DestroyMultiViewSwapchainResources(void)
{
    if (!vrMultiViewFBOs.empty()) {
        glDeleteFramebuffers((GLsizei)vrMultiViewFBOs.size(), vrMultiViewFBOs.data());
        vrMultiViewFBOs.clear();
    }
    if (!vrMultiViewDepthTextures.empty()) {
        glDeleteTextures((GLsizei)vrMultiViewDepthTextures.size(), vrMultiViewDepthTextures.data());
        vrMultiViewDepthTextures.clear();
    }
    if (xrMultiViewSwapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(xrMultiViewSwapchain);
        xrMultiViewSwapchain = XR_NULL_HANDLE;
    }
    xrMultiViewSwapchainImages.clear();
    vrMultiViewRenderActive = false;
}

// Create the MultiView swapchain (RazeXR-style: single array swapchain + one FBO/depth per image)
// at the current supersampled render size. Sets vrMultiViewEnabled on success. Returns false (and
// cleans up any partial state) on failure.
static bool stdVR_OpenXR_CreateMultiViewSwapchainResources(void)
{
    if (!vrMultiViewSupported) return false;

    int mvWidth = 0, mvHeight = 0;
    stdVR_OpenXR_GetMVRenderSize(&mvWidth, &mvHeight);

    XrSwapchainCreateInfo mvSwapchainInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    mvSwapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    mvSwapchainInfo.format = vrMVSwapchainFormat;
    mvSwapchainInfo.sampleCount = 1;
    mvSwapchainInfo.width = mvWidth;
    mvSwapchainInfo.height = mvHeight;
    mvSwapchainInfo.faceCount = 1;
    mvSwapchainInfo.arraySize = VR_MV_ARRAY_SIZE;  // Android: 2 (layers 0,1); desktop: 3 (skip layer 0 for VDXR)
    mvSwapchainInfo.mipCount = 1;

    XrResult result = xrCreateSwapchain(xrSession, &mvSwapchainInfo, &xrMultiViewSwapchain);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: MultiView swapchain creation failed (%d)\n", result);
        xrMultiViewSwapchain = XR_NULL_HANDLE;
        vrMultiViewEnabled = false;
        return false;
    }

    // Get swapchain images
    uint32_t mvImageCount = 0;
    xrEnumerateSwapchainImages(xrMultiViewSwapchain, 0, &mvImageCount, nullptr);
    xrMultiViewSwapchainImages.resize(mvImageCount, { XR_TYPE_SWAPCHAIN_IMAGE_GL });
    xrEnumerateSwapchainImages(xrMultiViewSwapchain, mvImageCount, &mvImageCount,
        (XrSwapchainImageBaseHeader*)xrMultiViewSwapchainImages.data());

    // Create per-swapchain-image FBOs and depth textures (like RazeXR)
    vrMultiViewFBOs.resize(mvImageCount);
    vrMultiViewDepthTextures.resize(mvImageCount);
    glGenFramebuffers(mvImageCount, vrMultiViewFBOs.data());
    glGenTextures(mvImageCount, vrMultiViewDepthTextures.data());

    bool allFBOsComplete = true;
    for (uint32_t i = 0; i < mvImageCount; i++) {
        const GLuint colorTexture = xrMultiViewSwapchainImages[i].image;

        // Set texture parameters on color texture array (like RazeXR)
        glBindTexture(GL_TEXTURE_2D_ARRAY, colorTexture);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        // Create depth texture array for this swapchain image. Layer count matches the color
        // swapchain's arraySize so that baseViewIndex+numViews stays within bounds on both layouts.
        glBindTexture(GL_TEXTURE_2D_ARRAY, vrMultiViewDepthTextures[i]);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH_COMPONENT24, mvWidth, mvHeight, VR_MV_ARRAY_SIZE);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        // Create FBO with both color and depth permanently attached (like RazeXR).
        // baseViewIndex is 0 on Android (layers 0,1) and 1 on desktop (layers 1,2).
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, vrMultiViewFBOs[i]);
        glFramebufferTextureMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            vrMultiViewDepthTextures[i], 0, VR_MV_BASE_VIEW_INDEX, 2);
        glFramebufferTextureMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            colorTexture, 0, VR_MV_BASE_VIEW_INDEX, 2);

        GLenum fboStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
            VR_Log("stdVR_OpenXR: MultiView FBO[%u] incomplete, status 0x%x\n", i, fboStatus);
            allFBOsComplete = false;
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    if (!allFBOsComplete) {
        VR_Log("stdVR_OpenXR: MultiView FBO creation failed\n");
        stdVR_OpenXR_DestroyMultiViewSwapchainResources();
        vrMultiViewEnabled = false;
        return false;
    }

    vrMultiViewWidth = mvWidth;
    vrMultiViewHeight = mvHeight;
    vrMultiViewEnabled = true;

    // Added: these were set once at startup from the runtime's UNSCALED recommended size and
    // never updated, so any supersampling other than 1.0 left the HUD and the weapon wheel
    // laid out for a buffer they were no longer drawn into.
    stdVR_clientInfo.renderWidth = mvWidth;
    stdVR_clientInfo.renderHeight = mvHeight;
    std3D_SetVRTargetSize(mvWidth, mvHeight);
    VR_Log("stdVR_OpenXR: MultiView swapchain created - %dx%d (ss=%.2f), %u images with per-image FBOs\n",
        mvWidth, mvHeight, (double)stdVR_config.supersampling, mvImageCount);
    return true;
}

// Rebuild the MultiView swapchain at the current supersampling. MUST be called between frames
// (no swapchain image acquired, not between xrBeginFrame/xrEndFrame) - see stdVR_OpenXR_BeginFrame.
static void stdVR_OpenXR_RebuildMultiViewSwapchain(void)
{
    if (!vrMultiViewSupported || xrSession == XR_NULL_HANDLE) return;

    int newW = 0, newH = 0;
    stdVR_OpenXR_GetMVRenderSize(&newW, &newH);

    // Skip if the pixel size is unchanged (small supersampling deltas can round to the same size).
    if (vrMultiViewEnabled && newW == vrMultiViewWidth && newH == vrMultiViewHeight) {
        return;
    }

    VR_Log("stdVR_OpenXR: rebuilding MultiView swapchain for supersampling change -> %dx%d\n", newW, newH);

    glFinish();  // ensure the GPU is done with the old swapchain images before destroying them
    stdVR_OpenXR_DestroyMultiViewSwapchainResources();

    if (!stdVR_OpenXR_CreateMultiViewSwapchainResources()) {
        // Recreate failed (e.g. out of GPU memory at a high scale). Fall back to 1.0x and retry
        // once so VR keeps rendering rather than going black.
        VR_Log("stdVR_OpenXR: MultiView rebuild failed; retrying at supersampling 1.0\n");
        stdVR_config.supersampling = 1.0f;
        stdVR_OpenXR_DestroyMultiViewSwapchainResources();
        stdVR_OpenXR_CreateMultiViewSwapchainResources();
    }
}

extern "C" void stdVR_OpenXR_RequestSupersampleRebuild(void)
{
    // Only meaningful once a MultiView session is live; the initial swapchain is already built at
    // the loaded supersampling. The rebuild itself runs between frames (consumed in BeginFrame).
    if (xrSessionRunning && vrMultiViewEnabled) {
        vrPendingSupersampleRebuild = 1;
    }
}

extern "C" int stdVR_OpenXR_WaitFrame(void)
{
    // Poll events first to catch session state changes
    PollEvents();

    // Check session state - don't try to wait if session isn't in a running state
    if (!xrSessionRunning) {
        return 0;
    }

    // Also check explicit session states that shouldn't call WaitFrame
    if (xrSessionState == XR_SESSION_STATE_STOPPING ||
        xrSessionState == XR_SESSION_STATE_EXITING ||
        xrSessionState == XR_SESSION_STATE_LOSS_PENDING ||
        xrSessionState == XR_SESSION_STATE_IDLE) {
        VR_Log("stdVR_OpenXR: WaitFrame skipped - session state is %s\n",
            SessionStateToString(xrSessionState));
        return 0;
    }

    xrFrameState = { XR_TYPE_FRAME_STATE };
    XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
    XrResult result = xrWaitFrame(xrSession, &waitInfo, &xrFrameState);

    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: xrWaitFrame failed with error %d\n", result);
        // If WaitFrame fails, session may be ending - poll again to update state
        PollEvents();
        return 0;
    }

    stdVR_clientInfo.bShouldRender = xrFrameState.shouldRender;
    stdVR_clientInfo.predictedDisplayTime = xrFrameState.predictedDisplayTime;

    vrFrameCount++;
    // Only log first 3 frames
    if (vrFrameCount <= 3) {
        VR_Log("stdVR_OpenXR: WaitFrame #%d - shouldRender=%d\n",
            vrFrameCount, xrFrameState.shouldRender ? 1 : 0);
    }

    return 1;
}

extern "C" int stdVR_OpenXR_BeginFrame(void)
{
    if (!xrSessionRunning) {
        return 0;
    }

    // Apply a pending supersampling (render scale) change here. This is the only safe point to
    // destroy/recreate the MultiView swapchain: the previous frame's xrEndFrame has completed and
    // this frame's swapchain image has not been acquired yet (acquire happens in Prepare*Buffer).
    if (vrPendingSupersampleRebuild) {
        vrPendingSupersampleRebuild = 0;
        stdVR_OpenXR_RebuildMultiViewSwapchain();
    }

    XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
    XrResult result = xrBeginFrame(xrSession, &beginInfo);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: xrBeginFrame failed with error %d\n", result);
        return 0;
    }

    xrFrameInProgress = true;
    return 1;
}

// Helper to create a quaternion from axis-angle
static XrQuaternionf QuaternionFromAxisAngle(const XrVector3f& axis, float angleRadians)
{
    float halfAngle = angleRadians * 0.5f;
    float sinHalf = std::sin(halfAngle);
    float cosHalf = std::cos(halfAngle);
    return { axis.x * sinHalf, axis.y * sinHalf, axis.z * sinHalf, cosHalf };
}

// Convert degrees to radians
#define DEG2RAD(x) ((x) * 3.14159265358979323846f / 180.0f)

extern "C" int stdVR_OpenXR_EndFrame(void)
{
    static int endFrameCount = 0;
    endFrameCount++;
#if STDVR_HOTPATH_DEBUG
    if (endFrameCount <= 20 || endFrameCount % 100 == 0) {
        stdPlatform_Printf("stdVR_OpenXR_EndFrame ENTRY #%d: running=%d, inProgress=%d\n",
            endFrameCount, xrSessionRunning, xrFrameInProgress);
    }
#endif

    if (!xrSessionRunning || !xrFrameInProgress) {
#if STDVR_HOTPATH_DEBUG
        if (endFrameCount <= 20) {
            stdPlatform_Printf("stdVR_OpenXR_EndFrame: early exit (running=%d, inProgress=%d)\n",
                xrSessionRunning, xrFrameInProgress);
        }
#endif
        return 0;
    }

    xrFrameInProgress = false;

    std::vector<XrCompositionLayerBaseHeader*> layers;
    XrCompositionLayerProjection projectionLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    projectionLayer.next = nullptr;
    // The 3D scene layer is OPAQUE (the HUD is baked into the eye buffer, not a separate
    // alpha-blended layer here). Do NOT set XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT:
    // it makes the compositor blend the eye image against the (opaque black) environment using
    // the eye buffer's per-texel alpha. The scene's accumulated framebuffer alpha is not
    // guaranteed to be 1.0 where transparent/blended surfaces overlap, and because that overlap
    // pattern differs per eye (stereo parallax) the right layer ends up with low-alpha holes the
    // left layer lacks -> black wedges on Quest (SteamVR's compositor ignores it for opaque
    // projection layers, which is why PCVR was unaffected). Keep the layer fully opaque.
    // Unified: applies to both PCVR and Quest.
    projectionLayer.layerFlags = XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
    std::vector<XrCompositionLayerProjectionView> projectionViews(STDVR_EYE_COUNT, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW });
    XrCompositionLayerQuad quadLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };

    if (stdVR_clientInfo.bShouldRender) {
        // Check if we should use screen layer mode (for menus)
        if (stdVR_clientInfo.bUseScreenLayer) {
            // Screen layer mode: render menu as a 2D quad floating in front of player

            // Ensure null swapchains are valid and cleared for a black projection layer
            for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
                if (!stdVR_OpenXR_ClearNullSwapchain(eye)) {
                    VR_Log("stdVR_OpenXR: Warning - failed to clear null swapchain for eye %d\n", eye);
                }
            }

            // Build a black projection layer using the null swapchains
            for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
                projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                projectionViews[eye].next = nullptr;
                projectionViews[eye].pose = xrViews[eye].pose;
                projectionViews[eye].fov = xrViews[eye].fov;
                projectionViews[eye].subImage.swapchain = xrNullSwapchains[eye];
                projectionViews[eye].subImage.imageRect.offset = { 0, 0 };
                projectionViews[eye].subImage.imageRect.extent = {
                    (int32_t)xrConfigViews[eye].recommendedImageRectWidth,
                    (int32_t)xrConfigViews[eye].recommendedImageRectHeight
                };
                projectionViews[eye].subImage.imageArrayIndex = 0;
            }

            projectionLayer.space = xrLocalSpace;
            projectionLayer.viewCount = STDVR_EYE_COUNT;
            projectionLayer.views = projectionViews.data();
            layers.push_back((XrCompositionLayerBaseHeader*)&projectionLayer);

            // Get swapchain dimensions
            int32_t width = (int32_t)xrConfigViews[0].recommendedImageRectWidth;
            int32_t height = (int32_t)xrConfigViews[0].recommendedImageRectHeight;

            // Configure the quad layer
            quadLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quadLayer.space = xrLocalSpace;  // Use local space to match tracking orientation
            quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;  // Show to both eyes

            // Use eye 0's swapchain for the menu content
            quadLayer.subImage.swapchain = xrSwapchains[0];
            quadLayer.subImage.imageRect.offset = { 0, 0 };
            quadLayer.subImage.imageRect.extent = { width, height };
            quadLayer.subImage.imageArrayIndex = 0;

            // Position the quad in front of where the player was looking when entering menu
            float distance = stdVR_clientInfo.screenLayerDistance;
            if (distance <= 0.0f) distance = 4.0f;

            float yawRad = DEG2RAD(stdVR_clientInfo.screenLayerSnapYaw);

            // Altered: the snap position is stored in JKDF2 axes (x=right, y=forward, z=up)
            // but the quad pose is OpenXR (x=right, y=up, z=back). Using it raw fed the head
            // HEIGHT in as the forward coordinate and threw the forward component away, which
            // offset the panel sideways by a fixed world vector — the menu appeared to sit
            // some 30 degrees off whichever way the player faced.
            float headX =  stdVR_clientInfo.screenLayerSnapPos.x;
            float headY =  stdVR_clientInfo.screenLayerSnapPos.z;
            float headZ = -stdVR_clientInfo.screenLayerSnapPos.y;

            XrVector3f pos = {
                headX - std::sin(yawRad) * distance,
                headY,
                headZ - std::cos(yawRad) * distance
            };
            quadLayer.pose.position = pos;

            // Orientation: face the player's snap position (rotate around Y axis)
            XrVector3f yAxis = { 0.0f, 1.0f, 0.0f };
            quadLayer.pose.orientation = QuaternionFromAxisAngle(yAxis, yawRad);

            // Screen size in meters
            float screenWidth = stdVR_clientInfo.screenLayerWidth;
            float screenHeight = stdVR_clientInfo.screenLayerHeight;
            if (screenWidth <= 0.0f) screenWidth = 6.0f;
            if (screenHeight <= 0.0f) screenHeight = 4.5f;
            quadLayer.size = { screenWidth, screenHeight };

            layers.push_back((XrCompositionLayerBaseHeader*)&quadLayer);
        } else {
            // Normal stereo projection mode for 3D gameplay
#if defined(MULTIVIEW_ENABLED)
            // MultiView: single array swapchain (single-pass stereo). On desktop this is the
            // only path; on Android it is preferred, with the per-eye fallback below.
            if (vrMultiViewEnabled && xrMultiViewSwapchain != XR_NULL_HANDLE) {
                // Submit each eye's REAL per-eye pose + native FOV on BOTH platforms. The GPU vertex
                // shader projects the view-space scene with each eye's real asymmetric projection
                // matrix (stdVR_SetMultiViewMatrices), so the submitted frustum is simply each eye's
                // native OpenXR FOV - correct convergence everywhere, no union/remap, no platform gate.
                for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
                    projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                    projectionViews[eye].next = nullptr;
                    projectionViews[eye].pose = xrViews[eye].pose;
                    projectionViews[eye].fov = xrViews[eye].fov;
                    projectionViews[eye].subImage.swapchain = xrMultiViewSwapchain;
                    projectionViews[eye].subImage.imageRect.offset = { 0, 0 };
                    // Submit the FULL supersampled image extent (the compositor downsamples to the panel).
                    projectionViews[eye].subImage.imageRect.extent = {
                        (int32_t)vrMultiViewWidth,
                        (int32_t)vrMultiViewHeight
                    };
                    // +VR_MV_LAYER_OFFSET skips layer 0 on desktop (VDXR-safe layout); 0 on Android
                    projectionViews[eye].subImage.imageArrayIndex = eye + VR_MV_LAYER_OFFSET;
                }
            }
#if defined(TARGET_ANDROID_NATIVE_GLES)
            else
            {
                // Per-eye swapchains (Android fallback only)
                for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
                    // Ensure proper initialization
                    projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                    projectionViews[eye].next = nullptr;
                    projectionViews[eye].pose = xrViews[eye].pose;
                    projectionViews[eye].fov = xrViews[eye].fov;
                    projectionViews[eye].subImage.swapchain = xrSwapchains[eye];
                    projectionViews[eye].subImage.imageRect.offset = { 0, 0 };
                    projectionViews[eye].subImage.imageRect.extent = {
                        (int32_t)xrConfigViews[eye].recommendedImageRectWidth,
                        (int32_t)xrConfigViews[eye].recommendedImageRectHeight
                    };
                    projectionViews[eye].subImage.imageArrayIndex = 0;
                }
            }
#endif
#endif // MULTIVIEW_ENABLED

            projectionLayer.space = xrLocalSpace;
            projectionLayer.viewCount = STDVR_EYE_COUNT;
            projectionLayer.views = projectionViews.data();
            layers.push_back((XrCompositionLayerBaseHeader*)&projectionLayer);
        }
    }

    XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime = xrFrameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = (uint32_t)layers.size();
    endInfo.layers = layers.data();

    XrResult result = xrEndFrame(xrSession, &endInfo);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: xrEndFrame failed with error %d\n", result);
        stateTrackingActive = false;  // Reset state tracking for next frame
        return 0;
    }

    // OPTIMIZATION: Reset state tracking at end of frame
    // Next frame will re-query GPU state once if needed
    stateTrackingActive = false;

    return 1;
}

static int vrEmptyFrameCount = 0;

extern "C" int stdVR_OpenXR_EndFrameEmpty(void)
{
    if (!xrSessionRunning || !xrFrameInProgress) {
        return 0;
    }

    xrFrameInProgress = false;
    vrEmptyFrameCount++;

    XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime = xrFrameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 0;
    endInfo.layers = nullptr;

    XrResult result = xrEndFrame(xrSession, &endInfo);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: xrEndFrame (empty) failed with error %d\n", result);
        stateTrackingActive = false;  // Reset state tracking for next frame
        return 0;
    }

    // OPTIMIZATION: Reset state tracking at end of frame
    stateTrackingActive = false;

    return 1;
}

extern "C" int stdVR_OpenXR_PrepareEyeBuffer(int eye)
{
    static int prepareEntryCount = 0;
    prepareEntryCount++;
#if STDVR_HOTPATH_DEBUG
    if (prepareEntryCount <= 20 || prepareEntryCount % 100 == 0) {
        stdPlatform_Printf("stdVR_OpenXR_PrepareEyeBuffer(%d) ENTRY #%d\n", eye, prepareEntryCount);
    }
#endif

    if (!xrSessionRunning || eye < 0 || eye >= STDVR_EYE_COUNT) {
        if (eye >= 0 && eye < STDVR_EYE_COUNT) {
            vrCurrentFBO[eye] = 0;
        }
#if STDVR_HOTPATH_DEBUG
        if (prepareEntryCount <= 20) {
            stdPlatform_Printf("stdVR_OpenXR_PrepareEyeBuffer(%d) early exit: running=%d\n", eye, xrSessionRunning);
        }
#endif
        return 0;
    }

    // Track which eye is being rendered (set early for debug logging)
    stdVR_currentEye = eye;

#if STDVR_HOTPATH_DEBUG
    if (prepareEntryCount <= 20) {
        stdPlatform_Printf("stdVR_OpenXR_PrepareEyeBuffer(%d): calling xrAcquireSwapchainImage\n", eye);
    }
#endif
    bool swapchainAcquired = false;
    XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrResult result = xrAcquireSwapchainImage(xrSwapchains[eye], &acquireInfo, &xrSwapchainImageIndex[eye]);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: xrAcquireSwapchainImage failed for eye %d: error %d\n", eye, result);
        vrCurrentFBO[eye] = 0;
        return 0;
    }
    swapchainAcquired = true;
#if STDVR_HOTPATH_DEBUG
    if (prepareEntryCount <= 20) {
        stdPlatform_Printf("stdVR_OpenXR_PrepareEyeBuffer(%d): xrAcquireSwapchainImage OK, calling WaitSwapchainImage\n", eye);
    }
#endif

    if (!stdVR_OpenXR_WaitSwapchainImage(xrSwapchains[eye], "main", eye)) {
        if (swapchainAcquired) {
            stdVR_OpenXR_ReleaseSwapchainImage(eye);
        }
#if STDVR_HOTPATH_DEBUG
        if (prepareEntryCount <= 20) {
            stdPlatform_Printf("stdVR_OpenXR_PrepareEyeBuffer(%d): WaitSwapchainImage FAILED\n", eye);
        }
#endif
        return 0;
    }
#if STDVR_HOTPATH_DEBUG
    if (prepareEntryCount <= 20) {
        stdPlatform_Printf("stdVR_OpenXR_PrepareEyeBuffer(%d): WaitSwapchainImage OK\n", eye);
    }
#endif

    // OPTIMIZATION: Track state manually instead of GPU queries (avoids pipeline stalls)
    // Only query once at start of VR frame, then track manually
    if (!stateTrackingActive) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &trackedFBO);
        glGetIntegerv(GL_VIEWPORT, trackedViewport);
        stateTrackingActive = true;
    }
    previousFBO = trackedFBO;
    memcpy(previousViewport, trackedViewport, sizeof(previousViewport));

    // Get the swapchain texture for this frame
    GLuint texture = xrSwapchainImages[eye][xrSwapchainImageIndex[eye]].image;
    uint32_t imageIdx = xrSwapchainImageIndex[eye];

    if (vrFBO[eye].empty() || vrDepthTex[eye].empty()) {
        VR_Log("stdVR_OpenXR: PrepareEyeBuffer(%d) no FBOs/depth textures allocated (images=%zu)\n",
            eye, vrFBO[eye].size());
        if (swapchainAcquired) {
            stdVR_OpenXR_ReleaseSwapchainImage(eye);
        }
        vrCurrentFBO[eye] = 0;
        return 0;
    }
    if (imageIdx >= vrFBO[eye].size()) {
        VR_Log("stdVR_OpenXR: PrepareEyeBuffer(%d) image index out of range: %u (images=%zu)\n",
            eye, imageIdx, vrFBO[eye].size());
        if (swapchainAcquired) {
            stdVR_OpenXR_ReleaseSwapchainImage(eye);
        }
        vrCurrentFBO[eye] = 0;
        return 0;
    }

    GLuint fbo = vrFBO[eye][imageIdx];
    vrCurrentFBO[eye] = fbo;

#if STDVR_HOTPATH_DEBUG
    // DEBUG: Log texture and FBO info including GL context
    static int prepareLogCount = 0;
    if (++prepareLogCount <= 30 || prepareLogCount % 300 == 0) {
        // Check current GL context
#ifdef __ANDROID__
        void* currentRC = eglGetCurrentContext();
        void* currentDC = eglGetCurrentDisplay();
#else
        void* currentRC = wglGetCurrentContext();
        void* currentDC = wglGetCurrentDC();
#endif
        VR_Log("stdVR_OpenXR: PrepareEyeBuffer(%d) idx=%u tex=%u fbo=%u swapchain=%llu RC=%p DC=%p\n",
               eye, imageIdx, texture, fbo, (unsigned long long)xrSwapchains[eye], currentRC, currentDC);
    }
#endif

    // Bind our VR FBO and attach the swapchain color texture
    // OPTIMIZATION: Depth is permanently attached, only need to swap color
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

#if STDVR_HOTPATH_DEBUG
    // DEBUG: Verify FBO is actually bound after attaching texture
    GLint boundFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &boundFbo);
    if (prepareLogCount <= 30 || prepareLogCount % 300 == 0) {
        VR_Log("stdVR_OpenXR:   After bind: boundFbo=%d (expected %u)\n", boundFbo, fbo);
    }
#endif

    // OPTIMIZATION: Only validate FBO on first use of each swapchain image
    // This avoids expensive glCheckFramebufferStatus every frame
    if (!vrFBOValidated[eye][imageIdx]) {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            VR_Log("stdVR_OpenXR: FBO incomplete for eye %d image %u, status 0x%x\n", eye, imageIdx, status);
            glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
            if (swapchainAcquired) {
                stdVR_OpenXR_ReleaseSwapchainImage(eye);
            }
            vrCurrentFBO[eye] = 0;
            return 0;
        }
        vrFBOValidated[eye][imageIdx] = true;
    }

    // Set viewport to VR render target size
    int width = xrConfigViews[eye].recommendedImageRectWidth;
    int height = xrConfigViews[eye].recommendedImageRectHeight;
    glViewport(0, 0, width, height);


    // Update tracked state
    trackedFBO = fbo;
    trackedViewport[0] = 0;
    trackedViewport[1] = 0;
    trackedViewport[2] = width;
    trackedViewport[3] = height;

    // Tell std3D to route all "window" FBO bindings to our VR FBO
    std3D_SetVRTargetFBO(fbo, width, height);

    // Altered: this cleared the eyes to red and blue to prove eye routing. A frame that
    // slipped through unrendered would then flash in the headset, so clear to black.
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

#if STDVR_HOTPATH_DEBUG
    // DEBUG: Read a pixel back to verify the clear worked
    if (prepareLogCount <= 10) {
        // Method 1: Read from currently bound FBO
        uint8_t pixel1[4] = {0};
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel1);

        // Method 2: Create a new FBO and read from the texture directly
        uint8_t pixel2[4] = {0};
        GLuint verifyFBO;
        glGenFramebuffers(1, &verifyFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, verifyFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel2);
        }
        glDeleteFramebuffers(1, &verifyFBO);

        // Rebind original FBO
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        VR_Log("stdVR_OpenXR:   After clear eye=%d: fbo_pixel=[%d,%d,%d,%d] tex_pixel=[%d,%d,%d,%d] tex=%u\n",
               eye, pixel1[0], pixel1[1], pixel1[2], pixel1[3],
               pixel2[0], pixel2[1], pixel2[2], pixel2[3], texture);
    }
#endif

    glDisable(GL_FRAMEBUFFER_SRGB);

    return 1;
}

extern "C" int stdVR_OpenXR_FinishEyeBuffer(int eye)
{
    if (!xrSessionRunning || eye < 0 || eye >= STDVR_EYE_COUNT) {
        return 0;
    }

    GLuint fbo = vrCurrentFBO[eye];
    if (fbo == 0 && !vrFBO[eye].empty()) {
        fbo = vrFBO[eye][xrSwapchainImageIndex[eye]];
    }

    // Clear alpha channel to 1.0 before releasing swapchain (like JKXR does)
    // Some OpenXR runtimes treat alpha=0 as "discard" which can cause missing content
    if (fbo != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);  // Only write alpha
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);  // Restore full color mask

#if STDVR_HOTPATH_DEBUG
        // DEBUG: Read right after alpha clear (FBO still has texture attached)
        static int postAlphaLogCount = 0;
        if (++postAlphaLogCount <= 10) {
            uint8_t pixel[4] = {0};
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            VR_Log("stdVR_OpenXR: PostAlpha eye=%d fbo=%u pixel=[%d,%d,%d,%d]\n",
                   eye, fbo, pixel[0], pixel[1], pixel[2], pixel[3]);
        }
#endif
    }

    // Capture the left-eye content for the desktop mirror while the swapchain image is still
    // acquired (per-eye / menu path). DESKTOP ONLY (see note in FinishMultiViewBuffer).
#if !defined(TARGET_ANDROID_NATIVE_GLES)
    if (eye == 0 && !xrSwapchainImages[eye].empty()) {
        stdVR_OpenXR_CaptureEyeMirror(xrSwapchainImages[eye][xrSwapchainImageIndex[eye]].image,
            false, 0,
            xrConfigViews[eye].recommendedImageRectWidth,
            xrConfigViews[eye].recommendedImageRectHeight);
    }
#endif

    // Restore std3D's window FBO routing
    std3D_ClearVRTargetFBO();

    // Detach color texture from FBO (required for some OpenXR runtimes to release the swapchain image)
    // Note: Depth remains attached permanently for performance
    if (fbo != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    }

    // Restore previous FBO and viewport, update tracked state
    glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    trackedFBO = previousFBO;
    memcpy(trackedViewport, previousViewport, sizeof(trackedViewport));

#if STDVR_HOTPATH_DEBUG
    // DEBUG: Read texture content right before release to verify it still has content
    static int preReleaseLogCount = 0;
    if (++preReleaseLogCount <= 10) {
        uint32_t imgIdx = xrSwapchainImageIndex[eye];
        if (imgIdx < xrSwapchainImages[eye].size()) {
            GLuint texture = xrSwapchainImages[eye][imgIdx].image;

            // Re-attach texture to read it
            GLuint verifyFBO;
            glGenFramebuffers(1, &verifyFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, verifyFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

            uint8_t pixel[4] = {0};
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &verifyFBO);

            VR_Log("stdVR_OpenXR: PreRelease eye=%d tex=%u pixel=[%d,%d,%d,%d]\n",
                   eye, texture, pixel[0], pixel[1], pixel[2], pixel[3]);
        }
    }
#endif

    // Release the swapchain image
    XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XrResult result = xrReleaseSwapchainImage(xrSwapchains[eye], &releaseInfo);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: xrReleaseSwapchainImage failed for eye %d: error %d\n", eye, result);
        return 0;
    }

    // Note: Don't reset stdVR_currentEye here - it needs to persist until the next PrepareEyeBuffer
    vrCurrentFBO[eye] = 0;

    return 1;
}

// ============================================================================
// MultiView Buffer Functions - Single-pass stereo rendering for Quest VR
// ============================================================================

#if defined(MULTIVIEW_ENABLED)
extern "C" int stdVR_OpenXR_IsMultiViewSupported(void)
{
    return vrMultiViewSupported && vrMultiViewEnabled ? 1 : 0;
}

extern "C" int stdVR_OpenXR_PrepareMultiViewBuffer(void)
{
    static int mvPrepareCount = 0;
    mvPrepareCount++;

    if (!xrSessionRunning || !vrMultiViewEnabled || xrMultiViewSwapchain == XR_NULL_HANDLE) {
        if (mvPrepareCount <= 10) {
            VR_Log("stdVR_OpenXR_PrepareMultiViewBuffer: early exit (running=%d, enabled=%d)\n",
                xrSessionRunning, vrMultiViewEnabled);
        }
        return 0;
    }

    // Acquire swapchain image (single image for both eyes)
    XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrResult result = xrAcquireSwapchainImage(xrMultiViewSwapchain, &acquireInfo, &xrMultiViewSwapchainImageIndex);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: xrAcquireSwapchainImage(MultiView) failed: error %d\n", result);
        return 0;
    }

    // Wait for the image to be available
    if (!stdVR_OpenXR_WaitSwapchainImage(xrMultiViewSwapchain, "MultiView", -1)) {
        XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage(xrMultiViewSwapchain, &releaseInfo);
        return 0;
    }

    // Initialize state tracking if needed
    if (!stateTrackingActive) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &trackedFBO);
        glGetIntegerv(GL_VIEWPORT, trackedViewport);
        stateTrackingActive = true;
    }
    previousFBO = trackedFBO;
    memcpy(previousViewport, trackedViewport, sizeof(previousViewport));

    // Validate swapchain image index
    if (xrMultiViewSwapchainImageIndex >= vrMultiViewFBOs.size()) {
        VR_Log("stdVR_OpenXR: MultiView image index out of range: %u\n", xrMultiViewSwapchainImageIndex);
        XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage(xrMultiViewSwapchain, &releaseInfo);
        return 0;
    }

    // Get the pre-configured FBO for this swapchain image (like RazeXR - no re-attaching)
    GLuint fbo = vrMultiViewFBOs[xrMultiViewSwapchainImageIndex];

    // Bind the FBO (use GL_DRAW_FRAMEBUFFER like RazeXR)
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);

    // Set viewport to the actual MultiView swapchain size (recommended * supersampling, clamped).
    int width = vrMultiViewWidth;
    int height = vrMultiViewHeight;
    glViewport(0, 0, width, height);


    // Update tracked state
    trackedFBO = fbo;
    trackedViewport[0] = 0;
    trackedViewport[1] = 0;
    trackedViewport[2] = width;
    trackedViewport[3] = height;

    // Tell std3D to route rendering to MultiView FBO
    std3D_SetVRTargetFBO(fbo, width, height);

    // Enable MultiView direct rendering mode (skip internal FBO)
    std3D_SetMultiViewActive(1);

    // Clear the buffer
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    vrMultiViewRenderActive = true;

    if (mvPrepareCount <= 10 || mvPrepareCount % 100 == 0) {
        VR_Log("stdVR_OpenXR_PrepareMultiViewBuffer #%d: fbo=%u %dx%d\n",
            mvPrepareCount, fbo, width, height);
    }

    return 1;
}

extern "C" int stdVR_OpenXR_FinishMultiViewBuffer(void)
{
    if (!vrMultiViewRenderActive) {
        return 0;
    }

    // Get current FBO
    GLuint fbo = vrMultiViewFBOs[xrMultiViewSwapchainImageIndex];

    // Capture the left-eye layer for the desktop mirror while the image is still acquired.
    // DESKTOP ONLY: on Android/Quest there is no desktop mirror window, and reading from the
    // multiview swapchain image here (glCopyTexSubImage2D) forces a tile resolve mid-finish that
    // leaves the right-eye layer incompletely rendered (large black regions).
#if !defined(TARGET_ANDROID_NATIVE_GLES)
    if (xrMultiViewSwapchainImageIndex < xrMultiViewSwapchainImages.size()) {
        stdVR_OpenXR_CaptureEyeMirror(xrMultiViewSwapchainImages[xrMultiViewSwapchainImageIndex].image,
            true, VR_MV_LAYER_OFFSET,
            vrMultiViewWidth,
            vrMultiViewHeight);
    }
#endif

    // Discard depth buffer so the tiler doesn't need to write it back (tiled-GPU
    // optimization). GLES3 core on Android; on desktop only present with GL 4.3+, so guard
    // on the (GLEW-loaded) function pointer to stay safe on a 3.3 context.
#if defined(TARGET_ANDROID_NATIVE_GLES)
    const GLenum depthAttachment[1] = {GL_DEPTH_ATTACHMENT};
    glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 1, depthAttachment);
#else
    if (glInvalidateFramebuffer) {
        const GLenum depthAttachment[1] = {GL_DEPTH_ATTACHMENT};
        glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 1, depthAttachment);
    }
#endif

    // Unbind FBO (like RazeXR - don't detach textures, they stay permanently attached)
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // Restore std3D's window FBO routing
    std3D_ClearVRTargetFBO();

    // Disable MultiView direct rendering mode
    std3D_ClearMultiViewActive();

    // Restore previous FBO and viewport
    glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    trackedFBO = previousFBO;
    memcpy(trackedViewport, previousViewport, sizeof(trackedViewport));

    // Release the swapchain image
    XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XrResult result = xrReleaseSwapchainImage(xrMultiViewSwapchain, &releaseInfo);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: xrReleaseSwapchainImage(MultiView) failed: error %d\n", result);
    }

    vrMultiViewRenderActive = false;
    return 1;
}

extern "C" int stdVR_OpenXR_GetMultiViewFBO(void)
{
    if (!vrMultiViewRenderActive || xrMultiViewSwapchainImageIndex >= vrMultiViewFBOs.size()) {
        return 0;
    }
    return (int)vrMultiViewFBOs[xrMultiViewSwapchainImageIndex];
}

// Desktop mirror: blit the last-rendered VR content to the SDL window's default framebuffer,
// followed by an SDL_GL_SwapWindow in the caller. Blits an owned 2D copy (vrMirrorEyeTex)
// captured during Finish*Buffer while the swapchain image was still acquired — NOT the live
// swapchain image, which intermittently hangs the GL driver if read after release. NOTE: the
// eye-buffer mirror shows only the 3D scene, not the HUD/menu quad layers (those are
// composited by the runtime).
extern "C" void stdVR_OpenXR_MirrorToWindow(int windowWidth, int windowHeight)
{
#if defined(MULTIVIEW_ENABLED)
    if (windowWidth <= 0 || windowHeight <= 0) return;

    GLuint srcColorTex = 0;
    GLint  srcLayer = 0;
    int    srcW = 0, srcH = 0;
    bool   srcIsArray = false;

    if (vrMirrorEyeValid && vrMirrorEyeTex != 0) {
        // Owned 2D copy of the last left-eye buffer (captured in Finish*Buffer). Works for
        // both the in-game multiview path and the menu per-eye path.
        srcColorTex = vrMirrorEyeTex;
        srcLayer = 0;
        srcW = vrMirrorEyeW;
        srcH = vrMirrorEyeH;
        srcIsArray = false;
    }

    if (srcColorTex == 0 || srcW <= 0 || srcH <= 0) return;

    if (vrMirrorFBO == 0) {
        glGenFramebuffers(1, &vrMirrorFBO);
    }

    // Bind the color texture as the read source. Array textures (multiview) need a layer
    // selected via glFramebufferTextureLayer; plain 2D textures use glFramebufferTexture2D.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, vrMirrorFBO);
    if (srcIsArray) {
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, srcColorTex, 0, srcLayer);
    } else {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcColorTex, 0);
    }

    // Blit to the window's default framebuffer, applying the selected flip (F9 cycles it).
    // Destination rectangle endpoints are swapped per-axis to mirror in X and/or Y.
    int dx0 = 0, dy0 = 0, dx1 = windowWidth, dy1 = windowHeight;
    if (stdVR_mirrorFlip & 2) { dx0 = windowWidth; dx1 = 0; }   // flip X
    if (stdVR_mirrorFlip & 1) { dy0 = windowHeight; dy1 = 0; }  // flip Y
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_FRAMEBUFFER_SRGB);  // avoid double sRGB encoding on the mirror
    glViewport(0, 0, windowWidth, windowHeight);
    glBlitFramebuffer(0, 0, srcW, srcH,
                      dx0, dy0, dx1, dy1,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#else
    (void)windowWidth; (void)windowHeight;
#endif
}

// Debug: dump the captured left-eye/projection buffer copy to vrtest_eye.ppm + opacity log,
// so we can tell whether the obscuring briefing is in the projection layer (eye buffer).
extern "C" void stdVR_OpenXR_DumpEyeMirror(void)
{
    if (!vrMirrorEyeValid || vrMirrorEyeTex == 0 || vrMirrorEyeW <= 0 || vrMirrorEyeH <= 0) {
        VR_Log("DumpEyeMirror: no valid eye mirror texture yet\n");
        return;
    }
    int w = vrMirrorEyeW, h = vrMirrorEyeH;
    if (vrMirrorFBO == 0) glGenFramebuffers(1, &vrMirrorFBO);
    GLint prevRead = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, vrMirrorFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vrMirrorEyeTex, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    uint8_t* pixels = (uint8_t*)malloc((size_t)w * h * 4);
    if (pixels) {
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        long total = (long)w * h, nonblack = 0;
        for (long i = 0; i < total; i++) {
            if (pixels[i*4] | pixels[i*4+1] | pixels[i*4+2]) nonblack++;
        }
        VR_Log("DumpEyeMirror: %dx%d nonblack=%ld (%.1f%%)\n", w, h, nonblack, 100.0*nonblack/total);
        FILE* fp = fopen("vrtest_eye.ppm", "wb");
        if (fp) {
            fprintf(fp, "P6\n%d %d\n255\n", w, h);
            for (int y = h - 1; y >= 0; y--) {
                for (int x = 0; x < w; x++) {
                    int idx = (y*w + x) * 4;
                    fputc(pixels[idx], fp);
                    fputc(pixels[idx+1], fp);
                    fputc(pixels[idx+2], fp);
                }
            }
            fclose(fp);
            VR_Log("DumpEyeMirror: wrote vrtest_eye.ppm\n");
        }
        free(pixels);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
}

// Get stereo eye offsets for MultiView rendering
// Returns the horizontal offset in clip space for each eye
extern "C" void stdVR_OpenXR_GetStereoOffsets(float* leftOffset, float* rightOffset)
{
    if (!leftOffset || !rightOffset) return;

    // Calculate IPD from eye positions
    // Left eye is typically at negative X, right eye at positive X
    float leftEyeX = xrViews[0].pose.position.x;
    float rightEyeX = xrViews[1].pose.position.x;
    float ipd = rightEyeX - leftEyeX;  // Inter-pupillary distance in meters

    // Get average horizontal FOV tangent for scale factor
    // fov.angleLeft is negative, fov.angleRight is positive
    float avgFovTan = (tanf(xrViews[0].fov.angleRight) - tanf(xrViews[0].fov.angleLeft)) * 0.5f;

    // Calculate offset in NDC
    // When camera moves left by IPD/2, objects shift right in the image
    // So for left eye (at -IPD/2), content needs positive X shift
    // For right eye (at +IPD/2), content needs negative X shift
    float offsetScale = 1.0f / avgFovTan;  // Convert meters to NDC at unit depth

    // Amplify the stereo effect (tune this value for comfort)
    float stereoStrength = 4.0f;

    *leftOffset = (ipd * 0.5f) * offsetScale * stereoStrength;   // Left eye: shift right
    *rightOffset = -(ipd * 0.5f) * offsetScale * stereoStrength; // Right eye: shift left

    static int logCount = 0;
    if (++logCount <= 10 || logCount % 600 == 0) {
        VR_Log("StereoOffsets: IPD=%.4f avgFovTan=%.4f leftOff=%.4f rightOff=%.4f\n",
            ipd, avgFovTan, *leftOffset, *rightOffset);
    }
}
#else
// Stubs for non-Quest platforms
extern "C" int stdVR_OpenXR_IsMultiViewSupported(void) { return 0; }
extern "C" int stdVR_OpenXR_PrepareMultiViewBuffer(void) { return 0; }
extern "C" int stdVR_OpenXR_FinishMultiViewBuffer(void) { return 0; }
extern "C" int stdVR_OpenXR_GetMultiViewFBO(void) { return 0; }
extern "C" void stdVR_OpenXR_GetStereoOffsets(float* leftOffset, float* rightOffset) { if (leftOffset) *leftOffset = 0; if (rightOffset) *rightOffset = 0; }
#endif

extern "C" int stdVR_OpenXR_GetCurrentEyeFBO(int eye)
{
    if (eye < 0 || eye >= STDVR_EYE_COUNT) {
        return 0;
    }
    return (int)vrCurrentFBO[eye];
}

extern "C" int stdVR_OpenXR_GetCurrentEye(void)
{
    return stdVR_currentEye;
}

// Helper to build asymmetric projection matrix from FOV tangents
static void BuildProjectionMatrix(float* out16, float tanLeft, float tanRight, float tanUp, float tanDown, float zNear, float zFar)
{
    // Column-major 4x4 perspective projection matrix
    // Uses OpenXR convention where tangents are already computed from angles
    float left = -tanLeft * zNear;
    float right = tanRight * zNear;
    float bottom = -tanDown * zNear;
    float top = tanUp * zNear;

    float width = right - left;
    float height = top - bottom;

    memset(out16, 0, 16 * sizeof(float));
    out16[0] = 2.0f * zNear / width;
    out16[5] = 2.0f * zNear / height;
    out16[8] = (right + left) / width;
    out16[9] = (top + bottom) / height;
    out16[10] = -(zFar + zNear) / (zFar - zNear);
    out16[11] = -1.0f;
    out16[14] = -2.0f * zFar * zNear / (zFar - zNear);
    out16[15] = 0.0f;
}

extern "C" void stdVR_OpenXR_UpdateTracking(void)
{
    if (!xrSessionRunning) {
        return;
    }

    // Locate views (HMD)
    XrViewLocateInfo viewLocateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewLocateInfo.displayTime = xrFrameState.predictedDisplayTime;
    viewLocateInfo.space = xrLocalSpace;

    XrViewState viewState = { XR_TYPE_VIEW_STATE };
    uint32_t viewCount = 0;
    XrResult result = xrLocateViews(xrSession, &viewLocateInfo, &viewState, STDVR_EYE_COUNT, &viewCount, xrViews);

    if (XR_FAILED(result) || viewCount < STDVR_EYE_COUNT) {
        return;
    }

    // Check tracking validity - only update if both position and orientation are valid
    bool positionValid = (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
    bool orientationValid = (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;

    if (!positionValid || !orientationValid) {
        // Tracking lost - keep last known pose, don't update
        static int trackingLostCount = 0;
        trackingLostCount++;
        if (trackingLostCount <= 5) {
            VR_Log("stdVR_OpenXR: Tracking invalid (pos=%d, ori=%d) - using last known pose\n",
                positionValid ? 1 : 0, orientationValid ? 1 : 0);
        }
        return;
    }

    // Update HMD pose (use center of both eyes)
    XrPosef centerPose;
    centerPose.position.x = (xrViews[0].pose.position.x + xrViews[1].pose.position.x) * 0.5f;
    centerPose.position.y = (xrViews[0].pose.position.y + xrViews[1].pose.position.y) * 0.5f;
    centerPose.position.z = (xrViews[0].pose.position.z + xrViews[1].pose.position.z) * 0.5f;
    centerPose.orientation = xrViews[0].pose.orientation; // Use left eye orientation for simplicity

#if STDVR_HOTPATH_DEBUG
    // Log raw HMD position before conversion
    static int hmdLogCounter = 0;
    hmdLogCounter++;
    if (hmdLogCounter % 60 == 1) {
        VR_Log("HMD OpenXR raw=(%.4f, %.4f, %.4f)\n",
            centerPose.position.x, centerPose.position.y, centerPose.position.z);
    }
#endif

    // Convert from OpenXR coords to JKDF2 coords:
    // OpenXR: X=right, Y=up, Z=back
    // JKDF2:  X=right, Y=forward, Z=up
    // JKDF2.x = OpenXR.x, JKDF2.y = -OpenXR.z, JKDF2.z = OpenXR.y
    stdVR_clientInfo.hmdPosition.x = centerPose.position.x;
    stdVR_clientInfo.hmdPosition.y = -centerPose.position.z;  // Forward = -back
    stdVR_clientInfo.hmdPosition.z = centerPose.position.y;   // Up = up

#if STDVR_HOTPATH_DEBUG
    if (hmdLogCounter % 60 == 1) {
        VR_Log("HMD JKDF2 pos=(%.4f, %.4f, %.4f)\n",
            stdVR_clientInfo.hmdPosition.x, stdVR_clientInfo.hmdPosition.y, stdVR_clientInfo.hmdPosition.z);
    }
#endif

    QuatToEuler(&centerPose.orientation, &stdVR_clientInfo.hmdOrientation);
    PoseToMatrix(&centerPose, &stdVR_clientInfo.hmdPoseMatrix);

    // Default near/far planes for projection matrix
    const float zNear = 0.1f;
    const float zFar = 1000.0f;

    // Update per-eye views
    for (int eye = 0; eye < STDVR_EYE_COUNT; eye++) {
        stdVR_EyeView* pEye = &stdVR_clientInfo.eyes[eye];

        // Store FOV tangents (OpenXR gives angles, we need tangents)
        // Note: angleLeft and angleDown are already negative in OpenXR
        pEye->fovLeft = std::tan(-xrViews[eye].fov.angleLeft);
        pEye->fovRight = std::tan(xrViews[eye].fov.angleRight);
        pEye->fovUp = std::tan(xrViews[eye].fov.angleUp);
        pEye->fovDown = std::tan(-xrViews[eye].fov.angleDown);

        // Build the full projection matrix
        BuildProjectionMatrix(pEye->projectionMatrix,
            pEye->fovLeft, pEye->fovRight, pEye->fovUp, pEye->fovDown,
            zNear, zFar);

        // Build eye view matrix directly from the per-eye pose (includes full transform)
        // This is the complete eye pose, not just an offset from center
        PoseToMatrix(&xrViews[eye].pose, &pEye->viewMatrix);
    }

    // Locate controllers with velocity tracking
    for (int hand = 0; hand < STDVR_CONTROLLER_COUNT; hand++) {
        // Chain velocity query to location query
        XrSpaceVelocity velocity = { XR_TYPE_SPACE_VELOCITY };
        XrSpaceLocation aim_location = { XR_TYPE_SPACE_LOCATION };
        XrSpaceLocation grip_location = { XR_TYPE_SPACE_LOCATION };
        aim_location.next = &velocity;  // Chain velocity struct
        grip_location.next = &velocity;  // Chain velocity struct

        if (xrAimControllerSpaces[hand] != XR_NULL_HANDLE && xrGripControllerSpaces[hand] != XR_NULL_HANDLE) {
            xrLocateSpace(xrAimControllerSpaces[hand], xrLocalSpace, xrFrameState.predictedDisplayTime, &aim_location);

            stdVR_ControllerState* pCtrl = &stdVR_clientInfo.controllers[hand];

            if (aim_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {
#if STDVR_HOTPATH_DEBUG
                // Log raw OpenXR position before conversion
                static int ctrlLogCounter = 0;
                ctrlLogCounter++;
                if (ctrlLogCounter % 60 == 1) {
                    VR_Log("Controller[%d] OpenXR raw=(%.4f, %.4f, %.4f)\n",
                        hand, aim_location.pose.position.x, aim_location.pose.position.y, aim_location.pose.position.z);
                }
#endif

                // Convert from OpenXR coords to JKDF2 coords:
                // OpenXR: X=right, Y=up, Z=back
                // JKDF2:  X=right, Y=forward, Z=up
                // JKDF2.x = OpenXR.x, JKDF2.y = -OpenXR.z, JKDF2.z = OpenXR.y
                pCtrl->position.x = aim_location.pose.position.x;
                pCtrl->position.y = -aim_location.pose.position.z;  // Forward = -back
                pCtrl->position.z = aim_location.pose.position.y;   // Up = up
                pCtrl->bTracking = 1;

#if STDVR_HOTPATH_DEBUG
                if (ctrlLogCounter % 60 == 1) {
                    VR_Log("Controller[%d] JKDF2 pos=(%.4f, %.4f, %.4f) tracking=%d\n",
                        hand, pCtrl->position.x, pCtrl->position.y, pCtrl->position.z, pCtrl->bTracking);
                }
#endif
            } else {
                pCtrl->bTracking = 0;
            }

            if (aim_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
                QuatToEuler(&aim_location.pose.orientation, &pCtrl->orientation);
            }

            // Store velocity data for motion controls
            if (velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) {
                // Convert from OpenXR coords to JKDF2 coords (same as pose conversion)
                pCtrl->motion.linearVelocity.x = velocity.linearVelocity.x;
                pCtrl->motion.linearVelocity.y = -velocity.linearVelocity.z;  // OpenXR Z -> JKDF2 -Y
                pCtrl->motion.linearVelocity.z = velocity.linearVelocity.y;   // OpenXR Y -> JKDF2 Z

                // Calculate swing speed (velocity magnitude)
                float vx = pCtrl->motion.linearVelocity.x;
                float vy = pCtrl->motion.linearVelocity.y;
                float vz = pCtrl->motion.linearVelocity.z;
                pCtrl->motion.swingSpeed = sqrtf(vx*vx + vy*vy + vz*vz);

                // Forward speed: component of linear velocity along the controller's pointing
                // direction, so a forward thrust/punch reads positive (and a backswing/retract
                // reads negative). Build the forward vector from the aim orientation (OpenXR
                // local -Z column of the rotation), converted to JKDF2 coords like the velocity.
                const XrQuaternionf* q = &aim_location.pose.orientation;
                float fwdOX = -2.0f * (q->x * q->z + q->w * q->y);
                float fwdOY = -2.0f * (q->y * q->z - q->w * q->x);
                float fwdOZ = -(1.0f - 2.0f * (q->x * q->x + q->y * q->y));
                // OpenXR -> JKDF2: x=x, y=-z, z=y
                float fwdJX = fwdOX;
                float fwdJY = -fwdOZ;
                float fwdJZ = fwdOY;
                pCtrl->motion.forwardSpeed = vx * fwdJX + vy * fwdJY + vz * fwdJZ;
            } else {
                pCtrl->motion.linearVelocity.x = 0.0f;
                pCtrl->motion.linearVelocity.y = 0.0f;
                pCtrl->motion.linearVelocity.z = 0.0f;
                pCtrl->motion.swingSpeed = 0.0f;
                pCtrl->motion.forwardSpeed = 0.0f;
            }

            if (velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) {
                pCtrl->motion.angularVelocity.x = velocity.angularVelocity.x;
                pCtrl->motion.angularVelocity.y = -velocity.angularVelocity.z;
                pCtrl->motion.angularVelocity.z = velocity.angularVelocity.y;
            } else {
                pCtrl->motion.angularVelocity.x = 0.0f;
                pCtrl->motion.angularVelocity.y = 0.0f;
                pCtrl->motion.angularVelocity.z = 0.0f;
            }

            xrLocateSpace(xrGripControllerSpaces[hand], xrLocalSpace, xrFrameState.predictedDisplayTime, &grip_location);
            if (grip_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
                // Use grip pose as the grip matrix too
                PoseToMatrix(&grip_location.pose, &pCtrl->gripPoseMatrix);
                PoseToMatrix(&grip_location.pose, &pCtrl->poseMatrix);
            }
        }
    }
}

extern "C" void stdVR_OpenXR_UpdateInput(void)
{
    if (!xrSessionRunning) {
        return;
    }

    stdVR_clientInfo.analogMove[0] = 0.0f;
    stdVR_clientInfo.analogMove[1] = 0.0f;
    stdVR_clientInfo.analogTurn[0] = 0.0f;
    stdVR_clientInfo.analogTurn[1] = 0.0f;
    stdVR_clientInfo.triggerLeft = 0.0f;
    stdVR_clientInfo.triggerRight = 0.0f;
    stdVR_clientInfo.gripLeft = 0.0f;
    stdVR_clientInfo.gripRight = 0.0f;

    // Sync actions
    XrActiveActionSet activeActionSet = {};
    activeActionSet.actionSet = xrActionSet;
    activeActionSet.subactionPath = XR_NULL_PATH;

    XrActionsSyncInfo syncInfo = { XR_TYPE_ACTIONS_SYNC_INFO };
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    xrSyncActions(xrSession, &syncInfo);

    // Log active interaction profile once for debugging Pico input issues
    {
        static int bProfileLogged = 0;
        if (!bProfileLogged) {
            bProfileLogged = 1;
            for (int h = 0; h < STDVR_CONTROLLER_COUNT; h++) {
                XrInteractionProfileState profileState = { XR_TYPE_INTERACTION_PROFILE_STATE };
                if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(xrSession, xrHandPaths[h], &profileState))
                    && profileState.interactionProfile != XR_NULL_PATH)
                {
                    char profileStr[256] = {0};
                    uint32_t len = 0;
                    xrPathToString(xrInstance, profileState.interactionProfile, sizeof(profileStr), &len, profileStr);
                    VR_Log("stdVR_OpenXR: Hand %d active profile: %s\n", h, profileStr);
                } else {
                    VR_Log("stdVR_OpenXR: Hand %d active profile: NONE\n", h);
                }
            }
        }
    }

    uint32_t prevButtonState = stdVR_clientInfo.buttonState;
    stdVR_clientInfo.buttonState = 0;

    // Get thumbstick values. Move is on the left stick and turn on the right by default, in
    // BOTH handedness modes (only the hand-relative actions follow dominantHand). The
    // "swap thumbsticks" option flips the two; stdVR_Input_GetMoveStickButton() keeps the
    // stick-click actions on the matching stick.
    int moveHand = stdVR_config.bSwapSticks ? STDVR_CONTROLLER_RIGHT : STDVR_CONTROLLER_LEFT;
    int turnHand = stdVR_config.bSwapSticks ? STDVR_CONTROLLER_LEFT : STDVR_CONTROLLER_RIGHT;
    for (int hand = 0; hand < STDVR_CONTROLLER_COUNT; hand++) {
        XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
        getInfo.subactionPath = xrHandPaths[hand];

        // Thumbstick
        getInfo.action = xrThumbstickAction;
        XrActionStateVector2f vec2State = { XR_TYPE_ACTION_STATE_VECTOR2F };
        xrGetActionStateVector2f(xrSession, &getInfo, &vec2State);
        if (vec2State.isActive) {
            // Added: keep each hand's own stick, unrouted, for consumers that need the
            // physical controller rather than the move/turn role. Stored in the PLAIN
            // convention - [0] horizontal, [1] vertical - measured on device: pushing the
            // stick sideways swings currentState.x to +-0.97. (analogMove/analogTurn below
            // keep their own historical axis handling; do not infer this one from those.)
            stdVR_clientInfo.controllers[hand].thumbstick[0] = vec2State.currentState.x;
            stdVR_clientInfo.controllers[hand].thumbstick[1] = vec2State.currentState.y;

            if (hand == moveHand) {
                // Map thumbstick axes to game movement
                // Based on user testing: X and Y axes are swapped
                // Stick left/right (X) -> forward/back
                // Stick up/down (Y) -> strafe left/right
                stdVR_clientInfo.analogMove[0] = vec2State.currentState.y;   // Y -> strafe
                stdVR_clientInfo.analogMove[1] = vec2State.currentState.x;   // X -> forward/back
            } else if (hand == turnHand) {
                stdVR_clientInfo.analogTurn[0] = vec2State.currentState.x;
                stdVR_clientInfo.analogTurn[1] = vec2State.currentState.y;
            }
        }

        // Trigger
        getInfo.action = xrTriggerAction;
        XrActionStateFloat floatState = { XR_TYPE_ACTION_STATE_FLOAT };
        xrGetActionStateFloat(xrSession, &getInfo, &floatState);
        if (floatState.isActive) {
            if (hand == STDVR_CONTROLLER_LEFT) {
                stdVR_clientInfo.triggerLeft = floatState.currentState;
                if (floatState.currentState > 0.5f) {
                    stdVR_clientInfo.buttonState |= STDVR_BTN_TRIGGER_L;
                }
            } else {
                stdVR_clientInfo.triggerRight = floatState.currentState;
                if (floatState.currentState > 0.5f) {
                    stdVR_clientInfo.buttonState |= STDVR_BTN_TRIGGER_R;
                }
            }
        }

        // Grip
        getInfo.action = xrGripAction;
        xrGetActionStateFloat(xrSession, &getInfo, &floatState);
        if (floatState.isActive) {
            if (hand == STDVR_CONTROLLER_LEFT) {
                stdVR_clientInfo.gripLeft = floatState.currentState;
                if (floatState.currentState > 0.5f) {
                    stdVR_clientInfo.buttonState |= STDVR_BTN_GRIP_L;
                }
            } else {
                stdVR_clientInfo.gripRight = floatState.currentState;
                if (floatState.currentState > 0.5f) {
                    stdVR_clientInfo.buttonState |= STDVR_BTN_GRIP_R;
                }
            }
        }
    }

    // Get button states per-hand (required for Pico compatibility —
    // querying with XR_NULL_PATH fails on Pico's stricter OpenXR runtime)
    for (int hand = 0; hand < STDVR_CONTROLLER_COUNT; hand++) {
        XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
        getInfo.subactionPath = xrHandPaths[hand];
        XrActionStateBoolean boolState = { XR_TYPE_ACTION_STATE_BOOLEAN };

        if (hand == STDVR_CONTROLLER_RIGHT) {
            // A and B are on the right controller
            getInfo.action = xrButtonAAction;
            boolState = { XR_TYPE_ACTION_STATE_BOOLEAN };
            xrGetActionStateBoolean(xrSession, &getInfo, &boolState);
            if (boolState.isActive && boolState.currentState) {
                stdVR_clientInfo.buttonState |= STDVR_BTN_A;
            }

            getInfo.action = xrButtonBAction;
            boolState = { XR_TYPE_ACTION_STATE_BOOLEAN };
            xrGetActionStateBoolean(xrSession, &getInfo, &boolState);
            if (boolState.isActive && boolState.currentState) {
                stdVR_clientInfo.buttonState |= STDVR_BTN_B;
            }

            getInfo.action = xrThumbstickClickRAction;
            boolState = { XR_TYPE_ACTION_STATE_BOOLEAN };
            xrGetActionStateBoolean(xrSession, &getInfo, &boolState);
            if (boolState.isActive && boolState.currentState) {
                stdVR_clientInfo.buttonState |= STDVR_BTN_THUMBSTICK_R;
            }
        } else {
            // X, Y, and Menu are on the left controller
            getInfo.action = xrButtonXAction;
            boolState = { XR_TYPE_ACTION_STATE_BOOLEAN };
            xrGetActionStateBoolean(xrSession, &getInfo, &boolState);
            if (boolState.isActive && boolState.currentState) {
                stdVR_clientInfo.buttonState |= STDVR_BTN_X;
            }

            getInfo.action = xrButtonYAction;
            boolState = { XR_TYPE_ACTION_STATE_BOOLEAN };
            xrGetActionStateBoolean(xrSession, &getInfo, &boolState);
            if (boolState.isActive && boolState.currentState) {
                stdVR_clientInfo.buttonState |= STDVR_BTN_Y;
            }

            getInfo.action = xrMenuAction;
            boolState = { XR_TYPE_ACTION_STATE_BOOLEAN };
            xrGetActionStateBoolean(xrSession, &getInfo, &boolState);
            if (boolState.isActive && boolState.currentState) {
                stdVR_clientInfo.buttonState |= STDVR_BTN_MENU;
            }

            getInfo.action = xrThumbstickClickLAction;
            boolState = { XR_TYPE_ACTION_STATE_BOOLEAN };
            xrGetActionStateBoolean(xrSession, &getInfo, &boolState);
            if (boolState.isActive && boolState.currentState) {
                stdVR_clientInfo.buttonState |= STDVR_BTN_THUMBSTICK_L;
            }
        }
    }

    // Compute pressed/released
    stdVR_clientInfo.buttonPressed = stdVR_clientInfo.buttonState & ~prevButtonState;
    stdVR_clientInfo.buttonReleased = prevButtonState & ~stdVR_clientInfo.buttonState;
}

// ============================================================================
// Display refresh rate (XR_FB_display_refresh_rate)
// ============================================================================

// Read the supported rates once, after the session exists. Leaves the list empty when the
// runtime does not have the extension, which is what hides the menu option.
void stdVR_OpenXR_InitRefreshRates(void)
{
    stdVR_clientInfo.numRefreshRates = 0;
    stdVR_clientInfo.currentRefreshRate = 0.0f;

    if (!stdVR_bHasRefreshRateExt || xrInstance == XR_NULL_HANDLE || xrSession == XR_NULL_HANDLE) {
        return;
    }

    xrGetInstanceProcAddr(xrInstance, "xrEnumerateDisplayRefreshRatesFB",
                          (PFN_xrVoidFunction*)&stdVR_pfnEnumerateDisplayRefreshRates);
    xrGetInstanceProcAddr(xrInstance, "xrGetDisplayRefreshRateFB",
                          (PFN_xrVoidFunction*)&stdVR_pfnGetDisplayRefreshRate);
    xrGetInstanceProcAddr(xrInstance, "xrRequestDisplayRefreshRateFB",
                          (PFN_xrVoidFunction*)&stdVR_pfnRequestDisplayRefreshRate);

    if (!stdVR_pfnEnumerateDisplayRefreshRates) {
        VR_Log("stdVR_OpenXR: refresh rate extension present but entry points missing\n");
        return;
    }

    uint32_t count = 0;
    if (XR_FAILED(stdVR_pfnEnumerateDisplayRefreshRates(xrSession, 0, &count, nullptr)) || count == 0) {
        return;
    }
    if (count > STDVR_MAX_REFRESH_RATES) {
        count = STDVR_MAX_REFRESH_RATES;
    }

    if (XR_FAILED(stdVR_pfnEnumerateDisplayRefreshRates(xrSession, count, &count,
                                                        stdVR_clientInfo.aRefreshRates))) {
        return;
    }
    stdVR_clientInfo.numRefreshRates = (int)count;

    if (stdVR_pfnGetDisplayRefreshRate) {
        float current = 0.0f;
        if (XR_SUCCEEDED(stdVR_pfnGetDisplayRefreshRate(xrSession, &current))) {
            stdVR_clientInfo.currentRefreshRate = current;
        }
    }

    for (int i = 0; i < stdVR_clientInfo.numRefreshRates; i++) {
        VR_Log("stdVR_OpenXR: refresh rate [%d] = %.1f Hz\n", i, stdVR_clientInfo.aRefreshRates[i]);
    }
    VR_Log("stdVR_OpenXR: current refresh rate = %.1f Hz\n", stdVR_clientInfo.currentRefreshRate);
}

// Ask the runtime for a rate. Returns 1 on success. The runtime can refuse, for example when
// the battery is low, so the caller must not assume the rate changed.
int stdVR_OpenXR_RequestRefreshRate(float hz)
{
    if (!stdVR_pfnRequestDisplayRefreshRate || xrSession == XR_NULL_HANDLE || hz <= 0.0f) {
        return 0;
    }

    XrResult result = stdVR_pfnRequestDisplayRefreshRate(xrSession, hz);
    if (XR_FAILED(result)) {
        VR_Log("stdVR_OpenXR: request %.1f Hz failed (%d)\n", hz, result);
        return 0;
    }

    stdVR_clientInfo.currentRefreshRate = hz;
    VR_Log("stdVR_OpenXR: refresh rate set to %.1f Hz\n", hz);
    return 1;
}

extern "C" void stdVR_OpenXR_TriggerHaptic(int hand, float amplitude, float duration, float frequency)
{
    if (!xrSessionRunning || hand < 0 || hand >= STDVR_CONTROLLER_COUNT) {
        return;
    }

    XrHapticActionInfo hapticInfo = { XR_TYPE_HAPTIC_ACTION_INFO };
    hapticInfo.action = xrHapticAction;
    hapticInfo.subactionPath = xrHandPaths[hand];

    XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
    vibration.amplitude = amplitude;
    vibration.duration = (int64_t)(duration * 1000000000.0f); // Convert to nanoseconds
    vibration.frequency = frequency;

    xrApplyHapticFeedback(xrSession, &hapticInfo, (XrHapticBaseHeader*)&vibration);
}

extern "C" void stdVR_OpenXR_StopHaptic(int hand)
{
    if (!xrSessionRunning || hand < 0 || hand >= STDVR_CONTROLLER_COUNT) {
        return;
    }

    XrHapticActionInfo hapticInfo = { XR_TYPE_HAPTIC_ACTION_INFO };
    hapticInfo.action = xrHapticAction;
    hapticInfo.subactionPath = xrHandPaths[hand];

    xrStopHapticFeedback(xrSession, &hapticInfo);
}

extern "C" void stdVR_OpenXR_RecenterView(void)
{
    // Altered: this used to recreate LOCAL with an identity offset, which the spec says gives
    // back the same space — so recentering did nothing at all. Put the head's current yaw and
    // ground position into poseInReferenceSpace instead.
    //
    // poseInReferenceSpace is measured against the runtime's own LOCAL space, not against the
    // space we are currently using, so the offset has to accumulate. Yaw-only rotations about
    // Y compose by adding the angles, which is why they are kept as a scalar.
    if (xrSession == XR_NULL_HANDLE || xrLocalSpace == XR_NULL_HANDLE || xrViewSpace == XR_NULL_HANDLE) {
        return;
    }

    XrSpaceLocation headLoc = { XR_TYPE_SPACE_LOCATION };
    if (XR_FAILED(xrLocateSpace(xrViewSpace, xrLocalSpace, xrFrameState.predictedDisplayTime, &headLoc))) {
        return;
    }

    const XrSpaceLocationFlags needed = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
                                      | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if ((headLoc.locationFlags & needed) != needed) {
        return; // Tracking is not up yet; leave the origin where it is
    }

    const XrQuaternionf& q = headLoc.pose.orientation;
    float fx = -2.0f * (q.x * q.z + q.w * q.y);
    float fz = 2.0f * (q.x * q.x + q.y * q.y) - 1.0f;
    float headYaw = std::atan2(-fx, -fz);

    float sinYaw = std::sin(xrRecenterYaw);
    float cosYaw = std::cos(xrRecenterYaw);
    xrRecenterPos.x += cosYaw * headLoc.pose.position.x + sinYaw * headLoc.pose.position.z;
    xrRecenterPos.z += -sinYaw * headLoc.pose.position.x + cosYaw * headLoc.pose.position.z;
    xrRecenterYaw += headYaw;

    XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.position = xrRecenterPos;
    spaceInfo.poseInReferenceSpace.orientation = QuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, xrRecenterYaw);

    XrSpace newSpace = XR_NULL_HANDLE;
    if (XR_FAILED(xrCreateReferenceSpace(xrSession, &spaceInfo, &newSpace))) {
        return; // Keep the old space rather than losing tracking entirely
    }

    xrDestroySpace(xrLocalSpace);
    xrLocalSpace = newSpace;

    VR_Log("stdVR_OpenXR: Recentered - yaw=%.1f deg, pos=(%.2f, %.2f)\n",
        xrRecenterYaw * 180.0f / 3.14159265f, xrRecenterPos.x, xrRecenterPos.z);
}

extern "C" const char* stdVR_OpenXR_GetRuntimeName(void)
{
    return xrRuntimeName;
}

extern "C" int stdVR_OpenXR_IsExitRequested(void)
{
    return xrExitRequested ? 1 : 0;
}

#endif // PLATFORM_VR

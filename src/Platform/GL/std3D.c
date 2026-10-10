#include "Platform/std3D.h"

#include "Raster/rdCache.h"
#include "Win95/stdDisplay.h"
#include "Win95/Window.h"
#include "World/sithWorld.h"
#include "Engine/rdColormap.h"
#include "Engine/rdCamera.h"
#include "Main/jkGame.h"
#include "World/jkPlayer.h"
#include "General/stdBitmap.h"
#include "General/stdMath.h"
#include "stdPlatform.h"

#include "jk.h"

// Added: VR support
#ifdef PLATFORM_VR
#include "Platform/VR/stdVR.h"
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "Platform/GL/shader_utils.h"
#include "Platform/GL/jkgm.h"

#include "SDL2_helper.h"

#ifdef WIN32
// Force Optimus/AMD to use non-integrated GPUs by default.
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

#define TEX_MODE_TEST 0
#define TEX_MODE_WORLDPAL 1
#define TEX_MODE_BILINEAR 2
#define TEX_MODE_16BPP 5
#define TEX_MODE_BILINEAR_16BPP 6

typedef struct std3DSimpleTexStage
{
    GLuint program;

    GLint attribute_coord3d;
    GLint attribute_v_color;
    GLint attribute_v_uv;
    GLint attribute_v_norm;

    GLint uniform_mvp;
    GLint uniform_tex;
    GLint uniform_tex2;
    GLint uniform_tex3;
    GLint uniform_iResolution;

    GLint uniform_param1;
    GLint uniform_param2;
    GLint uniform_param3;
} std3DSimpleTexStage;

typedef struct std3DIntermediateFbo
{
    GLuint fbo;
    GLuint tex;

    GLuint rbo;
    int32_t w;
    int32_t h;

    int32_t iw;
    int32_t ih;
} std3DIntermediateFbo;

typedef struct std3DFramebuffer
{
    GLuint fbo;
    GLuint tex0;
    GLuint tex1;
    GLuint tex2;
    GLuint tex3;

    std3DIntermediateFbo window;
    std3DIntermediateFbo main;

    int enable_extra;
    std3DIntermediateFbo blur1;
    std3DIntermediateFbo blur2;
    std3DIntermediateFbo blur3;
    std3DIntermediateFbo blur4;
    //std3DIntermediateFbo blurBlend;

    std3DIntermediateFbo ssaoBlur1;
    std3DIntermediateFbo ssaoBlur2;
    //std3DIntermediateFbo ssaoBlur3;

    GLuint rbo;
    int32_t w;
    int32_t h;
} std3DFramebuffer;

GLint std3D_windowFbo = 0;
std3DFramebuffer std3D_framebuffers[2];
std3DFramebuffer *std3D_pFb = NULL;

// Added: VR FBO override state
#ifdef PLATFORM_VR
static GLint std3D_savedWindowFbo = 0;
static int std3D_vrTargetActive = 0;
static int std3D_vrTargetSizeActive = 0;
static int std3D_vrTargetWidth = 0;
static int std3D_vrTargetHeight = 0;
static int std3D_multiViewActive = 0;  // Added: When true, skip internal FBO and render directly to MultiView FBO
int g_vrOpSequence = 0;  // Global sequence counter for debugging VR operations

static void std3D_ApplyWindowInfo(std3DFramebuffer* pFb, GLint fbo, int width, int height)
{
    if (!pFb) {
        return;
    }

    pFb->window.fbo = fbo;
    pFb->window.w = width;
    pFb->window.h = height;
    pFb->window.iw = width;
    pFb->window.ih = height;
}

static void std3D_ApplyVRWindowToAllFramebuffers(void)
{
    if (std3D_vrTargetWidth <= 0 || std3D_vrTargetHeight <= 0) {
        return;
    }

    std3D_ApplyWindowInfo(&std3D_framebuffers[0], std3D_windowFbo, std3D_vrTargetWidth, std3D_vrTargetHeight);
    std3D_ApplyWindowInfo(&std3D_framebuffers[1], std3D_windowFbo, std3D_vrTargetWidth, std3D_vrTargetHeight);
}

static void std3D_ApplyDesktopWindowToAllFramebuffers(void)
{
    std3D_ApplyWindowInfo(&std3D_framebuffers[0], std3D_windowFbo, Window_xSize, Window_ySize);
    std3D_ApplyWindowInfo(&std3D_framebuffers[1], std3D_windowFbo, Window_xSize, Window_ySize);
}
#endif

static bool has_initted = false;

static void* last_overlay = NULL;

static int std3D_activeFb = 1;

int init_once = 0;
GLuint programDefault, programMenu;
static GLuint programCrosshair = 0;
static GLint programCrosshair_attribute_coord3d = -1;
static GLint programCrosshair_attribute_v_color = -1;
static GLint programCrosshair_uniform_mvp = -1;
GLint attribute_coord3d, attribute_v_color, attribute_v_light, attribute_v_uv, attribute_v_norm;
GLint uniform_mvp, uniform_tex, uniform_texEmiss, uniform_displacement_map, uniform_tex_mode, uniform_blend_mode, uniform_worldPalette, uniform_worldPaletteLights;
GLint uniform_tint, uniform_filter, uniform_fade, uniform_add, uniform_emissiveFactor, uniform_albedoFactor;
GLint uniform_light_mult, uniform_displacement_factor, uniform_iResolution;
GLint uniform_vr_debug_mode; // VR debug mode uniform
#if defined(MULTIVIEW_ENABLED)
// Per-eye HUD horizontal shift in NATIVE-eye NDC, used ONLY by the baked-HUD shaders
// (menu/ui multiview). The HUD is authored directly in native-eye NDC (one draw -> both eye
// layers, each submitted with its own native FOV), so unlike the scene it needs NO combined->
// native scale (which would stretch the HUD's width and break its aspect). This single per-eye
// shift = forward-centering (-nativeCenter/nativeWidth, so the HUD sits at the binocular
// straight-ahead) + depth convergence (+/- IPD/(d*nativeWidth)). {eye0, eye1}.
float std3D_vrHudOffset[2] = { 0.0f, 0.0f };
#endif

GLint programMenu_attribute_coord3d, programMenu_attribute_v_color, programMenu_attribute_v_uv, programMenu_attribute_v_norm;
GLint programMenu_uniform_mvp, programMenu_uniform_tex, programMenu_uniform_displayPalette, programMenu_uniform_rgb_mode;

// Added: RGB video texture for full color video playback
uint32_t Video_videoRGBTexId = 0;
int std3D_bVideoRGBMode = 0;
uint32_t std3D_videoRGBWidth = 0;
uint32_t std3D_videoRGBHeight = 0;

std3DSimpleTexStage std3D_uiProgram;
#if defined(MULTIVIEW_ENABLED)
std3DSimpleTexStage std3D_uiProgramMV;  // MultiView UI program: bakes the HUD into both eye layers
static int std3D_uiProgramMVLoaded = 0;
// MultiView variant of programMenu: bakes the 2D HUD overlay into BOTH eye layers (the plain
// programMenu is single-view, so it would only reach one eye of the multiview array FBO).
GLuint programMenuMV;
static int programMenuMVLoaded = 0;
GLint programMenuMV_attribute_coord3d, programMenuMV_attribute_v_color, programMenuMV_attribute_v_uv;
GLint programMenuMV_uniform_mvp, programMenuMV_uniform_tex, programMenuMV_uniform_displayPalette;
#endif
std3DSimpleTexStage std3D_texFboStage;
std3DSimpleTexStage std3D_blurStage;
std3DSimpleTexStage std3D_ssaoStage;
std3DSimpleTexStage std3D_ssaoMixStage;

GLuint blank_tex, blank_tex_white;
void* blank_data = NULL, *blank_data_white = NULL;
GLuint worldpal_texture;
void* worldpal_data = NULL;
GLuint worldpal_lights_texture;
void* worldpal_lights_data = NULL;
GLuint displaypal_texture;
void* displaypal_data = NULL;
GLuint tiledrand_texture;
rdVector3* tiledrand_data = NULL;

size_t std3D_loadedUITexturesAmt = 0;
stdBitmap* std3D_aUIBitmaps[STD3D_MAX_TEXTURES] = {0};
GLuint std3D_aUITextures[STD3D_MAX_TEXTURES] = {0};
static rdUITri GL_tmpUITris[STD3D_MAX_UI_TRIS] = {0};
static size_t GL_tmpUITrisAmt = 0;
GLuint last_ui_tex = 0;
int last_ui_flags = 0;
static D3DVERTEX GL_tmpUIVertices[STD3D_MAX_UI_VERTICES] = {0};
static size_t GL_tmpUIVerticesAmt = 0;

rdDDrawSurface* std3D_aLoadedSurfaces[STD3D_MAX_TEXTURES] = {0};
GLuint std3D_aLoadedTextures[STD3D_MAX_TEXTURES] = {0};
size_t std3D_loadedTexturesAmt = 0;
static rdTri GL_tmpTris[STD3D_MAX_TRIS] = {0};
static size_t GL_tmpTrisAmt = 0;
static rdLine GL_tmpLines[STD3D_MAX_VERTICES] = {0};
static size_t GL_tmpLinesAmt = 0;
static D3DVERTEX GL_tmpVertices[STD3D_MAX_VERTICES] = {0};
static size_t GL_tmpVerticesAmt = 0;
static size_t rendered_tris = 0;

static void* loaded_colormap = NULL;

rdDDrawSurface* last_tex = NULL;
int last_flags = 0;

D3DVERTEX* world_data_all = NULL;
GLushort* world_data_elements = NULL;
GLuint world_vbo_all;
GLuint world_ibo_triangle;

D3DVERTEX* menu_data_all = NULL;
GLushort* menu_data_elements = NULL;
GLuint menu_vbo_all;
GLuint menu_ibo_triangle;

// Offscreen RGBA target for the VR HUD. The multi-textured HUD is rendered here SINGLE-VIEW (all
// its per-element texture switches happen on one layer, where Adreno multiview can't desync them),
// then composited into both eye layers as ONE single-texture multiview draw. See
// std3D_RenderVRHudViaTexture.
GLuint std3D_vrHudFbo = 0;
GLuint std3D_vrHudTex = 0;
int std3D_vrHudTexW = 0;
int std3D_vrHudTexH = 0;

// MultiView UBOs for single-pass stereo rendering (Quest VR)
#if defined(MULTIVIEW_ENABLED)
static GLuint std3D_viewMatricesUBO = 0;
static GLuint std3D_projMatricesUBO = 0;
static GLuint std3D_hudOffsetUBO = 0;   // Per-eye HUD NDC shift (vec4[2], binding point 2)
static int std3D_multiViewUBOInitted = 0;
#endif

extern int jkGuiBuildMulti_bRendering;

int std3D_bInitted = 0;
rdColormap std3D_ui_colormap;
int std3D_bReinitHudElements = 0;

void std3D_generateIntermediateFbo(int32_t width, int32_t height, std3DIntermediateFbo* pFbo, int isFloat)
{
    // Generate the framebuffer
    memset(pFbo, 0, sizeof(*pFbo));

    pFbo->w = width;
    pFbo->h = height;
    pFbo->iw = width;
    pFbo->ih = height;

    glActiveTexture(GL_TEXTURE0);

    glGenFramebuffers(1, &pFbo->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, pFbo->fbo);
    
    // Set up our framebuffer texture
    glGenTextures(1, &pFbo->tex);
    glBindTexture(GL_TEXTURE_2D, pFbo->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, isFloat ? GL_RGBA16F : GL_RGBA8, width, height, 0, GL_RGBA, isFloat ? GL_FLOAT : GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    glGenerateMipmap(GL_TEXTURE_2D);
    
    // Attach fbTex to our currently bound framebuffer fb
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pFbo->tex, 0);

    // Set up our render buffer
    glGenRenderbuffers(1, &pFbo->rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, pFbo->rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    
    // Bind it to our framebuffer fb
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, pFbo->rbo);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        stdPlatform_Printf("std3D: ERROR, Framebuffer is incomplete!\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void std3D_deleteIntermediateFbo(std3DIntermediateFbo* pFbo)
{
    glDeleteFramebuffers(1, &pFbo->fbo);
    glDeleteTextures(1, &pFbo->tex);
    glDeleteRenderbuffers(1, &pFbo->rbo);
}

void std3D_generateFramebuffer(int32_t width, int32_t height, std3DFramebuffer* pFb)
{
    // Generate the framebuffer
    memset(pFb, 0, sizeof(*pFb));

    pFb->w = width;
    pFb->h = height;

    glActiveTexture(GL_TEXTURE0);

    glGenFramebuffers(1, &pFb->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, pFb->fbo);
    
    // Set up our framebuffer texture
    glGenTextures(1, &pFb->tex0);
    glBindTexture(GL_TEXTURE_2D, pFb->tex0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Attach fbTex to our currently bound framebuffer fb
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pFb->tex0, 0);

    // Set up our emissive fb texture
    glGenTextures(1, &pFb->tex1);
    glBindTexture(GL_TEXTURE_2D, pFb->tex1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    glGenerateMipmap(GL_TEXTURE_2D);
    //glGenerateMipmap(GL_TEXTURE_2D);
    
    // Attach fbTex to our currently bound framebuffer fb
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, pFb->tex1, 0);

    // Set up our position fb texture
    glGenTextures(1, &pFb->tex2);
    glBindTexture(GL_TEXTURE_2D, pFb->tex2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Attach fbTex to our currently bound framebuffer fb
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, pFb->tex2, 0);

    // Set up our normal fb texture
    glGenTextures(1, &pFb->tex3);
    glBindTexture(GL_TEXTURE_2D, pFb->tex3);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Attach fbTex to our currently bound framebuffer fb
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, pFb->tex3, 0);

    // Set up our render buffer
    glGenRenderbuffers(1, &pFb->rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, pFb->rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    
    // Bind it to our framebuffer fb
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, pFb->rbo);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        stdPlatform_Printf("std3D: ERROR, Framebuffer is incomplete!\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (jkPlayer_enableSSAO)
    {
        std3D_generateIntermediateFbo(width, height, &pFb->ssaoBlur1, 0);
        std3D_generateIntermediateFbo(pFb->ssaoBlur1.w/2, pFb->ssaoBlur1.h/2, &pFb->ssaoBlur2, 0);
        //std3D_generateIntermediateFbo(pFb->ssaoBlur2.w/2, pFb->ssaoBlur2.h/2, &pFb->ssaoBlur3, 0);

        pFb->enable_extra |= 2;
    }

    if (jkPlayer_enableBloom)
    {
        pFb->enable_extra |= 1;
        std3D_generateIntermediateFbo(width, height, &pFb->blur1, 1);
        //std3D_generateIntermediateFbo(width, height, &pFb->blurBlend, 1);
        std3D_generateIntermediateFbo(pFb->blur1.w/4, pFb->blur1.h/4, &pFb->blur2, 1);
        std3D_generateIntermediateFbo(pFb->blur2.w/4, pFb->blur2.h/4, &pFb->blur3, 1);
        std3D_generateIntermediateFbo(pFb->blur3.w/4, pFb->blur3.h/4, &pFb->blur4, 1);

        /*pFb->blur1.iw = width;
        pFb->blur1.ih = height;
        pFb->blur2.iw = width;
        pFb->blur2.ih = height;
        pFb->blur3.iw = width;
        pFb->blur3.ih = height;
        pFb->blur4.iw = width;
        pFb->blur4.ih = height;*/
    }

    pFb->main.fbo = pFb->fbo;
    pFb->main.tex = pFb->tex1;
    pFb->main.rbo = pFb->rbo;
    pFb->main.w = pFb->w;
    pFb->main.h = pFb->h;
    pFb->main.iw = pFb->w;
    pFb->main.ih = pFb->h;

    pFb->window.fbo = std3D_windowFbo;
    pFb->window.w = Window_xSize;
    pFb->window.h = Window_ySize;
    pFb->window.iw = Window_xSize;
    pFb->window.ih = Window_ySize;
}

void std3D_deleteFramebuffer(std3DFramebuffer* pFb)
{
    glDeleteFramebuffers(1, &pFb->fbo);
    glDeleteTextures(1, &pFb->tex0);
    glDeleteTextures(1, &pFb->tex1);
    glDeleteTextures(1, &pFb->tex2);
    glDeleteTextures(1, &pFb->tex3);
    glDeleteRenderbuffers(1, &pFb->rbo);

    std3D_deleteIntermediateFbo(&pFb->blur1);
    std3D_deleteIntermediateFbo(&pFb->blur2);
    std3D_deleteIntermediateFbo(&pFb->blur3);
    std3D_deleteIntermediateFbo(&pFb->blur4);
    //std3D_deleteIntermediateFbo(&pFb->blurBlend);

    std3D_deleteIntermediateFbo(&pFb->ssaoBlur1);
    std3D_deleteIntermediateFbo(&pFb->ssaoBlur2);
    //std3D_deleteIntermediateFbo(&pFb->ssaoBlur3);
}

void std3D_swapFramebuffers()
{
    if (std3D_activeFb == 2)
    {
        std3D_activeFb = 1;
        std3D_pFb = &std3D_framebuffers[0];
    }
    else
    {
        std3D_activeFb = 2;
        std3D_pFb = &std3D_framebuffers[1];
    }

#ifdef PLATFORM_VR
    if (std3D_vrTargetActive) {
        std3D_ApplyVRWindowToAllFramebuffers();
    } else {
        std3D_ApplyDesktopWindowToAllFramebuffers();
    }
#endif
}

GLuint std3D_loadProgram(const char* fpath_base)
{
    GLuint out;
    GLint link_ok = GL_FALSE;
    
    char* tmp_vert = (char*)malloc(strlen(fpath_base) + 32);
    char* tmp_frag = (char*)malloc(strlen(fpath_base) + 32);
    
    strcpy(tmp_vert, fpath_base);
    strcat(tmp_vert, "_v.glsl");
    
    strcpy(tmp_frag, fpath_base);
    strcat(tmp_frag, "_f.glsl");
    
    GLuint vs, fs;
    if ((vs = load_shader_file(tmp_vert, GL_VERTEX_SHADER))   == 0) return 0;
    if ((fs = load_shader_file(tmp_frag, GL_FRAGMENT_SHADER)) == 0) return 0;
    
    free(tmp_vert);
    free(tmp_frag);
    
    out = glCreateProgram();
    glAttachShader(out, vs);
    glAttachShader(out, fs);
    glLinkProgram(out);
    glGetProgramiv(out, GL_LINK_STATUS, &link_ok);
    if (!link_ok)
    {
        print_log(out);
        return 0;
    }

    // Detach + delete the shader objects now that the program is linked (standard practice; frees
    // each program's shader objects instead of leaking them for the process lifetime).
    glDetachShader(out, vs);
    glDetachShader(out, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    return out;
}

GLint std3D_tryFindAttribute(GLuint program, const char* attribute_name)
{
    GLint out = glGetAttribLocation(program, attribute_name);
    if (out == -1) {
        stdPlatform_Printf("std3D: Could not bind attribute %s!\n", attribute_name);
    }
    return out;
}

GLint std3D_tryFindUniform(GLuint program, const char* uniform_name)
{
    GLint out = glGetUniformLocation(program, uniform_name);
    if (out == -1) {
        stdPlatform_Printf("std3D: Could not bind uniform %s!\n", uniform_name);
    }
    return out;
}

bool std3D_loadSimpleTexProgram(const char* fpath_base, std3DSimpleTexStage* pOut)
{
    if (!pOut) return false;
    if ((pOut->program = std3D_loadProgram(fpath_base)) == 0) return false;
    
    pOut->attribute_coord3d = std3D_tryFindAttribute(pOut->program, "coord3d");
    pOut->attribute_v_color = std3D_tryFindAttribute(pOut->program, "v_color");
    pOut->attribute_v_uv = std3D_tryFindAttribute(pOut->program, "v_uv");
    pOut->uniform_mvp = std3D_tryFindUniform(pOut->program, "mvp");
    pOut->uniform_iResolution = std3D_tryFindUniform(pOut->program, "iResolution");
    pOut->uniform_tex = std3D_tryFindUniform(pOut->program, "tex");
    pOut->uniform_tex2 = std3D_tryFindUniform(pOut->program, "tex2");
    pOut->uniform_tex3 = std3D_tryFindUniform(pOut->program, "tex3");

    pOut->uniform_param1 = std3D_tryFindUniform(pOut->program, "param1");
    pOut->uniform_param2 = std3D_tryFindUniform(pOut->program, "param2");
    pOut->uniform_param3 = std3D_tryFindUniform(pOut->program, "param3");

    return true;
}

int init_resources()
{
    stdPlatform_Printf("std3D: OpenGL init...\n");

    std3D_bReinitHudElements = 1;

    memset(std3D_aUITextures, 0, sizeof(std3D_aUITextures));

    stdPlatform_Printf("std3D: Getting current FBO binding...\n");
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &std3D_windowFbo);

    int32_t tex_w = Window_xSize;
    int32_t tex_h = Window_ySize;
    stdPlatform_Printf("std3D: Generating framebuffers %dx%d...\n", tex_w, tex_h);

    std3D_generateFramebuffer(tex_w, tex_h, &std3D_framebuffers[0]);
    stdPlatform_Printf("std3D: Framebuffer 0 done\n");
    std3D_generateFramebuffer(tex_w, tex_h, &std3D_framebuffers[1]);
    stdPlatform_Printf("std3D: Framebuffer 1 done\n");

    std3D_activeFb = 1;
    std3D_pFb = &std3D_framebuffers[0];

    // Initialize MultiView UBOs BEFORE loading shaders. std3D_BindMultiViewUBOs() is called
    // during shader loading below (crosshair, and the menu/ui MultiView variants) and calls
    // glBindBufferBase on these buffers — they must already exist, or it binds buffer 0 (null)
    // to the binding point. The scene programs survive a null bind because programDefault
    // rebinds points 0/1 every draw, but HudOffsets (point 2) is only bound here at load, so a
    // null bind left the per-eye HUD offset reading an unbound block -> undefined on Pico's
    // tiled GPU (HUD dropped out of one eye).
#if defined(MULTIVIEW_ENABLED)
    if (!std3D_multiViewUBOInitted) {
        // View matrices UBO (2 x mat4 = 128 bytes, std140 layout)
        glGenBuffers(1, &std3D_viewMatricesUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, std3D_viewMatricesUBO);
        glBufferData(GL_UNIFORM_BUFFER, 2 * 16 * sizeof(float), NULL, GL_DYNAMIC_DRAW);

        // Projection matrices UBO (2 x mat4 = 128 bytes, std140 layout)
        glGenBuffers(1, &std3D_projMatricesUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, std3D_projMatricesUBO);
        glBufferData(GL_UNIFORM_BUFFER, 2 * 16 * sizeof(float), NULL, GL_DYNAMIC_DRAW);

        // Per-eye HUD offset UBO (vec4[2] = 32 bytes, std140). Replaces a default-block
        // uniform array that crashed Pico's Adreno GLSL linker when indexed by gl_ViewID_OVR.
        glGenBuffers(1, &std3D_hudOffsetUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, std3D_hudOffsetUBO);
        glBufferData(GL_UNIFORM_BUFFER, 2 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        std3D_multiViewUBOInitted = 1;
        stdPlatform_Printf("std3D: MultiView UBOs created (view=%u, proj=%u, hud=%u)\n",
            std3D_viewMatricesUBO, std3D_projMatricesUBO, std3D_hudOffsetUBO);
    }
#endif

    stdPlatform_Printf("std3D: Loading shader 'default'...\n");
    if ((programDefault = std3D_loadProgram("shaders/default")) == 0) { stdPlatform_Printf("std3D: FAILED to load default shader\n"); return false; }
    stdPlatform_Printf("std3D: Loading shader 'menu'...\n");
    if ((programMenu = std3D_loadProgram("shaders/menu")) == 0) { stdPlatform_Printf("std3D: FAILED to load menu shader\n"); return false; }
    stdPlatform_Printf("std3D: Loading shader 'ui'...\n");
    if (!std3D_loadSimpleTexProgram("shaders/ui", &std3D_uiProgram)) { stdPlatform_Printf("std3D: FAILED to load ui shader\n"); return false; }
#if defined(MULTIVIEW_ENABLED)
    // Also load a MultiView variant of the UI program. The HUD is baked into the multiview
    // eye buffer (rather than a separate quad layer, which not all runtimes composite), so it
    // must be drawn with num_views=2 to reach both eye layers. The mono std3D_uiProgram is
    // still used for single-layer targets (menus).
    {
        extern int g_shaderForceMultiView;
        g_shaderForceMultiView = 1;
        std3D_uiProgramMVLoaded = std3D_loadSimpleTexProgram("shaders/ui", &std3D_uiProgramMV) ? 1 : 0;
        g_shaderForceMultiView = 0;
        if (!std3D_uiProgramMVLoaded) stdPlatform_Printf("std3D: WARNING - failed to load MultiView ui shader\n");
        else std3D_BindMultiViewUBOs(std3D_uiProgramMV.program);  // bind HudOffsets (point 2)

        // MultiView variant of the menu program for baking the 2D HUD overlay into both eyes.
        g_shaderForceMultiView = 1;
        programMenuMV = std3D_loadProgram("shaders/menu");
        g_shaderForceMultiView = 0;
        programMenuMVLoaded = (programMenuMV != 0) ? 1 : 0;
        if (!programMenuMVLoaded) {
            stdPlatform_Printf("std3D: WARNING - failed to load MultiView menu shader\n");
        } else {
            programMenuMV_attribute_coord3d     = std3D_tryFindAttribute(programMenuMV, "coord3d");
            programMenuMV_attribute_v_color     = std3D_tryFindAttribute(programMenuMV, "v_color");
            programMenuMV_attribute_v_uv        = std3D_tryFindAttribute(programMenuMV, "v_uv");
            programMenuMV_uniform_mvp           = std3D_tryFindUniform(programMenuMV, "mvp");
            programMenuMV_uniform_tex           = std3D_tryFindUniform(programMenuMV, "tex");
            programMenuMV_uniform_displayPalette = std3D_tryFindUniform(programMenuMV, "displayPalette");
            std3D_BindMultiViewUBOs(programMenuMV);  // bind HudOffsets (point 2); per-eye shift now a UBO
        }
    }
#endif
    stdPlatform_Printf("std3D: Loading shader 'crosshair'...\n");
    programCrosshair = std3D_loadProgram("shaders/crosshair");
    if (programCrosshair) {
        programCrosshair_attribute_coord3d = glGetAttribLocation(programCrosshair, "coord3d");
        programCrosshair_attribute_v_color = glGetAttribLocation(programCrosshair, "v_color");
        programCrosshair_uniform_mvp = glGetUniformLocation(programCrosshair, "mvp");
        // Bind MultiView UBOs for the crosshair shader
        std3D_BindMultiViewUBOs(programCrosshair);
    } else {
        stdPlatform_Printf("std3D: WARNING - crosshair shader not loaded\n");
    }
    stdPlatform_Printf("std3D: Loading shader 'texfbo'...\n");
    if (!std3D_loadSimpleTexProgram("shaders/texfbo", &std3D_texFboStage)) { stdPlatform_Printf("std3D: FAILED to load texfbo shader\n"); return false; }
    stdPlatform_Printf("std3D: Loading shader 'blur'...\n");
    if (!std3D_loadSimpleTexProgram("shaders/blur", &std3D_blurStage)) { stdPlatform_Printf("std3D: FAILED to load blur shader\n"); return false; }
    stdPlatform_Printf("std3D: Loading shader 'ssao'...\n");
    if (!std3D_loadSimpleTexProgram("shaders/ssao", &std3D_ssaoStage)) { stdPlatform_Printf("std3D: FAILED to load ssao shader\n"); return false; }
    stdPlatform_Printf("std3D: Loading shader 'ssao_mix'...\n");
    if (!std3D_loadSimpleTexProgram("shaders/ssao_mix", &std3D_ssaoMixStage)) { stdPlatform_Printf("std3D: FAILED to load ssao_mix shader\n"); return false; }
    stdPlatform_Printf("std3D: All shaders loaded!\n");

    // Attributes/uniforms
    attribute_coord3d = std3D_tryFindAttribute(programDefault, "coord3d");
    attribute_v_color = std3D_tryFindAttribute(programDefault, "v_color");
    attribute_v_light = std3D_tryFindAttribute(programDefault, "v_light");
    attribute_v_uv = std3D_tryFindAttribute(programDefault, "v_uv");
    uniform_mvp = std3D_tryFindUniform(programDefault, "mvp");
    uniform_tex = std3D_tryFindUniform(programDefault, "tex");
    uniform_texEmiss = std3D_tryFindUniform(programDefault, "texEmiss");
    uniform_worldPalette = std3D_tryFindUniform(programDefault, "worldPalette");
    uniform_worldPaletteLights = std3D_tryFindUniform(programDefault, "worldPaletteLights");
    uniform_displacement_map = std3D_tryFindUniform(programDefault, "displacement_map");
    uniform_tex_mode = std3D_tryFindUniform(programDefault, "tex_mode");
    uniform_blend_mode = std3D_tryFindUniform(programDefault, "blend_mode");
    uniform_tint = std3D_tryFindUniform(programDefault, "colorEffects_tint");
    uniform_filter = std3D_tryFindUniform(programDefault, "colorEffects_filter");
    uniform_fade = std3D_tryFindUniform(programDefault, "colorEffects_fade");
    uniform_add = std3D_tryFindUniform(programDefault, "colorEffects_add");
    uniform_emissiveFactor = std3D_tryFindUniform(programDefault, "emissiveFactor");
    uniform_albedoFactor = std3D_tryFindUniform(programDefault, "albedoFactor");
    uniform_light_mult = std3D_tryFindUniform(programDefault, "light_mult");
    uniform_displacement_factor = std3D_tryFindUniform(programDefault, "displacement_factor");
    uniform_iResolution = std3D_tryFindUniform(programDefault, "iResolution");
    uniform_vr_debug_mode = std3D_tryFindUniform(programDefault, "vr_debug_mode");

    programMenu_attribute_coord3d = std3D_tryFindAttribute(programMenu, "coord3d");
    programMenu_attribute_v_color = std3D_tryFindAttribute(programMenu, "v_color");
    programMenu_attribute_v_uv = std3D_tryFindAttribute(programMenu, "v_uv");
    programMenu_uniform_mvp = std3D_tryFindUniform(programMenu, "mvp");
    programMenu_uniform_tex = std3D_tryFindUniform(programMenu, "tex");
    programMenu_uniform_displayPalette = std3D_tryFindUniform(programMenu, "displayPalette");
    programMenu_uniform_rgb_mode = std3D_tryFindUniform(programMenu, "rgb_mode"); // Added: for full color video

    // Blank texture
    glGenTextures(1, &blank_tex);
    blank_data = jkgm_alloc_aligned(0x400);
    memset(blank_data, 0x0, 0x400);
    
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, blank_data);

    // Blank texture
    glGenTextures(1, &blank_tex_white);
    blank_data_white = jkgm_alloc_aligned(0x400);
    memset(blank_data_white, 0xFF, 0x400);
    
    glBindTexture(GL_TEXTURE_2D, blank_tex_white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, blank_data_white);

    // World palette
    glGenTextures(1, &worldpal_texture);
    worldpal_data = jkgm_alloc_aligned(0x300);
    memset(worldpal_data, 0xFF, 0x300);
    
    glBindTexture(GL_TEXTURE_2D, worldpal_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, worldpal_data);

    // World palette lights
    glGenTextures(1, &worldpal_lights_texture);
    worldpal_lights_data = jkgm_alloc_aligned(0x4000);
    memset(worldpal_lights_data, 0xFF, 0x4000);
    
    glBindTexture(GL_TEXTURE_2D, worldpal_lights_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 0x40, 0, GL_RED, GL_UNSIGNED_BYTE, worldpal_lights_data);
    
    
    // Display palette
    glGenTextures(1, &displaypal_texture);
    displaypal_data = jkgm_alloc_aligned(0x400);
    memset(displaypal_data, 0xFF, 0x300);
    
    glBindTexture(GL_TEXTURE_2D, displaypal_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, displaypal_data);

    // Tiled random
    // FLEXTODO
    glGenTextures(1, &tiledrand_texture);
    if (tiledrand_data) {
        free(tiledrand_data);
    }
    tiledrand_data = (rdVector3*)malloc(3 * 4 * 4 * sizeof(float));
    memset(tiledrand_data, 0, 3 * 4 * 4 * sizeof(float));

    for (int i = 0; i < 4*4; i++)
    {
        tiledrand_data[i].x = (_frand() * 2.0) - 1.0;
        tiledrand_data[i].y = (_frand() * 2.0) - 1.0;
        tiledrand_data[i].z = 0.0;
        rdVector_Normalize3Acc(&tiledrand_data[i]);
    }

    glBindTexture(GL_TEXTURE_2D, tiledrand_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

#if defined(TARGET_ANDROID_NATIVE_GLES)
    // GLES3 doesn't support GL_RGB32F in core, use RGBA32F with padded data
    float tiledrand_rgba[4 * 4 * 4];
    for (int i = 0; i < 4*4; i++) {
        tiledrand_rgba[i*4 + 0] = tiledrand_data[i].x;
        tiledrand_rgba[i*4 + 1] = tiledrand_data[i].y;
        tiledrand_rgba[i*4 + 2] = tiledrand_data[i].z;
        tiledrand_rgba[i*4 + 3] = 1.0f;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 4, 0, GL_RGBA, GL_FLOAT, tiledrand_rgba);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, 4, 4, 0, GL_RGB, GL_FLOAT, tiledrand_data);
#endif

    unsigned int vao;
    glGenVertexArrays( 1, &vao );
    glBindVertexArray( vao ); 

    world_data_all = (D3DVERTEX*)malloc(STD3D_MAX_VERTICES * sizeof(D3DVERTEX));
    world_data_elements = (GLushort*)malloc(sizeof(GLushort) * 3 * STD3D_MAX_TRIS);

    menu_data_all = (D3DVERTEX*)malloc(STD3D_MAX_UI_VERTICES * sizeof(D3DVERTEX));
    menu_data_elements = (GLushort*)malloc(sizeof(GLushort) * 3 * STD3D_MAX_UI_TRIS);

    glGenBuffers(1, &world_vbo_all);
    glGenBuffers(1, &world_ibo_triangle);

    glGenBuffers(1, &menu_vbo_all);
    glGenBuffers(1, &menu_ibo_triangle);

    has_initted = true;
    return true;
}

int std3D_Startup()
{
    if (std3D_bInitted) {
        return 1;
    }

#ifdef TARGET_CAN_JKGM
    jkgm_startup();
#endif

    memset(&std3D_ui_colormap, 0, sizeof(std3D_ui_colormap));
    rdColormap_LoadEntry("misc\\cmp\\UIColormap.cmp", &std3D_ui_colormap);

    std3D_bReinitHudElements = 1;

    std3D_bInitted = 1;
    return 1;
}

void std3D_Shutdown()
{
    if (!std3D_bInitted) {
        return;
    }

    std3D_bReinitHudElements = 0;

    rdColormap_FreeEntry(&std3D_ui_colormap);
    std3D_bInitted = 0;
}

void std3D_FreeResources()
{
    std3D_PurgeEntireTextureCache();

    glDeleteProgram(programDefault);
    glDeleteProgram(programMenu);
#if defined(MULTIVIEW_ENABLED)
    if (programMenuMV) { glDeleteProgram(programMenuMV); programMenuMV = 0; programMenuMVLoaded = 0; }
#endif
    std3D_deleteFramebuffer(&std3D_framebuffers[0]);
    std3D_deleteFramebuffer(&std3D_framebuffers[1]);
    glDeleteTextures(1, &blank_tex);
    glDeleteTextures(1, &blank_tex_white);
    glDeleteTextures(1, &worldpal_texture);
    glDeleteTextures(1, &worldpal_lights_texture);
    glDeleteTextures(1, &displaypal_texture);
    if (blank_data)
        jkgm_aligned_free(blank_data);
    if (blank_data_white)
        jkgm_aligned_free(blank_data_white);
    if (worldpal_data)
        jkgm_aligned_free(worldpal_data);
    if (worldpal_lights_data)
        jkgm_aligned_free(worldpal_lights_data);
    if (displaypal_data)
        jkgm_aligned_free(displaypal_data);

    blank_data = NULL;
    blank_data_white = NULL;
    worldpal_data = NULL;
    worldpal_lights_data = NULL;
    displaypal_data = NULL;

    if (world_data_all)
        free(world_data_all);
    world_data_all = NULL;

    if (world_data_elements)
        free(world_data_elements);
    world_data_elements = NULL;

    if (menu_data_all)
        free(menu_data_all);
    menu_data_all = NULL;

    if (menu_data_elements)
        free(menu_data_elements);
    menu_data_elements = NULL;

    loaded_colormap = NULL;

    glDeleteBuffers(1, &world_vbo_all);
    glDeleteBuffers(1, &world_ibo_triangle);

    glDeleteBuffers(1, &menu_vbo_all);

    // VR HUD render-to-texture target (recreated lazily by std3D_RenderVRHudViaTexture).
    if (std3D_vrHudTex) { glDeleteTextures(1, &std3D_vrHudTex); std3D_vrHudTex = 0; }
    if (std3D_vrHudFbo) { glDeleteFramebuffers(1, &std3D_vrHudFbo); std3D_vrHudFbo = 0; }
    std3D_vrHudTexW = 0;
    std3D_vrHudTexH = 0;

#if defined(MULTIVIEW_ENABLED)
    // Altered: the MultiView matrix UBOs belong to the GL context being torn down here.
    // On a context recreate (std3D_Shutdown/Startup via a video/resolution change) the old
    // buffer names become invalid, but init_resources() guards UBO creation on
    // std3D_multiViewUBOInitted and would otherwise SKIP recreating them — leaving stale IDs
    // that fail glBindBuffer with GL_INVALID_OPERATION, so the per-eye matrices never upload
    // and the VR world renders black. Delete them and clear the flag so they are recreated.
    glDeleteBuffers(1, &std3D_viewMatricesUBO);
    glDeleteBuffers(1, &std3D_projMatricesUBO);
    glDeleteBuffers(1, &std3D_hudOffsetUBO);
    std3D_viewMatricesUBO = 0;
    std3D_projMatricesUBO = 0;
    std3D_hudOffsetUBO = 0;
    std3D_multiViewUBOInitted = 0;
#endif

    std3D_bReinitHudElements = 1;

    has_initted = false;
}

int std3D_StartScene()
{
    if (Main_bHeadless) return 1;

    ++std3D_frameCount;

    //printf("Begin draw\n");
    if (!has_initted)
    {
        if (!init_resources()) {
            stdPlatform_Printf("std3D: Failed to init resources, exiting...");
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Failed to init resources, exiting...", NULL);
            exit(-1);
        }
    }
    
    rendered_tris = 0;
    
    std3D_swapFramebuffers();
    
    double supersample_level = jkPlayer_ssaaMultiple; // Can also be set lower
    int32_t tex_w = (int32_t)((double)Window_xSize * supersample_level);
    int32_t tex_h = (int32_t)((double)Window_ySize * supersample_level);

#ifdef PLATFORM_VR
    if ((std3D_vrTargetActive || std3D_vrTargetSizeActive) && stdVR_bEnabled && stdVR_IsSessionRunning()
        && std3D_vrTargetWidth > 0 && std3D_vrTargetHeight > 0)
    {
        double vr_supersample = stdVR_config.supersampling > 0.0 ? stdVR_config.supersampling : 1.0;
        // In MultiView mode (all VR gameplay) the VR target size already includes supersampling
        // (the swapchain is created at recommended*supersampling and the scene renders straight
        // into it), so don't apply the multiplier a second time here. The only other VR user of
        // std3D_pFb is the per-eye MENU/GUI path (multiViewActive=0, native target), where the
        // internal FBO is supersampled here and blitted down into the native per-eye swapchain.
        if (std3D_multiViewActive) vr_supersample = 1.0;
        supersample_level = vr_supersample;
        tex_w = (int32_t)((double)std3D_vrTargetWidth * supersample_level);
        tex_h = (int32_t)((double)std3D_vrTargetHeight * supersample_level);
    }
#endif

    if (tex_w != std3D_pFb->w || tex_h != std3D_pFb->h 
        || (!(std3D_pFb->enable_extra & 1) && jkPlayer_enableBloom)
        || (!(std3D_pFb->enable_extra & 2) && jkPlayer_enableSSAO))
    {
        std3D_deleteFramebuffer(std3D_pFb);
        std3D_generateFramebuffer(tex_w, tex_h, std3D_pFb);
    }

#ifdef PLATFORM_VR
    if (std3D_vrTargetActive) {
        std3D_ApplyVRWindowToAllFramebuffers();
    }
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->fbo);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glCullFace(GL_FRONT);
    //glClampColor(GL_CLAMP_FRAGMENT_COLOR, GL_FALSE);

    // Technically this should be from Clear2
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    
    if (jkGuiBuildMulti_bRendering && rdColormap_pCurMap && loaded_colormap != rdColormap_pCurMap)
    {
        glBindTexture(GL_TEXTURE_2D, worldpal_texture);
        memcpy(worldpal_data, rdColormap_pCurMap->colors, 0x300);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, worldpal_data);
    
        if (rdColormap_pCurMap->lightlevel)
        {
            glBindTexture(GL_TEXTURE_2D, worldpal_lights_texture);
            memcpy(worldpal_lights_data, rdColormap_pCurMap->lightlevel, 0x4000);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 0x40, GL_RED, GL_UNSIGNED_BYTE, worldpal_lights_data);
        }

        loaded_colormap = rdColormap_pCurMap;
    }
    else if (sithWorld_pCurrentWorld && sithWorld_pCurrentWorld->colormaps && loaded_colormap != sithWorld_pCurrentWorld->colormaps)
    {
        glBindTexture(GL_TEXTURE_2D, worldpal_texture);
        memcpy(worldpal_data, sithWorld_pCurrentWorld->colormaps->colors, 0x300);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, worldpal_data);
    
        if (sithWorld_pCurrentWorld->colormaps->lightlevel)
        {
            glBindTexture(GL_TEXTURE_2D, worldpal_lights_texture);
            memcpy(worldpal_lights_data, sithWorld_pCurrentWorld->colormaps->lightlevel, 0x4000);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 0x40, GL_RED, GL_UNSIGNED_BYTE, worldpal_lights_data);
        }

        loaded_colormap = sithWorld_pCurrentWorld->colormaps;
    }

    if (memcmp(displaypal_data, stdDisplay_masterPalette, 0x300))
    {
        glBindTexture(GL_TEXTURE_2D, displaypal_texture);
        memcpy(displaypal_data, stdDisplay_masterPalette, 0x300);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, displaypal_data);
    }

#if 0
    // New random values
    glBindTexture(GL_TEXTURE_2D, tiledrand_texture);
    for (int i = 0; i < 4*4; i++)
    {
        tiledrand_data[i].x = (_frand() * 2.0) - 1.0;
        tiledrand_data[i].y = (_frand() * 2.0) - 1.0;
        tiledrand_data[i].z = 0.0;
        rdVector_Normalize3Acc(&tiledrand_data[i]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGB, GL_FLOAT, tiledrand_data);
    }
#endif

    // Describe our vertices array to OpenGL (it can't guess its format automatically)
    glBindBuffer(GL_ARRAY_BUFFER, world_vbo_all);
    glBufferData(GL_ARRAY_BUFFER, 1 * sizeof(D3DVERTEX), GL_tmpVertices, GL_STREAM_DRAW);
    glVertexAttribPointer(
        attribute_coord3d, // attribute
        3,                 // number of elements per vertex, here (x,y,z)
        GL_FLOAT,          // the type of each element
        GL_FALSE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // data stride
        (GLvoid*)offsetof(D3DVERTEX, x)                  // offset of first element
    );
    
    glVertexAttribPointer(
        attribute_v_color, // attribute
        4,                 // number of elements per vertex, here (R,G,B,A)
        GL_UNSIGNED_BYTE,  // the type of each element
        GL_TRUE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, color) // offset of first element
    );

    glVertexAttribPointer(
        attribute_v_light, // attribute
        1,                 // number of elements per vertex, here (L)
        GL_FLOAT,  // the type of each element
        GL_FALSE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, lightLevel) // offset of first element
    );

    glVertexAttribPointer(
        attribute_v_uv,    // attribute
        2,                 // number of elements per vertex, here (U,V)
        GL_FLOAT,          // the type of each element
        GL_FALSE,          // take our values as-is
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, tu)                  // offset of first element
    );

    glEnableVertexAttribArray(attribute_coord3d);
    glEnableVertexAttribArray(attribute_v_color);
    glEnableVertexAttribArray(attribute_v_light);
    glEnableVertexAttribArray(attribute_v_uv);
    
    return 1;
}

int std3D_EndScene()
{
    if (Main_bHeadless) {
        last_tex = NULL;
        last_flags = 0;
        std3D_ResetRenderList();
        return 1;
    }

    glDisableVertexAttribArray(attribute_v_uv);
    glDisableVertexAttribArray(attribute_v_color);
    glDisableVertexAttribArray(attribute_coord3d);

    //printf("End draw\n");
    last_tex = NULL;
    last_flags = 0;
    std3D_ResetRenderList();
    //printf("%u tris\n", rendered_tris);
    return 1;
}

void std3D_ResetUIRenderList()
{
    rendered_tris += GL_tmpUITrisAmt;

    GL_tmpUIVerticesAmt = 0;
    GL_tmpUITrisAmt = 0;
    //GL_tmpLinesAmt = 0;
    
    //memset(GL_tmpTris, 0, sizeof(GL_tmpTris));
    //memset(GL_tmpVertices, 0, sizeof(GL_tmpVertices));
}

void std3D_ResetRenderList()
{
    rendered_tris += GL_tmpTrisAmt;

    GL_tmpVerticesAmt = 0;
    GL_tmpTrisAmt = 0;
    GL_tmpLinesAmt = 0;
    
    //memset(GL_tmpTris, 0, sizeof(GL_tmpTris));
    //memset(GL_tmpVertices, 0, sizeof(GL_tmpVertices));
}

int std3D_RenderListVerticesFinish()
{
    return 1;
}

void std3D_DrawMenuSubrect(flex_t x, flex_t y, flex_t w, flex_t h, flex_t dstX, flex_t dstY, flex_t scale)
{
    //double tex_w = (double)Window_xSize;
    //double tex_h = (double)Window_ySize;
    double tex_w = Video_menuBuffer.format.width;
    double tex_h = Video_menuBuffer.format.height;

    float w_dst = w;
    float h_dst = h;

    if (scale == 0.0)
    {
        w_dst = (w / tex_w) * (double)Window_xSize;
        h_dst = (h / tex_h) * (double)Window_ySize;

        dstX = (dstX / tex_w) * (double)Window_xSize;
        dstY = (dstY / tex_h) * (double)Window_ySize;

        scale = 1.0;
    }

    double u1 = (x / tex_w);
    double u2 = ((x+w) / tex_w);
    double v1 = (y / tex_h);
    double v2 = ((y+h) / tex_h);

    GL_tmpVertices[GL_tmpVerticesAmt+0].x = dstX;
    GL_tmpVertices[GL_tmpVerticesAmt+0].y = dstY;
    GL_tmpVertices[GL_tmpVerticesAmt+0].z = 0.0;
    GL_tmpVertices[GL_tmpVerticesAmt+0].tu = u1;
    GL_tmpVertices[GL_tmpVerticesAmt+0].tv = v1;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+0].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+0].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+0].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+1].x = dstX;
    GL_tmpVertices[GL_tmpVerticesAmt+1].y = dstY + (scale * h_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+1].z = 0.0;
    GL_tmpVertices[GL_tmpVerticesAmt+1].tu = u1;
    GL_tmpVertices[GL_tmpVerticesAmt+1].tv = v2;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+1].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+1].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+1].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+2].x = dstX + (scale * w_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+2].y = dstY + (scale * h_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+2].z = 0.0;
    GL_tmpVertices[GL_tmpVerticesAmt+2].tu = u2;
    GL_tmpVertices[GL_tmpVerticesAmt+2].tv = v2;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+2].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+2].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+2].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+3].x = dstX + (scale * w_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+3].y = dstY;
    GL_tmpVertices[GL_tmpVerticesAmt+3].z = 0.0;
    GL_tmpVertices[GL_tmpVerticesAmt+3].tu = u2;
    GL_tmpVertices[GL_tmpVerticesAmt+3].tv = v1;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+3].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+3].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+3].nz = 0;
    
    GL_tmpTris[GL_tmpTrisAmt+0].v1 = GL_tmpVerticesAmt+1;
    GL_tmpTris[GL_tmpTrisAmt+0].v2 = GL_tmpVerticesAmt+0;
    GL_tmpTris[GL_tmpTrisAmt+0].v3 = GL_tmpVerticesAmt+2;
    
    GL_tmpTris[GL_tmpTrisAmt+1].v1 = GL_tmpVerticesAmt+0;
    GL_tmpTris[GL_tmpTrisAmt+1].v2 = GL_tmpVerticesAmt+3;
    GL_tmpTris[GL_tmpTrisAmt+1].v3 = GL_tmpVerticesAmt+2;
    
    GL_tmpVerticesAmt += 4;
    GL_tmpTrisAmt += 2;
}

void std3D_DrawMenuSubrect2(flex_t x, flex_t y, flex_t w, flex_t h, flex_t dstX, flex_t dstY, flex_t scale)
{
    //double tex_w = (double)Window_xSize;
    //double tex_h = (double)Window_ySize;
    double tex_w = Video_menuBuffer.format.width;
    double tex_h = Video_menuBuffer.format.height;

    float w_dst = w;
    float h_dst = h;

    if (scale == 0.0)
    {
        w_dst = (w / tex_w) * (double)Window_xSize;
        h_dst = (h / tex_h) * (double)Window_ySize;

        dstX = (dstX / tex_w) * (double)Window_xSize;
        dstY = (dstY / tex_h) * (double)Window_ySize;

        scale = 1.0;
    }

    double u1 = (x / tex_w);
    double u2 = ((x+w) / tex_w);
    double v1 = (y / tex_h);
    double v2 = ((y+h) / tex_h);

    GL_tmpVertices[GL_tmpVerticesAmt+0].x = dstX;
    GL_tmpVertices[GL_tmpVerticesAmt+0].y = dstY;
    GL_tmpVertices[GL_tmpVerticesAmt+0].z = 0.0;
    GL_tmpVertices[GL_tmpVerticesAmt+0].tu = u1;
    GL_tmpVertices[GL_tmpVerticesAmt+0].tv = v1;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+0].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+0].color = 0x000000FF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+0].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+1].x = dstX;
    GL_tmpVertices[GL_tmpVerticesAmt+1].y = dstY + (scale * h_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+1].z = 0.0;
    GL_tmpVertices[GL_tmpVerticesAmt+1].tu = u1;
    GL_tmpVertices[GL_tmpVerticesAmt+1].tv = v2;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+1].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+1].color = 0x000000FF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+1].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+2].x = dstX + (scale * w_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+2].y = dstY + (scale * h_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+2].z = 0.0;
    GL_tmpVertices[GL_tmpVerticesAmt+2].tu = u2;
    GL_tmpVertices[GL_tmpVerticesAmt+2].tv = v2;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+2].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+2].color = 0x000000FF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+2].nz = 0;
    
    GL_tmpVertices[GL_tmpVerticesAmt+3].x = dstX + (scale * w_dst);
    GL_tmpVertices[GL_tmpVerticesAmt+3].y = dstY;
    GL_tmpVertices[GL_tmpVerticesAmt+3].z = 0.0;
    GL_tmpVertices[GL_tmpVerticesAmt+3].tu = u2;
    GL_tmpVertices[GL_tmpVerticesAmt+3].tv = v1;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+3].nx = 0;
    GL_tmpVertices[GL_tmpVerticesAmt+3].color = 0x000000FF;
    *(uint32_t*)&GL_tmpVertices[GL_tmpVerticesAmt+3].nz = 0;
    
    GL_tmpTris[GL_tmpTrisAmt+0].v1 = GL_tmpVerticesAmt+1;
    GL_tmpTris[GL_tmpTrisAmt+0].v2 = GL_tmpVerticesAmt+0;
    GL_tmpTris[GL_tmpTrisAmt+0].v3 = GL_tmpVerticesAmt+2;
    
    GL_tmpTris[GL_tmpTrisAmt+1].v1 = GL_tmpVerticesAmt+0;
    GL_tmpTris[GL_tmpTrisAmt+1].v2 = GL_tmpVerticesAmt+3;
    GL_tmpTris[GL_tmpTrisAmt+1].v3 = GL_tmpVerticesAmt+2;
    
    GL_tmpVerticesAmt += 4;
    GL_tmpTrisAmt += 2;
}

static rdDDrawSurface* test_idk = NULL;
void std3D_DrawSimpleTex(std3DSimpleTexStage* pStage, std3DIntermediateFbo* pFbo, GLuint texId, GLuint texId2, GLuint texId3, flex_t param1, flex_t param2, flex_t param3, int gen_mips);
void std3D_DrawMapOverlay();
void std3D_DrawUIRenderList();

void std3D_DrawMenu()
{
    if (Main_bHeadless) return;

    //printf("Draw menu\n");
#ifdef PLATFORM_VR
    // In VR menu mode, skip std3D_DrawSceneFbo() - we only need the 2D menu texture.
    // std3D_DrawSceneFbo() would potentially return early for non-DDraw mode.
    // But we DO need to clear the buffer to black for the menu background.
    if (!std3D_vrTargetActive || jkGame_isDDraw) {
        std3D_DrawSceneFbo();
    } else {
        // VR menu mode: clear to black before drawing menu texture
        glBindFramebuffer(GL_FRAMEBUFFER, std3D_windowFbo);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
#else
    std3D_DrawSceneFbo();
#endif
    //glFlush();

    glBindFramebuffer(GL_FRAMEBUFFER, std3D_windowFbo);
    glDepthMask(GL_TRUE);
    glCullFace(GL_FRONT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_ALWAYS);
    glUseProgram(programMenu);

    // Get the actual render target dimensions
    // In VR mode, use VR target size; otherwise use window size
    int targetWidth = Window_xSize;
    int targetHeight = Window_ySize;
#ifdef PLATFORM_VR
    if (std3D_vrTargetActive && std3D_vrTargetWidth > 0 && std3D_vrTargetHeight > 0) {
        targetWidth = std3D_vrTargetWidth;
        targetHeight = std3D_vrTargetHeight;
    }
#endif

    float menu_w, menu_h, menu_u, menu_v, menu_x, menu_y;
    menu_w = (double)targetWidth;
    menu_h = (double)targetHeight;
    menu_u = 1.0;
    menu_v = 1.0;
    menu_x = 0.0;
    menu_y = 0.0;

    int bFixHudScale = 0;

    double fake_windowW = (double)Window_xSize;
    double fake_windowH = (double)Window_ySize;

#ifdef PLATFORM_VR
    // In VR mode, use VR target dimensions for cutscene scaling calculations
    if (std3D_vrTargetActive && std3D_vrTargetWidth > 0 && std3D_vrTargetHeight > 0) {
        fake_windowW = (double)targetWidth;
        fake_windowH = (double)targetHeight;
    }
#endif

    if (!jkGame_isDDraw && !jkGuiBuildMulti_bRendering && !jkCutscene_isRendering)
    {
        //menu_w = 640.0;
        //menu_h = 480.0;

        // Stretch screen
        menu_u = (1.0 / Video_menuBuffer.format.width) * 640.0;
        menu_v = (1.0 / Video_menuBuffer.format.height) * 480.0;

        // Keep 4:3 aspect - fit within target while maintaining aspect ratio
        // Calculate what 4:3 dimensions would be if constrained by height vs width
        float aspectRatio = 640.0 / 480.0;  // 1.333
        float menu4to3Width = menu_h * aspectRatio;
        float menu4to3Height = menu_w / aspectRatio;

        if (menu4to3Width <= menu_w) {
            // Target is wider than 4:3 - fit by height, pillarbox sides
            menu_w = menu4to3Width;
            menu_x = (targetWidth - menu_w) / 2.0;
            // menu_y stays 0
        } else {
            // Target is taller than 4:3 - fit by width, letterbox top/bottom
            menu_h = menu4to3Height;
            menu_x = 0.0;
            menu_y = (targetHeight - menu_h) / 2.0;
        }
    }
    else if (jkCutscene_isRendering) {
        // Added: For RGB video mode, fill the window with proper aspect ratio
        if (std3D_bVideoRGBMode && Video_videoRGBTexId && std3D_videoRGBWidth > 0 && std3D_videoRGBHeight > 0) {
            bFixHudScale = 0;  // Use simple quad rendering

            float videoW = (float)std3D_videoRGBWidth;
            float videoH = (float)std3D_videoRGBHeight;
            float videoAspect = videoW / videoH;
            float windowAspect = (float)targetWidth / (float)targetHeight;

            // Fit video to window maintaining aspect ratio
            if (videoAspect > windowAspect) {
                // Video is wider than window - fit by width, letterbox top/bottom
                menu_w = (float)targetWidth;
                menu_h = menu_w / videoAspect;
                menu_x = 0.0f;
                menu_y = ((float)targetHeight - menu_h) / 2.0f;
            } else {
                // Video is taller than window - fit by height, pillarbox sides
                menu_h = (float)targetHeight;
                menu_w = menu_h * videoAspect;
                menu_x = ((float)targetWidth - menu_w) / 2.0f;
                menu_y = 0.0f;
            }
            // Full UV range for RGB texture
            menu_u = 1.0f;
            menu_v = 1.0f;
        } else {
            // Original indexed palette mode
            bFixHudScale = 1;

            menu_w = Video_menuBuffer.format.width;
            menu_h = Video_menuBuffer.format.height;

            // For ultrawide screens, limit the width to 16:9
            if (Window_xSize > Window_ySize && ((double)Window_xSize / (double)Window_ySize) > (Main_bMotsCompat ? (16.0/9.0) : (21.0/9.0))) {
                fake_windowW = fake_windowH * (16.0/9.0);
            }

            // Keep 4:3 aspect
            menu_x = (menu_w - (menu_h * (640.0 / 480.0))) / 2.0;
        }
    }
    else if (jkGuiBuildMulti_bRendering)
    {
        bFixHudScale = 1;

        // Stretch screen
        menu_u = (1.0 / Video_menuBuffer.format.width) * 640.0;
        menu_v = (1.0 / Video_menuBuffer.format.height) * 480.0;

        // Keep 4:3 aspect - fit within target while maintaining aspect ratio
        float aspectRatio = 640.0 / 480.0;  // 1.333
        float menu4to3Width = menu_h * aspectRatio;
        float menu4to3Height = menu_w / aspectRatio;

        if (menu4to3Width <= menu_w) {
            // Target is wider than 4:3 - fit by height, pillarbox sides
            menu_w = menu4to3Width;
            menu_x = (targetWidth - menu_w) / 2.0;
            // menu_y stays 0
        } else {
            // Target is taller than 4:3 - fit by width, letterbox top/bottom
            menu_h = menu4to3Height;
            menu_x = 0.0;
            menu_y = (targetHeight - menu_h) / 2.0;
        }
    }
    else
    {
        bFixHudScale = 0;

        // Added: For RGB video mode, use video dimensions with proper aspect ratio
        if (std3D_bVideoRGBMode && Video_videoRGBTexId && std3D_videoRGBWidth > 0 && std3D_videoRGBHeight > 0) {
            // Video dimensions
            float videoW = (float)std3D_videoRGBWidth;
            float videoH = (float)std3D_videoRGBHeight;
            float videoAspect = videoW / videoH;
            float targetAspect = (float)targetWidth / (float)targetHeight;

            // Fit video to screen maintaining aspect ratio
            if (videoAspect > targetAspect) {
                // Video is wider - fit by width
                menu_w = (float)targetWidth;
                menu_h = menu_w / videoAspect;
                menu_x = 0.0f;
                menu_y = ((float)targetHeight - menu_h) / 2.0f;
            } else {
                // Video is taller - fit by height
                menu_h = (float)targetHeight;
                menu_w = menu_h * videoAspect;
                menu_x = ((float)targetWidth - menu_w) / 2.0f;
                menu_y = 0.0f;
            }
            // RGB texture uses full UV range
            menu_u = 1.0f;
            menu_v = 1.0f;
        } else {
            menu_w = Video_menuBuffer.format.width;
            menu_h = Video_menuBuffer.format.height;
        }
    }

    if (!bFixHudScale)
    {
        GL_tmpVertices[0].x = menu_x;
        GL_tmpVertices[0].y = menu_y;
        GL_tmpVertices[0].z = 0.0;
        GL_tmpVertices[0].tu = 0.0;
        GL_tmpVertices[0].tv = 0.0;
        *(uint32_t*)&GL_tmpVertices[0].nx = 0;
        GL_tmpVertices[0].color = 0xFFFFFFFF;
        *(uint32_t*)&GL_tmpVertices[0].nz = 0;

        GL_tmpVertices[1].x = menu_x;
        GL_tmpVertices[1].y = menu_y + menu_h;
        GL_tmpVertices[1].z = 0.0;
        GL_tmpVertices[1].tu = 0.0;
        GL_tmpVertices[1].tv = menu_v;
        *(uint32_t*)&GL_tmpVertices[1].nx = 0;
        GL_tmpVertices[1].color = 0xFFFFFFFF;
        *(uint32_t*)&GL_tmpVertices[1].nz = 0;

        GL_tmpVertices[2].x = menu_x + menu_w;
        GL_tmpVertices[2].y = menu_y + menu_h;
        GL_tmpVertices[2].z = 0.0;
        GL_tmpVertices[2].tu = menu_u;
        GL_tmpVertices[2].tv = menu_v;
        *(uint32_t*)&GL_tmpVertices[2].nx = 0;
        GL_tmpVertices[2].color = 0xFFFFFFFF;
        *(uint32_t*)&GL_tmpVertices[2].nz = 0;

        GL_tmpVertices[3].x = menu_x + menu_w;
        GL_tmpVertices[3].y = menu_y;
        GL_tmpVertices[3].z = 0.0;
        GL_tmpVertices[3].tu = menu_u;
        GL_tmpVertices[3].tv = 0.0;
        *(uint32_t*)&GL_tmpVertices[3].nx = 0;
        GL_tmpVertices[3].color = 0xFFFFFFFF;
        *(uint32_t*)&GL_tmpVertices[3].nz = 0;
        
        GL_tmpTris[0].v1 = 1;
        GL_tmpTris[0].v2 = 0;
        GL_tmpTris[0].v3 = 2;
        
        GL_tmpTris[1].v1 = 0;
        GL_tmpTris[1].v2 = 3;
        GL_tmpTris[1].v3 = 2;
        
        GL_tmpVerticesAmt = 4;
        GL_tmpTrisAmt = 2;
    }
    else if (jkGuiBuildMulti_bRendering)
    {
        GL_tmpVerticesAmt = 0;
        GL_tmpTrisAmt = 0;

        // Main View
        std3D_DrawMenuSubrect(0, 0, 640, 480, menu_x, menu_y, menu_w/640.0);
    }
    else if (jkCutscene_isRendering)
    {
        GL_tmpVerticesAmt = 0;
        GL_tmpTrisAmt = 0;

        glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA);

#ifdef PLATFORM_VR
        // The cutscene is authored as a 640x480 (4:3) presentation (video in the upper rows,
        // subtitles below). The VR render target is ~square, so fitting this canvas to the full
        // target width left the video anchored to the top with a large black gap below. Fit the
        // 4:3 canvas inside the target instead; shift_x/shift_y (below) then center it.
        if (std3D_vrTargetActive && targetWidth > 0 && targetHeight > 0) {
            if ((double)targetWidth / (double)targetHeight > (640.0 / 480.0)) {
                fake_windowH = (double)targetHeight;            // target wider than 4:3 -> pillarbox
                fake_windowW = fake_windowH * (640.0 / 480.0);
            } else {
                fake_windowW = (double)targetWidth;             // target taller than 4:3 -> letterbox
                fake_windowH = fake_windowW * (480.0 / 640.0);
            }
        }
#endif

        int video_height = Main_bMotsCompat ? 350 : 300;
        int subs_y = 350;
        int subs_h = 130;
        if (Main_bMotsCompat) {
            subs_y = 400;
            subs_h = 80;
        }

        float partial_menu_w = (menu_h * (640.0 / 480.0));
        float upscale = fake_windowW/640.0;
        float upscale2 = (fake_windowH - (50 + video_height * upscale))/((double)subs_h);
        float upscale3 = 1.0;//Window_ySize/480.0;

        if (upscale2 < 1.0) {
            upscale2 = 1.0;
            if (fake_windowH > 480.0) {
                upscale2 = 2.0;
            }
        }
        if (upscale2 > upscale) {
            upscale2 = upscale;
        }

        float shift_y = ((double)targetHeight - fake_windowH) / 2.0;
        float shift_x = ((double)targetWidth - fake_windowW) / 2.0;

        float sub_width = 640*upscale2;
        float sub_x = (fake_windowW - sub_width) / 2.0;

        float pause_width = 640*upscale3;
        float pause_x = (fake_windowW - pause_width) / 2.0;

        //printf("%f %f, %f %f %f, %d %d\n", sub_x, pause_x, upscale, upscale2, upscale3, Window_xSize, Window_ySize);

        // Main View
        std3D_DrawMenuSubrect(0, 50, 640, video_height, shift_x + 0, shift_y + 50, upscale);

        // Subtitles
        if (jkCutscene_dword_55B750) {
            

            // Some monitors might not have a bottom black bar, so draw an outline
            std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x-2, shift_y + fake_windowH - (subs_h*upscale2), upscale2);
            std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x+2, shift_y + fake_windowH - (subs_h*upscale2), upscale2);
            std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x, shift_y + fake_windowH - (subs_h*upscale2) - 2, upscale2);
            std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x, shift_y + fake_windowH - (subs_h*upscale2) + 2, upscale2);

            //std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x-2, shift_y + fake_windowH - (subs_h*upscale2) -2, upscale2);
            //std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x+2, shift_y + fake_windowH - (subs_h*upscale2) +2, upscale2);
            //std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x+2, shift_y + fake_windowH - (subs_h*upscale2) - 2, upscale2);
            //std3D_DrawMenuSubrect2(0, subs_y, 640, subs_h, shift_x + sub_x-2, shift_y + fake_windowH - (subs_h*upscale2) + 2, upscale2);

            std3D_DrawMenuSubrect(0, subs_y, 640, subs_h, shift_x + sub_x, shift_y + fake_windowH - (subs_h*upscale2), upscale2);
        }

        // Paused
        std3D_DrawMenuSubrect(0, 10, 640, 40, shift_x + pause_x, shift_y + 0*upscale, upscale3);
    }
    else
    {
        GL_tmpVerticesAmt = 0;
        GL_tmpTrisAmt = 0;

        // Main View
        std3D_DrawMenuSubrect(0, 128, menu_w, menu_h-256, 0, 128, 0.0);

        float hudScale = Window_ySize / 480.0;

        /*if (menu_w >= 3600)
            hudScale = 4;
        else if (menu_w >= 1800)
            hudScale = 3;
        else if (menu_w >= 1200)
            hudScale = 2;*/

        // Left and Right HUD
        std3D_DrawMenuSubrect(0, menu_h - 64, 64, 64, 0, Window_ySize - 64*hudScale, hudScale);
        std3D_DrawMenuSubrect(menu_w - 64, menu_h - 64, 64, 64, Window_xSize - 64*hudScale, Window_ySize - 64*hudScale, hudScale);

        // Items
        std3D_DrawMenuSubrect((menu_w / 2) - 128, menu_h - 64, 256, 64, (Window_xSize / 2) - (128*hudScale), Window_ySize - 64*hudScale, hudScale);

        // Text
        float textScale = hudScale;
        if (jkDev_BMFontHeight > 11) {
            textScale *= 11.0 / (float)jkDev_BMFontHeight;
        }
        float textWidth = menu_w - (48*2);
        float textHeight = jkDev_BMFontHeight * 5.5;
        float destTextWidth = textWidth * textScale;
        std3D_DrawMenuSubrect(48, 0, menu_w - (48*2), textHeight, (Window_xSize / 2) - (destTextWidth / 2), 0, textScale);

        // Active forcepowers/items
        std3D_DrawMenuSubrect(menu_w - 48, 0, 48, 128, Window_xSize - (48*hudScale), 0, hudScale);
    }

    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    
    glActiveTexture(GL_TEXTURE0 + 0);
    // Added: Support RGB video mode for full color playback
    if (std3D_bVideoRGBMode && Video_videoRGBTexId) {
        glBindTexture(GL_TEXTURE_2D, Video_videoRGBTexId);
        glUniform1i(programMenu_uniform_rgb_mode, 1);
    } else {
        glBindTexture(GL_TEXTURE_2D, Video_menuTexId);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Video_menuBuffer.format.width, Video_menuBuffer.format.height, GL_RED, GL_UNSIGNED_BYTE, Video_menuBuffer.sdlSurface->pixels);
        glUniform1i(programMenu_uniform_rgb_mode, 0);
    }

    //GLushort data_elements[32 * 3];
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, displaypal_texture);

    glActiveTexture(GL_TEXTURE0 + 0);
    glUniform1i(programMenu_uniform_tex, 0);
    glUniform1i(programMenu_uniform_displayPalette, 1);

    D3DVERTEX* vertexes = GL_tmpVertices;

    glBindBuffer(GL_ARRAY_BUFFER, menu_vbo_all);
    glBufferData(GL_ARRAY_BUFFER, GL_tmpVerticesAmt * sizeof(D3DVERTEX), GL_tmpVertices, GL_STREAM_DRAW);
    glVertexAttribPointer(
        programMenu_attribute_coord3d, // attribute
        3,                 // number of elements per vertex, here (x,y,z)
        GL_FLOAT,          // the type of each element
        GL_FALSE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // data stride
        (GLvoid*)offsetof(D3DVERTEX, x)                  // offset of first element
    );
    
    glVertexAttribPointer(
        programMenu_attribute_v_color, // attribute
        4,                 // number of elements per vertex, here (R,G,B,A)
        GL_UNSIGNED_BYTE,  // the type of each element
        GL_TRUE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, color) // offset of first element
    );

    /*glVertexAttribPointer(
        std3D_texFboStage.attribute_v_light, // attribute
        1,                 // number of elements per vertex, here (L)
        GL_FLOAT,  // the type of each element
        GL_FALSE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, lightLevel) // offset of first element
    );*/

    glVertexAttribPointer(
        programMenu_attribute_v_uv,    // attribute
        2,                 // number of elements per vertex, here (U,V)
        GL_FLOAT,          // the type of each element
        GL_FALSE,          // take our values as-is
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, tu)                  // offset of first element
    );

    glEnableVertexAttribArray(programMenu_attribute_coord3d);
    glEnableVertexAttribArray(programMenu_attribute_v_color);
    glEnableVertexAttribArray(programMenu_attribute_v_uv);

    {

    float maxX, maxY, scaleX, scaleY, width, height;

    // Use VR-aware dimensions for the transformation matrix and viewport
    scaleX = 1.0/((double)targetWidth / 2.0);
    scaleY = 1.0/((double)targetHeight / 2.0);
    maxX = 1.0;
    maxY = 1.0;
    width = targetWidth;
    height = targetHeight;

    float d3dmat[16] = {
       maxX*scaleX,      0,                                          0,      0, // right
       0,                                       -maxY*scaleY,               0,      0, // up
       0,                                       0,                                          1,     0, // forward
       -(width/2)*scaleX,  (height/2)*scaleY,     -1,      1  // pos
    };

    glUniformMatrix4fv(programMenu_uniform_mvp, 1, GL_FALSE, d3dmat);
    glViewport(0, 0, width, height);

    }
    
    rdTri* tris = GL_tmpTris;
    
    rdDDrawSurface* last_tex = (rdDDrawSurface*)(intptr_t)-1;
    int last_tex_idx = 0;
    //GLushort* data_elements = malloc(sizeof(GLushort) * 3 * GL_tmpTrisAmt);
    for (int j = 0; j < GL_tmpTrisAmt; j++)
    {
        menu_data_elements[(j*3)+0] = tris[j].v1;
        menu_data_elements[(j*3)+1] = tris[j].v2;
        menu_data_elements[(j*3)+2] = tris[j].v3;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, menu_ibo_triangle);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GL_tmpTrisAmt * 3 * sizeof(GLushort), menu_data_elements, GL_STREAM_DRAW);

    int tris_size = 0;  
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &tris_size);
    glDrawElements(GL_TRIANGLES, tris_size / sizeof(GLushort), GL_UNSIGNED_SHORT, 0);

    glDisableVertexAttribArray(programMenu_attribute_v_uv);
    glDisableVertexAttribArray(programMenu_attribute_v_color);
    glDisableVertexAttribArray(programMenu_attribute_coord3d);

    std3D_DrawMapOverlay();
    std3D_DrawUIRenderList();

    last_flags = 0;
}

void std3D_DrawMapOverlay()
{
    if (Main_bHeadless) return;

    //glFlush();

    glBindFramebuffer(GL_FRAMEBUFFER, std3D_windowFbo);
    glDepthMask(GL_TRUE);
    glCullFace(GL_FRONT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_ALWAYS);
    glUseProgram(programMenu);
    
    float menu_w = (double)Window_xSize;
    float menu_h = (double)Window_ySize;

    if (!jkGame_isDDraw)
    {
        return;
    }

    menu_w = Video_menuBuffer.format.width;
    menu_h = Video_menuBuffer.format.height;

    GL_tmpVerticesAmt = 0;
    GL_tmpTrisAmt = 0;

    // Main View
    std3D_DrawMenuSubrect(0, 0, menu_w, menu_h, 0, 0, 0.0);

    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, Video_overlayTexId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Video_overlayMapBuffer.format.width, Video_overlayMapBuffer.format.height, GL_RED, GL_UNSIGNED_BYTE, Video_overlayMapBuffer.sdlSurface->pixels);

    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, displaypal_texture);

    glActiveTexture(GL_TEXTURE0 + 0);
    glUniform1i(programMenu_uniform_tex, 0);
    glUniform1i(programMenu_uniform_displayPalette, 1);

    D3DVERTEX* vertexes = GL_tmpVertices;
    glBindBuffer(GL_ARRAY_BUFFER, menu_vbo_all);
    glBufferData(GL_ARRAY_BUFFER, GL_tmpVerticesAmt * sizeof(D3DVERTEX), GL_tmpVertices, GL_STREAM_DRAW);
    glVertexAttribPointer(
        programMenu_attribute_coord3d, // attribute
        3,                 // number of elements per vertex, here (x,y,z)
        GL_FLOAT,          // the type of each element
        GL_FALSE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // data stride
        (GLvoid*)offsetof(D3DVERTEX, x)                  // offset of first element
    );
    
    glVertexAttribPointer(
        programMenu_attribute_v_color, // attribute
        4,                 // number of elements per vertex, here (R,G,B,A)
        GL_UNSIGNED_BYTE,  // the type of each element
        GL_TRUE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, color) // offset of first element
    );

    glVertexAttribPointer(
        programMenu_attribute_v_uv,    // attribute
        2,                 // number of elements per vertex, here (U,V)
        GL_FLOAT,          // the type of each element
        GL_FALSE,          // take our values as-is
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, tu)                  // offset of first element
    );

    glEnableVertexAttribArray(programMenu_attribute_coord3d);
    glEnableVertexAttribArray(programMenu_attribute_v_color);
    glEnableVertexAttribArray(programMenu_attribute_v_uv);


    {

    float maxX, maxY, scaleX, scaleY, width, height;

    scaleX = 1.0/((double)Window_xSize / 2.0);
    scaleY = 1.0/((double)Window_ySize / 2.0);
    maxX = 1.0;
    maxY = 1.0;
    width = Window_xSize;
    height = Window_ySize;
    
    float d3dmat[16] = {
       maxX*scaleX,      0,                                          0,      0, // right
       0,                                       -maxY*scaleY,               0,      0, // up
       0,                                       0,                                          1,     0, // forward
       -(width/2)*scaleX,  (height/2)*scaleY,     -1,      1  // pos
    };
    
    glUniformMatrix4fv(programMenu_uniform_mvp, 1, GL_FALSE, d3dmat);
    glViewport(0, 0, width, height);

    }
    
    rdTri* tris = GL_tmpTris;
    
    rdDDrawSurface* last_tex = (rdDDrawSurface*)(intptr_t)-1;
    int last_tex_idx = 0;
    //GLushort* data_elements = malloc(sizeof(GLushort) * 3 * GL_tmpTrisAmt);
    for (int j = 0; j < GL_tmpTrisAmt; j++)
    {
        menu_data_elements[(j*3)+0] = tris[j].v1;
        menu_data_elements[(j*3)+1] = tris[j].v2;
        menu_data_elements[(j*3)+2] = tris[j].v3;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, menu_ibo_triangle);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GL_tmpTrisAmt * 3 * sizeof(GLushort), menu_data_elements, GL_STREAM_DRAW);

    int tris_size = 0;  
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &tris_size);
    glDrawElements(GL_TRIANGLES, tris_size / sizeof(GLushort), GL_UNSIGNED_SHORT, 0);

    glDisableVertexAttribArray(programMenu_attribute_v_uv);
    glDisableVertexAttribArray(programMenu_attribute_v_color);
    glDisableVertexAttribArray(programMenu_attribute_coord3d);
}

void std3D_DrawUIBitmapRGBA(stdBitmap* pBmp, int mipIdx, flex_t dstX, flex_t dstY, rdRect* srcRect, flex_t scaleX, flex_t scaleY, int bAlphaOverwrite, uint8_t color_r, uint8_t color_g, uint8_t color_b, uint8_t color_a)
{
    float internalWidth = Video_menuBuffer.format.width;
    float internalHeight = Video_menuBuffer.format.height;

    if (!pBmp) return;
    if (!pBmp->abLoadedToGPU[mipIdx]) {
        std3D_AddBitmapToTextureCache(pBmp, mipIdx, !(pBmp->palFmt & 1), 0);
    }

    if (jkGuiBuildMulti_bRendering) {
        internalWidth = 640.0;
        internalHeight = 480.0;
    }

    double scaleX_ = (double)Window_xSize/(double)internalWidth;
    double scaleY_ = (double)Window_ySize/(double)internalHeight;

    dstX *= scaleX_;
    dstY *= scaleY_;

    //double tex_w = (double)Window_xSize;
    //double tex_h = (double)Window_ySize;
    double tex_w = pBmp->mipSurfaces[0]->format.width;
    double tex_h = pBmp->mipSurfaces[0]->format.height;

    double w = tex_w;
    double h = tex_h;
    double x = 0;
    double y = 0;

    if (srcRect) {
        x = srcRect->x;
        y = srcRect->y;
        w = srcRect->width;
        h = srcRect->height;
    }

    float w_dst = w;
    float h_dst = h;

    if (scaleX == 0.0 && scaleY == 0.0)
    {
        w_dst = (w / tex_w) * (double)Window_xSize;
        h_dst = (h / tex_h) * (double)Window_ySize;

        dstX = (dstX / tex_w) * (double)Window_xSize;
        dstY = (dstY / tex_h) * (double)Window_ySize;

        scaleX = 1.0;
        scaleY = 1.0;
    }

    double dstScaleX = scaleX;
    double dstScaleY = scaleY;
    dstScaleX *= scaleX_;
    dstScaleY *= scaleY_;

    double u1 = (x / tex_w);
    double u2 = ((x+w) / tex_w);
    double v1 = (y / tex_h);
    double v2 = ((y+h) / tex_h);

    uint32_t color = 0;

    color |= (color_r << 0);
    color |= (color_g << 8);
    color |= (color_b << 16);
    color |= (color_a << 24);

    if (GL_tmpUIVerticesAmt + 4 > STD3D_MAX_UI_VERTICES) {
        return;
    }
    if (GL_tmpUITrisAmt + 2 > STD3D_MAX_UI_TRIS) {
        return;
    }

    if (dstY + (dstScaleY * h_dst) < 0.0 || dstX + (dstScaleX * w_dst) < 0.0) {
        return;
    }
    if (dstY > Window_ySize || dstX > Window_xSize) {
        return;
    }

    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].x = dstX;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].y = dstY;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].tu = u1;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].tv = v1;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].x = dstX;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].y = dstY + (dstScaleY * h_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].tu = u1;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].tv = v2;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].x = dstX + (dstScaleX * w_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].y = dstY + (dstScaleY * h_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].tu = u2;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].tv = v2;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].x = dstX + (dstScaleX * w_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].y = dstY;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].tu = u2;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].tv = v1;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].nz = 0;
    
    GL_tmpUITris[GL_tmpUITrisAmt+0].v1 = GL_tmpUIVerticesAmt+1;
    GL_tmpUITris[GL_tmpUITrisAmt+0].v2 = GL_tmpUIVerticesAmt+0;
    GL_tmpUITris[GL_tmpUITrisAmt+0].v3 = GL_tmpUIVerticesAmt+2;
    GL_tmpUITris[GL_tmpUITrisAmt+0].flags = bAlphaOverwrite;
    GL_tmpUITris[GL_tmpUITrisAmt+0].texture = pBmp->aTextureIds[mipIdx];
    
    GL_tmpUITris[GL_tmpUITrisAmt+1].v1 = GL_tmpUIVerticesAmt+0;
    GL_tmpUITris[GL_tmpUITrisAmt+1].v2 = GL_tmpUIVerticesAmt+3;
    GL_tmpUITris[GL_tmpUITrisAmt+1].v3 = GL_tmpUIVerticesAmt+2;
    GL_tmpUITris[GL_tmpUITrisAmt+1].flags = bAlphaOverwrite;
    GL_tmpUITris[GL_tmpUITrisAmt+1].texture = pBmp->aTextureIds[mipIdx];
    
    GL_tmpUIVerticesAmt += 4;
    GL_tmpUITrisAmt += 2;
}

void std3D_DrawUIBitmap(stdBitmap* pBmp, int mipIdx, flex_t dstX, flex_t dstY, rdRect* srcRect, flex_t scale, int bAlphaOverwrite)
{
    std3D_DrawUIBitmapRGBA(pBmp, mipIdx, dstX, dstY, srcRect, scale, scale, bAlphaOverwrite, 0xFF, 0xFF, 0xFF, 0xFF);
}

void std3D_DrawUIClearedRect(uint8_t palIdx, rdRect* dstRect)
{
    if (!displaypal_data) return;
    uint32_t color = 0;
    uint8_t color_r = ((uint8_t*)displaypal_data)[(palIdx*3) + 0];
    uint8_t color_g = ((uint8_t*)displaypal_data)[(palIdx*3) + 1];
    uint8_t color_b = ((uint8_t*)displaypal_data)[(palIdx*3) + 2];

    std3D_DrawUIClearedRectRGBA(color_r, color_g, color_b, 0xFF, dstRect);
}

void std3D_DrawUIClearedRectRGBA(uint8_t color_r, uint8_t color_g, uint8_t color_b, uint8_t color_a, rdRect* dstRect)
{
    if (!has_initted) return;
    if (!dstRect) return;
    double dstX = dstRect->x;
    double dstY = dstRect->y;

    float internalWidth = Video_menuBuffer.format.width;
    float internalHeight = Video_menuBuffer.format.height;
    if (!internalWidth || !internalHeight) return;

    if (jkGuiBuildMulti_bRendering) {
        internalWidth = 640.0;
        internalHeight = 480.0;
    }

    double scaleX = (double)Window_xSize/(double)internalWidth;
    double scaleY = (double)Window_ySize/(double)internalHeight;

    dstX *= scaleX;
    dstY *= scaleY;

    //double tex_w = (double)Window_xSize;
    //double tex_h = (double)Window_ySize;
    double tex_w = dstRect->width;
    double tex_h = dstRect->height;
    if (!tex_w || !tex_h) return;

    double w = tex_w;
    double h = tex_h;
    double x = 0;
    double y = 0;

    float w_dst = w;
    float h_dst = h;
    double scale = 1.0;

    double dstScaleX = scale;
    double dstScaleY = scale;
    dstScaleX *= scaleX;
    dstScaleY *= scaleY;

    double u1 = (x / tex_w);
    double u2 = ((x+w) / tex_w);
    double v1 = (y / tex_h);
    double v2 = ((y+h) / tex_h);

    uint32_t color = 0;

    color |= (color_r << 0);
    color |= (color_g << 8);
    color |= (color_b << 16);
    color |= (color_a << 24);
    if (GL_tmpUIVerticesAmt + 4 > STD3D_MAX_UI_VERTICES) {
        return;
    }
    if (GL_tmpUITrisAmt + 2 > STD3D_MAX_UI_TRIS) {
        return;
    }

    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].x = dstX;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].y = dstY;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].tu = u1;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].tv = v1;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+0].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].x = dstX;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].y = dstY + (dstScaleY * h_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].tu = u1;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].tv = v2;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+1].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].x = dstX + (dstScaleX * w_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].y = dstY + (dstScaleY * h_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].tu = u2;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].tv = v2;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+2].nz = 0;
    
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].x = dstX + (dstScaleX * w_dst);
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].y = dstY;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].z = 0.0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].tu = u2;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].tv = v1;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].nx = 0;
    GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].color = color;
    *(uint32_t*)&GL_tmpUIVertices[GL_tmpUIVerticesAmt+3].nz = 0;
    
    GL_tmpUITris[GL_tmpUITrisAmt+0].v1 = GL_tmpUIVerticesAmt+1;
    GL_tmpUITris[GL_tmpUITrisAmt+0].v2 = GL_tmpUIVerticesAmt+0;
    GL_tmpUITris[GL_tmpUITrisAmt+0].v3 = GL_tmpUIVerticesAmt+2;
    GL_tmpUITris[GL_tmpUITrisAmt+0].flags = 0;
    GL_tmpUITris[GL_tmpUITrisAmt+0].texture = blank_tex_white;
    
    GL_tmpUITris[GL_tmpUITrisAmt+1].v1 = GL_tmpUIVerticesAmt+0;
    GL_tmpUITris[GL_tmpUITrisAmt+1].v2 = GL_tmpUIVerticesAmt+3;
    GL_tmpUITris[GL_tmpUITrisAmt+1].v3 = GL_tmpUIVerticesAmt+2;
    GL_tmpUITris[GL_tmpUITrisAmt+1].flags = 0;
    GL_tmpUITris[GL_tmpUITrisAmt+1].texture = blank_tex_white;
    
    GL_tmpUIVerticesAmt += 4;
    GL_tmpUITrisAmt += 2;
}

void std3D_DrawUIRenderList()
{
    if (Main_bHeadless) return;
    if (!GL_tmpUITrisAmt) return;

    //glFlush();

    //printf("Draw render list\n");
    glBindFramebuffer(GL_FRAMEBUFFER, std3D_windowFbo);
    glDepthMask(GL_TRUE);
    glCullFace(GL_FRONT);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_ALWAYS);
    glUseProgram(std3D_uiProgram.program); // TODO: simpler shader
    
    last_ui_tex = 0;
    last_ui_flags = -1;

    // Generate vertices list
    D3DVERTEX* vertexes = GL_tmpUIVertices;

    float maxX, maxY, scaleX, scaleY, width, height;

    float internalWidth = Window_xSize;//Video_menuBuffer.format.width;
    float internalHeight = Window_ySize;//Video_menuBuffer.format.height;

    if (jkGuiBuildMulti_bRendering) {
        internalWidth = 640.0;
        internalHeight = 480.0;
    }

    maxX = 1.0;
    maxY = 1.0;
    scaleX = 1.0/((double)internalWidth / 2.0);
    scaleY = 1.0/((double)internalHeight / 2.0);
    width = Window_xSize;
    height = Window_ySize;

    if (jkGuiBuildMulti_bRendering) {
        width = 640;
        height = 480;
    }

    // JKDF2's vertical FOV is fixed with their projection, for whatever reason. 
    // This ends up resulting in the view looking squished vertically at wide/ultrawide aspect ratios.
    // To compensate, we zoom the y axis here.
    // I also went ahead and fixed vertical displays in the same way because it seems to look better.
    float zoom_yaspect = 1.0;
    float zoom_xaspect = 1.0;
    
    float shift_add_x = 0;
    float shift_add_y = 0;
    
    glUniform1i(std3D_uiProgram.uniform_tex, 0);
    glUniform1i(std3D_uiProgram.uniform_tex2, 1);
    glUniform1i(std3D_uiProgram.uniform_tex3, 2);

    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, blank_tex_white);
    
    {
    
    float d3dmat[16] = {
       maxX*scaleX,      0,                                          0,      0, // right
       0,                                       -maxY*scaleY,               0,      0, // up
       0,                                       0,                                          1,     0, // forward
       -(width/2)*scaleX,  (height/2)*scaleY,     -1,      1  // pos
    };
    
    glUniformMatrix4fv(std3D_uiProgram.uniform_mvp, 1, GL_FALSE, d3dmat);
    glViewport(0, 0, width, height);
    glUniform2f(std3D_uiProgram.uniform_iResolution, internalWidth, internalHeight);

    float param1 = 1.0;
    float param2 = 1.0;

    glUniform1f(std3D_uiProgram.uniform_param1, param1);
    glUniform1f(std3D_uiProgram.uniform_param2, param2);
    glUniform1f(std3D_uiProgram.uniform_param3, jkPlayer_gamma);
    
    }

    rdUITri* tris = GL_tmpUITris;
    glEnableVertexAttribArray(std3D_uiProgram.attribute_coord3d);
    glEnableVertexAttribArray(std3D_uiProgram.attribute_v_color);
    glEnableVertexAttribArray(std3D_uiProgram.attribute_v_uv);

    glBindBuffer(GL_ARRAY_BUFFER, menu_vbo_all);
    glBufferData(GL_ARRAY_BUFFER, GL_tmpUIVerticesAmt * sizeof(D3DVERTEX), GL_tmpUIVertices, GL_STREAM_DRAW);
    glVertexAttribPointer(
        std3D_uiProgram.attribute_coord3d, // attribute
        3,                 // number of elements per vertex, here (x,y,z)
        GL_FLOAT,          // the type of each element
        GL_FALSE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // data stride
        (GLvoid*)offsetof(D3DVERTEX, x)                  // offset of first element
    );
    
    glVertexAttribPointer(
        std3D_uiProgram.attribute_v_color, // attribute
        4,                 // number of elements per vertex, here (R,G,B,A)
        GL_UNSIGNED_BYTE,  // the type of each element
        GL_TRUE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, color) // offset of first element
    );

    /*glVertexAttribPointer(
        std3D_uiProgram.attribute_v_light, // attribute
        1,                 // number of elements per vertex, here (L)
        GL_FLOAT,  // the type of each element
        GL_FALSE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, lightLevel) // offset of first element
    );*/

    glVertexAttribPointer(
        std3D_uiProgram.attribute_v_uv,    // attribute
        2,                 // number of elements per vertex, here (U,V)
        GL_FLOAT,          // the type of each element
        GL_FALSE,          // take our values as-is
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, tu)                  // offset of first element
    );
    
    //glEnableVertexAttribArray(attribute_v_norm);

    int last_flags = 0;
    int last_tex_idx = 0;
    //GLushort* menu_data_elements = malloc(sizeof(GLushort) * 3 * GL_tmpTrisAmt);
    for (int j = 0; j < GL_tmpUITrisAmt; j++)
    {
        menu_data_elements[(j*3)+0] = tris[j].v1;
        menu_data_elements[(j*3)+1] = tris[j].v2;
        menu_data_elements[(j*3)+2] = tris[j].v3;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, menu_ibo_triangle);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GL_tmpUITrisAmt * 3 * sizeof(GLushort), menu_data_elements, GL_STREAM_DRAW);
    
    int do_batch = 0;

    int tex_id = tris[0].texture;
    glActiveTexture(GL_TEXTURE0 + 0);
    if (tex_id == 0)
        glBindTexture(GL_TEXTURE_2D, blank_tex_white);
    else
        glBindTexture(GL_TEXTURE_2D, tex_id);

    if (tris[0].flags) {
        glUniform1f(std3D_uiProgram.uniform_param1, 1.0);
    }
    else {
        glUniform1f(std3D_uiProgram.uniform_param1, 0.0);
    }
    
    for (int j = 0; j < GL_tmpUITrisAmt; j++)
    {
        if (tris[j].texture != last_ui_tex || tris[j].flags != last_ui_flags)
        {
            do_batch = 1;
        }
        
        if (do_batch)
        {
            int num_tris_batch = j - last_tex_idx;

            if (num_tris_batch)
            {
                //printf("batch %u~%u\n", last_tex_idx, j);
                glDrawElements(GL_TRIANGLES, num_tris_batch * 3, GL_UNSIGNED_SHORT, (GLvoid*)((intptr_t)&menu_data_elements[last_tex_idx * 3] - (intptr_t)&menu_data_elements[0]));
            }

            int tex_id = tris[j].texture;
            glActiveTexture(GL_TEXTURE0 + 0);
            if (tex_id == 0)
                glBindTexture(GL_TEXTURE_2D, blank_tex_white);
            else
                glBindTexture(GL_TEXTURE_2D, tex_id);

            if (tris[j].flags) {
                glUniform1f(std3D_uiProgram.uniform_param1, 1.0);
            }
            else {
                glUniform1f(std3D_uiProgram.uniform_param1, 0.0);
            }
            
            last_ui_tex = tris[j].texture;
            last_ui_flags = tris[j].flags;
            last_tex_idx = j;

            do_batch = 0;
        }
        //printf("tri %u: %u,%u,%u\n", j, tris[j].v1, tris[j].v2, tris[j].v3);
        
        
        /*int vert = tris[j].v1;
        stdPlatform_Printf("%u: %f %f %f, %f %f %f, %f %f\n", vert, vertexes[vert].x, vertexes[vert].y, vertexes[vert].z,
                                      vertexes[vert].nx, vertexes[vert].ny, vertexes[vert].nz,
                                      vertexes[vert].tu, vertexes[vert].tv);
        
        vert = tris[j].v2;
        stdPlatform_Printf("%u: %f %f %f, %f %f %f, %f %f\n", vert, vertexes[vert].x, vertexes[vert].y, vertexes[vert].z,
                                      vertexes[vert].nx, vertexes[vert].ny, vertexes[vert].nz,
                                      vertexes[vert].tu, vertexes[vert].tv);
        
        vert = tris[j].v3;
        stdPlatform_Printf("%u: %f %f %f, %f %f %f, %f %f\n", vert, vertexes[vert].x, vertexes[vert].y, vertexes[vert].z,
                                      vertexes[vert].nx, vertexes[vert].ny, vertexes[vert].nz,
                                      vertexes[vert].tu, vertexes[vert].tv);*/
    }
    
    int remaining_batch = GL_tmpUITrisAmt - last_tex_idx;

    if (remaining_batch)
    {
        glDrawElements(GL_TRIANGLES, remaining_batch * 3, GL_UNSIGNED_SHORT, (GLvoid*)((intptr_t)&menu_data_elements[last_tex_idx * 3] - (intptr_t)&menu_data_elements[0]));
    }

    // Done drawing    
    glBindTexture(GL_TEXTURE_2D, blank_tex_white);

    glDisableVertexAttribArray(std3D_uiProgram.attribute_coord3d);
    glDisableVertexAttribArray(std3D_uiProgram.attribute_v_color);
    glDisableVertexAttribArray(std3D_uiProgram.attribute_v_uv);
    
    std3D_ResetUIRenderList();
}

void std3D_DrawSimpleTex(std3DSimpleTexStage* pStage, std3DIntermediateFbo* pFbo, GLuint texId, GLuint texId2, GLuint texId3, flex_t param1, flex_t param2, flex_t param3, int gen_mips)
{
    glBindFramebuffer(GL_FRAMEBUFFER, pFbo->fbo);
    glDepthFunc(GL_ALWAYS);
    glUseProgram(pStage->program);
    
    float menu_w, menu_h, menu_u, menu_v, menu_x;
    menu_w = (double)pFbo->w;
    menu_h = (double)pFbo->h;
    menu_u = 1.0;
    menu_v = 1.0;
    menu_x = 0.0;

    GL_tmpVertices[0].x = menu_x;
    GL_tmpVertices[0].y = 0.0;
    GL_tmpVertices[0].z = 0.0;
    GL_tmpVertices[0].tu = 0.0;
    GL_tmpVertices[0].tv = menu_v;
    *(uint32_t*)&GL_tmpVertices[0].nx = 0;
    GL_tmpVertices[0].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[0].nz = 0;
    
    GL_tmpVertices[1].x = menu_x;
    GL_tmpVertices[1].y = menu_h;
    GL_tmpVertices[1].z = 0.0;
    GL_tmpVertices[1].tu = 0.0;
    GL_tmpVertices[1].tv = 0.0;
    *(uint32_t*)&GL_tmpVertices[1].nx = 0;
    GL_tmpVertices[1].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[1].nz = 0;
    
    GL_tmpVertices[2].x = menu_x + menu_w;
    GL_tmpVertices[2].y = menu_h;
    GL_tmpVertices[2].z = 0.0;
    GL_tmpVertices[2].tu = menu_u;
    GL_tmpVertices[2].tv = 0.0;
    *(uint32_t*)&GL_tmpVertices[2].nx = 0;
    GL_tmpVertices[2].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[2].nz = 0;
    
    GL_tmpVertices[3].x = menu_x + menu_w;
    GL_tmpVertices[3].y = 0.0;
    GL_tmpVertices[3].z = 0.0;
    GL_tmpVertices[3].tu = menu_u;
    GL_tmpVertices[3].tv = menu_v;
    *(uint32_t*)&GL_tmpVertices[3].nx = 0;
    GL_tmpVertices[3].color = 0xFFFFFFFF;
    *(uint32_t*)&GL_tmpVertices[3].nz = 0;
    
    GL_tmpTris[0].v1 = 1;
    GL_tmpTris[0].v2 = 0;
    GL_tmpTris[0].v3 = 2;
    
    GL_tmpTris[1].v1 = 0;
    GL_tmpTris[1].v2 = 3;
    GL_tmpTris[1].v3 = 2;
    
    GL_tmpVerticesAmt = 4;
    GL_tmpTrisAmt = 2;
    
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, texId);
    if (gen_mips)
        glGenerateMipmap(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, texId2 ? texId2 : blank_tex);
    if (texId2 && gen_mips)
        glGenerateMipmap(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, texId3 ? texId3 : blank_tex);
    if (texId3 && gen_mips)
        glGenerateMipmap(GL_TEXTURE_2D);

    GLushort data_elements[32 * 3];

    D3DVERTEX* vertexes = GL_tmpVertices;
    
    glUniform1i(pStage->uniform_tex, 0);
    glUniform1i(pStage->uniform_tex2, 1);
    glUniform1i(pStage->uniform_tex3, 2);
    
    {

    float maxX, maxY, scaleX, scaleY, width, height;

    scaleX = 1.0/((double)pFbo->w / 2.0);
    scaleY = 1.0/((double)pFbo->h / 2.0);
    maxX = 1.0;
    maxY = 1.0;
    width = pFbo->w;
    height = pFbo->h;
    
    float d3dmat[16] = {
       maxX*scaleX,      0,                                          0,      0, // right
       0,                                       -maxY*scaleY,               0,      0, // up
       0,                                       0,                                          1,     0, // forward
       -(width/2)*scaleX,  (height/2)*scaleY,     -1,      1  // pos
    };
    
    glUniformMatrix4fv(pStage->uniform_mvp, 1, GL_FALSE, d3dmat);
    glViewport(0, 0, width, height);
    glUniform2f(pStage->uniform_iResolution, pFbo->iw, pFbo->ih);

    glUniform1f(pStage->uniform_param1, param1);
    glUniform1f(pStage->uniform_param2, param2);
    glUniform1f(pStage->uniform_param3, param3);

    }
    
    rdTri* tris = GL_tmpTris;
    glEnableVertexAttribArray(pStage->attribute_coord3d);
    glEnableVertexAttribArray(pStage->attribute_v_color);
    glEnableVertexAttribArray(pStage->attribute_v_uv);

    glBindBuffer(GL_ARRAY_BUFFER, menu_vbo_all);
    glBufferData(GL_ARRAY_BUFFER, GL_tmpVerticesAmt * sizeof(D3DVERTEX), GL_tmpVertices, GL_STREAM_DRAW);
    glVertexAttribPointer(
        programMenu_attribute_coord3d, // attribute
        3,                 // number of elements per vertex, here (x,y,z)
        GL_FLOAT,          // the type of each element
        GL_FALSE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // data stride
        (GLvoid*)offsetof(D3DVERTEX, x)                  // offset of first element
    );
    
    glVertexAttribPointer(
        programMenu_attribute_v_color, // attribute
        4,                 // number of elements per vertex, here (R,G,B,A)
        GL_UNSIGNED_BYTE,  // the type of each element
        GL_TRUE,          // normalize fixed-point data?
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, color) // offset of first element
    );

    glVertexAttribPointer(
        programMenu_attribute_v_uv,    // attribute
        2,                 // number of elements per vertex, here (U,V)
        GL_FLOAT,          // the type of each element
        GL_FALSE,          // take our values as-is
        sizeof(D3DVERTEX),                 // no extra data between each position
        (GLvoid*)offsetof(D3DVERTEX, tu)                  // offset of first element
    );

    glEnableVertexAttribArray(pStage->attribute_coord3d);
    glEnableVertexAttribArray(pStage->attribute_v_color);
    glEnableVertexAttribArray(pStage->attribute_v_uv);
    
    rdDDrawSurface* last_tex = (rdDDrawSurface*)(intptr_t)-1;
    int last_tex_idx = 0;
    //GLushort* data_elements = malloc(sizeof(GLushort) * 3 * GL_tmpTrisAmt);
    for (int j = 0; j < GL_tmpTrisAmt; j++)
    {
        data_elements[(j*3)+0] = tris[j].v1;
        data_elements[(j*3)+1] = tris[j].v2;
        data_elements[(j*3)+2] = tris[j].v3;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, menu_ibo_triangle);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GL_tmpTrisAmt * 3 * sizeof(GLushort), data_elements, GL_STREAM_DRAW);

    int tris_size;
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &tris_size);
    glDrawElements(GL_TRIANGLES, tris_size / sizeof(GLushort), GL_UNSIGNED_SHORT, 0);

    glDisableVertexAttribArray(pStage->attribute_v_uv);
    glDisableVertexAttribArray(pStage->attribute_v_color);
    glDisableVertexAttribArray(pStage->attribute_coord3d);

    //free(data_elements);
        
    //glBindTexture(GL_TEXTURE_2D, 0);
}

void std3D_DrawSceneFbo()
{
    //printf("Draw scene FBO\n");

#if defined(MULTIVIEW_ENABLED)
    // MultiView: scene already rendered directly to target FBO, skip blit
    if (std3D_multiViewActive) {
        return;
    }
#endif

#ifdef PLATFORM_VR
    // Get current eye and VR FBO target
    extern int stdVR_OpenXR_GetCurrentEyeFBO(int eye);
    extern int stdVR_OpenXR_GetCurrentEye(void);
    extern void VR_Log(const char* fmt, ...);
    int eye = stdVR_OpenXR_GetCurrentEye();
    GLint vrFboTarget = (eye >= 0 && eye < 2) ? stdVR_OpenXR_GetCurrentEyeFBO(eye) : 0;

    static int drawSceneFboCount = 0;
    drawSceneFboCount++;
#endif

    glEnable(GL_BLEND);

    glBlendEquation(GL_FUNC_ADD);

    // In VR mode, use the direct VR FBO instead of relying on window.fbo override
#ifdef PLATFORM_VR
    GLint targetFbo = (vrFboTarget > 0) ? vrFboTarget : std3D_pFb->window.fbo;
#else
    GLint targetFbo = std3D_pFb->window.fbo;
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glClear( GL_COLOR_BUFFER_BIT );

    static float frameNum = 1.0;
    //frameNum += (rand() % 16);

    int draw_ssao = jkPlayer_enableSSAO;
    int draw_bloom = jkPlayer_enableBloom;

    float add_luma = (((float)rdroid_curColorEffects.add.x / 255.0f) * 0.2125)
                     + (((float)rdroid_curColorEffects.add.y / 255.0f)* 0.7154)
                     + (((float)rdroid_curColorEffects.add.z / 255.0f) * 0.0721); // FLEXTODO

    // HACK: Force blinding shouldn't show the SSAO
    if (add_luma >= 0.7) {
        draw_ssao = 0;
    }

    // Added: In VR mode, always blit the internal FBO to the VR target
    // even if jkGame_isDDraw is false (can happen during transitions)
#ifdef PLATFORM_VR
    int skipEarlyReturn = std3D_vrTargetActive;

    if (drawSceneFboCount <= 30 || drawSceneFboCount % 300 == 0) {
        VR_Log("std3D_DrawSceneFbo #%d: eye=%d vrFboTarget=%d targetFbo=%d vrActive=%d isDDraw=%d\n",
            drawSceneFboCount, eye, vrFboTarget, targetFbo, std3D_vrTargetActive, jkGame_isDDraw);
        VR_Log("  window.fbo=%d window.w=%d window.h=%d tex0=%d\n",
            std3D_pFb->window.fbo, std3D_pFb->window.w, std3D_pFb->window.h, std3D_pFb->tex0);
    }
#else
    int skipEarlyReturn = 0;
#endif
    if (!jkGame_isDDraw && !jkGuiBuildMulti_bRendering && !skipEarlyReturn)
    {
#ifdef PLATFORM_VR
        if (drawSceneFboCount <= 30 || drawSceneFboCount % 300 == 0) {
            VR_Log("std3D_DrawSceneFbo #%d: EARLY RETURN (isDDraw=%d, skipEarlyReturn=%d)\n",
                drawSceneFboCount, jkGame_isDDraw, skipEarlyReturn);
        }
#endif
        return;
    }

    if (jkGuiBuildMulti_bRendering) {
        draw_ssao = 0;
        //draw_bloom = 0;
    }

    if (draw_bloom)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->blur1.fbo);
        glClear( GL_COLOR_BUFFER_BIT );
        glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->blur2.fbo);
        glClear( GL_COLOR_BUFFER_BIT );
        glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->blur3.fbo);
        glClear( GL_COLOR_BUFFER_BIT );
        glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->blur4.fbo);
        glClear( GL_COLOR_BUFFER_BIT );
        //glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->blurBlend.fbo);
        //glClear( GL_COLOR_BUFFER_BIT );
    }

    // Clear SSAO stuff
    if (draw_ssao)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->ssaoBlur1.fbo);
        glClear( GL_COLOR_BUFFER_BIT );
        glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->ssaoBlur2.fbo);
        glClear( GL_COLOR_BUFFER_BIT );
    }

    float rad_scale = (float)std3D_pFb->w / 640.0;

#ifdef PLATFORM_VR
    // FIX: Ensure window.fbo matches vrFboTarget for the blit functions
    // std3D_DrawSimpleTex binds to pFbo->fbo, so we need to redirect it to the VR swapchain
    GLint savedWindowFbo = std3D_pFb->window.fbo;
    if (vrFboTarget > 0) {
        std3D_pFb->window.fbo = vrFboTarget;
    }
#endif

    if (!draw_ssao)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->window, std3D_pFb->tex0, 0, 0, 1.0, 1.0, jkPlayer_gamma, 0);
        //std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->window, std3D_pFb->tex1, 0, 0, 0.0, 1.0, jkPlayer_gamma, 0); // test emission output
    }
    else
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        std3D_DrawSimpleTex(&std3D_ssaoStage, &std3D_pFb->ssaoBlur1, std3D_pFb->tex2, std3D_pFb->tex3, tiledrand_texture, frameNum, 0.0, 0.0, 0); // test ssao output
        std3D_DrawSimpleTex(&std3D_blurStage, &std3D_pFb->ssaoBlur2, std3D_pFb->ssaoBlur1.tex, 0, 0, 14.0, 3.0, 1.0 * rad_scale, 1);
        //std3D_DrawSimpleTex(&std3D_blurStage, &std3D_pFb->ssaoBlur3, std3D_pFb->ssaoBlur2.tex, 0, 0, 8.0, 3.0, 4.0);

        glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA);
        std3D_DrawSimpleTex(&std3D_ssaoMixStage, &std3D_pFb->window, std3D_pFb->ssaoBlur2.tex, std3D_pFb->tex0, 0, 0.0, 0.0, jkPlayer_gamma, 0);
    }

    glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA);
    if (!draw_bloom)
        std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->window, std3D_pFb->tex1, 0, 0, 1.0, 1.0, jkPlayer_gamma, 0);

    if (draw_bloom)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        std3D_DrawSimpleTex(&std3D_blurStage, &std3D_pFb->blur1, std3D_pFb->tex1, 0, 0, 16.0, 3.0, 2.0 * rad_scale, 1);
        std3D_DrawSimpleTex(&std3D_blurStage, &std3D_pFb->blur2, std3D_pFb->blur1.tex, 0, 0, 16.0, 3.0, 2.0 * rad_scale, 1);
        std3D_DrawSimpleTex(&std3D_blurStage, &std3D_pFb->blur3, std3D_pFb->blur2.tex, 0, 0, 16.0, 3.0, 2.0 * rad_scale, 1);
        std3D_DrawSimpleTex(&std3D_blurStage, &std3D_pFb->blur4, std3D_pFb->blur3.tex, 0, 0, 16.0, 3.0, 2.0 * rad_scale, 1);

        float bloom_intensity = 1.0;
        float bloom_gamma = 1.0;
        glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA);
        /*std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->blurBlend, std3D_pFb->tex1, 0, 0, 1.0, bloom_intensity * 1.0, bloom_gamma, 0);
        std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->blurBlend, std3D_pFb->blur1.tex, 0, 0, 1.0, bloom_intensity * 1.0, bloom_gamma, 0);
        std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->blurBlend, std3D_pFb->blur2.tex, 0, 0, 1.0, bloom_intensity * 1.2, bloom_gamma, 0);
        std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->blurBlend, std3D_pFb->blur3.tex, 0, 0, 1.0, bloom_intensity * 1.0, bloom_gamma, 0);
        std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->blurBlend, std3D_pFb->blur4.tex, 0, 0, 1.0, bloom_intensity * 1.2, bloom_gamma, 0);
        */

        glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA);
        //std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->window, std3D_pFb->blurBlend.tex, 0, 0, 1.0, 1.0, 1.0, 0);
        std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->window, std3D_pFb->tex1, 0, 0, 1.0, 1.5, jkPlayer_gamma, 0);
        std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->window, std3D_pFb->blur1.tex, 0, 0, 1.0, bloom_intensity * 1.5, jkPlayer_gamma, 0);
        std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->window, std3D_pFb->blur2.tex, 0, 0, 1.0, bloom_intensity * 1.0, jkPlayer_gamma, 0);
        std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->window, std3D_pFb->blur3.tex, 0, 0, 1.0, bloom_intensity * 1.0, jkPlayer_gamma, 0);
        std3D_DrawSimpleTex(&std3D_texFboStage, &std3D_pFb->window, std3D_pFb->blur4.tex, 0, 0, 1.0, bloom_intensity * 0.8, jkPlayer_gamma, 0);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#ifdef PLATFORM_VR
    // Restore original window.fbo after blits
    if (vrFboTarget > 0) {
        std3D_pFb->window.fbo = savedWindowFbo;

        // Debug: check what got rendered to VR FBO
        if (drawSceneFboCount <= 30 || drawSceneFboCount % 300 == 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, vrFboTarget);
            GLubyte pixel[4] = {0};
            glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            VR_Log("std3D_DrawSceneFbo #%d: DONE - vrFBO center pixel=[%d,%d,%d,%d]\n",
                drawSceneFboCount, pixel[0], pixel[1], pixel[2], pixel[3]);
        }
    }
#endif
}

void std3D_DoTex(rdDDrawSurface* tex, rdTri* tri, int tris_left)
{
    if (!tex) {
        glActiveTexture(GL_TEXTURE0 + 3);
        glBindTexture(GL_TEXTURE_2D, blank_tex); // emissive
        glActiveTexture(GL_TEXTURE0 + 4);
        glBindTexture(GL_TEXTURE_2D, blank_tex); // displace

        glActiveTexture(GL_TEXTURE0 + 0);
        glBindTexture(GL_TEXTURE_2D, blank_tex_white);
        glUniform1i(uniform_tex_mode, TEX_MODE_TEST);
        glUniform1i(uniform_blend_mode, 2);
        return;
    }
    int tex_id = tex->texture_id;
    glActiveTexture(GL_TEXTURE0 + 0);
    if (tex_id == 0)
        glBindTexture(GL_TEXTURE_2D, blank_tex_white);
    else
        glBindTexture(GL_TEXTURE_2D, tex_id);

    int emiss_tex_id = tex->emissive_texture_id;
    glActiveTexture(GL_TEXTURE0 + 3);
    if (emiss_tex_id == 0) {
        glBindTexture(GL_TEXTURE_2D, blank_tex);
    }
    else {
        //printf("emissive tex id %x\n", emiss_tex_id);
        glBindTexture(GL_TEXTURE_2D, emiss_tex_id);

        // HACK
        if (tri[0].flags & 0x600) {
            //glUniform1i(uniform_blend_mode, 6);
            //last_flags |= 0x200;
        }

        
        for (int i = 0; i < tris_left; i++) {
            if (tri[i].texture != tex) break;
            if (tri[i].flags & 0x600) {
                //tri[i].flags |= 0x200;
            }
        }
    }

    int displace_tex_id = tex->displacement_texture_id;
    glActiveTexture(GL_TEXTURE0 + 4);
    if (displace_tex_id == 0) {
        glBindTexture(GL_TEXTURE_2D, blank_tex);
    }
    else {
        glBindTexture(GL_TEXTURE_2D, displace_tex_id);
    }
    //if (tex->emissive_factor[0] != 0.0 || tex->emissive_factor[1] != 0.0 || tex->emissive_factor[2] != 0.0)
    //    stdPlatform_Printf("%f %f %f\n", tex->emissive_factor[0], tex->emissive_factor[1], tex->emissive_factor[2]);
    float emissive_mult = (jkPlayer_enableBloom ? 1.0 : 5.0);
    glUniform3f(uniform_emissiveFactor, tex->emissive_factor[0] * emissive_mult, tex->emissive_factor[1] * emissive_mult, tex->emissive_factor[2] * emissive_mult);
    glUniform4f(uniform_albedoFactor, tex->albedo_factor[0], tex->albedo_factor[1], tex->albedo_factor[2], tex->albedo_factor[3]);
    if (tex->displacement_factor) {
        //printf("%f\n", tex->displacement_factor);
        //tex->displacement_factor = -0.4;
    }
    glUniform1f(uniform_displacement_factor, tex->displacement_factor);
    glActiveTexture(GL_TEXTURE0 + 0);

    if (!jkPlayer_enableTextureFilter)
        glUniform1i(uniform_tex_mode, tex->is_16bit ? TEX_MODE_16BPP : TEX_MODE_WORLDPAL);
    else
        glUniform1i(uniform_tex_mode, tex->is_16bit ? TEX_MODE_BILINEAR_16BPP : TEX_MODE_BILINEAR);
    
     glActiveTexture(GL_TEXTURE0 + 0);

    if (tex_id == 0)
        glUniform1i(uniform_tex_mode, TEX_MODE_TEST);
}

void std3D_DrawRenderList()
{
    if (Main_bHeadless) return;

    //printf("Draw render list\n");
#if defined(MULTIVIEW_ENABLED)
    // MultiView: render directly to the MultiView FBO (already bound), skip internal FBO
    if (!std3D_multiViewActive)
#endif
    {
        glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->fbo);
    }
    glUseProgram(programDefault);

#if defined(MULTIVIEW_ENABLED)
    // Bind MultiView UBOs (ViewMatrices + ProjectionMatrices) for per-eye GPU projection.
    std3D_BindMultiViewUBOs(programDefault);
#endif

#if defined(MULTIVIEW_ENABLED)
    // MultiView FBO only has 1 color attachment, skip multiple draw buffers
    if (!std3D_multiViewActive)
#endif
    {
        GLenum bufs[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
        glDrawBuffers(4, bufs);
    }
    
    last_tex = NULL;

    // Generate vertices list
    D3DVERTEX* vertexes = GL_tmpVertices;

    float maxX, maxY, scaleX, scaleY, width, height;

    float internalWidth = Video_menuBuffer.format.width;
    float internalHeight = Video_menuBuffer.format.height;

    if (jkGuiBuildMulti_bRendering) {
        internalWidth = 640.0;
        internalHeight = 480.0;
    }
#ifdef PLATFORM_VR
    // Added: When VR is active, use VR render target dimensions for projection
    // This ensures CPU projection (rdCamera_PerspProjectVR) and GPU projection match
    else if (std3D_vrTargetActive && std3D_vrTargetWidth > 0 && std3D_vrTargetHeight > 0) {
        internalWidth = (float)std3D_vrTargetWidth;
        internalHeight = (float)std3D_vrTargetHeight;
    }
#endif

    maxX = 1.0;
    maxY = 1.0;
    scaleX = 1.0/((double)internalWidth / 2.0);
    scaleY = 1.0/((double)internalHeight / 2.0);
    width = std3D_pFb->w;
    height = std3D_pFb->h;

    if (jkGuiBuildMulti_bRendering) {
        width = 640;
        height = 480;
    }
#ifdef PLATFORM_VR
    // Added: Match width/height to VR dimensions as well
    else if (std3D_vrTargetActive && std3D_vrTargetWidth > 0 && std3D_vrTargetHeight > 0) {
        width = (float)std3D_vrTargetWidth;
        height = (float)std3D_vrTargetHeight;
    }
#endif

    // JKDF2's vertical FOV is fixed with their projection, for whatever reason. 
    // This ends up resulting in the view looking squished vertically at wide/ultrawide aspect ratios.
    // To compensate, we zoom the y axis here.
    // I also went ahead and fixed vertical displays in the same way because it seems to look better.
    float zoom_yaspect = 1.0;//(width/height);
    float zoom_xaspect = 1.0;//(height/width);

    if (height > width)
    {
        zoom_yaspect = 1.0;
    }

    if (width > height)
    {
        zoom_xaspect = 1.0;
    }

    // We no longer need all the weird squishing
    if (!jkGuiBuildMulti_bRendering) {
        zoom_yaspect = 1.0;
        zoom_xaspect = 1.0;
    }
    
    float shift_add_x = 0;
    float shift_add_y = 0;

    if (jkGuiBuildMulti_bRendering) {
        float menu_w, menu_h, menu_x;
        menu_w = (double)std3D_pFb->w;
        menu_h = (double)std3D_pFb->h;

        // Keep 4:3 aspect
        menu_x = (menu_w - (menu_h * (640.0 / 480.0))) / 2.0;

        width = std3D_pFb->w;
        height = std3D_pFb->h;

        zoom_xaspect = (height/width);

        shift_add_x = (((1.0 - ((menu_x * zoom_xaspect) / std3D_pFb->w)) + 0.15) * zoom_xaspect);
        shift_add_y = -0.5;
        zoom_yaspect = 1.0;
    }

    glBindBuffer(GL_ARRAY_BUFFER, world_vbo_all);
    glBufferData(GL_ARRAY_BUFFER, GL_tmpVerticesAmt * sizeof(D3DVERTEX), vertexes, GL_STREAM_DRAW);
    
    glUniform1i(uniform_tex_mode, TEX_MODE_TEST);
    glUniform1i(uniform_blend_mode, 2);
#ifdef PLATFORM_VR
    extern int std3D_vrDebugMode;
    glUniform1i(uniform_vr_debug_mode, std3D_vrDebugMode);
#else
    glUniform1i(uniform_vr_debug_mode, 0);
#endif
    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, worldpal_lights_texture);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, worldpal_texture);
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, blank_tex_white);
    
    glUniform1i(uniform_tex, 0);
    glUniform1i(uniform_worldPalette, 1);
    glUniform1i(uniform_worldPaletteLights, 2);
    glUniform1i(uniform_texEmiss, 3);
    glUniform1i(uniform_displacement_map, 4);
    
    {

    // Standard projection matrix (VR uses CPU projection into screen space)
    float d3dmat[16] = {
       (float)(maxX*scaleX*zoom_xaspect),      0,                                          0,      0, // right
       0,                                       (float)(-maxY*scaleY*zoom_yaspect),               0,      0, // up
       0,                                       0,                                          1,     0, // forward
       (float)(-(internalWidth/2)*scaleX*zoom_xaspect + shift_add_x),  (float)((internalHeight/2)*scaleY*zoom_yaspect + shift_add_y),     (float)((!rdCamera_pCurCamera || rdCamera_pCurCamera->projectType == rdCameraProjectType_Perspective) ? -1 : 1),      1  // pos
    };

    glUniformMatrix4fv(uniform_mvp, 1, GL_FALSE, d3dmat);
    glViewport(0, 0, width, height);

    }

    glUniform2f(uniform_iResolution, width, height);

    //rdroid_curColorEffects.tint.x = 0.0;
    //rdroid_curColorEffects.tint.y = 0.5;
    //rdroid_curColorEffects.tint.z = 0.5;

#if 0
    //if (rdroid_curColorEffects.filter.x || rdroid_curColorEffects.filter.y || rdroid_curColorEffects.filter.z)
    //if (rdroid_curColorEffects.tint.x || rdroid_curColorEffects.tint.y || rdroid_curColorEffects.tint.z)
    if (rdroid_curColorEffects.add.x || rdroid_curColorEffects.add.y || rdroid_curColorEffects.add.z)
    {
        stdPlatform_Printf("a %f %f %f ", rdroid_curColorEffects.tint.x, rdroid_curColorEffects.tint.y, rdroid_curColorEffects.tint.z);
        stdPlatform_Printf("%d %d %d ", rdroid_curColorEffects.filter.x, rdroid_curColorEffects.filter.y, rdroid_curColorEffects.filter.z);
        stdPlatform_Printf("%d %d %d ", rdroid_curColorEffects.add.x, rdroid_curColorEffects.add.y, rdroid_curColorEffects.add.z);
        stdPlatform_Printf("%f\n", rdroid_curColorEffects.fade);
    }
#endif

    glUniform3f(uniform_tint, rdroid_curColorEffects.tint.x, rdroid_curColorEffects.tint.y, rdroid_curColorEffects.tint.z);
    if (rdroid_curColorEffects.filter.x || rdroid_curColorEffects.filter.y || rdroid_curColorEffects.filter.z)
        glUniform3f(uniform_filter, rdroid_curColorEffects.filter.x ? 1.0 : 0.25, rdroid_curColorEffects.filter.y ? 1.0 : 0.25, rdroid_curColorEffects.filter.z ? 1.0 : 0.25);
    else
        glUniform3f(uniform_filter, 1.0, 1.0, 1.0);
    glUniform1f(uniform_fade, rdroid_curColorEffects.fade);
    glUniform3f(uniform_add, (float)rdroid_curColorEffects.add.x / 255.0f, (float)rdroid_curColorEffects.add.y / 255.0f, (float)rdroid_curColorEffects.add.z / 255.0f);
    glUniform3f(uniform_emissiveFactor, 0.0, 0.0, 0.0);
    glUniform4f(uniform_albedoFactor, 1.0, 1.0, 1.0, 1.0);
    glUniform1f(uniform_light_mult, jkGuiBuildMulti_bRendering ? 0.85 : (jkPlayer_enableBloom ? 0.9 : 0.85));
    glUniform1f(uniform_displacement_factor, 1.0);

    rdTri* tris = GL_tmpTris;
    rdLine* lines = GL_tmpLines;
    
    //glEnableVertexAttribArray(attribute_v_norm);

    int last_tex_idx = 0;
    //GLushort* world_data_elements = malloc(sizeof(GLushort) * 3 * GL_tmpTrisAmt);
    for (int j = 0; j < GL_tmpTrisAmt; j++)
    {
        world_data_elements[(j*3)+0] = tris[j].v1;
        world_data_elements[(j*3)+1] = tris[j].v2;
        world_data_elements[(j*3)+2] = tris[j].v3;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, world_ibo_triangle);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GL_tmpTrisAmt * 3 * sizeof(GLushort), world_data_elements, GL_STREAM_DRAW);

    int do_batch = 0;

    //glDepthFunc(GL_LESS);
    //glDepthMask(GL_TRUE);
    //glCullFace(GL_FRONT);

    if (last_tex) {
        std3D_DoTex(last_tex, &tris[0], GL_tmpTrisAmt);
    }

    last_flags = tris[0].flags;

    if (last_flags & 0x800) {
        glDepthFunc(GL_LESS);
        //glClear(GL_DEPTH_BUFFER_BIT);
    }
    else {
        glDepthFunc(GL_ALWAYS);
    }

    if (last_flags & 0x600) {
        
        if (last_flags & 0x200) {
            glUniform1i(uniform_blend_mode, D3DBLEND_INVSRCALPHA);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
        else {
            glUniform1i(uniform_blend_mode, D3DBLEND_SRCALPHA);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
    }
    else {
        glUniform1i(uniform_blend_mode, D3DBLEND_ONE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    if (last_flags & 0x1000)
    {
        glDepthMask(GL_TRUE);
    }
    else
    {
        glDepthMask(GL_FALSE);
    }

    if (last_flags & 0x10000) {
        glCullFace(GL_BACK);
    }
    else
    {
        glCullFace(GL_FRONT);
    }

    for (int j = 0; j < GL_tmpTrisAmt; j++)
    {
        if (tris[j].texture != last_tex || tris[j].flags != last_flags)
        {
            do_batch = 1;
        }
        
        if (do_batch)
        {
            int num_tris_batch = j - last_tex_idx;
            rdDDrawSurface* tex = tris[j].texture;


            
            test_idk = tex;

            if (num_tris_batch)
            {
                //printf("batch %u~%u\n", last_tex_idx, j);
                glDrawElements(GL_TRIANGLES, num_tris_batch * 3, GL_UNSIGNED_SHORT, (GLvoid*)((intptr_t)&world_data_elements[last_tex_idx * 3] - (intptr_t)&world_data_elements[0]));
            }

            std3D_DoTex(tex, &tris[j], GL_tmpTrisAmt-j);
            
            int changed_flags = (last_flags ^ tris[j].flags);

            if (changed_flags & 0x600)
            {
                if (tris[j].flags & 0x600) {
                    
                    if (tris[j].flags & 0x200) {
                        glUniform1i(uniform_blend_mode, D3DBLEND_INVSRCALPHA);
                        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                    }
                    else {
                        //printf ("flags %x\n", tris[j].flags);
                        glUniform1i(uniform_blend_mode, D3DBLEND_SRCALPHA);
                        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                    }
                }
                else {
                    glUniform1i(uniform_blend_mode, D3DBLEND_ONE);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }
            }
            
            if (changed_flags & 0x1800)
            {
                if (tris[j].flags & 0x800)
                {
                    glDepthFunc(GL_LESS);
                }
                else
                {
                    glDepthFunc(GL_ALWAYS);
                    //glClear(GL_DEPTH_BUFFER_BIT);
                }
                
                if (tris[j].flags & 0x1000)
                {
                    glDepthMask(GL_TRUE);
                }
                else
                {
                    glDepthMask(GL_FALSE);
                }
            }

            if (changed_flags & 0x10000)
            {
                if (tris[j].flags & 0x10000) {
                    glCullFace(GL_BACK);
                }
                else
                {
                    glCullFace(GL_FRONT);
                }
            }
            
            last_tex = tris[j].texture;
            last_flags = tris[j].flags;
            last_tex_idx = j;

            do_batch = 0;
        }
        //printf("tri %u: %u,%u,%u, flags %x\n", j, tris[j].v1, tris[j].v2, tris[j].v3, tris[j].flags);
        
        
        /*int vert = tris[j].v1;
        stdPlatform_Printf("%u: %f %f %f, %f %f %f, %f %f\n", vert, vertexes[vert].x, vertexes[vert].y, vertexes[vert].z,
                                      vertexes[vert].nx, vertexes[vert].ny, vertexes[vert].nz,
                                      vertexes[vert].tu, vertexes[vert].tv);
        
        vert = tris[j].v2;
        stdPlatform_Printf("%u: %f %f %f, %f %f %f, %f %f\n", vert, vertexes[vert].x, vertexes[vert].y, vertexes[vert].z,
                                      vertexes[vert].nx, vertexes[vert].ny, vertexes[vert].nz,
                                      vertexes[vert].tu, vertexes[vert].tv);
        
        vert = tris[j].v3;
        stdPlatform_Printf("%u: %f %f %f, %f %f %f, %f %f\n", vert, vertexes[vert].x, vertexes[vert].y, vertexes[vert].z,
                                      vertexes[vert].nx, vertexes[vert].ny, vertexes[vert].nz,
                                      vertexes[vert].tu, vertexes[vert].tv);*/
    }
    
    int remaining_batch = GL_tmpTrisAmt - last_tex_idx;

    if (remaining_batch)
    {
        glDrawElements(GL_TRIANGLES, remaining_batch * 3, GL_UNSIGNED_SHORT, (GLvoid*)((intptr_t)&world_data_elements[last_tex_idx * 3] - (intptr_t)&world_data_elements[0]));
    }

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    
    
#if 0
    // Draw all lines
    world_data_elements = malloc(sizeof(GLushort) * 2 * GL_tmpLinesAmt);
    for (int j = 0; j < GL_tmpLinesAmt; j++)
    {
        world_data_elements[(j*2)+0] = lines[j].v1;
        world_data_elements[(j*2)+1] = lines[j].v2;
    }
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, world_ibo_triangle);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GL_tmpLinesAmt * 2 * sizeof(GLushort), world_data_elements, GL_STREAM_DRAW);

    int lines_size;
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &lines_size);
    glDrawElements(GL_LINES, lines_size / sizeof(GLushort), GL_UNSIGNED_SHORT, 0);
#endif
        
    // Done drawing    
    glBindTexture(GL_TEXTURE_2D, worldpal_texture);
    glCullFace(GL_FRONT);
    
    std3D_ResetRenderList();
}

int std3D_SetCurrentPalette(rdColor24 *a1, int a2)
{
    return 1;
}

void std3D_GetValidDimension(unsigned int inW, unsigned int inH, unsigned int *outW, unsigned int *outH)
{
    // TODO hack for JKE? I don't know what they're doing
    *outW = inW > 256 ? 256 : inW;
    *outH = inH > 256 ? 256 : inH;
}

int std3D_DrawOverlay()
{
    return 1;
}

void std3D_UnloadAllTextures()
{
#ifndef SDL2_RENDER
    if (!Main_bHeadless) {
        glDeleteTextures(std3D_loadedTexturesAmt, std3D_aLoadedTextures);
    }
    std3D_loadedTexturesAmt = 0;
#else
    std3D_UpdateSettings();
#endif
}

void std3D_AddRenderListTris(rdTri *tris, unsigned int num_tris)
{
    if (Main_bHeadless) return;

    if (GL_tmpTrisAmt + num_tris > STD3D_MAX_TRIS)
    {
        return;
    }
    
    memcpy(&GL_tmpTris[GL_tmpTrisAmt], tris, sizeof(rdTri) * num_tris);
    
    GL_tmpTrisAmt += num_tris;
}

void std3D_AddRenderListLines(rdLine* lines, uint32_t num_lines)
{
    if (Main_bHeadless) return;

    if (GL_tmpLinesAmt + num_lines > STD3D_MAX_VERTICES)
    {
        return;
    }
    
    memcpy(&GL_tmpLines[GL_tmpLinesAmt], lines, sizeof(rdLine) * num_lines);
    GL_tmpLinesAmt += num_lines;
}

int std3D_AddRenderListVertices(D3DVERTEX *vertices, int count)
{
    if (Main_bHeadless) return 1;

    if (GL_tmpVerticesAmt + count >= STD3D_MAX_VERTICES)
    {
        return 0;
    }
    
    memcpy(&GL_tmpVertices[GL_tmpVerticesAmt], vertices, sizeof(D3DVERTEX) * count);
    
    GL_tmpVerticesAmt += count;
    
    return 1;
}

void std3D_AddRenderListUITris(rdUITri *tris, unsigned int num_tris)
{
    if (Main_bHeadless) return;

    if (GL_tmpUITrisAmt + num_tris > STD3D_MAX_TRIS)
    {
        return;
    }
    
    memcpy(&GL_tmpUITris[GL_tmpUITrisAmt], tris, sizeof(rdUITri) * num_tris);
    
    GL_tmpUITrisAmt += num_tris;
}

int std3D_ClearZBuffer()
{
    glDepthMask(GL_TRUE);
#if defined(MULTIVIEW_ENABLED)
    // MultiView: clear Z buffer of currently bound FBO (the MultiView FBO)
    if (!std3D_multiViewActive)
#endif
    {
        glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->fbo);
    }
    glClear(GL_DEPTH_BUFFER_BIT);
    return 1;
}

int std3D_ClearMainFbo()
{
    if (Main_bHeadless) return 1;

#if defined(MULTIVIEW_ENABLED)
    // MultiView: scene renders directly to MultiView FBO, skip internal FBO binding
    // but still do GL state resets below
    if (!std3D_multiViewActive)
#endif
    {
        glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->fbo);

        GLenum bufs[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
        glDrawBuffers(4, bufs);
    }

    // CRITICAL: Enable depth writes before clearing! If depthMask is GL_FALSE
    // (left over from previous rendering), the depth clear will have no effect
    // and subsequent depth tests will fail against stale depth values.
    glDepthMask(GL_TRUE);

    // FIX: Comprehensive GL state reset for VR per-eye rendering
    // std3D_StartScene() is only called once per frame, not per-eye.
    // Eye 0's blit (std3D_DrawSceneFbo -> std3D_DrawSimpleTex) changes various
    // GL states that must be reset for Eye 1's scene rendering.
    // NOTE: These resets are safe to do unconditionally.
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glCullFace(GL_FRONT);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);

#ifdef PLATFORM_VR
    // Re-enable vertex attributes (std3D_DrawSimpleTex disables them)
    // Also restore the default shader program, VBO, and attribute pointers
    // (std3D_DrawSimpleTex changes them to use menu_vbo_all and different attribute IDs)
    {
        extern int stdVR_bEnabled;
        if (stdVR_bEnabled) {
            glUseProgram(programDefault);  // Restore default shader
            glBindBuffer(GL_ARRAY_BUFFER, world_vbo_all);  // Restore world VBO

            // Reset vertex attribute pointers to point to world_vbo_all with correct offsets
            // (std3D_DrawSimpleTex set them to menu_vbo_all with different offsets)
            glVertexAttribPointer(
                attribute_coord3d, 3, GL_FLOAT, GL_FALSE,
                sizeof(D3DVERTEX), (GLvoid*)offsetof(D3DVERTEX, x));
            glVertexAttribPointer(
                attribute_v_color, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                sizeof(D3DVERTEX), (GLvoid*)offsetof(D3DVERTEX, color));
            glVertexAttribPointer(
                attribute_v_light, 1, GL_FLOAT, GL_FALSE,
                sizeof(D3DVERTEX), (GLvoid*)offsetof(D3DVERTEX, lightLevel));
            glVertexAttribPointer(
                attribute_v_uv, 2, GL_FLOAT, GL_FALSE,
                sizeof(D3DVERTEX), (GLvoid*)offsetof(D3DVERTEX, tu));

            glEnableVertexAttribArray(attribute_coord3d);
            glEnableVertexAttribArray(attribute_v_color);
            glEnableVertexAttribArray(attribute_v_light);
            glEnableVertexAttribArray(attribute_v_uv);
        }
    }
#endif

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    return 1;
}

#ifdef PLATFORM_VR
static int std3D_ShouldLogVRFrame(int frame)
{
    return (frame <= 5 || frame % 300 == 0);
}

void std3D_DebugLogGLState(const char* tag, int eye, int frame)
{
#ifndef TARGET_ANDROID
    // GL_FRAMEBUFFER_SRGB and GL_DRAW_BUFFER are desktop GL features not available in GLES
    extern int stdVR_bEnabled;
    extern void VR_Log(const char* fmt, ...);
    if (!stdVR_bEnabled || !std3D_ShouldLogVRFrame(frame)) {
        return;
    }

    GLint fbo = 0;
    GLint drawBuf = 0;
    GLint readBuf = 0;
    GLint viewport[4] = {0};
    GLint scissor[4] = {0};
    GLboolean colorMask[4] = {0};
    GLboolean depthMask = GL_FALSE;
    GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean srgbEnabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_DRAW_BUFFER, &drawBuf);
    glGetIntegerv(GL_READ_BUFFER, &readBuf);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

    VR_Log("VR GL STATE [%s] frame=%d eye=%d: fbo=%d drawBuf=0x%x readBuf=0x%x viewport=(%d,%d,%d,%d) scissor=(%d,%d,%d,%d) scissorOn=%d colorMask=%d%d%d%d depthMask=%d blend=%d srgb=%d\n",
        tag ? tag : "?", frame, eye, fbo, drawBuf, readBuf,
        viewport[0], viewport[1], viewport[2], viewport[3],
        scissor[0], scissor[1], scissor[2], scissor[3],
        scissorEnabled ? 1 : 0,
        colorMask[0] ? 1 : 0, colorMask[1] ? 1 : 0, colorMask[2] ? 1 : 0, colorMask[3] ? 1 : 0,
        depthMask ? 1 : 0, blendEnabled ? 1 : 0, srgbEnabled ? 1 : 0);
#else
    (void)tag;
    (void)eye;
    (void)frame;
#endif
}

void std3D_DebugProbeInternalFbo(const char* tag, int eye, int frame)
{
    extern int stdVR_bEnabled;
    extern void VR_Log(const char* fmt, ...);
    if (!stdVR_bEnabled || !std3D_pFb || !std3D_ShouldLogVRFrame(frame)) {
        return;
    }

    GLint prevFbo = 0;
    GLint prevReadBuf = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuf);

    glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    // Ensure draws are complete before sampling
    glFinish();

    int w = std3D_pFb->w;
    int h = std3D_pFb->h;
    unsigned char c[4] = {0};
    unsigned char tl[4] = {0};
    unsigned char tr[4] = {0};
    unsigned char bl[4] = {0};
    unsigned char br[4] = {0};

    glReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
    glReadPixels(w / 8, h / 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bl);
    glReadPixels(7 * w / 8, h / 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, br);
    glReadPixels(w / 8, 7 * h / 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, tl);
    glReadPixels(7 * w / 8, 7 * h / 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, tr);

    int sum = c[0] + c[1] + c[2] + tl[0] + tl[1] + tl[2] + tr[0] + tr[1] + tr[2]
              + bl[0] + bl[1] + bl[2] + br[0] + br[1] + br[2];

    VR_Log("VR FBO PROBE [%s] frame=%d eye=%d: fbo=%d size=%dx%d sumRGB=%d center=(%d,%d,%d) TL=(%d,%d,%d) TR=(%d,%d,%d) BL=(%d,%d,%d) BR=(%d,%d,%d)\n",
        tag ? tag : "?", frame, eye, std3D_pFb->fbo, w, h, sum,
        c[0], c[1], c[2],
        tl[0], tl[1], tl[2],
        tr[0], tr[1], tr[2],
        bl[0], bl[1], bl[2],
        br[0], br[1], br[2]);

    glReadBuffer(prevReadBuf);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
}

// Clear internal FBO to a test color (for debugging stereo rendering)
// Eye 0: dark red, Eye 1: dark green
void std3D_DebugClearTestColor(int eye)
{
    if (!std3D_pFb) return;

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->fbo);
    if (eye == 0) {
        glClearColor(0.25f, 0.0f, 0.0f, 1.0f);  // Dark red for eye 0
    } else {
        glClearColor(0.0f, 0.25f, 0.0f, 1.0f);  // Dark green for eye 1
    }
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // Reset to black

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
}

// Save internal FBO to PPM file (for debugging stereo rendering)
void std3D_DebugSaveInternalFbo(const char* filename)
{
    extern void VR_Log(const char* fmt, ...);
    if (!std3D_pFb || !filename) return;

    // Ensure all pending GL operations are complete
    glFinish();

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->fbo);

    // Log which FBO we're reading from
    GLint actualFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &actualFbo);
    VR_Log("std3D_DebugSaveInternalFbo: saving %s from fbo=%d (std3D_pFb->fbo=%d)\n",
           filename, actualFbo, std3D_pFb->fbo);

    int w = std3D_pFb->w;
    int h = std3D_pFb->h;
    unsigned char* pixels = (unsigned char*)malloc(w * h * 3);
    if (pixels) {
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);

        // Sample a few pixels for debugging
        int cx = w / 2, cy = h / 2;
        int idx = cy * w * 3 + cx * 3;
        VR_Log("  Center pixel (%d,%d): R=%d G=%d B=%d\n", cx, cy,
               pixels[idx], pixels[idx+1], pixels[idx+2]);

        FILE* fp = fopen(filename, "wb");
        if (fp) {
            fprintf(fp, "P6\n%d %d\n255\n", w, h);
            // PPM stores top-to-bottom, GL reads bottom-to-top, so flip
            for (int y = h - 1; y >= 0; y--) {
                fwrite(pixels + y * w * 3, 1, w * 3, fp);
            }
            fclose(fp);
            VR_Log("  SCREENSHOT SAVED: %s (%dx%d)\n", filename, w, h);
        }
        free(pixels);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
}

// Added: draw a straight line between two WORLD positions, for alignment aids.
// Uses the crosshair shader, which is the one shader that applies the real per-eye projection
// from the MultiView UBOs. The vertices must be in ENGINE VIEW space, so this transforms the
// two world points by the current camera view matrix first.
// Call this while the VR camera is active, before sithCamera_RestoreVRView.
void std3D_DrawWorldLine(const rdVector3* pWorldA, const rdVector3* pWorldB,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a, float width)
{
    if (Main_bHeadless || !programCrosshair || !pWorldA || !pWorldB) return;

    extern rdCamera* rdCamera_pCurCamera;
    if (!rdCamera_pCurCamera) return;

    rdVector3 viewA, viewB;
    rdMatrix_TransformPoint34(&viewA, pWorldA, &rdCamera_pCurCamera->view_matrix);
    rdMatrix_TransformPoint34(&viewB, pWorldB, &rdCamera_pCurCamera->view_matrix);

    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);   // an alignment aid must stay visible through the weapon model
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(width);

    glUseProgram(programCrosshair);

    // The MultiView branch of crosshair_v.glsl ignores mvp and uses the UBO matrices. Send an
    // identity anyway so the single-view branch stays valid.
    float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    if (programCrosshair_uniform_mvp != -1) {
        glUniformMatrix4fv(programCrosshair_uniform_mvp, 1, GL_FALSE, identity);
    }

    typedef struct { float x, y, z; uint8_t cr, cg, cb, ca; } LineVert;
    LineVert verts[2] = {
        { viewA.x, viewA.y, viewA.z, r, g, b, a },
        { viewB.x, viewB.y, viewB.z, r, g, b, a },
    };

    glEnableVertexAttribArray(programCrosshair_attribute_coord3d);
    glEnableVertexAttribArray(programCrosshair_attribute_v_color);

    glBindBuffer(GL_ARRAY_BUFFER, menu_vbo_all);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glVertexAttribPointer(programCrosshair_attribute_coord3d, 3, GL_FLOAT, GL_FALSE, sizeof(LineVert), (GLvoid*)0);
    glVertexAttribPointer(programCrosshair_attribute_v_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(LineVert), (GLvoid*)(3 * sizeof(float)));

    glDrawArrays(GL_LINES, 0, 2);

    glDisableVertexAttribArray(programCrosshair_attribute_coord3d);
    glDisableVertexAttribArray(programCrosshair_attribute_v_color);

    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

// Added: Get internal rendering FBO info
int std3D_GetInternalFBO(int* pWidth, int* pHeight)
{
    if (!std3D_pFb) return 0;
    if (pWidth) *pWidth = std3D_pFb->w;
    if (pHeight) *pHeight = std3D_pFb->h;
    return (int)std3D_pFb->fbo;
}

// Added: Render overlay buffer to currently bound FBO for VR HUD
void std3D_DrawOverlayToCurrentFBO(int targetWidth, int targetHeight)
{
    extern void VR_Log(const char* fmt, ...);

    if (Main_bHeadless) return;
    if (!has_initted) return;
    // HUD draws to Video_overlayMapBuffer (via Video_pCanvasOverlayMap->vbuffer), not Video_menuBuffer
    if (!Video_overlayMapBuffer.sdlSurface) return;

    static int overlayLogCount = 0;
    if (++overlayLogCount <= 10) {
        VR_Log("std3D_DrawOverlayToCurrentFBO: target=%dx%d, overlayMapBuffer=%dx%d\n",
               targetWidth, targetHeight,
               (int)Video_overlayMapBuffer.format.width,
               (int)Video_overlayMapBuffer.format.height);
    }

    // Select the menu program. When baking into the multiview eye buffer, use the MultiView
    // variant (num_views=2) so the HUD reaches BOTH eye layers; the plain programMenu is
    // single-view and would only fill one eye.
    GLuint mprog = programMenu;
    GLint a_coord = programMenu_attribute_coord3d, a_color = programMenu_attribute_v_color, a_uv = programMenu_attribute_v_uv;
    GLint u_mvp = programMenu_uniform_mvp, u_tex = programMenu_uniform_tex, u_pal = programMenu_uniform_displayPalette;
#if defined(MULTIVIEW_ENABLED)
    if (std3D_multiViewActive && programMenuMVLoaded) {
        mprog   = programMenuMV;
        a_coord = programMenuMV_attribute_coord3d; a_color = programMenuMV_attribute_v_color; a_uv = programMenuMV_attribute_v_uv;
        u_mvp   = programMenuMV_uniform_mvp; u_tex = programMenuMV_uniform_tex; u_pal = programMenuMV_uniform_displayPalette;
    }
#endif

    // Set up GL state for 2D overlay rendering (matching std3D_DrawMenu pattern)
    glDepthMask(GL_TRUE);
    glCullFace(GL_FRONT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_ALWAYS);
    glUseProgram(mprog);

    // Set viewport to target size
    glViewport(0, 0, targetWidth, targetHeight);

    // Use Video_overlayMapBuffer dimensions (where HUD is drawn by jkHud_Draw)
    float srcW = (float)Video_overlayMapBuffer.format.width;
    float srcH = (float)Video_overlayMapBuffer.format.height;

    // Build quad vertices for full overlay
    GL_tmpVerticesAmt = 0;
    GL_tmpTrisAmt = 0;
    std3D_DrawMenuSubrect(0, 0, srcW, srcH, 0, 0, 0.0);

    // Clear texture slots
    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, blank_tex);
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, blank_tex);

    // Upload overlay map buffer to texture (this is where HUD content is drawn)
    // HUD functions draw to Video_pCanvasOverlayMap->vbuffer which is Video_overlayMapBuffer
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, Video_overlayTexId);
    if (Video_overlayMapBuffer.sdlSurface && Video_overlayMapBuffer.sdlSurface->pixels) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        (GLsizei)Video_overlayMapBuffer.format.width, (GLsizei)Video_overlayMapBuffer.format.height,
                        GL_RED, GL_UNSIGNED_BYTE, Video_overlayMapBuffer.sdlSurface->pixels);

        // Debug: check if there's any non-zero content
        static int contentCheckCount = 0;
        if (++contentCheckCount <= 5) {
            uint8_t* pixels = (uint8_t*)Video_overlayMapBuffer.sdlSurface->pixels;
            int nonZero = 0;
            int total = Video_overlayMapBuffer.format.width * Video_overlayMapBuffer.format.height;
            for (int i = 0; i < total && i < 10000; i++) {
                if (pixels[i] != 0) nonZero++;
            }
            VR_Log("std3D_DrawOverlayToCurrentFBO: overlayMapBuffer has %d non-zero pixels in first 10000\n", nonZero);
        }
    }

    // Bind palette texture
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, displaypal_texture);

    // Set uniforms for menu shader
    glActiveTexture(GL_TEXTURE0 + 0);
    glUniform1i(u_tex, 0);
    glUniform1i(u_pal, 1);

    // Per-eye HUD shift (forward-centering + depth convergence) is delivered via the HudOffsets
    // UBO (bound at load, updated per-frame in std3D_SetVRHudOffset) — no per-draw uniform.

    // Upload vertices
    glBindBuffer(GL_ARRAY_BUFFER, menu_vbo_all);
    glBufferData(GL_ARRAY_BUFFER, GL_tmpVerticesAmt * sizeof(D3DVERTEX), GL_tmpVertices, GL_STREAM_DRAW);

    glVertexAttribPointer(a_coord, 3, GL_FLOAT, GL_FALSE,
                          sizeof(D3DVERTEX), (GLvoid*)offsetof(D3DVERTEX, x));
    glVertexAttribPointer(a_color, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(D3DVERTEX), (GLvoid*)offsetof(D3DVERTEX, color));
    glVertexAttribPointer(a_uv, 2, GL_FLOAT, GL_FALSE,
                          sizeof(D3DVERTEX), (GLvoid*)offsetof(D3DVERTEX, tu));

    glEnableVertexAttribArray(a_coord);
    glEnableVertexAttribArray(a_color);
    glEnableVertexAttribArray(a_uv);

    // Set up transformation matrix.
    float d3dmat[16];
#if defined(MULTIVIEW_ENABLED)
    {
        // VR HUD "safe zone": map the HUD quad into an explicit, independently-sized NDC rect in
        // the comfortable central/lower viewing zone (rather than stretching it across the very
        // wide eye buffer). Mapped from the quad's ACTUAL bounding box so it's robust to any
        // window/eye-buffer size mismatch. Applied UNCONDITIONALLY (regardless of which menu
        // program is active) so the layout always responds to the tunables even if the MultiView
        // menu program isn't active; per-eye fusion/depth is added in the shader (programMenuMV
        // only, via the HudOffsets UBO). All tunable
        // live in the VR Options menu (persisted in the player config).
        extern float jkPlayer_vrHudWidth, jkPlayer_vrHudHeight, jkPlayer_vrHudPosX, jkPlayer_vrHudPosY;
        const float hudHFrac   = jkPlayer_vrHudWidth;   // HUD half-WIDTH  in NDC (smaller = narrower)
        const float hudVFrac   = jkPlayer_vrHudHeight;  // HUD half-HEIGHT in NDC (smaller = shorter)
        const float hudCenterY = jkPlayer_vrHudPosY;    // vertical centre in NDC (more negative = lower)
        const float hudCenterX = jkPlayer_vrHudPosX;    // horizontal centre in NDC

        // Bounding box of the quad just built by std3D_DrawMenuSubrect.
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (int i = 0; i < GL_tmpVerticesAmt; i++) {
            float vx = GL_tmpVertices[i].x, vy = GL_tmpVertices[i].y;
            if (vx < minX) minX = vx; if (vx > maxX) maxX = vx;
            if (vy < minY) minY = vy; if (vy > maxY) maxY = vy;
        }
        float qw = maxX - minX, qh = maxY - minY;
        if (qw < 1.0f) qw = 1.0f;
        if (qh < 1.0f) qh = 1.0f;
        float sx = (2.0f * hudHFrac) / qw;
        float sy = (2.0f * hudVFrac) / qh;
        float d3dmatMV[16] = {
            sx, 0, 0, 0,
            0, -sy, 0, 0,
            0, 0, 1, 0,
            hudCenterX - hudHFrac - sx * minX, hudCenterY + hudVFrac + sy * minY, -1, 1
        };
        memcpy(d3dmat, d3dmatMV, sizeof(d3dmat));
    }
#else
    {
        float scaleX = 1.0f / ((float)targetWidth / 2.0f);
        float scaleY = 1.0f / ((float)targetHeight / 2.0f);
        float d3dmatMono[16] = {
           scaleX, 0, 0, 0,
           0, -scaleY, 0, 0,
           0, 0, 1, 0,
           -(targetWidth/2.0f)*scaleX, (targetHeight/2.0f)*scaleY, -1, 1
        };
        memcpy(d3dmat, d3dmatMono, sizeof(d3dmat));
    }
#endif
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, d3dmat);

    // Upload and draw indices
    for (int j = 0; j < GL_tmpTrisAmt; j++) {
        menu_data_elements[(j*3)+0] = GL_tmpTris[j].v1;
        menu_data_elements[(j*3)+1] = GL_tmpTris[j].v2;
        menu_data_elements[(j*3)+2] = GL_tmpTris[j].v3;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, menu_ibo_triangle);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GL_tmpTrisAmt * 3 * sizeof(GLushort), menu_data_elements, GL_STREAM_DRAW);

    int tris_size = 0;
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &tris_size);
    glDrawElements(GL_TRIANGLES, tris_size / sizeof(GLushort), GL_UNSIGNED_SHORT, 0);

    // Cleanup
    glDisableVertexAttribArray(a_uv);
    glDisableVertexAttribArray(a_color);
    glDisableVertexAttribArray(a_coord);

    GL_tmpVerticesAmt = 0;
    GL_tmpTrisAmt = 0;
}

// Added: Draw UI render list to the currently bound FBO with specified dimensions
// This is a VR-specific version that doesn't bind std3D_windowFbo
void std3D_DrawUIRenderListToCurrentFBO(int fboWidth, int fboHeight, float dstX, float dstY, float dstW, float dstH)
{
    extern void VR_Log(const char* fmt, ...);

    if (Main_bHeadless) return;
    if (!GL_tmpUITrisAmt) {
        static int noTriLogCount = 0;
        if (++noTriLogCount <= 5) {
            VR_Log("std3D_DrawUIRenderListToCurrentFBO: No UI tris to draw (GL_tmpUITrisAmt=0)\n");
        }
        return;
    }

#if defined(MULTIVIEW_ENABLED)
    // When baking the HUD into the MultiView eye buffer (multiview FBO bound), draw the UI
    // with the MultiView UI program (num_views=2 -> both eye layers). Temporarily swap it in
    // so the rest of this function (which references std3D_uiProgram) uses it unchanged.
    std3DSimpleTexStage std3D_uiProgramSaved;
    int uiUseMV = (std3D_multiViewActive && std3D_uiProgramMVLoaded);
    if (uiUseMV) { std3D_uiProgramSaved = std3D_uiProgram; std3D_uiProgram = std3D_uiProgramMV; }
#endif

    // The HUD elements are positioned for desktop resolution (Window_xSize x Window_ySize).
    // Scale and offset them into the destination rect within the FBO.
    float scaleFromX = (float)Window_xSize;
    float scaleFromY = (float)Window_ySize;
    float scaleX_factor = dstW / scaleFromX;
    float scaleY_factor = dstH / scaleFromY;

    // Scale vertex positions to destination rect
    D3DVERTEX* verts = GL_tmpUIVertices;
    for (int i = 0; i < GL_tmpUIVerticesAmt; i++) {
        verts[i].x = dstX + verts[i].x * scaleX_factor;
        verts[i].y = dstY + verts[i].y * scaleY_factor;
    }

    static int drawUILogCount = 0;
    if (++drawUILogCount <= 10) {
        VR_Log("std3D_DrawUIRenderListToCurrentFBO: fbo=%dx%d dst=(%.0f,%.0f,%.0f,%.0f) from=%dx%d tris=%d verts=%d\n",
            fboWidth, fboHeight, dstX, dstY, dstW, dstH, (int)scaleFromX, (int)scaleFromY,
            GL_tmpUITrisAmt, GL_tmpUIVerticesAmt);
    }

    // NOTE: We do NOT bind a specific FBO here - caller must have bound the target FBO already
    glDepthMask(GL_TRUE);
    glCullFace(GL_FRONT);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_ALWAYS);
    glUseProgram(std3D_uiProgram.program);

    last_ui_tex = 0;
    last_ui_flags = -1;

    // Ortho projection covers the full FBO
    float internalWidth = (float)fboWidth;
    float internalHeight = (float)fboHeight;

    float maxX = 1.0f;
    float maxY = 1.0f;
    float scaleX = 1.0f / (internalWidth / 2.0f);
    float scaleY = 1.0f / (internalHeight / 2.0f);

    glUniform1i(std3D_uiProgram.uniform_tex, 0);
    glUniform1i(std3D_uiProgram.uniform_tex2, 1);
    glUniform1i(std3D_uiProgram.uniform_tex3, 2);

    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, blank_tex_white);

    float d3dmat[16] = {
        maxX * scaleX, 0, 0, 0,
        0, -maxY * scaleY, 0, 0,
        0, 0, 1, 0,
        -(internalWidth / 2.0f) * scaleX, (internalHeight / 2.0f) * scaleY, -1, 1
    };

    glUniformMatrix4fv(std3D_uiProgram.uniform_mvp, 1, GL_FALSE, d3dmat);
    glViewport(0, 0, fboWidth, fboHeight);
    glUniform2f(std3D_uiProgram.uniform_iResolution, internalWidth, internalHeight);

    // When baking into the multiview eye buffer, the per-eye HUD shift (so the HUD lands at the
    // same WORLD angle in both eyes) is delivered via the HudOffsets UBO bound to the MV ui
    // program at load and updated per-frame in std3D_SetVRHudOffset — no per-draw uniform.

    glUniform1f(std3D_uiProgram.uniform_param1, 1.0f);
    glUniform1f(std3D_uiProgram.uniform_param2, 1.0f);
    glUniform1f(std3D_uiProgram.uniform_param3, jkPlayer_gamma);

    rdUITri* tris = GL_tmpUITris;
    glEnableVertexAttribArray(std3D_uiProgram.attribute_coord3d);
    glEnableVertexAttribArray(std3D_uiProgram.attribute_v_color);
    glEnableVertexAttribArray(std3D_uiProgram.attribute_v_uv);

    glBindBuffer(GL_ARRAY_BUFFER, menu_vbo_all);
    glBufferData(GL_ARRAY_BUFFER, GL_tmpUIVerticesAmt * sizeof(D3DVERTEX), GL_tmpUIVertices, GL_STREAM_DRAW);
    glVertexAttribPointer(std3D_uiProgram.attribute_coord3d, 3, GL_FLOAT, GL_FALSE,
                          sizeof(D3DVERTEX), (GLvoid*)offsetof(D3DVERTEX, x));
    glVertexAttribPointer(std3D_uiProgram.attribute_v_color, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(D3DVERTEX), (GLvoid*)offsetof(D3DVERTEX, color));
    glVertexAttribPointer(std3D_uiProgram.attribute_v_uv, 2, GL_FLOAT, GL_FALSE,
                          sizeof(D3DVERTEX), (GLvoid*)offsetof(D3DVERTEX, tu));

    for (int j = 0; j < GL_tmpUITrisAmt; j++) {
        menu_data_elements[(j * 3) + 0] = tris[j].v1;
        menu_data_elements[(j * 3) + 1] = tris[j].v2;
        menu_data_elements[(j * 3) + 2] = tris[j].v3;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, menu_ibo_triangle);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GL_tmpUITrisAmt * 3 * sizeof(GLushort), menu_data_elements, GL_STREAM_DRAW);

    int last_tex_idx = 0;
    int tex_id = tris[0].texture;
    glActiveTexture(GL_TEXTURE0 + 0);
    if (tex_id == 0)
        glBindTexture(GL_TEXTURE_2D, blank_tex_white);
    else
        glBindTexture(GL_TEXTURE_2D, tex_id);

    if (tris[0].flags) {
        glUniform1f(std3D_uiProgram.uniform_param1, 1.0f);
    } else {
        glUniform1f(std3D_uiProgram.uniform_param1, 0.0f);
    }

    last_ui_tex = tris[0].texture;
    last_ui_flags = tris[0].flags;

    for (int j = 1; j < GL_tmpUITrisAmt; j++) {
        if (tris[j].texture == last_ui_tex && tris[j].flags == last_ui_flags) {
            continue;
        }

        int num_tris_batch = j - last_tex_idx;
        if (num_tris_batch) {
            glDrawElements(GL_TRIANGLES, num_tris_batch * 3, GL_UNSIGNED_SHORT,
                (GLvoid*)((intptr_t)&menu_data_elements[last_tex_idx * 3] - (intptr_t)&menu_data_elements[0]));
        }

        tex_id = tris[j].texture;
        glActiveTexture(GL_TEXTURE0 + 0);
        if (tex_id == 0)
            glBindTexture(GL_TEXTURE_2D, blank_tex_white);
        else
            glBindTexture(GL_TEXTURE_2D, tex_id);

        if (tris[j].flags) {
            glUniform1f(std3D_uiProgram.uniform_param1, 1.0f);
        } else {
            glUniform1f(std3D_uiProgram.uniform_param1, 0.0f);
        }

        last_ui_tex = tris[j].texture;
        last_ui_flags = tris[j].flags;
        last_tex_idx = j;
    }

    int remaining_batch = GL_tmpUITrisAmt - last_tex_idx;
    if (remaining_batch) {
        glDrawElements(GL_TRIANGLES, remaining_batch * 3, GL_UNSIGNED_SHORT,
            (GLvoid*)((intptr_t)&menu_data_elements[last_tex_idx * 3] - (intptr_t)&menu_data_elements[0]));
    }

    glBindTexture(GL_TEXTURE_2D, blank_tex_white);
    glDisableVertexAttribArray(std3D_uiProgram.attribute_coord3d);
    glDisableVertexAttribArray(std3D_uiProgram.attribute_v_color);
    glDisableVertexAttribArray(std3D_uiProgram.attribute_v_uv);

#if defined(MULTIVIEW_ENABLED)
    if (uiUseMV) std3D_uiProgram = std3D_uiProgramSaved;  // restore mono UI program
#endif

    std3D_ResetUIRenderList();
}

// Static helper: push one textured quad (white vertex colour) into the UI render list.
// Vertex layout matches std3D_DrawUIBitmapRGBA: 0=TL,1=BL,2=BR,3=TR. UVs: TL=(u0,v0), BR=(u1,v1).
static void std3D_PushUIQuad(float x0, float y0, float x1, float y1,
                             float u0, float v0, float u1, float v1, GLuint tex, int flags)
{
    if (GL_tmpUIVerticesAmt + 4 > STD3D_MAX_UI_VERTICES) return;
    if (GL_tmpUITrisAmt + 2 > STD3D_MAX_UI_TRIS) return;
    int b = GL_tmpUIVerticesAmt;
    uint32_t white = 0xFFFFFFFFu;
    GL_tmpUIVertices[b+0].x=x0; GL_tmpUIVertices[b+0].y=y0; GL_tmpUIVertices[b+0].z=0.0f; GL_tmpUIVertices[b+0].tu=u0; GL_tmpUIVertices[b+0].tv=v0; GL_tmpUIVertices[b+0].color=white; *(uint32_t*)&GL_tmpUIVertices[b+0].nx=0; *(uint32_t*)&GL_tmpUIVertices[b+0].nz=0;
    GL_tmpUIVertices[b+1].x=x0; GL_tmpUIVertices[b+1].y=y1; GL_tmpUIVertices[b+1].z=0.0f; GL_tmpUIVertices[b+1].tu=u0; GL_tmpUIVertices[b+1].tv=v1; GL_tmpUIVertices[b+1].color=white; *(uint32_t*)&GL_tmpUIVertices[b+1].nx=0; *(uint32_t*)&GL_tmpUIVertices[b+1].nz=0;
    GL_tmpUIVertices[b+2].x=x1; GL_tmpUIVertices[b+2].y=y1; GL_tmpUIVertices[b+2].z=0.0f; GL_tmpUIVertices[b+2].tu=u1; GL_tmpUIVertices[b+2].tv=v1; GL_tmpUIVertices[b+2].color=white; *(uint32_t*)&GL_tmpUIVertices[b+2].nx=0; *(uint32_t*)&GL_tmpUIVertices[b+2].nz=0;
    GL_tmpUIVertices[b+3].x=x1; GL_tmpUIVertices[b+3].y=y0; GL_tmpUIVertices[b+3].z=0.0f; GL_tmpUIVertices[b+3].tu=u1; GL_tmpUIVertices[b+3].tv=v0; GL_tmpUIVertices[b+3].color=white; *(uint32_t*)&GL_tmpUIVertices[b+3].nx=0; *(uint32_t*)&GL_tmpUIVertices[b+3].nz=0;
    GL_tmpUITris[GL_tmpUITrisAmt+0].v1=b+1; GL_tmpUITris[GL_tmpUITrisAmt+0].v2=b+0; GL_tmpUITris[GL_tmpUITrisAmt+0].v3=b+2; GL_tmpUITris[GL_tmpUITrisAmt+0].flags=flags; GL_tmpUITris[GL_tmpUITrisAmt+0].texture=tex;
    GL_tmpUITris[GL_tmpUITrisAmt+1].v1=b+0; GL_tmpUITris[GL_tmpUITrisAmt+1].v2=b+3; GL_tmpUITris[GL_tmpUITrisAmt+1].v3=b+2; GL_tmpUITris[GL_tmpUITrisAmt+1].flags=flags; GL_tmpUITris[GL_tmpUITrisAmt+1].texture=tex;
    GL_tmpUIVerticesAmt += 4;
    GL_tmpUITrisAmt += 2;
}

// VR HUD: render the (multi-textured) UI render list to a single RGBA texture SINGLE-VIEW, then
// composite that one texture into both multiview eye layers as a single-texture MV draw. This is
// the fix for the Adreno multiview per-eye HUD desync (a multi-texture UI draw replicated across
// the two eye layers sampled inconsistent texture state -> e.g. left eye 100/100, right eye 050).
// A single-texture MV draw replicates correctly (verified on-device: solid-colour and overlay
// single-texture draws never desync).
void std3D_RenderVRHudViaTexture(int fboWidth, int fboHeight, float dstX, float dstY, float dstW, float dstH)
{
    if (Main_bHeadless || !has_initted) return;
    if (!GL_tmpUITrisAmt) return;  // no HUD content this frame

    int W = (int)Window_xSize, H = (int)Window_ySize;
    if (W <= 0 || H <= 0) return;

    // Save the multiview eye FBO + viewport FIRST (before any bind below clobbers them), so we can
    // restore them for the composite.
    GLint savedFbo = 0, savedVp[4] = {0,0,0,0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &savedFbo);
    glGetIntegerv(GL_VIEWPORT, savedVp);

    // Lazy-create / resize the HUD RGBA target.
    if (std3D_vrHudFbo == 0 || std3D_vrHudTexW != W || std3D_vrHudTexH != H) {
        if (std3D_vrHudTex) { glDeleteTextures(1, &std3D_vrHudTex); std3D_vrHudTex = 0; }
        if (std3D_vrHudFbo) { glDeleteFramebuffers(1, &std3D_vrHudFbo); std3D_vrHudFbo = 0; }
        glGenTextures(1, &std3D_vrHudTex);
        glBindTexture(GL_TEXTURE_2D, std3D_vrHudTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &std3D_vrHudFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, std3D_vrHudFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, std3D_vrHudTex, 0);
        std3D_vrHudTexW = W; std3D_vrHudTexH = H;
    }

    // --- Step 1: render the UI list to the HUD texture, SINGLE-VIEW. All the per-element texture
    // switches happen here, on one layer, where Adreno multiview cannot desync them. Gamma is
    // applied here (once); the composite below uses gamma 1.0 so it isn't applied twice. ---
    glBindFramebuffer(GL_FRAMEBUFFER, std3D_vrHudFbo);
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    {
        int savedMV = std3D_multiViewActive;
        std3D_multiViewActive = 0;  // force the mono UI program -> plain 2D render
        std3D_DrawUIRenderListToCurrentFBO(W, H, 0.0f, 0.0f, (float)W, (float)H);
        std3D_multiViewActive = savedMV;
    }
    // (std3D_DrawUIRenderListToCurrentFBO calls std3D_ResetUIRenderList internally.)

    // --- Step 2: composite the single HUD texture into both eye layers (MV, ONE texture -> syncs).
    glBindFramebuffer(GL_FRAMEBUFFER, savedFbo);
    glViewport(savedVp[0], savedVp[1], savedVp[2], savedVp[3]);

    // One full-window quad sampling the HUD texture. UV is V-flipped: the HUD texture was written
    // with the UI ortho (which flips Y), so Window (x,y) lives at UV (x/W, 1 - y/H). flags=1 ->
    // discard fully-transparent texels so only the HUD composites over the scene.
    std3D_PushUIQuad(0.0f, 0.0f, (float)W, (float)H, 0.0f, 1.0f, 1.0f, 0.0f, std3D_vrHudTex, 1);

    extern float jkPlayer_gamma;
    float savedGamma = jkPlayer_gamma;
    jkPlayer_gamma = 1.0f;            // gamma already baked into the HUD texture in step 1
    std3D_DrawUIRenderListToCurrentFBO(fboWidth, fboHeight, dstX, dstY, dstW, dstH);
    jkPlayer_gamma = savedGamma;
}

#define STD3D_VR_SCOPE_MASK_SIZE (256)
static GLuint std3D_vrScopeMaskTex = 0;

// Premultiplied RGBA (the UI blend is ONE, ONE_MINUS_SRC_ALPHA): opaque black outside the
// eyepiece, clear inside, plus a crosshair with an open centre. Texel (0,0) is opaque black and
// doubles as the fill for the surround quads.
static void std3D_CreateVRScopeMaskTex(void)
{
    const int N = STD3D_VR_SCOPE_MASK_SIZE;
    uint8_t* pPixels = (uint8_t*)malloc(N * N * 4);
    if (!pPixels) return;

    const float c = (N - 1) * 0.5f;
    const float radius = N * 0.5f - 1.0f;
    const float gap = N * 0.06f;
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            float dx = x - c, dy = y - c;
            float d = sqrtf(dx * dx + dy * dy);

            float a = stdMath_Clamp(d - (radius - 1.5f), 0.0f, 1.0f) * 255.0f;
            int bOnLine = (fabsf(dx) < 0.75f || fabsf(dy) < 0.75f) && d > gap;
            if (bOnLine && a < 200.0f) a = 200.0f;

            uint8_t* p = &pPixels[(y * N + x) * 4];
            p[0] = p[1] = p[2] = 0;
            p[3] = (uint8_t)a;
        }
    }

    glGenTextures(1, &std3D_vrScopeMaskTex);
    glBindTexture(GL_TEXTURE_2D, std3D_vrScopeMaskTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, pPixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, blank_tex_white);
    free(pPixels);
}

void std3D_DrawVRScopeMask(int fboWidth, int fboHeight, float diameterFrac, float centreNdcY)
{
    if (Main_bHeadless || !has_initted) return;
    if (fboWidth <= 0 || fboHeight <= 0 || Window_xSize <= 0 || Window_ySize <= 0) return;

    if (!std3D_vrScopeMaskTex) {
        std3D_CreateVRScopeMaskTex();
        if (!std3D_vrScopeMaskTex) return;
    }

    // The UI list is laid out in window space and stretched onto the dst rect, so size the
    // eyepiece in eye-buffer pixels first to keep it round.
    float sx = (float)Window_xSize / (float)fboWidth;
    float sy = (float)Window_ySize / (float)fboHeight;
    float diam = diameterFrac * (float)((fboWidth < fboHeight) ? fboWidth : fboHeight);
    float x0 = ((float)fboWidth - diam) * 0.5f * sx;
    float x1 = x0 + diam * sx;
    float centreY = (float)fboHeight * 0.5f * (1.0f - centreNdcY);
    float y0 = (centreY - diam * 0.5f) * sy;
    float y1 = y0 + diam * sy;

    // The surround overshoots the buffer: the per-eye HUD shift moves the whole list sideways.
    float W = (float)Window_xSize, H = (float)Window_ySize;
    float ox = W * 0.5f, oy = H * 0.5f;
    const float b = 0.5f / STD3D_VR_SCOPE_MASK_SIZE;

    std3D_PushUIQuad(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, std3D_vrScopeMaskTex, 1);
    std3D_PushUIQuad(-ox, -oy, W + ox, y0, b, b, b, b, std3D_vrScopeMaskTex, 1);
    std3D_PushUIQuad(-ox, y1, W + ox, H + oy, b, b, b, b, std3D_vrScopeMaskTex, 1);
    std3D_PushUIQuad(-ox, y0, x0, y1, b, b, b, b, std3D_vrScopeMaskTex, 1);
    std3D_PushUIQuad(x1, y0, W + ox, y1, b, b, b, b, std3D_vrScopeMaskTex, 1);

    std3D_DrawUIRenderListToCurrentFBO(fboWidth, fboHeight, 0.0f, 0.0f, (float)fboWidth, (float)fboHeight);
}
#endif

int std3D_AddToTextureCache(stdVBuffer *vbuf, rdDDrawSurface *texture, int is_alpha_tex, int no_alpha)
{
    if (Main_bHeadless) return 1;
    if (!vbuf || !texture) return 1;
    if (texture->texture_loaded) return 1;

    if (std3D_loadedTexturesAmt >= STD3D_MAX_TEXTURES) {
        stdPlatform_Printf("ERROR: Texture cache exhausted!! Ask ShinyQuagsire to increase the size.\n");
        return 1;
    }
    //printf("Add to texture cache\n");
    
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    uint8_t* image_8bpp = (uint8_t*)vbuf->sdlSurface->pixels;
    uint16_t* image_16bpp = (uint16_t*)vbuf->sdlSurface->pixels;
    uint8_t* pal = (uint8_t*)vbuf->palette;
    
    uint32_t width, height;
    width = vbuf->format.width;
    height = vbuf->format.height;

    glBindTexture(GL_TEXTURE_2D, image_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    if (jkPlayer_enableTextureFilter && texture->is_16bit)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }

    if (vbuf->format.format.is16bit)
    {
        texture->is_16bit = 1;
#if 1
        if (!is_alpha_tex)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0,  GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, image_8bpp);
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,  GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, image_8bpp);
#endif

#ifdef __NOTDEF_FORMAT_CONVERSION
        void* image_data = malloc(width*height*4);
    
        for (int j = 0; j < height; j++)
        {
            for (int i = 0; i < width; i++)
            {
                uint32_t index = (i*height) + j;
                uint32_t val_rgba = 0x00000000;
                
                uint16_t val = image_16bpp[index];
                if (!is_alpha_tex) // RGB565
                {
                    uint8_t val_a1 = 1;
                    uint8_t val_r5 = (val >> 11) & 0x1F;
                    uint8_t val_g6 = (val >> 5) & 0x3F;
                    uint8_t val_b5 = (val >> 0) & 0x1F;

                    uint8_t val_a8 = val_a1 ? 0xFF : 0x0;
                    uint8_t val_r8 = ( val_r5 * 527 + 23 ) >> 6;
                    uint8_t val_g8 = ( val_g6 * 259 + 33 ) >> 6;
                    uint8_t val_b8 = ( val_b5 * 527 + 23 ) >> 6;

#ifdef __NOTDEF_TRANSPARENT_BLACK
                    uint8_t transparent_r8 = (vbuf->transparent_color >> 16) & 0xFF;
                    uint8_t transparent_g8 = (vbuf->transparent_color >> 8) & 0xFF;
                    uint8_t transparent_b8 = (vbuf->transparent_color >> 0) & 0xFF;

                    if (val_r8 == transparent_r8 && val_g8 == transparent_g8 && val_b8 == transparent_b8) {
                        val_a1 = 0;
                    }
#endif // __NOTDEF_TRANSPARENT_BLACK

                    val_rgba |= (val_a8 << 24);
                    val_rgba |= (val_b8 << 16);
                    val_rgba |= (val_g8 << 8);
                    val_rgba |= (val_r8 << 0);
                }
                else // RGB1555
                {
                    uint8_t val_a1 = (val >> 15);
                    uint8_t val_r5 = (val >> 10) & 0x1F;
                    uint8_t val_g5 = (val >> 5) & 0x1F;
                    uint8_t val_b5 = (val >> 0) & 0x1F;

                    uint8_t val_a8 = val_a1 ? 0xFF : 0x0;
                    uint8_t val_r8 = ( val_r5 * 527 + 23 ) >> 6;
                    uint8_t val_g8 = ( val_g5 * 527 + 23 ) >> 6;
                    uint8_t val_b8 = ( val_b5 * 527 + 23 ) >> 6;

                    val_rgba |= (val_a8 << 24);
                    val_rgba |= (val_b8 << 16);
                    val_rgba |= (val_g8 << 8);
                    val_rgba |= (val_r8 << 0);
                }
                    
                *(uint32_t*)(image_data + index*4) = val_rgba;
            }
        }
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, image_data);

        texture->pDataDepthConverted = image_data;
#endif // __NOTDEF_FORMAT_CONVERSION
    }
    else {
#if 0
        void* image_data = malloc(width*height*4);
    
        for (int j = 0; j < height; j++)
        {
            for (int i = 0; i < width; i++)
            {
                uint32_t index = (i*height) + j;
                uint32_t val_rgba = 0xFF000000;
                
                if (pal)
                {
                    uint8_t val = image_8bpp[index];
                    val_rgba |= (pal[(val * 3) + 2] << 16);
                    val_rgba |= (pal[(val * 3) + 1] << 8);
                    val_rgba |= (pal[(val * 3) + 0] << 0);
                }
                else
                {
                    uint8_t val = image_8bpp[index];
                    rdColor24* pal_master = (rdColor24*)sithWorld_pCurrentWorld->colormaps->colors;//stdDisplay_gammaPalette;
                    rdColor24* color = &pal_master[val];
                    val_rgba |= (color->r << 16);
                    val_rgba |= (color->g << 8);
                    val_rgba |= (color->b << 0);
                }
                
                *(uint32_t*)(image_data + index*4) = val_rgba;
            }
        }
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

        texture->pDataDepthConverted = image_data;
#endif
        texture->is_16bit = 0;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_8bpp);

        texture->pDataDepthConverted = NULL;
    }

    
    std3D_aLoadedSurfaces[std3D_loadedTexturesAmt] = texture;
    std3D_aLoadedTextures[std3D_loadedTexturesAmt++] = image_texture;
    /*ext->surfacebuf = image_data;
    ext->surfacetex = image_texture;
    ext->surfacepaltex = pal_texture;*/
    
    texture->texture_id = image_texture;
    texture->emissive_texture_id = 0;
    texture->displacement_texture_id = 0;
    texture->texture_loaded = 1;
    texture->emissive_factor[0] = 0.0;
    texture->emissive_factor[1] = 0.0;
    texture->emissive_factor[2] = 0.0;
    texture->albedo_factor[0] = 1.0;
    texture->albedo_factor[1] = 1.0;
    texture->albedo_factor[2] = 1.0;
    texture->albedo_factor[3] = 1.0;
    texture->displacement_factor = 0.0;
    texture->albedo_data = NULL;
    texture->displacement_data = NULL;
    texture->emissive_data = NULL;

    glBindTexture(GL_TEXTURE_2D, blank_tex);
    
    return 1;
}

int std3D_GetBitmapCacheIdx()
{
    for (int i = 0; i < STD3D_MAX_TEXTURES; i++)
    {
        if (!std3D_aUIBitmaps[i]) {
            return i;
        }
    }
    return -1;
}

int std3D_AddBitmapToTextureCache(stdBitmap *texture, int mipIdx, int is_alpha_tex, int no_alpha)
{
    if (Main_bHeadless) return 1;
    if (!has_initted) return 0;
    if (!texture) return 1;
    if (mipIdx >= texture->numMips) return 1;
    if (!texture->abLoadedToGPU || texture->abLoadedToGPU[mipIdx]) return 1;

    stdVBuffer *vbuf = texture->mipSurfaces[mipIdx];
     if (!vbuf) return 1;

    int cacheIdx = std3D_GetBitmapCacheIdx();
    if (cacheIdx < 0) {
        stdPlatform_Printf("ERROR: Texture cache exhausted!! Ask ShinyQuagsire to increase the size.\n");
        return 1;
    }
    //printf("Add to texture cache\n");
    
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    uint8_t* image_8bpp = (uint8_t*)vbuf->sdlSurface->pixels;
    uint16_t* image_16bpp = (uint16_t*)vbuf->sdlSurface->pixels;
    uint8_t* pal = (uint8_t*)vbuf->palette;
    
    uint32_t width, height;
    width = vbuf->format.width;
    height = vbuf->format.height;

    glBindTexture(GL_TEXTURE_2D, image_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    /*if (jkPlayer_enableTextureFilter && texture->is_16bit)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    else*/
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }

    if (vbuf->format.format.is16bit || texture->format.bpp == 16)
    {
        texture->is_16bit = 1;

#if 0
        if (!is_alpha_tex)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0,  GL_RGB, GL_UNSIGNED_SHORT_5_6_5, image_8bpp);
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,  GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, image_8bpp);
#endif
        uint32_t tex_width, tex_height, tex_row_stride;
        uint32_t row_stride = vbuf->format.width_in_bytes / 2;
        tex_width = width;//vbuf->format.width_in_bytes / 2;
        tex_height = height;
        tex_row_stride = width;

        void* image_data = malloc(tex_width*tex_height*4);
        memset(image_data, 0, tex_width*tex_height*4);
    
        //uint32_t index = 0;
        for (int j = 0; j < height; j++)
        {
            for (int i = 0; i < width; i++)
            {
                uint32_t index = (j*row_stride) + i;
                uint32_t tex_index = (j*tex_row_stride) + i;
                uint32_t val_rgba = 0x00000000;
                
                uint16_t val = image_16bpp[index];
                if (vbuf->format.format.r_bits == 5 && vbuf->format.format.g_bits == 6 && vbuf->format.format.b_bits == 5) // RGB565
                {
                    uint8_t val_a1 = 1;
                    uint8_t val_r5 = (val >> 11) & 0x1F;
                    uint8_t val_g6 = (val >> 5) & 0x3F;
                    uint8_t val_b5 = (val >> 0) & 0x1F;

                    uint8_t val_a8 = val_a1 ? 0xFF : 0x0;
                    uint8_t val_r8 = ( val_r5 * 527 + 23 ) >> 6;
                    uint8_t val_g8 = ( val_g6 * 259 + 33 ) >> 6;
                    uint8_t val_b8 = ( val_b5 * 527 + 23 ) >> 6;

                    //uint8_t transparent_r8 = (vbuf->transparent_color >> 16) & 0xFF;
                    //uint8_t transparent_g8 = (vbuf->transparent_color >> 8) & 0xFF;
                    //uint8_t transparent_b8 = (vbuf->transparent_color >> 0) & 0xFF;

                    uint8_t transparent_r5 = (vbuf->transparent_color >> 11) & 0x1F;
                    uint8_t transparent_g6 = (vbuf->transparent_color >> 5) & 0x3F;
                    uint8_t transparent_b5 = (vbuf->transparent_color >> 0) & 0x1F;

                    uint8_t transparent_r8 = ( transparent_r5 * 527 + 23 ) >> 6;
                    uint8_t transparent_g8 = ( transparent_g6 * 259 + 33 ) >> 6;
                    uint8_t transparent_b8 = ( transparent_b5 * 527 + 23 ) >> 6;

                    //
                    if (vbuf->transparent_color && val_r5 == transparent_r5 && val_g6 == transparent_g6 && val_b5 == transparent_b5) {
                        val_a8 = 0;
                        //val_r8 = 0;
                        //val_g8 = 0;
                        //val_b8 = 0;
                    }

                    val_rgba |= (val_a8 << 24);
                    val_rgba |= (val_b8 << 16);
                    val_rgba |= (val_g8 << 8);
                    val_rgba |= (val_r8 << 0);

#if 0
                    val_rgba = 0xFF000000;
                    val_rgba |= (transparent_b8 << 16);
                    val_rgba |= (transparent_g8 << 8);
                    val_rgba |= (transparent_r8 << 0);
#endif
                }
                else if (vbuf->format.format.r_bits == 5 && vbuf->format.format.g_bits == 5 && vbuf->format.format.b_bits == 5) // RGB1555
                {
                    uint8_t val_a1 = (val >> 15);
                    uint8_t val_r5 = (val >> 10) & 0x1F;
                    uint8_t val_g5 = (val >> 5) & 0x1F;
                    uint8_t val_b5 = (val >> 0) & 0x1F;

                    uint8_t val_a8 = val_a1 ? 0xFF : 0x0;
                    uint8_t val_r8 = ( val_r5 * 527 + 23 ) >> 6;
                    uint8_t val_g8 = ( val_g5 * 527 + 23 ) >> 6;
                    uint8_t val_b8 = ( val_b5 * 527 + 23 ) >> 6;

                    uint8_t transparent_a1 = (vbuf->transparent_color >> 15) & 0x1;
                    uint8_t transparent_r5 = (vbuf->transparent_color >> 10) & 0x1F;
                    uint8_t transparent_g5 = (vbuf->transparent_color >> 5) & 0x1F;
                    uint8_t transparent_b5 = (vbuf->transparent_color >> 0) & 0x1F;

                    uint8_t transparent_r8 = ( transparent_r5 * 527 + 23 ) >> 6;
                    uint8_t transparent_g8 = ( transparent_g5 * 527 + 23 ) >> 6;
                    uint8_t transparent_b8 = ( transparent_b5 * 527 + 23 ) >> 6;

#if 0
                    //vbuf->transparent_color && 
                    if (val_a1 == transparent_a1 && val_r5 == transparent_r5 && val_g5 == transparent_g5 && val_b5 == transparent_b5) {
                        val_a8 = 0;
                        //val_r8 = 0;
                        //val_g8 = 0;
                        //val_b8 = 0;
                    }
#endif

                    val_rgba |= (val_a8 << 24);
                    val_rgba |= (val_b8 << 16);
                    val_rgba |= (val_g8 << 8);
                    val_rgba |= (val_r8 << 0);
#if 0
                    val_rgba = 0xFF000000;
                    val_rgba |= (transparent_b8 << 16);
                    val_rgba |= (transparent_g8 << 8);
                    val_rgba |= (transparent_r8 << 0);
#endif
                }
                else {
                    stdPlatform_Printf("wtf is this %u %u %u %u\n", vbuf->format.format.unk_40, vbuf->format.format.r_bits, vbuf->format.format.g_bits, vbuf->format.format.b_bits);
                }
                    
                ((uint32_t*)image_data)[tex_index] = val_rgba;
            }
        }
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex_width, tex_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

        texture->paDataDepthConverted[mipIdx] = image_data;
    }
    else {
        texture->is_16bit = 0;
#if 1
        uint32_t tex_width, tex_height, tex_row_stride;
        uint32_t row_stride = vbuf->format.width_in_bytes;
        tex_width = width;//vbuf->format.width_in_bytes / 2;
        tex_height = height;
        tex_row_stride = width;

        void* image_data = malloc(tex_width*tex_height*4);
        memset(image_data, 0, tex_width*tex_height*4);

        void* palette_data = texture->palette;//displaypal_data;

        if (!palette_data) 
        {
            palette_data = std3D_ui_colormap.colors;//jkGui_stdBitmaps[2]->palette;
            pal = NULL;//palette_data;
        }
        else {
            pal = NULL;//texture->palette;
        }
    
        for (int j = 0; j < height; j++)
        {
            for (int i = 0; i < width; i++)
            {
                uint32_t index = (j*row_stride) + i;
                uint32_t tex_index = (j*tex_row_stride) + i;
                uint32_t val_rgba = 0x00000000;
                
                if (pal)
                {
                    uint8_t val = image_8bpp[index];
                    val_rgba |= (pal[(val * 3) + 2] << 16);
                    val_rgba |= (pal[(val * 3) + 1] << 8);
                    val_rgba |= (pal[(val * 3) + 0] << 0);
                    val_rgba |= (0xFF << 24);

                    if (!val) {
                        val_rgba = 0;
                    }
                }
                else
                {
                    uint8_t val = image_8bpp[index];
#if 0
                    if (sithWorld_pCurrentWorld && sithWorld_pCurrentWorld->colormaps && sithWorld_pCurrentWorld->colormaps->colors)
                    {
                        rdColor24* pal_master = (rdColor24*)sithWorld_pCurrentWorld->colormaps->colors;//stdDisplay_gammaPalette;
                        rdColor24* color = &pal_master[val];
                        val_rgba |= (color->r << 16);
                        val_rgba |= (color->g << 8);
                        val_rgba |= (color->b << 0);
                        val_rgba |= (0xFF << 24);
                        stdPlatform_Printf("%x %x\n", val_rgba, val);
                    }
                    else {
                        val_rgba = 0xFFFFFFFF; // HACK
                    }
#endif

                    if (palette_data)
                    {
                        uint8_t color_r = ((uint8_t*)palette_data)[(val*3) + 0];
                        uint8_t color_g = ((uint8_t*)palette_data)[(val*3) + 1];
                        uint8_t color_b = ((uint8_t*)palette_data)[(val*3) + 2];

                        val_rgba |= (0xFF << 24);
                        val_rgba |= (color_b << 16);
                        val_rgba |= (color_g << 8);
                        val_rgba |= (color_r << 0);
                    }
                    else {
                        val_rgba = 0xFFFFFF00; // HACK
                    }
                    

                    if (!val) {
                        val_rgba = 0;
                    }
                }
                
                ((uint32_t*)image_data)[tex_index] = val_rgba;
            }
        }
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

        texture->paDataDepthConverted[mipIdx] = image_data;
#endif
        //glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image_8bpp);
    }
    
    std3D_aUIBitmaps[cacheIdx] = texture;
    std3D_aUITextures[cacheIdx] = image_texture;
    std3D_loadedUITexturesAmt++;
    /*ext->surfacebuf = image_data;
    ext->surfacetex = image_texture;
    ext->surfacepaltex = pal_texture;*/
    
    texture->aTextureIds[mipIdx] = image_texture;
    texture->abLoadedToGPU[mipIdx] = 1;

    glBindTexture(GL_TEXTURE_2D, blank_tex);
    
    return 1;
}

void std3D_UpdateFrameCount(rdDDrawSurface *pTexture) {
    //pTexture->frameNum = std3D_frameCount; // lol LEC bug
    std3D_RemoveTextureFromCacheList(pTexture);
    std3D_AddTextureToCacheList(pTexture);
    pTexture->frameNum = std3D_frameCount;
}
void std3D_RemoveTextureFromCacheList(rdDDrawSurface *surface) {
}
void std3D_AddTextureToCacheList(rdDDrawSurface *surface) {
}

// Added helpers
void std3D_UpdateSettings()
{
    jk_printf("Updating texture cache...\n");
    for (int i = 0; i < STD3D_MAX_TEXTURES; i++)
    {
        rdDDrawSurface* tex = std3D_aLoadedSurfaces[i];
        if (!tex) continue;

        if (!std3D_aLoadedTextures[i]) continue;
        glBindTexture(GL_TEXTURE_2D, std3D_aLoadedTextures[i]);

        if (jkPlayer_enableTextureFilter && tex->is_16bit)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
        else
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        }

        if (tex->emissive_texture_id != 0) {
            glBindTexture(GL_TEXTURE_2D, tex->emissive_texture_id);
            
            if (jkPlayer_enableTextureFilter)
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            }
            else
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            }
        }

        if (tex->displacement_texture_id != 0) {
            glBindTexture(GL_TEXTURE_2D, tex->displacement_texture_id);
            
            if (jkPlayer_enableTextureFilter)
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            }
            else
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            }
        }
    }

#if 0
    for (int i = 0; i < STD3D_MAX_TEXTURES; i++)
    {
        if (!std3D_aUITextures[i]) continue;
        glBindTexture(GL_TEXTURE_2D, std3D_aUITextures[i]);

        if (jkPlayer_enableTextureFilter)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
        else
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        }
        
    }
#endif

    glBindTexture(GL_TEXTURE_2D, blank_tex);
}

// Added
void std3D_Screenshot(const char* pFpath)
{
#ifdef TARGET_CAN_JKGM
    if (!std3D_pFb) return;

    uint8_t* data = (uint8_t*)malloc(std3D_pFb->w * std3D_pFb->h * 3 * sizeof(uint8_t));
    glBindFramebuffer(GL_FRAMEBUFFER, std3D_pFb->fbo);
    glReadPixels(0, 0, std3D_pFb->w, std3D_pFb->h, GL_RGB, GL_UNSIGNED_BYTE, data);
    jkgm_write_png(pFpath, std3D_pFb->w, std3D_pFb->h, data);
    free(data);
#endif
}

int std3D_HasAlpha()
{
    return 1;
}

int std3D_HasModulateAlpha()
{
    return 1;
}

int std3D_HasAlphaFlatStippled()
{
    return 1;
}

void std3D_PurgeBitmapRefs(stdBitmap *pBitmap)
{
    for (int i = 0; i < STD3D_MAX_TEXTURES; i++)
    {
        int texId = std3D_aUITextures[i];
        stdBitmap* tex = std3D_aUIBitmaps[i];
        if (!tex) continue;
        if (tex != pBitmap) continue;

        for (int j = 0; j < tex->numMips; j++)
        {
            if (tex->aTextureIds[j] == texId) {
                std3D_PurgeUIEntry(i, j);
                break;
            }
        }
    }
}

void std3D_PurgeSurfaceRefs(rdDDrawSurface *texture)
{
    for (int i = 0; i < STD3D_MAX_TEXTURES; i++)
    {
        rdDDrawSurface* tex = std3D_aLoadedSurfaces[i];
        if (!tex) continue;
        if (tex != texture) continue;

        std3D_PurgeTextureEntry(i);
    }
}

void std3D_PurgeTextureEntry(int i) {
    if (std3D_aLoadedTextures[i]) {
        glDeleteTextures(1, &std3D_aLoadedTextures[i]);
        std3D_aLoadedTextures[i] = 0;
    }

    rdDDrawSurface* tex = std3D_aLoadedSurfaces[i];
    if (!tex) return;

    if (tex->pDataDepthConverted != NULL) {
        free(tex->pDataDepthConverted);
        tex->pDataDepthConverted = NULL;
    }

    if (tex->albedo_data != NULL) {
        //jkgm_aligned_free(tex->albedo_data);
        tex->albedo_data = NULL;
    }

    if (tex->emissive_data != NULL) {
        //jkgm_aligned_free(tex->emissive_data);
        tex->emissive_data = NULL;
    }

    if (tex->displacement_data != NULL) {
        //jkgm_aligned_free(tex->displacement_data);
        tex->displacement_data = NULL;
    }

    if (tex->emissive_texture_id != 0) {
        glDeleteTextures(1, &tex->emissive_texture_id);
        tex->emissive_texture_id = 0;
    }

    if (tex->displacement_texture_id != 0) {
        glDeleteTextures(1, &tex->displacement_texture_id);
        tex->displacement_texture_id = 0;
    }

    tex->emissive_factor[0] = 0.0;
    tex->emissive_factor[1] = 0.0;
    tex->emissive_factor[2] = 0.0;
    tex->albedo_factor[0] = 1.0;
    tex->albedo_factor[1] = 1.0;
    tex->albedo_factor[2] = 1.0;
    tex->albedo_factor[3] = 1.0;
    tex->displacement_factor = 0.0;

    tex->texture_loaded = 0;
    tex->texture_id = 0;

    std3D_aLoadedSurfaces[i] = NULL;
}

void std3D_PurgeUIEntry(int i, int idx) {
    if (std3D_aUITextures[i]) {
        glDeleteTextures(1, &std3D_aUITextures[i]);
        std3D_aUITextures[i] = 0;
    }

    stdBitmap* tex = std3D_aUIBitmaps[i];
    if (!tex) return;

    tex->abLoadedToGPU[idx] = 0;
    tex->aTextureIds[idx] = 0;
    free(tex->paDataDepthConverted[idx]);
    tex->paDataDepthConverted[idx] = NULL;

    std3D_aUIBitmaps[i] = NULL;
    std3D_loadedUITexturesAmt--;
}

// From https://github.com/smlu/OpenJones3D/blob/main/Libs/std/Win95/std3D.c
int std3D_PurgeTextureCache(size_t size)
{
    size_t purgedBytes = 0;
    for ( rdDDrawSurface* pCacheTexture = std3D_pFirstTexCache; pCacheTexture && pCacheTexture->frameNum != std3D_frameCount; pCacheTexture = pCacheTexture->pNextCachedTexture )
    {
        if ( pCacheTexture->textureSize == size )
        {
            //IDirect3DTexture2_Release(pCacheTexture->pD3DCachedTex);
            std3D_PurgeSurfaceRefs(pCacheTexture);
            //pCacheTexture->pD3DCachedTex = NULL;
            std3D_RemoveTextureFromCacheList(pCacheTexture);
            return 1;
        }
    }

    rdDDrawSurface* pNextCachedTexture = NULL;
    for ( rdDDrawSurface* pCacheTexture = std3D_pFirstTexCache; pCacheTexture && purgedBytes < size; pCacheTexture = pNextCachedTexture )
    {
        pNextCachedTexture = pCacheTexture->pNextCachedTexture;
        if ( pCacheTexture->frameNum != std3D_frameCount )
        {
            //if ( pCacheTexture->pD3DCachedTex ) { // Added: Added check for null pointer
                //IDirect3DTexture2_Release(pCacheTexture->pD3DCachedTex);
                std3D_PurgeSurfaceRefs(pCacheTexture);
            //}
            //pCacheTexture->pD3DCachedTex = NULL;
            purgedBytes += pCacheTexture->textureSize;
            std3D_RemoveTextureFromCacheList(pCacheTexture);
        }
    }

    return purgedBytes != 0;
}

void std3D_PurgeEntireTextureCache()
{
    if (Main_bHeadless) {
        std3D_loadedTexturesAmt = 0;
        return;
    }

    if (!std3D_loadedTexturesAmt) {
        jk_printf("Skipping texture cache purge, nothing loaded.\n");
        return;
    }

    jk_printf("Purging texture cache... %x\n", std3D_loadedTexturesAmt);
    for (int i = 0; i < std3D_loadedTexturesAmt; i++)
    {
        std3D_PurgeTextureEntry(i);
    }
    std3D_loadedTexturesAmt = 0;
}

void std3D_InitializeViewport(rdRect *viewRect)
{
    signed int v1; // ebx
    signed int height; // ebp

    float viewXMax_2; // [esp+14h] [ebp+4h]
    float viewRectYMax; // [esp+14h] [ebp+4h]

    std3D_rectViewIdk.x = viewRect->x;
    v1 = viewRect->width;
    std3D_rectViewIdk.y = viewRect->y;
    std3D_rectViewIdk.width = v1;
    height = viewRect->height;
    memset(std3D_aViewIdk, 0, sizeof(std3D_aViewIdk));
    std3D_aViewIdk[0] = (float)std3D_rectViewIdk.x;
    std3D_aViewIdk[1] = (float)std3D_rectViewIdk.y;
    std3D_rectViewIdk.height = height;
    std3D_aViewTris[0].v1 = 0;
    std3D_aViewTris[0].v2 = 1;
    viewXMax_2 = (float)(v1 + std3D_rectViewIdk.x);
    std3D_aViewIdk[8] = viewXMax_2;
    std3D_aViewIdk[9] = std3D_aViewIdk[1];
    std3D_aViewIdk[16] = viewXMax_2;
    viewRectYMax = (float)(height + std3D_rectViewIdk.y);
    std3D_aViewTris[0].texture = 0;
    std3D_aViewIdk[17] = viewRectYMax;
    std3D_aViewIdk[25] = viewRectYMax;
    std3D_aViewIdk[24] = std3D_aViewIdk[0];
    std3D_aViewTris[0].v3 = 2;
    std3D_aViewTris[0].flags = 0x8200;
    std3D_aViewTris[1].v1 = 0;
    std3D_aViewTris[1].v2 = 2;
    std3D_aViewTris[1].v3 = 3;
    std3D_aViewTris[1].texture = 0;
    std3D_aViewTris[1].flags = 0x8200;
}

int std3D_GetValidDimensions(int a1, int a2, int a3, int a4)
{
    int result; // eax

    std3D_gpuMaxTexSizeMaybe = a1;
    result = a4;
    std3D_dword_53D66C = a2;
    std3D_dword_53D670 = a3;
    std3D_dword_53D674 = a4;
    return result;
}

int std3D_FindClosestDevice(uint32_t index, int a2)
{
    return 0;
}

int std3D_SetRenderList(intptr_t a1)
{
    std3D_renderList = a1;
    return std3D_CreateExecuteBuffer();
}

intptr_t std3D_GetRenderList()
{
    return std3D_renderList;
}

int std3D_CreateExecuteBuffer()
{
    return 1;
}

int std3D_IsReady()
{
    return has_initted;
}

// Added: RGB video texture functions for full color video playback
void std3D_SetVideoRGBMode(int enabled)
{
    static int lastMode = -1;
    if (lastMode != enabled) {
        jk_printf("std3D: RGB video mode %s (texId=%u, w=%u, h=%u)\n",
            enabled ? "ENABLED" : "DISABLED", Video_videoRGBTexId, std3D_videoRGBWidth, std3D_videoRGBHeight);
        lastMode = enabled;
    }
    std3D_bVideoRGBMode = enabled;
}

void std3D_CreateVideoRGBTexture(uint32_t width, uint32_t height)
{
    jk_printf("std3D_CreateVideoRGBTexture: %ux%u (existing texId=%u)\n", width, height, Video_videoRGBTexId);

    // Delete existing texture if size changed
    if (Video_videoRGBTexId && (std3D_videoRGBWidth != width || std3D_videoRGBHeight != height)) {
        glDeleteTextures(1, &Video_videoRGBTexId);
        Video_videoRGBTexId = 0;
    }

    if (!Video_videoRGBTexId) {
        glGenTextures(1, &Video_videoRGBTexId);
        glBindTexture(GL_TEXTURE_2D, Video_videoRGBTexId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        std3D_videoRGBWidth = width;
        std3D_videoRGBHeight = height;
        jk_printf("std3D_CreateVideoRGBTexture: Created texture id=%u\n", Video_videoRGBTexId);
    }
}

void std3D_UpdateVideoRGBTexture(const uint8_t* rgbData, uint32_t width, uint32_t height)
{
    if (!Video_videoRGBTexId || !rgbData) return;

    glBindTexture(GL_TEXTURE_2D, Video_videoRGBTexId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgbData);
}

void std3D_DestroyVideoRGBTexture(void)
{
    if (Video_videoRGBTexId) {
        glDeleteTextures(1, &Video_videoRGBTexId);
        Video_videoRGBTexId = 0;
        std3D_videoRGBWidth = 0;
        std3D_videoRGBHeight = 0;
    }
    std3D_bVideoRGBMode = 0;
}

// Added: Mirror mode for left-handed VR rendering
void std3D_SetFrontFaceCW(int bCW)
{
    glFrontFace(bCW ? GL_CW : GL_CCW);
}

// Added: VR FBO override functions
#ifdef PLATFORM_VR
void std3D_SetVRTargetSize(int width, int height)
{
    if (width <= 0 || height <= 0) {
        std3D_vrTargetSizeActive = 0;
        return;
    }

    std3D_vrTargetWidth = width;
    std3D_vrTargetHeight = height;
    std3D_vrTargetSizeActive = 1;
}

void std3D_SetVRTargetFBO(int fbo, int width, int height)
{
    extern void VR_Log(const char* fmt, ...);
    extern int stdVR_currentEye;
    static int setVRTargetCount = 0;
    setVRTargetCount++;

    if (setVRTargetCount <= 60 || setVRTargetCount % 600 == 0) {
        VR_Log("std3D_SetVRTargetFBO #%d (eye=%d): fbo=%d, size=%dx%d, wasActive=%d, savedFbo=%d\n",
            setVRTargetCount, stdVR_currentEye, fbo, width, height, std3D_vrTargetActive, std3D_savedWindowFbo);
    }

    if (!std3D_vrTargetActive) {
        // Save the original window FBO the first time
        std3D_savedWindowFbo = std3D_windowFbo;
    }

    // Ensure VR target size is known even before swapchain override
    std3D_SetVRTargetSize(width, height);

    // Override the window FBO so all rendering goes to VR target
    std3D_windowFbo = fbo;
    std3D_vrTargetActive = 1;

    // Update BOTH framebuffers' window info (important because std3D_swapFramebuffers can switch)
    std3D_ApplyVRWindowToAllFramebuffers();

    if (setVRTargetCount <= 60 || setVRTargetCount % 600 == 0) {
        VR_Log("  After apply: fb[0].window.fbo=%d, fb[1].window.fbo=%d, pFb->window.fbo=%d\n",
            std3D_framebuffers[0].window.fbo, std3D_framebuffers[1].window.fbo,
            std3D_pFb ? std3D_pFb->window.fbo : -1);
    }
}

void std3D_ClearVRTargetFBO(void)
{
    extern void VR_Log(const char* fmt, ...);
    static int clearVRTargetCount = 0;
    clearVRTargetCount++;

    if (clearVRTargetCount <= 60 || clearVRTargetCount % 600 == 0) {
        VR_Log("std3D_ClearVRTargetFBO #%d: wasActive=%d, savedFbo=%d, windowFbo=%d\n",
            clearVRTargetCount, std3D_vrTargetActive, std3D_savedWindowFbo, std3D_windowFbo);
    }

    if (std3D_vrTargetActive) {
        // Restore the original window FBO
        std3D_windowFbo = std3D_savedWindowFbo;
        std3D_vrTargetActive = 0;

        // Restore BOTH framebuffers' window info
        std3D_ApplyDesktopWindowToAllFramebuffers();

        if (clearVRTargetCount <= 60 || clearVRTargetCount % 600 == 0) {
            VR_Log("  After restore: fb[0].window.fbo=%d, fb[1].window.fbo=%d, pFb->window.fbo=%d\n",
                std3D_framebuffers[0].window.fbo, std3D_framebuffers[1].window.fbo,
                std3D_pFb ? std3D_pFb->window.fbo : -1);
        }
    }
}

// Added: MultiView direct rendering mode (skip internal FBO)
void std3D_SetMultiViewActive(int active)
{
#if defined(MULTIVIEW_ENABLED)
    extern void VR_Log(const char* fmt, ...);
    static int setMVActiveCount = 0;
    setMVActiveCount++;
    if (setMVActiveCount <= 60 || setMVActiveCount % 600 == 0) {
        VR_Log("std3D_SetMultiViewActive #%d: %d -> %d\n", setMVActiveCount, std3D_multiViewActive, active);
    }
    std3D_multiViewActive = active;
#endif
}

void std3D_ClearMultiViewActive(void)
{
#if defined(MULTIVIEW_ENABLED)
    extern void VR_Log(const char* fmt, ...);
    static int clearMVActiveCount = 0;
    clearMVActiveCount++;
    if (clearMVActiveCount <= 60 || clearMVActiveCount % 600 == 0) {
        VR_Log("std3D_ClearMultiViewActive #%d: %d -> 0\n", clearMVActiveCount, std3D_multiViewActive);
    }
    std3D_multiViewActive = 0;
#endif
}


// Set per-eye HUD horizontal shift (NDC) = forward-centering + depth convergence.
void std3D_SetVRHudOffset(float eye0, float eye1)
{
#if defined(MULTIVIEW_ENABLED)
    std3D_vrHudOffset[0] = eye0;
    std3D_vrHudOffset[1] = eye1;

    // Push into the HudOffsets UBO (vec4[2], .x = per-eye shift). Orphan before subdata for
    // the same tiled-GPU race-avoidance as the matrix UBOs. The buffer stays bound to point 2
    // (std3D_BindMultiViewUBOs at load), so the menu/ui MultiView programs pick this up.
    if (std3D_multiViewUBOInitted) {
        float hud[8] = { eye0, 0.0f, 0.0f, 0.0f, eye1, 0.0f, 0.0f, 0.0f };
        glBindBuffer(GL_UNIFORM_BUFFER, std3D_hudOffsetUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(hud), NULL, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(hud), hud);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }
#endif
}

// ============================================================================
// MultiView Matrix Update for Single-Pass Stereo Rendering
// ============================================================================

#if defined(MULTIVIEW_ENABLED)
void std3D_UpdateMultiViewMatrices(float* viewMatrices, float* projMatrices)
{
    if (!std3D_multiViewUBOInitted) {
        stdPlatform_Printf("std3D_UpdateMultiViewMatrices: UBOs not initialized\n");
        return;
    }

    // Update view matrices UBO (2 x mat4 = 128 bytes)
    // Use buffer orphaning (glBufferData with NULL) before glBufferSubData to avoid
    // race conditions on tiled GPUs where the GPU may still be reading the old data
    glBindBuffer(GL_UNIFORM_BUFFER, std3D_viewMatricesUBO);
    glBufferData(GL_UNIFORM_BUFFER, 2 * 16 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 2 * 16 * sizeof(float), viewMatrices);

    // Update projection matrices UBO (2 x mat4 = 128 bytes)
    glBindBuffer(GL_UNIFORM_BUFFER, std3D_projMatricesUBO);
    glBufferData(GL_UNIFORM_BUFFER, 2 * 16 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 2 * 16 * sizeof(float), projMatrices);

    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void std3D_BindMultiViewUBOs(GLuint program)
{
    if (!std3D_multiViewUBOInitted) {
        return;
    }

    // Get uniform block indices
    GLuint viewBlockIndex = glGetUniformBlockIndex(program, "ViewMatrices");
    GLuint projBlockIndex = glGetUniformBlockIndex(program, "ProjectionMatrices");

    if (viewBlockIndex != GL_INVALID_INDEX) {
        glUniformBlockBinding(program, viewBlockIndex, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, std3D_viewMatricesUBO);
    }

    if (projBlockIndex != GL_INVALID_INDEX) {
        glUniformBlockBinding(program, projBlockIndex, 1);
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, std3D_projMatricesUBO);
    }

    // HudOffsets is present only in the menu/ui MultiView programs; a no-op for the scene
    // programs (returns GL_INVALID_INDEX). Binding point 2 (0=view, 1=proj).
    GLuint hudBlockIndex = glGetUniformBlockIndex(program, "HudOffsets");
    if (hudBlockIndex != GL_INVALID_INDEX) {
        glUniformBlockBinding(program, hudBlockIndex, 2);
        glBindBufferBase(GL_UNIFORM_BUFFER, 2, std3D_hudOffsetUBO);
    }
}
#else
// Stub for non-Quest platforms
void std3D_UpdateMultiViewMatrices(float* viewMatrices, float* projMatrices)
{
    (void)viewMatrices;
    (void)projMatrices;
}

void std3D_BindMultiViewUBOs(GLuint program)
{
    (void)program;
}
#endif // MULTIVIEW_ENABLED

#endif // PLATFORM_VR

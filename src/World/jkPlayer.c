#include "jkPlayer.h"

#include <math.h>
#include "General/stdString.h"
#include "General/stdFnames.h"
#include "General/stdFileUtil.h"
#include "Engine/sithAnimClass.h"
#include "Dss/sithGamesave.h"
#include "Engine/rdPuppet.h"
#include "Gameplay/sithTime.h"
#include "Engine/sithCamera.h"
#include "Raster/rdCache.h"
#include "Engine/rdPuppet.h"
#include "Engine/rdCamera.h"
#include "Engine/rdroid.h"
#include "Engine/rdColormap.h"
#include "World/sithTemplate.h"
#include "General/stdMath.h"
#include "Gameplay/sithInventory.h"
#include "Gameplay/jkSaber.h"
#include "World/sithThing.h"
#include "Gameplay/sithPlayer.h"
#include "World/sithWeapon.h"
#include "World/sithWorld.h"
#include "World/sithModel.h"
#include "World/sithSector.h"
#include "Primitives/rdMatrix.h"
#include "Devices/sithControl.h"
#include "Main/jkHudInv.h"
#include "Main/jkGame.h"
#include "jk.h"
#include "Win95/Window.h"
#include "General/stdJSON.h"
#include "Platform/std3D.h"
#include "Main/sithCvar.h"
#ifdef PLATFORM_VR
#include "Platform/VR/stdVR.h"
#include "Platform/VR/stdVR_WeaponWheel.h"
#endif

// DSi has *plenty* of time to read the text.
#ifdef TARGET_TWL
#define FAST_MISSION_TEXT_DEFAULT (1)
#else
#define FAST_MISSION_TEXT_DEFAULT (0)
#endif

// DO NOT FORGET TO ADD TO jkPlayer_ResetVars()
#ifdef QOL_IMPROVEMENTS
int Window_isHiDpi_tmp = 0;
int Window_isFullscreen_tmp = 0;
int jkPlayer_fov = 90;
int jkPlayer_fovIsVertical = 1;
int jkPlayer_enableTextureFilter = 0;
int jkPlayer_enableOrigAspect = 0;
int jkPlayer_enableBloom = 0;
int jkPlayer_enableSSAO = 0;
int jkPlayer_fpslimit = 0;
int jkPlayer_enableVsync = 1;
flex_t jkPlayer_ssaaMultiple = 1.0;
flex_t jkPlayer_gamma = 1.0;
int jkPlayer_bEnableJkgm = 1;
int jkPlayer_bEnableTexturePrecache = 1;
int jkPlayer_bKeepCorpses = 0;
int jkPlayer_bFastMissionText = FAST_MISSION_TEXT_DEFAULT;
int jkPlayer_bUseOldPlayerPhysics = 0;
flex_t jkPlayer_hudScale = 2.0;
flex_t jkPlayer_crosshairLineWidth = 1.0;
flex_t jkPlayer_crosshairScale = 1.0;
flex_t jkPlayer_canonicalCogTickrate = CANONICAL_COG_TICKRATE;
flex_t jkPlayer_canonicalPhysTickrate = CANONICAL_PHYS_TICKRATE;

int jkPlayer_setCrosshairOnLightsaber = 1;
int jkPlayer_setCrosshairOnFist = 1;
int jkPlayer_bDisableWeaponWaggle = 0;
int jkPlayer_bHasLoadedSettingsOnce = 0;
int jkPlayer_bEnableEmissiveTextures = 1;
int jkPlayer_bEnableClassicLighting = 0;
#endif

#ifdef FIXED_TIMESTEP_PHYS
int jkPlayer_bJankyPhysics = 0;
#endif

// Added: VR settings
#ifdef PLATFORM_VR
int jkPlayer_vrEnabled = 0;
int jkPlayer_vrSnapTurnAngle = 45;       // 0 = smooth turn, 30/45/90 = snap turn degrees
int jkPlayer_vrSmoothTurnSpeed = 120;   // degrees per second
int jkPlayer_vrWeaponPitchAdjust = 0.f;
float jkPlayer_vrHeightOffset = 0.0f;   // Player height offset in meters
int jkPlayer_vrWeaponCrosshair = 0;     // Show in-world weapon crosshair (0/1)
int jkPlayer_vrDominantHand = 1;        // 0=left, 1=right
int jkPlayer_vrSwapSticks = 0;          // Move on the right stick, turn on the left
int jkPlayer_vrPromptsShown = 0;        // Bitmask of instructional prompts already taught
int jkPlayer_vrAlignTool = 0;           // Weapon alignment tool active (dev tool, cvar-driven)
float jkPlayer_vrRefreshRate = 0.0f;    // Display refresh rate in Hz (0 = keep the runtime default)
int jkPlayer_vrMoveDirection = 1;       // 0=head, 1=controller
float jkPlayer_vrSupersampling = 1.0f;  // VR render scale multiplier
float jkPlayer_vr6DoFScale = 1.0f;      // 6DoF head->body movement scale (0 = disabled / camera-float)
int jkPlayer_vrCameraInterp = 1;        // Interpolate the camera across the fixed physics step
// Quake-style ground movement for the local player: short accel and decel ramps instead of
// the stock drag-limited ramp + hard static-drag stop. See sithPhysics_VRQuakeGroundMove.
int jkPlayer_vrMoveQuakeFeel = 1;       // 0 = stock JK thrust/drag movement
flex_t jkPlayer_vrMoveAccel = 14.0;     // Approach rate toward top speed (1/sec)
flex_t jkPlayer_vrMoveFriction = 10.0;  // Slow-down rate when releasing the stick (1/sec)
flex_t jkPlayer_vrMoveStopFrac = 0.35;  // Below this fraction of top speed, decel goes linear
// Baked-HUD layout (multiview eye buffer), tunable in the VR Options menu. Half-extents and
// centre are in NDC; depth is the virtual distance in metres for the per-eye convergence.
float jkPlayer_vrHudWidth  = 0.60f;     // HUD half-width  in NDC (smaller = narrower)
float jkPlayer_vrHudHeight = 0.60f;     // HUD half-height in NDC (smaller = shorter)
// PosX/PosY defaults differ per platform (standalone headsets sit the HUD centred and higher).
#if defined(TARGET_ANDROID_NATIVE_GLES)
float jkPlayer_vrHudPosX   = 0.00f;     // Standalone (Quest): horizontally centred
float jkPlayer_vrHudPosY   = -0.25f;    // Standalone (Quest): higher
#else
float jkPlayer_vrHudPosX   = 0.25f;     // PCVR: HUD horizontal centre in NDC (+ = right)
float jkPlayer_vrHudPosY   = -0.50f;    // PCVR: HUD vertical centre in NDC (- = lower)
#endif
float jkPlayer_vrHudDepth  = 0.40f;     // HUD virtual depth in metres (0 = infinity)
static rdModel3* pVRFistsModel3 = NULL;
static rdThing vrOffhandThing;
static int bVROffhandReady = 0;
static rdVector3 vrOffhandLhandOffset = {0}; // Cached K_Lhand bind-pose offset for centering
// Model that vrOffhandThing is bound to. Normally the fists model, but some weapons carry a
// better off-hand in their own POV model, so the binding can change while playing.
static rdModel3* pVROffhandBoundModel = NULL;
// K_Lhand node index inside pVROffhandBoundModel. It differs per model: 2 in the fists, 3 in
// the sequencer, and the gun models have no left hand at all.
static int vrOffhandLhandNodeIdx = -1;

// VR "hands only" rendering: show just the hand by suppressing the meshes of its ancestor
// arm nodes (shoulder/upper-arm/forearm). Amputation can't do this because it hides a node AND
// its whole subtree, and the hand is a descendant of the arm; but the draw only skips a node's
// own mesh when meshIdx == -1 (it still recurses into children), so temporarily clearing each
// ancestor's meshIdx hides the arm while keeping the hand (and fingers) visible.
#define JKPLAYER_VR_MAX_ARM_NODES 16

// Scale for the bare off-hand model. 0.9 makes it 10% smaller than the weapon hand.
#define JKPLAYER_VR_OFFHAND_SCALE (0.9f)
typedef struct { rdHierarchyNode* node; int meshIdx; } jkPlayerVRSavedMesh;

// Find a hierarchy node by name. The hand node index is NOT the same in every POV model:
// the fists put K_Rhand at 5, the pistols and rifles at 2, the sequencer at 7. Hardcoding an
// index hid nothing on the gun models, which have only 4 nodes.
static int jkPlayer_VRFindNodeByName(rdModel3* model, const char* pName)
{
    if (!model || !pName) return -1;

    for (int i = 0; i < model->numHierarchyNodes; i++) {
        if (!__strcmpi(model->hierarchyNodes[i].name, pName)) {
            return i;
        }
    }

    return -1;
}

static int jkPlayer_VRHideArmKeepHand(rdModel3* model, int handNodeIdx, jkPlayerVRSavedMesh* saved)
{
    int n = 0;
    if (!model || handNodeIdx < 0 || handNodeIdx >= model->numHierarchyNodes) return 0;
    rdHierarchyNode* node = model->hierarchyNodes[handNodeIdx].parent;
    while (node && n < JKPLAYER_VR_MAX_ARM_NODES) {
        if (node->meshIdx != -1) {
            saved[n].node = node;
            saved[n].meshIdx = node->meshIdx;
            node->meshIdx = -1;  // suppress this arm segment's mesh for the upcoming draw
            n++;
        }
        node = node->parent;
    }
    return n;
}

static void jkPlayer_VRRestoreArmMeshes(jkPlayerVRSavedMesh* saved, int n)
{
    for (int i = 0; i < n; i++) {
        saved[i].node->meshIdx = saved[i].meshIdx;
    }
}
#endif // PLATFORM_VR

#ifdef JKM_DSS
jkPlayerInfo jkPlayer_aMotsInfos[NUM_JKPLAYER_THINGS] = {0};
jkBubbleInfo jkPlayer_aBubbleInfo[NUM_JKPLAYER_THINGS] = {0};
int jkPlayer_personality = 0;
flex_t jkPlayer_aMultiParams[0x100];
#endif

int jkPlayer_aMotsFpBins[74] =
{
    // Category 1
    SITHBIN_F_JUMP,
    SITHBIN_F_PROJECT,
    SITHBIN_F_SEEING,
    SITHBIN_F_SPEED,
    SITHBIN_F_PUSH,
    0,
    0,
    0,
    
    // Category 2
    SITHBIN_F_PULL,
    SITHBIN_F_SABERTHROW,
    SITHBIN_F_GRIP,
    SITHBIN_F_FARSIGHT,
    0,
    0,
    0,
    0,
    
    // Category 3
    SITHBIN_F_PERSUASION,
    SITHBIN_F_HEALING,
    SITHBIN_F_BLINDING,
    SITHBIN_F_CHAINLIGHT,
    0,
    0,
    0,
    0,
    
    // Category 4
    SITHBIN_F_ABSORB,
    SITHBIN_F_DESTRUCTION,
    SITHBIN_F_DEADLYSIGHT,
    SITHBIN_F_PROTECTION,
    0,
    0,
    0,
    0,
    
    //
    // Category amounts per rank
    //

    // Rank 0
    0, 0, 0, 0,
    
    // Rank 1
    2, 0, 0, 0,
    
    // Rank 2
    2, 1, 0, 0,
    
    // Rank 3
    3, 1, 0, 0,
    
    // Rank 4
    4, 1, 0, 0,
    
    // Rank 5
    4, 2, 1, 0,
    
    // Rank 6
    4, 2, 2, 0,
    
    // Rank 7
    4, 2, 2, 1,
    
    // Rank 8
    4, 2, 2, 2,
    
    // Something else?
    3, 2, 1, 0,
    
    // End
    -1,
    0
};

// Added: cvars
void jkPlayer_StartupVars()
{
    sithCvar_RegisterInt("r_fov",                       90,                         &jkPlayer_fov,                      CVARFLAG_LOCAL);
    sithCvar_RegisterBool("r_fovIsVertical",            1,                          &jkPlayer_fovIsVertical,            CVARFLAG_LOCAL);
    sithCvar_RegisterBool("r_enableTextureFilter",      0,                          &jkPlayer_enableTextureFilter,      CVARFLAG_LOCAL);
    sithCvar_RegisterBool("r_enableOrigAspect",         0,                          &jkPlayer_enableOrigAspect,         CVARFLAG_LOCAL);
    sithCvar_RegisterBool("r_enableBloom",              0,                          &jkPlayer_enableBloom,              CVARFLAG_LOCAL);
    sithCvar_RegisterBool("r_enableSSAO",               0,                          &jkPlayer_enableSSAO,               CVARFLAG_LOCAL);
    sithCvar_RegisterInt("r_fpslimit",                  0,                          &jkPlayer_fpslimit,                 CVARFLAG_LOCAL);
    sithCvar_RegisterBool("r_enableVsync",              1,                          &jkPlayer_enableVsync,              CVARFLAG_LOCAL);
    sithCvar_RegisterFlex("r_ssaaMultiple",             1.0,                        &jkPlayer_ssaaMultiple,             CVARFLAG_LOCAL);
    sithCvar_RegisterFlex("r_gamma",                    1.0,                        &jkPlayer_gamma,                    CVARFLAG_LOCAL);
    sithCvar_RegisterBool("r_bEnableJkgm",              1,                          &jkPlayer_bEnableJkgm,              CVARFLAG_LOCAL|CVARFLAG_READONLY);
    sithCvar_RegisterBool("r_bEnableTexturePrecache",   1,                          &jkPlayer_bEnableTexturePrecache,   CVARFLAG_LOCAL|CVARFLAG_READONLY);
    sithCvar_RegisterBool("g_bKeepCorpses",             0,                          &jkPlayer_bKeepCorpses,             CVARFLAG_LOCAL);
    sithCvar_RegisterBool("menu_bFastMissionText",      FAST_MISSION_TEXT_DEFAULT,  &jkPlayer_bFastMissionText,         CVARFLAG_LOCAL);
    sithCvar_RegisterBool("g_bUseOldPlayerPhysics",     0,                          &jkPlayer_bUseOldPlayerPhysics,     CVARFLAG_LOCAL);
    sithCvar_RegisterFlex("hud_scale",                  2.0,                        &jkPlayer_hudScale,                 CVARFLAG_LOCAL|CVARFLAG_RESETHUD);
    sithCvar_RegisterFlex("hud_crosshairLineWidth",     1.0,                        &jkPlayer_crosshairLineWidth,       CVARFLAG_LOCAL|CVARFLAG_RESETHUD);
    sithCvar_RegisterFlex("hud_crosshairScale",         1.0,                        &jkPlayer_crosshairScale,           CVARFLAG_LOCAL|CVARFLAG_RESETHUD);
    sithCvar_RegisterBool("hud_setCrosshairOnLightsaber", 1,                        &jkPlayer_setCrosshairOnLightsaber, CVARFLAG_LOCAL);
    sithCvar_RegisterBool("hud_setCrosshairOnFist",     1,                          &jkPlayer_setCrosshairOnFist,       CVARFLAG_LOCAL);
    sithCvar_RegisterBool("hud_disableWeaponWaggle",    0,                          &jkPlayer_bDisableWeaponWaggle,     CVARFLAG_LOCAL);
    sithCvar_RegisterBool("hud_disablePovShake",       1,                          &sithCamera_bDisablePovShake,       CVARFLAG_LOCAL);
    sithCvar_RegisterFlex("g_canonicalCogTickrate",     CANONICAL_COG_TICKRATE,     &jkPlayer_canonicalCogTickrate,     CVARFLAG_LOCAL);
    sithCvar_RegisterFlex("g_canonicalPhysTickrate",    CANONICAL_PHYS_TICKRATE,    &jkPlayer_canonicalPhysTickrate,    CVARFLAG_LOCAL);

    sithCvar_RegisterBool("r_hidpi",                     0,                         &Window_isHiDpi_tmp,                CVARFLAG_LOCAL|CVARFLAG_READONLY);
    sithCvar_RegisterBool("r_fullscreen",                0,                         &Window_isFullscreen_tmp,           CVARFLAG_LOCAL|CVARFLAG_READONLY);

    // TODO: port to SDL
#ifdef TARGET_TWL
    sithCvar_RegisterBool("r_emissiveTextures",          0,                         &jkPlayer_bEnableEmissiveTextures,  CVARFLAG_LOCAL|CVARFLAG_READONLY);
    sithCvar_RegisterBool("r_classicLighting",           0,                         &jkPlayer_bEnableClassicLighting,   CVARFLAG_LOCAL|CVARFLAG_READONLY);
#endif

#ifdef FIXED_TIMESTEP_PHYS
    sithCvar_RegisterBool("g_bJankyPhysics",             0,                         &jkPlayer_bJankyPhysics,            CVARFLAG_LOCAL);
#endif

#ifdef PLATFORM_VR
    // Smooths the fixed-step physics staircase at the 72/90Hz VR refresh (sithCamera.c)
    sithCvar_RegisterBool("g_vrCameraInterp",            1,                         &jkPlayer_vrCameraInterp,           CVARFLAG_LOCAL);
    sithCvar_RegisterBool("g_vrSwapSticks",              0,                         &jkPlayer_vrSwapSticks,             CVARFLAG_LOCAL);
#ifdef VR_WEAPON_ALIGNMENT_TOOL
    // Dev tool: 1 opens the weapon alignment editor, 0 closes it AND writes the offsets JSON
    sithCvar_RegisterBool("g_vrAlignTool",               0,                         &jkPlayer_vrAlignTool,              CVARFLAG_LOCAL);
#endif
    // Quake-style ground movement ramps (sithPhysics_VRQuakeGroundMove)
    sithCvar_RegisterBool("g_vrMoveQuakeFeel",           1,                         &jkPlayer_vrMoveQuakeFeel,          CVARFLAG_LOCAL);
    sithCvar_RegisterFlex("g_vrMoveAccel",               14.0,                      &jkPlayer_vrMoveAccel,              CVARFLAG_LOCAL);
    sithCvar_RegisterFlex("g_vrMoveFriction",            10.0,                      &jkPlayer_vrMoveFriction,           CVARFLAG_LOCAL);
    sithCvar_RegisterFlex("g_vrMoveStopFrac",            0.35,                      &jkPlayer_vrMoveStopFrac,           CVARFLAG_LOCAL);
#endif
}

// Added: Clean reset
void jkPlayer_ResetVars()
{
#ifdef QOL_IMPROVEMENTS
    Window_isHiDpi_tmp = 0;
    Window_isFullscreen_tmp = 0;
    jkPlayer_fov = 90;
    jkPlayer_fovIsVertical = 1;
    jkPlayer_enableTextureFilter = 0;
    jkPlayer_enableOrigAspect = 0;
    jkPlayer_enableBloom = 0;
    jkPlayer_enableSSAO = 0;
    jkPlayer_fpslimit = 0;
    jkPlayer_enableVsync = 1;
    jkPlayer_ssaaMultiple = 1.0;
    jkPlayer_gamma = 1.0;
    jkPlayer_bEnableJkgm = 1;
    jkPlayer_bEnableTexturePrecache = 1;
    jkPlayer_bKeepCorpses = 0;
    jkPlayer_bFastMissionText = 0;
    jkPlayer_bUseOldPlayerPhysics = 0;
    jkPlayer_hudScale = 2.0;
    jkPlayer_crosshairLineWidth = 1.0;
    jkPlayer_crosshairScale = 1.0;
    jkPlayer_canonicalCogTickrate = CANONICAL_COG_TICKRATE;
    jkPlayer_canonicalPhysTickrate = CANONICAL_PHYS_TICKRATE;

    jkPlayer_setCrosshairOnLightsaber = 1;
    jkPlayer_setCrosshairOnFist = 1;
    jkPlayer_bDisableWeaponWaggle = 0;
    sithCamera_bDisablePovShake = 1;

    jkPlayer_bHasLoadedSettingsOnce = 0;
#endif

#ifdef FIXED_TIMESTEP_PHYS
    jkPlayer_bJankyPhysics = 0;
#endif

#ifdef PLATFORM_VR
    jkPlayer_vrEnabled = 0;
    jkPlayer_vrSnapTurnAngle = 45;        // 0 = smooth turn
    jkPlayer_vrSmoothTurnSpeed = 120;
    jkPlayer_vrWeaponPitchAdjust = 0;
    jkPlayer_vrHeightOffset = 0.0f;
    jkPlayer_vrDominantHand = 1;
    jkPlayer_vrSwapSticks = 0;
    jkPlayer_vrPromptsShown = 0;
    jkPlayer_vrRefreshRate = 0.0f;
    jkPlayer_vrMoveDirection = 1;
    // Added: PCVR has the GPU headroom, so default to the top of the slider. Standalone
    // must protect its frame budget.
#if defined(TARGET_ANDROID_NATIVE_GLES)
    jkPlayer_vrSupersampling = 1.0f;
#else
    jkPlayer_vrSupersampling = 1.25f;
#endif
    jkPlayer_vr6DoFScale = 1.0f;
    jkPlayer_vrCameraInterp = 1;
    jkPlayer_vrMoveQuakeFeel = 1;
    jkPlayer_vrMoveAccel = 14.0;
    jkPlayer_vrMoveFriction = 10.0;
    jkPlayer_vrMoveStopFrac = 0.35;
    jkPlayer_vrHudWidth  = 0.60f;
    jkPlayer_vrHudHeight = 0.60f;
#if defined(TARGET_ANDROID_NATIVE_GLES)
    jkPlayer_vrHudPosX   = 0.00f;   // Standalone (Quest): centred
    jkPlayer_vrHudPosY   = -0.25f;  // Standalone (Quest): higher
#else
    jkPlayer_vrHudPosX   = 0.25f;   // PCVR
    jkPlayer_vrHudPosY   = -0.50f;  // PCVR
#endif
    jkPlayer_vrHudDepth  = 0.40f;
#endif

#ifdef JKM_DSS
    memset(jkPlayer_aMotsInfos, 0, sizeof(jkPlayer_aMotsInfos));
    memset(jkPlayer_aBubbleInfo, 0, sizeof(jkPlayer_aBubbleInfo));
    jkPlayer_personality = 0;
    memset(jkPlayer_aMultiParams, 0, sizeof(jkPlayer_aMultiParams));
#endif
}

int jkPlayer_LoadAutosave()
{
    char tmp[128];

    jkPlayer_bLoadingSomething = 1;
    stdString_snprintf(tmp, 128, "%s%s", "_JKAUTO_", sithWorld_pCurrentWorld->map_jkl_fname);
    stdFnames_ChangeExt(tmp, "jks");
    return sithGamesave_Load(tmp, 0, 0);
}

int jkPlayer_LoadSave(char *path)
{
    jkPlayer_bLoadingSomething = 1;
    return sithGamesave_Load(path, 0, 1);
}

#ifdef PLATFORM_VR
// Added: sweep away profile directories whose names carry stray bytes.
//
// stdJSON_GetString used to return the stored player name without a null terminator, so
// startup read "VRPlayer" plus whatever followed it in the caller's buffer and created a
// directory under that name. That is fixed at the source now; this clears up what it left.
//
// Only partly, though, and the limit is not ours. Android serves /sdcard through a FUSE layer
// that reconstructs the path as a Java string. A path holding bytes that are not valid UTF-8
// no longer matches what is on disk, so every unlink under such a directory returns ENOENT.
// rmdir on an empty one still works, so this removes the empty directories and leaves the rest.
// MANAGE_EXTERNAL_STORAGE does not help - measured on a Quest 3, this app fails to unlink those
// files exactly as adb does. Removing them needs root or a factory reset.
//
// The test is the game's own name validator, so a directory is only removed when the game
// itself would refuse to create a profile with that name. Accented names stay.
static void jkPlayer_VRCleanCorruptProfileDirs(const char* pParent)
{
    stdFileSearch* pSearch = stdFileUtil_NewFind(pParent, 2, NULL);
    if (!pSearch) {
        return;
    }

    // scandir() snapshots the whole directory up front, so deleting while iterating is safe.
    stdFileSearchResult res;
    while (stdFileUtil_FindNext(pSearch, &res)) {
        if (!res.is_subdirectory || res.fpath[0] == '.') {
            continue;
        }

        wchar_t wName[256];
        _memset(wName, 0, sizeof(wName));
        stdString_CharToWchar(wName, res.fpath, 255);
        wName[255] = 0;

        // Test each character on its own, wrapped in filler. jkPlayer_VerifyWcharName also
        // rejects a name for where its spaces sit (leading, trailing, all-space), and an old
        // profile could trip that without being corrupt. The filler neutralises those rules
        // so only a genuinely disallowed character counts.
        int bCorrupt = 0;
        for (int i = 0; wName[i]; i++) {
            wchar_t probe[4] = { 'a', wName[i], 'a', 0 };
            if (!jkPlayer_VerifyWcharName(probe)) {
                bCorrupt = 1;
                break;
            }
        }

        if (!bCorrupt) {
            continue;
        }

        char path[256];
        stdFnames_MakePath(path, sizeof(path), pParent, res.fpath);
        stdPlatform_Printf("jkPlayer: removing corrupt profile dir '%s'\n", path);
        stdFileUtil_Deltree(path);
    }

    stdFileUtil_DisposeFind(pSearch);
}

void jkPlayer_VRCleanupProfiles(void)
{
    static int bDone = 0;
    if (bDone) {
        return;
    }
    bDone = 1;

    jkPlayer_VRCleanCorruptProfileDirs("player");

    // Legacy: some installs have a hand-made "player_garbage_archive" holding nothing but
    // these undeletable directories. Empty it and drop it. Both calls are no-ops when the
    // folder is absent. Safe to delete this block once no headset still carries one.
    jkPlayer_VRCleanCorruptProfileDirs("player_garbage_archive");
    stdFileUtil_Deltree("player_garbage_archive");
}
#endif // PLATFORM_VR

void jkPlayer_Startup()
{
#ifdef PLATFORM_VR
    // Runs before any profile is read or created. Guarded internally to run once.
    jkPlayer_VRCleanupProfiles();
#endif
    jkPlayer_InitThings();
    _memcpy(&jkSaber_rotateMat, &rdroid_identMatrix34, sizeof(jkSaber_rotateMat));
}

// MOTS altered, also fixed the memleak lol
void jkPlayer_Shutdown()
{
    for (int i = 0; i < jkPlayer_numThings; i++ )
    {
        rdPolyLine_FreeEntry(&playerThings[i].polyline); // Added: prevent memleak

        if (playerThings[i].polylineThing.model3)
        {
            rdThing_FreeEntry(&playerThings[i].polylineThing);
            playerThings[i].polylineThing.model3 = 0;
        }

        rdThing_FreeEntry(&playerThings[i].povModel); // Added: prevent memleak

        rdThing_FreeEntry(&playerThings[i].rd_thing); // Added: fix memleak
    }
    _memset(playerThings, 0, sizeof(playerThings));
    
    for (int i = 0; i < jkPlayer_numOtherThings; i++)
    {
        if (jkPlayer_otherThings[i].polylineThing.model3)
        {
            rdThing_FreeEntry(&jkPlayer_otherThings[i].polylineThing);
            jkPlayer_otherThings[i].polylineThing.model3 = 0;
        }
    }
    _memset(jkPlayer_otherThings, 0, sizeof(jkPlayer_otherThings));

#ifdef JKM_DSS
    for (int i = 0; i < NUM_JKPLAYER_THINGS; i++)
    {
        rdPolyLine_FreeEntry(&jkPlayer_aMotsInfos[i].polyline); // Added: prevent memleak

        if (jkPlayer_aMotsInfos[i].polylineThing.model3)
        {
            rdThing_FreeEntry(&jkPlayer_otherThings[i].polylineThing);
            jkPlayer_aMotsInfos[i].polylineThing.model3 = 0;
        }

        rdThing_FreeEntry(&jkPlayer_aMotsInfos[i].povModel); // Added: prevent memleak

        rdThing_FreeEntry(&jkPlayer_aMotsInfos[i].rd_thing); // Added: fix memleak
    }
    _memset(jkPlayer_aMotsInfos, 0, sizeof(jkPlayer_aMotsInfos));
#endif
    //nullsub_28_free();

#ifdef PLATFORM_VR
    if (bVROffhandReady) {
        rdThing_FreeEntry(&vrOffhandThing);
        bVROffhandReady = 0;
    }
    pVRFistsModel3 = NULL;
    pVROffhandBoundModel = NULL;
    vrOffhandLhandNodeIdx = -1;
    stdVR_WeaponWheel_ResetCache();
#endif
}

void jkPlayer_Open()
{
    // MOTS added
    if (sithPlayer_pLocalPlayerThing && sithPlayer_pLocalPlayerThing->playerInfo) {
        sithPlayer_pLocalPlayerThing->playerInfo->personality = jkPlayer_personality;
    }
}

void jkPlayer_Close()
{
}

// MOTS altered
void jkPlayer_InitSaber()
{
    jkPlayer_numThings = jkPlayer_maxPlayers;
    for (int i = 0; i < jkPlayer_maxPlayers; i++)
    {
        jkPlayerInfo* playerInfoJk = &playerThings[i];
        sithPlayerInfo* playerInfo = &jkPlayer_playerInfos[i];

        playerInfoJk->actorThing = playerInfo->playerThing;
        if (playerInfo->playerThing) // Added
            playerInfo->playerThing->playerInfo = playerInfoJk;
        playerInfoJk->maxTwinkles = 8;
        playerInfoJk->twinkleSpawnRate = 16;
        playerInfoJk->bHasSuperWeapon = 0;
        playerInfoJk->bHasSuperShields = 0;
        if (playerInfo->playerThing) // Added
            playerInfo->playerThing->thingflags |= SITH_TF_RENDERWEAPON;
        playerInfoJk->bHasForceSurge = 0;
        
        // MOTS added
#ifdef JKM_DSS
        playerInfoJk->jkmUnk4 = 0;
        playerInfoJk->jkmUnk5 = 0;
        playerInfoJk->jkmUnk6 = 0;
#endif

        sithThing* saberSparks = sithTemplate_GetEntryByName("+ssparks_saber");
        sithThing* bloodSparks = sithTemplate_GetEntryByName("+ssparks_blood");
        sithThing* wallSparks = sithTemplate_GetEntryByName("+ssparks_wall");
        
        jkSaber_InitializeSaberInfo(playerThings[i].actorThing, "sabergreen1.mat", "sabergreen0.mat", 0.0032, 0.0018, 0.12, wallSparks, bloodSparks, saberSparks);
    }
}

void jkPlayer_InitThings()
{
    jkPlayer_numThings = jkPlayer_maxPlayers;
    for (int i = 0; i < jkPlayer_maxPlayers; i++)
    {
        jkPlayerInfo* playerInfoJk = &playerThings[i];
        sithPlayerInfo* playerInfo = &jkPlayer_playerInfos[i];

        playerInfoJk->actorThing = playerInfo->playerThing;

        // Added: Possible nullptr deref in co-op? wtf is this loop doing anyhow
        if (playerInfo->playerThing)
        {
            playerInfo->playerThing->playerInfo = playerInfoJk;
            playerInfo->playerThing->thingflags |= SITH_TF_RENDERWEAPON;
        }
    }

    int num = 0;
    jkPlayer_numOtherThings = 0;

    // Added: Properly serialize sabers.
#ifdef QOL_IMPROVEMENTS
    for (int i = 0; i < sithWorld_pCurrentWorld->numThingsLoaded; i++)
    {
        sithThing* thingIter = &sithWorld_pCurrentWorld->things[i];

        if (thingIter->type == SITH_THING_ACTOR 
            && thingIter->actorParams.typeflags & SITH_AF_BOSS 
            && thingIter->playerInfo )
        {
            thingIter->playerInfo->actorThing = thingIter;
            thingIter->playerInfo->rd_thing.model3 = 0;
            thingIter->thingflags |= SITH_TF_RENDERWEAPON;

            jkPlayer_numOtherThings++;
            num++;
        }
    }
#endif
    
    // Added: skip already initted
    jkPlayerInfo* playerInfoIter = &jkPlayer_otherThings[jkPlayer_numOtherThings];
    for (int i = 0; i < sithWorld_pCurrentWorld->numThingsLoaded; i++)
    {
        sithThing* thingIter = &sithWorld_pCurrentWorld->things[i];

        if (thingIter->type == SITH_THING_ACTOR 
            && thingIter->actorParams.typeflags & SITH_AF_BOSS 
            && playerInfoIter < &jkPlayer_otherThings[NUM_JKPLAYER_THINGS] // off by one?
            && !thingIter->playerInfo // Added: skip already initted
            ) 
        {
            playerInfoIter->actorThing = thingIter;
            thingIter->playerInfo = playerInfoIter;
            playerInfoIter->rd_thing.model3 = 0;
            thingIter->thingflags |= SITH_TF_RENDERWEAPON;

            // MOTS added: weird hack?
            if (Main_bMotsCompat && !playerInfoIter->polylineThing.polyline) {
                sithThing* saberSparks = sithTemplate_GetEntryByName("+ssparks_saber");
                sithThing* bloodSparks = sithTemplate_GetEntryByName("+ssparks_blood");
                sithThing* wallSparks = sithTemplate_GetEntryByName("+ssparks_wall");

                jkSaber_InitializeSaberInfo(thingIter, "saberred1.mat", "saberred0.mat", 0.0032, 0.0018, 0.12, wallSparks, bloodSparks, saberSparks);
            }

            playerInfoIter++;
            ++num;
        }
    }

    jkPlayer_numOtherThings = num;
}

void jkPlayer_nullsub_1(jkPlayerInfo* unk)
{
}

// MOTS altered? TODO
void jkPlayer_CreateConf(wchar_t *name)
{
    int v6; // ebp
    char *v7; // edi
    int *v8; // esi
    char *v9; // edi
    int *v10; // esi
    int v11; // [esp+10h] [ebp-144h]
    char a1[32]; // [esp+34h] [ebp-120h]
    char pathName[128]; // [esp+D4h] [ebp-80h]

#ifdef QOL_IMPROVEMENTS
    jkPlayer_ResetVars();
    sithCvar_ResetLocals();
#endif

    stdString_WcharToChar(a1, name, 31);
    a1[31] = 0;
    stdFileUtil_MkDir("player");
    stdFnames_MakePath(pathName, 128, "player", a1);
    stdFileUtil_MkDir(pathName);
    sithControl_InputInit();
    jkHudInv_InputInit();
    jkPlayer_SetRank(0);
    sithPlayer_SetBinAmt(SITHBIN_CHOICE, 0.0);
    sithWeapon_InitDefaults();
    jkGame_SetDefaultSettings();
    stdString_SafeWStrCopy(jkPlayer_playerShortName, name, 32);
    jkPlayer_setNumCutscenes = 0;
    v11 = sithControl_IsOpen();
    if ( v11 )
        sithControl_Close();
    jkPlayer_ReadConf(jkPlayer_playerShortName);

    if ( jkPlayer_setNumCutscenes <= 0 )
    {
LABEL_7:
        if ( jkPlayer_setNumCutscenes < 32 )
        {
            _strncpy(&jkPlayer_cutscenePath[32 * jkPlayer_setNumCutscenes], "01-02a.smk", 0x1Fu);
            jkPlayer_cutscenePath[32 * jkPlayer_setNumCutscenes + 31] = 0; // TODO macro 
            jkPlayer_aCutsceneVal[jkPlayer_setNumCutscenes] = 1;
            jkPlayer_setNumCutscenes = jkPlayer_setNumCutscenes + 1;
            jkPlayer_WriteConf(name);
            if ( v11 )
                sithControl_Open();
        }
    }
    else
    {
        char* pathIter = jkPlayer_cutscenePath;
        int count = 0;
        while ( 1 )
        {
            if ( !_memcmp(pathIter, "01-02a.smk", 0xBu) )
                break;
            ++count;
            pathIter += 32;
            if ( count >= jkPlayer_setNumCutscenes )
                goto LABEL_7;
        }
    }
    jkPlayer_WriteConf(name);
}

// MOTS altered
void jkPlayer_WriteConf(wchar_t *name)
{
    char nameTmp[32]; // [esp+0h] [ebp-A0h]
    char fpath[128]; // [esp+20h] [ebp-80h]
    char ext_fpath[256];
    char ext_fpath_cvars[256];

#ifdef QOL_IMPROVEMENTS
    sithCvar_SaveGlobals();
#endif

    if (!name || !name[0]) {
        printf("jkPlayer_WriteConf NULL name?\n");
        return; // Added
    }

    stdString_WcharToChar(nameTmp, name, 31);
    nameTmp[31] = 0;
    stdFnames_MakePath3(ext_fpath, 256, "player", nameTmp, "openjkdf2.json"); // Added
    stdFnames_MakePath3(ext_fpath_cvars, 256, "player", nameTmp, SITHCVAR_FNAME); // Added
    stdString_snprintf(fpath, 128, "player\\%s\\%s.plr", nameTmp, nameTmp);
    if ( stdConffile_OpenWriteBypass(fpath) )
    {
        stdConffile_Printf("version %d\n", 1);
        stdConffile_Printf("diff %d\n", jkPlayer_setDiff);
        jkPlayer_WriteOptionsConf();
        sithWeapon_WriteConf();
        sithControl_WriteConf();
        if ( stdConffile_Printf("numCutscenes %d\n", jkPlayer_setNumCutscenes) )
        {
            char* pathIter = jkPlayer_cutscenePath;
            for (int i = 0; i < jkPlayer_setNumCutscenes; i++)
            {
                if ( !stdConffile_Printf("%s %d\n", pathIter, jkPlayer_aCutsceneVal[i]) )
                    break;
                pathIter += 32;
            }
        }
#ifdef QOL_IMPROVEMENTS
        stdJSON_SaveInt(ext_fpath, "fov", jkPlayer_fov);
        stdJSON_SaveBool(ext_fpath, "fovisvertical", jkPlayer_fovIsVertical);
        stdJSON_SaveBool(ext_fpath, "windowishidpi", Window_isHiDpi);
        stdJSON_SaveBool(ext_fpath, "windowfullscreen", Window_isFullscreen);
        stdJSON_SaveBool(ext_fpath, "texturefiltering", jkPlayer_enableTextureFilter);
        stdJSON_SaveBool(ext_fpath, "originalaspect", jkPlayer_enableOrigAspect);
        stdJSON_SaveInt(ext_fpath, "fpslimit", jkPlayer_fpslimit);
        stdJSON_SaveBool(ext_fpath, "enablevsync", jkPlayer_enableVsync);
        stdJSON_SaveBool(ext_fpath, "enablebloom", jkPlayer_enableBloom);
        stdJSON_SaveFloat(ext_fpath, "ssaamultiple", jkPlayer_ssaaMultiple);
        stdJSON_SaveInt(ext_fpath, "enablessao", jkPlayer_enableSSAO);
        stdJSON_SaveFloat(ext_fpath, "gamma", jkPlayer_gamma);
#ifdef PLATFORM_VR
        stdJSON_SaveFloat(ext_fpath, "vrSupersampling", jkPlayer_vrSupersampling);
        stdJSON_SaveFloat(ext_fpath, "vr6DoFScale", jkPlayer_vr6DoFScale);
        stdJSON_SaveInt(ext_fpath, "vrSnapTurnAngle", jkPlayer_vrSnapTurnAngle);
        stdJSON_SaveInt(ext_fpath, "vrSmoothTurnSpeed", jkPlayer_vrSmoothTurnSpeed);
        stdJSON_SaveInt(ext_fpath, "vrWeaponPitchAdjust", jkPlayer_vrWeaponPitchAdjust);
        stdJSON_SaveInt(ext_fpath, "vrDominantHand", jkPlayer_vrDominantHand);
        stdJSON_SaveInt(ext_fpath, "vrSwapSticks", jkPlayer_vrSwapSticks);
        stdJSON_SaveInt(ext_fpath, "vrPromptsShown", jkPlayer_vrPromptsShown);
        stdJSON_SaveFloat(ext_fpath, "vrRefreshRate", jkPlayer_vrRefreshRate);
        stdJSON_SaveInt(ext_fpath, "vrWeaponCrosshair", jkPlayer_vrWeaponCrosshair);
        // Added: these three were modifiable in the VR options menu but never
        // written back, so they reverted to defaults every session.
        stdJSON_SaveInt(ext_fpath, "vrMoveDirection", jkPlayer_vrMoveDirection);
        stdJSON_SaveFloat(ext_fpath, "vrHeightOffset", jkPlayer_vrHeightOffset);
        stdJSON_SaveFloat(ext_fpath, "vrHudWidth", jkPlayer_vrHudWidth);
        stdJSON_SaveFloat(ext_fpath, "vrHudHeight", jkPlayer_vrHudHeight);
        stdJSON_SaveFloat(ext_fpath, "vrHudPosX", jkPlayer_vrHudPosX);
        stdJSON_SaveFloat(ext_fpath, "vrHudPosY", jkPlayer_vrHudPosY);
        stdJSON_SaveFloat(ext_fpath, "vrHudDepth", jkPlayer_vrHudDepth);
#endif
        stdJSON_SaveBool(ext_fpath, "bEnableJkgm", jkPlayer_bEnableJkgm);
        stdJSON_SaveBool(ext_fpath, "bEnableTexturePrecache", jkPlayer_bEnableTexturePrecache);
        stdJSON_SaveBool(ext_fpath, "bKeepCorpses", jkPlayer_bKeepCorpses);
        stdJSON_SaveBool(ext_fpath, "bFastMissionText", jkPlayer_bFastMissionText);
        stdJSON_SaveFloat(ext_fpath, "hudScale", jkPlayer_hudScale);
        stdJSON_SaveFloat(ext_fpath, "crosshairLineWidth", jkPlayer_crosshairLineWidth);
        stdJSON_SaveFloat(ext_fpath, "crosshairScale", jkPlayer_crosshairScale);
        stdJSON_SaveFloat(ext_fpath, "canonicalCogTickrate", jkPlayer_canonicalCogTickrate);
        stdJSON_SaveFloat(ext_fpath, "canonicalPhysTickrate", jkPlayer_canonicalPhysTickrate);

        stdJSON_SaveBool(ext_fpath, "bUseOldPlayerPhysics", jkPlayer_bUseOldPlayerPhysics);

        stdJSON_SaveBool(ext_fpath, "setCrosshairOnLightsaber", jkPlayer_setCrosshairOnLightsaber);
        stdJSON_SaveBool(ext_fpath, "setCrosshairOnFist", jkPlayer_setCrosshairOnFist);
        stdJSON_SaveBool(ext_fpath, "bDisableWeaponWaggle", jkPlayer_bDisableWeaponWaggle);
        stdJSON_SaveBool(ext_fpath, "bDisablePovShake", sithCamera_bDisablePovShake);
#endif
#ifdef FIXED_TIMESTEP_PHYS
        stdJSON_SaveBool(ext_fpath, "bJankyPhysics", jkPlayer_bJankyPhysics);
#endif

#ifdef QOL_IMPROVEMENTS
        Window_isHiDpi_tmp = Window_isHiDpi;
        Window_isFullscreen_tmp = Window_isFullscreen;
        sithCvar_SaveLocals(ext_fpath_cvars);
#endif

        stdConffile_CloseWrite();
    }
}

#ifdef QOL_IMPROVEMENTS
void jkPlayer_ParseLegacyExt()
{
    flex32_t ftmp;
    if (stdConffile_ReadLine())
    {
        _sscanf(stdConffile_aLine, "fov %d", &jkPlayer_fov);
    }

    if (stdConffile_ReadLine())
    {
        _sscanf(stdConffile_aLine, "fovisvertical %d", &jkPlayer_fovIsVertical);
        jkPlayer_fovIsVertical = !!jkPlayer_fovIsVertical;
    }

    int dpi_tmp = 0;
    if (stdConffile_ReadLine())
    {
        _sscanf(stdConffile_aLine, "windowishidpi %d", &dpi_tmp);
        dpi_tmp = !!dpi_tmp;
        Window_SetHiDpi(dpi_tmp);
    }

    int fulltmp = 0;
    if (stdConffile_ReadLine())
    {
        _sscanf(stdConffile_aLine, "windowfullscreen %d", &fulltmp);
        fulltmp = !!fulltmp;
        Window_SetFullscreen(fulltmp);
    }

    if (stdConffile_ReadLine())
    {
        _sscanf(stdConffile_aLine, "texturefiltering %d", &jkPlayer_enableTextureFilter);
        jkPlayer_enableTextureFilter = !!jkPlayer_enableTextureFilter;
    }

    if (stdConffile_ReadLine())
    {
        _sscanf(stdConffile_aLine, "originalaspect %d", &jkPlayer_enableOrigAspect);
        jkPlayer_enableOrigAspect = !!jkPlayer_enableOrigAspect;
    }

    if (stdConffile_ReadLine())
    {
        _sscanf(stdConffile_aLine, "fpslimit %d", &jkPlayer_fpslimit);
    }

    if (stdConffile_ReadLine())
    {
        _sscanf(stdConffile_aLine, "enablevsync %d", &jkPlayer_enableVsync);
        jkPlayer_enableVsync = !!jkPlayer_enableVsync;
    }

    if (stdConffile_ReadLine())
    {
        _sscanf(stdConffile_aLine, "enablebloom %d", &jkPlayer_enableBloom);
        jkPlayer_enableBloom = !!jkPlayer_enableBloom;
    }

    if (stdConffile_ReadLine())
    {
        if (_sscanf(stdConffile_aLine, "ssaamultiple %f", &ftmp) != 1)
            jkPlayer_ssaaMultiple = 1.0;
        else
            jkPlayer_ssaaMultiple = ftmp;
    }

    if (stdConffile_ReadLine())
    {
        _sscanf(stdConffile_aLine, "enablessao %d", &jkPlayer_enableSSAO);
        jkPlayer_enableSSAO = !!jkPlayer_enableSSAO;
    }

    if (stdConffile_ReadLine())
    {
        if (_sscanf(stdConffile_aLine, "gamma %f", &ftmp) != 1)
            jkPlayer_gamma = 1.0;
        else
            jkPlayer_gamma = ftmp;
    }
}
#endif

int jkPlayer_ReadConf(wchar_t *name)
{
    char *v4; // edi
    char v6[32]; // [esp+10h] [ebp-A0h]
    char fpath[256]; // Added: 128 -> 256
    char ext_fpath[256];
    char ext_fpath_cvars[256];

    int version = 0;
    if (!jkPlayer_VerifyWcharName(name))
        return 0;

    stdString_WcharToChar(v6, name, 31);
    v6[31] = 0;
    _wcsncpy(jkPlayer_playerShortName, name, 0x1Fu);
    jkPlayer_playerShortName[31] = 0;
    stdFnames_MakePath3(ext_fpath, 256, "player", v6, "openjkdf2.json");
    stdFnames_MakePath3(ext_fpath_cvars, 256, "player", v6, SITHCVAR_FNAME); // Added
    stdString_snprintf(fpath, 256, "player\\%s\\%s.plr", v6, v6); // Added: sprintf -> snprintf
    if (!stdConffile_OpenReadBypass(fpath))
        return 0;

    if ( stdConffile_ReadLine() && _sscanf(stdConffile_aLine, "version %d", &version) == 1 && version == 1 && stdConffile_ReadLine() )
    {
        _sscanf(stdConffile_aLine, "diff %d", &jkPlayer_setDiff);
        if ( jkPlayer_setDiff < 0 )
        {
            jkPlayer_setDiff = 0;
        }
        else if ( jkPlayer_setDiff > 2 )
        {
            jkPlayer_setDiff = 2;
        }
        jkPlayer_ReadOptionsConf();
        sithWeapon_ReadConf();
        //jk_printf("%s\n", stdConffile_aLine);
        sithControl_ReadConf();

        // HACK
#ifdef TARGET_TWL
        sithControl_InputInit();
#endif
        if ( stdConffile_ReadArgs() )
        {
            if ( stdConffile_entry.numArgs >= 1u
              && !_memcmp(stdConffile_entry.args[0].key, "numcutscenes", 0xDu)
              && _sscanf(stdConffile_entry.args[1].value, "%d", &jkPlayer_setNumCutscenes) == 1 )
            {
                v4 = jkPlayer_cutscenePath;
                for (int i = 0; i < jkPlayer_setNumCutscenes; i++)
                {
                    if ( !stdConffile_ReadArgs() )
                        break;
                    if ( stdConffile_entry.numArgs < 2u )
                        break;
                    if ( _sscanf(stdConffile_entry.args[0].key, "%s", v4) != 1 )
                        break;
                    if ( _sscanf(stdConffile_entry.args[1].value, "%d", &jkPlayer_aCutsceneVal[i]) != 1 )
                        break;
                    v4 += 32;
                }
            }
        }
#ifdef QOL_IMPROVEMENTS
        // Unfortunately we have to live with our past mistakes and keep all of this parsing.
        jkPlayer_ParseLegacyExt();

        // New JSON parsing
        jkPlayer_fov = stdJSON_GetInt(ext_fpath, "fov", jkPlayer_fov);
        jkPlayer_fovIsVertical = stdJSON_GetBool(ext_fpath, "fovisvertical", jkPlayer_fovIsVertical);
        Window_isHiDpi_tmp = stdJSON_GetBool(ext_fpath, "windowishidpi", Window_isHiDpi);
        Window_isFullscreen_tmp = stdJSON_GetBool(ext_fpath, "windowfullscreen", Window_isFullscreen);
        jkPlayer_enableTextureFilter = stdJSON_GetBool(ext_fpath, "texturefiltering", jkPlayer_enableTextureFilter);
        jkPlayer_enableOrigAspect = stdJSON_GetBool(ext_fpath, "originalaspect", jkPlayer_enableOrigAspect);
        jkPlayer_fpslimit = stdJSON_GetInt(ext_fpath, "fpslimit", jkPlayer_fpslimit);
        jkPlayer_enableVsync = stdJSON_GetBool(ext_fpath, "enablevsync", jkPlayer_enableVsync);
        jkPlayer_enableBloom = stdJSON_GetBool(ext_fpath, "enablebloom", jkPlayer_enableBloom);
        jkPlayer_ssaaMultiple = stdJSON_GetFloat(ext_fpath, "ssaamultiple", jkPlayer_ssaaMultiple);
        jkPlayer_enableSSAO = stdJSON_GetInt(ext_fpath, "enablessao", jkPlayer_enableSSAO);
        jkPlayer_gamma = stdJSON_GetFloat(ext_fpath, "gamma", jkPlayer_gamma);
#ifdef PLATFORM_VR
        jkPlayer_vrSupersampling = stdJSON_GetFloat(ext_fpath, "vrSupersampling", jkPlayer_vrSupersampling);
        jkPlayer_vr6DoFScale = stdJSON_GetFloat(ext_fpath, "vr6DoFScale", jkPlayer_vr6DoFScale);
        jkPlayer_vrSnapTurnAngle = stdJSON_GetInt(ext_fpath, "vrSnapTurnAngle", jkPlayer_vrSnapTurnAngle);
        jkPlayer_vrWeaponPitchAdjust = stdJSON_GetInt(ext_fpath, "vrWeaponPitchAdjust", jkPlayer_vrWeaponPitchAdjust);
        jkPlayer_vrDominantHand = stdJSON_GetInt(ext_fpath, "vrDominantHand", jkPlayer_vrDominantHand);
        jkPlayer_vrSwapSticks = stdJSON_GetInt(ext_fpath, "vrSwapSticks", jkPlayer_vrSwapSticks);
        jkPlayer_vrPromptsShown = stdJSON_GetInt(ext_fpath, "vrPromptsShown", jkPlayer_vrPromptsShown);
        jkPlayer_vrRefreshRate = stdJSON_GetFloat(ext_fpath, "vrRefreshRate", jkPlayer_vrRefreshRate);
        jkPlayer_vrWeaponCrosshair = stdJSON_GetInt(ext_fpath, "vrWeaponCrosshair", jkPlayer_vrWeaponCrosshair);
        // Added: mirror the write-side additions. vrSmoothTurnSpeed was also
        // missing here even though the writer already saved it.
        jkPlayer_vrSmoothTurnSpeed = stdJSON_GetInt(ext_fpath, "vrSmoothTurnSpeed", jkPlayer_vrSmoothTurnSpeed);
        jkPlayer_vrMoveDirection = stdJSON_GetInt(ext_fpath, "vrMoveDirection", jkPlayer_vrMoveDirection);
        jkPlayer_vrHeightOffset = stdJSON_GetFloat(ext_fpath, "vrHeightOffset", jkPlayer_vrHeightOffset);
        jkPlayer_vrHudWidth  = stdJSON_GetFloat(ext_fpath, "vrHudWidth",  jkPlayer_vrHudWidth);
        jkPlayer_vrHudHeight = stdJSON_GetFloat(ext_fpath, "vrHudHeight", jkPlayer_vrHudHeight);
        jkPlayer_vrHudPosX   = stdJSON_GetFloat(ext_fpath, "vrHudPosX",   jkPlayer_vrHudPosX);
        jkPlayer_vrHudPosY   = stdJSON_GetFloat(ext_fpath, "vrHudPosY",   jkPlayer_vrHudPosY);
        jkPlayer_vrHudDepth  = stdJSON_GetFloat(ext_fpath, "vrHudDepth",  jkPlayer_vrHudDepth);
#endif

        jkPlayer_bEnableJkgm = stdJSON_GetBool(ext_fpath, "bEnableJkgm", jkPlayer_bEnableJkgm);
        jkPlayer_bEnableTexturePrecache = stdJSON_GetBool(ext_fpath, "bEnableTexturePrecache", jkPlayer_bEnableTexturePrecache);
        jkPlayer_bKeepCorpses = stdJSON_GetBool(ext_fpath, "bKeepCorpses", jkPlayer_bKeepCorpses);
        jkPlayer_bFastMissionText = stdJSON_GetBool(ext_fpath, "bFastMissionText", jkPlayer_bFastMissionText);
        jkPlayer_hudScale = stdJSON_GetFloat(ext_fpath, "hudScale", jkPlayer_hudScale);
        jkPlayer_crosshairLineWidth = stdJSON_GetFloat(ext_fpath, "crosshairLineWidth", jkPlayer_crosshairLineWidth);
        jkPlayer_crosshairScale = stdJSON_GetFloat(ext_fpath, "crosshairScale", jkPlayer_crosshairScale);
        jkPlayer_canonicalCogTickrate = stdJSON_GetFloat(ext_fpath, "canonicalCogTickrate", jkPlayer_canonicalCogTickrate);
        jkPlayer_canonicalPhysTickrate = stdJSON_GetFloat(ext_fpath, "canonicalPhysTickrate", jkPlayer_canonicalPhysTickrate);

        jkPlayer_bUseOldPlayerPhysics = stdJSON_GetBool(ext_fpath, "bUseOldPlayerPhysics", jkPlayer_bUseOldPlayerPhysics);

        jkPlayer_setCrosshairOnLightsaber = stdJSON_GetBool(ext_fpath, "setCrosshairOnLightsaber", jkPlayer_setCrosshairOnLightsaber);
        jkPlayer_setCrosshairOnFist = stdJSON_GetBool(ext_fpath, "setCrosshairOnFist", jkPlayer_setCrosshairOnFist);
        jkPlayer_bDisableWeaponWaggle = stdJSON_GetBool(ext_fpath, "bDisableWeaponWaggle", jkPlayer_bDisableWeaponWaggle);
        sithCamera_bDisablePovShake = stdJSON_GetBool(ext_fpath, "bDisablePovShake", sithCamera_bDisablePovShake);
#endif
#ifdef FIXED_TIMESTEP_PHYS
        jkPlayer_bJankyPhysics = stdJSON_GetBool(ext_fpath, "bJankyPhysics", jkPlayer_bJankyPhysics);
#endif

#ifdef QOL_IMPROVEMENTS
        sithCvar_LoadLocals(ext_fpath_cvars);

        if (jkPlayer_fov < FOV_MIN)
            jkPlayer_fov = FOV_MIN;
        if (jkPlayer_fov > FOV_MAX)
            jkPlayer_fov = FOV_MAX;

        Window_SetHiDpi(Window_isHiDpi_tmp);
        Window_SetFullscreen(Window_isFullscreen_tmp);

        std3D_UpdateSettings();

        jkPlayer_bHasLoadedSettingsOnce = 1;
#endif

#ifdef PLATFORM_VR
        stdVR_SyncConfigFromJkPlayer();
        jk_printf("OpenJKDF2 VR: synced config from profile (moveDir=%d snap=%d smooth=%d world=%.2f height=%.2f hand=%d ssaa=%.2f)\n",
            stdVR_config.moveDirection,
            stdVR_config.snapTurnAngle,
            (int)stdVR_config.smoothTurnSpeed,
            stdVR_config.worldScale,
            stdVR_config.heightOffset,
            stdVR_config.dominantHand,
            stdVR_config.supersampling);
#endif
        
        stdConffile_Close();
        return 1;
    }
    else
    {
        stdConffile_Close();
        jkPlayer_setDiff = 1;
        sithControl_InputInit();
        return 0;
    }
    return 0;
}

void jkPlayer_SetPovModel(jkPlayerInfo *info, rdModel3 *model)
{
    rdThing *thing; // esi

    thing = &info->povModel;
    if ( info->povModel.type != 1 || info->povModel.model3 != model )
    {
        rdThing_FreeEntry(&info->povModel);
        rdThing_NewEntry(thing, info->actorThing);

        // Added: nullptr check, for fixing UAF on second world load
        if (model) {
            rdThing_SetModel3(thing, model);
            info->povModel.puppet = rdPuppet_New(thing);
        }
        else
        {
            info->povModel.puppet = NULL;
        }
    }

#ifdef PLATFORM_VR
    // Log model filenames for VR off-hand debugging
    if (model) {
        extern void VR_Log(const char* fmt, ...);
        VR_Log("SetPovModel: '%s' (curWeapon=%d, pVRFistsModel3=%p)\n",
            model->filename, info->actorThing ? sithInventory_GetCurWeapon(info->actorThing) : -1, (void*)pVRFistsModel3);

    }

#endif
}

int jkPlayer_checkPov = 0;
void jkPlayer_DrawPov()
{
    rdVector3 trans;
    rdMatrix34 viewMat;

#ifdef PLATFORM_VR
    // Don't render weapon when 3D map is visible
    if (stdVR_Map3D_IsVisible()) {
        return;
    }
#endif

    if (!playerThings[playerThingIdx].povModel.model3)
        return;

    if ( playerThings[playerThingIdx].povModel.puppet )
    {
        rdPuppet_UpdateTracks(playerThings[playerThingIdx].povModel.puppet, sithTime_deltaSeconds);
    }

    if ( !(sithCamera_currentCamera->cameraPerspective & 0xFC) && sithCamera_currentCamera->primaryFocus == sithWorld_pCurrentWorld->cameraFocus )
    {
        sithThing* player = playerThings[playerThingIdx].actorThing;

        // TODO: I think this explains some weird duplication
#ifndef QOL_IMPROVEMENTS
        flex_t waggleAmt = (stdMath_Fabs(player->waggle) > 0.02 ? 0.02 : stdMath_Fabs(player->waggle)) * jkPlayer_waggleMag;
#else
        // scale animation to be in line w/ 25fps (presumed 'mastering' FPS of whoever was coding the waggle)
        flex_t waggleAmt = (stdMath_Fabs(player->waggle) > 0.02 * (sithTime_deltaSeconds / (1.0/25)) ? 0.02 * (sithTime_deltaSeconds / (1.0/25)) : stdMath_Fabs(player->waggle)) * jkPlayer_waggleMag;

        if (jkPlayer_bDisableWeaponWaggle) {
            waggleAmt = 0.0;
        }
#endif
        if ( waggleAmt == 0.0 )
            jkPlayer_waggleAngle = 0.0;
        else
            jkPlayer_waggleAngle = waggleAmt + jkPlayer_waggleAngle;

        // TODO is this a macro/func?
        flex_t angleSin, angleCos;
        stdMath_SinCos(jkPlayer_waggleAngle, &angleSin, &angleCos);
        flex_t velNorm = rdVector_Len3(&player->physicsParams.vel) / player->physicsParams.maxVel; // MOTS altered: uses 1.538462 for something (performance hack?)
        if (angleCos > 0) // verify?
            angleCos = -angleCos;
#ifdef QOL_IMPROVEMENTS
        if (jkPlayer_bDisableWeaponWaggle) {
            velNorm *= 0.5;
        }
#endif
        jkSaber_rotateVec.x = angleCos * jkPlayer_waggleVec.x * velNorm;
        jkSaber_rotateVec.y = angleSin * jkPlayer_waggleVec.y * velNorm;
        jkSaber_rotateVec.z = angleSin * jkPlayer_waggleVec.z * velNorm;
        rdMatrix_BuildRotate34(&jkSaber_rotateMat, &jkSaber_rotateVec);

#if defined(SDL2_RENDER) || defined(TARGET_TWL)
        // Force weapon to draw in front of scene
        std3D_ClearZBuffer();
        rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
        rdSetSortingMethod(2);
        rdSetOcclusionMethod(0);
#else
        // Force weapon to draw in front of scene
        rdSetZBufferMethod(RD_ZBUFFER_NOREAD_NOWRITE); // set RD_ZBUFFER_READ_WRITE to have guns clip through walls
        rdSetSortingMethod(2);
        rdSetOcclusionMethod(0);
#endif

        flex_t ambLight = stdMath_Clamp(sithCamera_currentCamera->sector->extraLight + sithCamera_currentCamera->sector->ambientLight, 0.0, 1.0);

        rdCamera_SetAmbientLight(&sithCamera_currentCamera->rdCam, ambLight);
        rdColormap_SetCurrent(sithCamera_currentCamera->sector->colormap);

#ifdef PLATFORM_VR
        // Draw 3D weapon models on the weapon wheel (if active)
        if (stdVR_WeaponWheel_IsActive()) {
            stdVR_WeaponWheel_Draw3D(&sithCamera_currentCamera->viewMat);
        }
#endif

        // In VR mode, use the PER-EYE camera matrix for weapon positioning.
        // This must match the view_matrix used in rdModel3_DrawMesh (which is the inverse of the per-eye matrix).
        // The weapon's hierarchyNodeMatrices are built with viewMat, then multiplied by view_matrix:
        //   final = view_matrix * hierarchyNodeMatrices = inverse(per_eye) * per_eye * bone = bone
        // This correctly positions the weapon in camera space without double-transformation.
        // We also force matrix rebuild per eye since the per-eye viewMat differs between eyes.
#ifdef PLATFORM_VR
        extern int stdVR_bEnabled;
        extern stdVR_MotionConfig stdVR_motionConfig;
        int vrMotionWeapon = 0;

        if (stdVR_bEnabled && stdVR_motionConfig.bMotionAimEnabled) {
            // Motion controls: render weapon at controller position/orientation
            int hand = stdVR_GetDominantHand();

            if (stdVR_GetControllerViewMatrix(hand, &viewMat)) {
                vrMotionWeapon = 1;
                // Force weapon model to rebuild hierarchyNodeMatrices for this eye
                playerThings[playerThingIdx].povModel.frameTrue = 0;

                // Apply weapon model scale for VR (makes weapon appear larger)
                // Use per-weapon scale if available, otherwise fall back to global config
                stdVR_WeaponOffset* pWeaponOffset = stdVR_GetCurrentWeaponOffset();
                float weaponScale = pWeaponOffset ? pWeaponOffset->modelScale : stdVR_motionConfig.weaponModelScale;
                if (weaponScale > 0.0f && weaponScale != 1.0f) {
                    viewMat.rvec.x *= weaponScale;
                    viewMat.rvec.y *= weaponScale;
                    viewMat.rvec.z *= weaponScale;
                    viewMat.lvec.x *= weaponScale;
                    viewMat.lvec.y *= weaponScale;
                    viewMat.lvec.z *= weaponScale;
                    viewMat.uvec.x *= weaponScale;
                    viewMat.uvec.y *= weaponScale;
                    viewMat.uvec.z *= weaponScale;
                }
            }
        }

        if (!vrMotionWeapon) {
            // Fallback: use eye view matrix (HMD-attached weapon)
            if (stdVR_bEnabled && stdVR_GetCurrentEyeViewMatrix(&viewMat)) {
                playerThings[playerThingIdx].povModel.frameTrue = 0;
            } else {
                rdMatrix_Copy34(&viewMat, &sithCamera_currentCamera->viewMat);
            }
        }
#else
        rdMatrix_Copy34(&viewMat, &sithCamera_currentCamera->viewMat);
#endif

#ifdef PLATFORM_VR
        // Only apply eye offset and weapon waggle for HMD-attached weapons, not motion-controlled
        if (!vrMotionWeapon) {
#endif
        rdVector_Copy3(&trans, &playerThings[playerThingIdx].actorThing->actorParams.eyeOffset);
        //printf("%f %f %f\n", (flex32_t)playerThings[playerThingIdx].actorThing->actorParams.eyeOffset.x, (flex32_t)playerThings[playerThingIdx].actorThing->actorParams.eyeOffset.y, (flex32_t)playerThings[playerThingIdx].actorThing->actorParams.eyeOffset.z);
#ifdef QOL_IMPROVEMENTS
        // Shift gun down slightly at higher aspect ratios
        // TODO just make a cvar-alike for this
        //trans.z += 0.007 * (1.0 / sithCamera_currentCamera->rdCam.screenAspectRatio);
#endif
        //printf("%f %f %f\n", (flex32_t)viewMat.scale.x, (flex32_t)viewMat.scale.y, (flex32_t)viewMat.scale.z);

        // Shift gun up slightly
#ifdef TARGET_TWL
        //trans.y -= 0.037; //znear 16
        //trans.z += 0.013; //znear 16

        //trans.x += 0.009; //30deg fov
        //trans.z -= 0.017; //30deg fov
#endif

        rdVector_Neg3Acc(&trans);
        rdMatrix_PreTranslate34(&viewMat, &trans);
        rdMatrix_PreMultiply34(&viewMat, &jkSaber_rotateMat);
#ifdef PLATFORM_VR
        }
#endif

        // Moved: see below.
#if !(defined(SDL2_RENDER) || defined(TARGET_TWL))
        // Render saber if applicable
#ifdef PLATFORM_VR
        if (!stdVR_WeaponWheel_IsActive())
#endif
        if (playerThings[playerThingIdx].actorThing->jkFlags & JKFLAG_SABERON)
        {
            jkSaber_Draw(&viewMat);
        }
#endif
        
        //printf("pov in\n");
        //jkPlayer_checkPov = 1;
#ifdef PLATFORM_VR
        // Hide the entire off-hand arm when rendering one-handed weapons with motion controls.
        // The VR weapon is attached to the dominant hand only, so the dangling
        // left arm looks wrong. Walk up from K_Lhand (node 2) to find the
        // topmost arm node (direct child of root) and amputate it, which
        // hides it and all its children (forearm, hand).
        int vrAmputatedNodeIdx = -1;
        int bVRHideLeftArm = 0;
        int bVRFistsHandsOnly = 0;
        if (stdVR_bEnabled && vrMotionWeapon) {
            sithThing* pActorThing = playerThings[playerThingIdx].actorThing;
            // Altered: Check weapon ID, not saber jkFlags — the flags persist after
            // switching away from the saber, causing arm amputation on the wrong weapon model.
            int curWeap = sithInventory_GetCurWeapon(pActorThing);
            if (curWeap == SITHBIN_LIGHTSABER || curWeap == SITHBIN_MOTS_LIGHTSABER)
                bVRHideLeftArm = 1;
            if (curWeap == SITHBIN_THERMAL_DETONATOR)
                bVRHideLeftArm = 1;
            // Added: the MotS thrown weapons hold the item in one hand, so the off-hand arm
            // dangles the same way the DF2 detonator did.
            if (curWeap == SITHBIN_MOTS_THERMAL_DETONATOR
             || curWeap == SITHBIN_MOTS_SEQUENCER_CHARGE
             || curWeap == SITHBIN_MOTS_FLASH_BOMB
             || curWeap == SITHBIN_MOTS_MANUAL_SEQUENCER)
                bVRHideLeftArm = 1;
            if (curWeap == SITHBIN_FISTS || curWeap == SITHBIN_MOTS_FISTS) {
                bVRHideLeftArm = 1;
            }

            // Altered: hide the weapon-hand arm for EVERY motion weapon, not only the fists.
            // The arm cannot follow a hand that a tracked controller drives, so a floating
            // hand holding the weapon reads better than an arm at the wrong angle.
            bVRFistsHandsOnly = 1;
        }
        if (bVRHideLeftArm
            && playerThings[playerThingIdx].povModel.amputatedJoints
            && playerThings[playerThingIdx].povModel.model3)
        {
            rdModel3* model = playerThings[playerThingIdx].povModel.model3;
            int lhandIdx = jkPlayer_VRFindNodeByName(model, "k_lhand");
            if (lhandIdx >= 0) {
                rdHierarchyNode* node = &model->hierarchyNodes[lhandIdx];
                // Walk up to the highest ancestor that still has a parent (stop at root's child)
                while (node->parent && node->parent->parent) {
                    node = node->parent;
                }
                vrAmputatedNodeIdx = node->idx;
                playerThings[playerThingIdx].povModel.amputatedJoints[vrAmputatedNodeIdx] = 1;
            }
        }

        // Hide the weapon-hand arm so only the hand and the weapon show. The hand node is
        // looked up by name because its index differs per model.
        jkPlayerVRSavedMesh vrArmSaved[JKPLAYER_VR_MAX_ARM_NODES];
        int vrArmSavedCount = 0;
        if (bVRFistsHandsOnly && playerThings[playerThingIdx].povModel.model3) {
            rdModel3* pPovModel = playerThings[playerThingIdx].povModel.model3;
            int rhandIdx = jkPlayer_VRFindNodeByName(pPovModel, "k_rhand");
            if (rhandIdx >= 0) {
                vrArmSavedCount = jkPlayer_VRHideArmKeepHand(pPovModel, rhandIdx, vrArmSaved);
            }
        }

        // VR motion controls: disable software backface culling for the weapon model.
        // The SW backface test dots face normals (model space) against vertex positions
        // (view space). When the controller points differently from the camera, the
        // spaces don't match and visible faces get incorrectly culled.
        int vrSavedRenderOptions = 0;
        if (vrMotionWeapon) {
            vrSavedRenderOptions = rdGetRenderOptions();
            rdSetRenderOptions(vrSavedRenderOptions & ~1);
        }

        // Left-handed: mirror the weapon model along X so it appears in the correct hand.
        // Negate rvec to flip the model and flip GL winding to compensate.
        int bVRMirrorWeapon = (vrMotionWeapon && stdVR_GetDominantHand() == 0);
        if (bVRMirrorWeapon) {
            viewMat.rvec.x = -viewMat.rvec.x;
            viewMat.rvec.y = -viewMat.rvec.y;
            viewMat.rvec.z = -viewMat.rvec.z;
            std3D_SetFrontFaceCW(1);
        }
#endif
#ifdef PLATFORM_VR
        if (!stdVR_WeaponWheel_IsActive())
#endif
        rdThing_Draw(&playerThings[playerThingIdx].povModel, &viewMat);
#ifdef PLATFORM_VR
        // Restore arm visibility after draw
        if (vrAmputatedNodeIdx >= 0) {
            playerThings[playerThingIdx].povModel.amputatedJoints[vrAmputatedNodeIdx] = 0;
        }
        // Restore suppressed fists arm meshes
        jkPlayer_VRRestoreArmMeshes(vrArmSaved, vrArmSavedCount);
#endif
        //jkPlayer_checkPov = 0;
        //printf("pov done\n");

        // DSi doesn't really have Z buffer stuff so just batch everything
#ifndef TARGET_TWL
        rdCache_Flush();
#endif

#ifdef PLATFORM_VR
        if (bVRMirrorWeapon) {
            std3D_SetFrontFaceCW(0);
        }
        if (vrMotionWeapon) {
            rdSetRenderOptions(vrSavedRenderOptions);
        }
#endif

#ifdef PLATFORM_VR
        // Off-hand rendering: always show a bare fist hand at the off-hand controller.
        // Uses a dedicated rdThing initialized from the fists POV model so that
        // amputatedJoints/hierarchyNodeMatrices arrays match the fists hierarchy,
        // regardless of what weapon is currently equipped.
        if (stdVR_bEnabled && vrMotionWeapon && !stdVR_WeaponWheel_IsActive()) {
            // Try to find the fists model if we don't have it yet
            if (!pVRFistsModel3) {
                // Method 1: capture from current POV when fists are equipped
                sithThing* pActorThing = playerThings[playerThingIdx].actorThing;
                if (sithInventory_GetCurWeapon(pActorThing) == SITHBIN_FISTS
                    && playerThings[playerThingIdx].povModel.model3)
                {
                    pVRFistsModel3 = playerThings[playerThingIdx].povModel.model3;
                }
            }
            if (!pVRFistsModel3) {
                // Method 2: look up common fists model names from the loaded model cache
                static const char* aFistsModelNames[] = { "fistv.3do", "kyhand.3do", "fistpov.3do", NULL };
                for (int i = 0; aFistsModelNames[i]; i++) {
                    rdModel3* pModel = sithModel_LoadEntry(aFistsModelNames[i], 1);
                    if (pModel) {
                        pVRFistsModel3 = pModel;
                        break;
                    }
                }
            }

            // Pick the model that supplies the off-hand. Added: the Manual Sequencer holds the
            // charge in its off hand, and its own POV model has that hand posed correctly, so
            // the generic fist looks wrong. Use the weapon's own model for it.
            rdModel3* pVROffhandSrc = pVRFistsModel3;
            {
                sithThing* pActorThing = playerThings[playerThingIdx].actorThing;
                int offhandWeap = sithInventory_GetCurWeapon(pActorThing);
                if (offhandWeap == SITHBIN_MOTS_MANUAL_SEQUENCER
                    && playerThings[playerThingIdx].povModel.model3
                    && playerThings[playerThingIdx].povModel.model3->numHierarchyNodes > 5)
                {
                    pVROffhandSrc = playerThings[playerThingIdx].povModel.model3;
                }
            }

            // Bind the off-hand rdThing, and rebind when the source model changes
            if (pVROffhandSrc && (!bVROffhandReady || pVROffhandSrc != pVROffhandBoundModel)) {
                sithThing* pActorThing = playerThings[playerThingIdx].actorThing;
                rdThing_NewEntry(&vrOffhandThing, pActorThing);
                if (rdThing_SetModel3(&vrOffhandThing, pVROffhandSrc)) {
                    bVROffhandReady = 1;
                    pVROffhandBoundModel = pVROffhandSrc;

                    // Compute K_Lhand bind-pose offset: build hierarchy matrices
                    // with identity to get the model-space position of K_Lhand (node 2).
                    // We translate the view matrix by the negative of this offset so
                    // K_Lhand ends up centered on the controller instead of the model origin.
                    rdMatrix34 identityMat;
                    rdMatrix_Identity34(&identityMat);
                    vrOffhandThing.frameTrue = 0;
                    rdPuppet_BuildJointMatrices(&vrOffhandThing, &identityMat);

                    int srcLhandIdx = jkPlayer_VRFindNodeByName(pVROffhandSrc, "k_lhand");
                    vrOffhandLhandNodeIdx = srcLhandIdx;
                    if (srcLhandIdx >= 0) {
                        rdVector_Copy3(&vrOffhandLhandOffset,
                                       &vrOffhandThing.hierarchyNodeMatrices[srcLhandIdx].scale);
                    } else {
                        rdVector_Zero3(&vrOffhandLhandOffset);
                    }
                }
            }

            if (bVROffhandReady) {
                {
                rdMatrix34 offhandViewMat;
                int offHand = 1 - stdVR_GetDominantHand();
                // Use raw controller matrix (no weapon pitch/position offsets)
                if (stdVR_GetControllerViewMatrixRaw(offHand, &offhandViewMat)) {
                    // Disable software backface culling (same reason as main weapon)
                    int vrOffhandSavedRenderOptions = rdGetRenderOptions();
                    rdSetRenderOptions(vrOffhandSavedRenderOptions & ~1);

                    // Left-handed: mirror the off-hand model along X so K_Lhand
                    // appears as a right hand at the right controller.
                    int bVRMirrorOffhand = (stdVR_GetDominantHand() == 0);
                    if (bVRMirrorOffhand) {
                        offhandViewMat.rvec.x = -offhandViewMat.rvec.x;
                        offhandViewMat.rvec.y = -offhandViewMat.rvec.y;
                        offhandViewMat.rvec.z = -offhandViewMat.rvec.z;
                        std3D_SetFrontFaceCW(1);
                    }

                    // Added: the bare off-hand reads slightly too large next to the weapon
                    // hand, so scale the model down. This must happen BEFORE the pre-translate
                    // below: the pre-translate rotates the K_Lhand offset through this basis,
                    // so scaling first keeps the hand exactly on the controller.
                    rdVector_Scale3Acc(&offhandViewMat.rvec, JKPLAYER_VR_OFFHAND_SCALE);
                    rdVector_Scale3Acc(&offhandViewMat.lvec, JKPLAYER_VR_OFFHAND_SCALE);
                    rdVector_Scale3Acc(&offhandViewMat.uvec, JKPLAYER_VR_OFFHAND_SCALE);

                    // Translate so K_Lhand is centered on the controller position
                    rdVector3 negLhandOffset;
                    rdVector_Neg3(&negLhandOffset, &vrOffhandLhandOffset);
                    rdMatrix_PreTranslate34(&offhandViewMat, &negLhandOffset);

                    // Amputate K_Rhand (node 5) chain — always hide the weapon arm,
                    // show only the bare off-hand arm
                    int offhandAmputatedNodeIdx = -1;
                    int offhandRhandIdx = jkPlayer_VRFindNodeByName(pVROffhandBoundModel, "k_rhand");
                    if (offhandRhandIdx >= 0) {
                        rdHierarchyNode* pNode = &pVROffhandBoundModel->hierarchyNodes[offhandRhandIdx];
                        while (pNode->parent && pNode->parent->parent) {
                            pNode = pNode->parent;
                        }
                        offhandAmputatedNodeIdx = pNode->idx;
                        vrOffhandThing.amputatedJoints[offhandAmputatedNodeIdx] = 1;
                    }

                    // Hands-only: hide the off-hand arm segments so only K_Lhand (node 2) shows.
                    jkPlayerVRSavedMesh vrOffhandArmSaved[JKPLAYER_VR_MAX_ARM_NODES];
                    int vrOffhandArmSavedCount =
                        (vrOffhandLhandNodeIdx >= 0)
                            ? jkPlayer_VRHideArmKeepHand(pVROffhandBoundModel, vrOffhandLhandNodeIdx,
                                                         vrOffhandArmSaved)
                            : 0;

                    // Force hierarchy matrix rebuild for off-hand position
                    vrOffhandThing.frameTrue = 0;
                    rdThing_Draw(&vrOffhandThing, &offhandViewMat);

                    // Restore suppressed arm meshes
                    jkPlayer_VRRestoreArmMeshes(vrOffhandArmSaved, vrOffhandArmSavedCount);

                    // Restore amputated joints
                    if (offhandAmputatedNodeIdx >= 0) {
                        vrOffhandThing.amputatedJoints[offhandAmputatedNodeIdx] = 0;
                    }

#ifndef TARGET_TWL
                    rdCache_Flush();
#endif

                    if (bVRMirrorOffhand) {
                        std3D_SetFrontFaceCW(0);
                    }
                    rdSetRenderOptions(vrOffhandSavedRenderOptions);
                }
            }
            }
        }
#endif

        // Added: we want the polyline to render in draw order so the spheres don't clip,
        // but we want the POV model to be aware of the depths still.
#if defined(SDL2_RENDER) || defined(TARGET_TWL)
        if (playerThings[playerThingIdx].actorThing->jkFlags & JKFLAG_SABERON
#ifdef PLATFORM_VR
            && !stdVR_WeaponWheel_IsActive()
#endif
        )
        {
            rdSetZBufferMethod(RD_ZBUFFER_READ_NOWRITE);
            jkSaber_Draw(&viewMat);
        }


#ifndef TARGET_TWL
        rdCache_Flush(); // Added: force polyline to be underneath model
#endif
        rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);
#endif

#ifdef PLATFORM_VR
        // Added: alignment aid. While the weapon alignment tool is open, draw the controller
        // axes as rays. The green forward ray is the path a projectile takes, so the model can
        // be lined up against it.
        if (stdVR_bEnabled && stdVR_AlignmentTool_IsActive()) {
            stdVR_DrawControllerAxisRays(stdVR_GetDominantHand());
        }

        // Debug: Draw controller axes to visualize tracking
/*        if (stdVR_bEnabled) {
            int hand = stdVR_GetDominantHand();
            stdVR_DrawDebugControllerAxes(hand);
        }*/

        // Draw VR saber collision debug line
        if (stdVR_bEnabled && stdVR_motionConfig.bMotionSaberEnabled) {
            jkSaber_DrawVRDebugLine();
        }
#endif
    }
}

void jkPlayer_renderSaberWeaponMesh(sithThing *thing)
{
    jkPlayerInfo* playerInfo = thing->playerInfo;
    if (!playerInfo) {
        // Added: hackfix for weird blades?
        if (thing->actorParams.typeflags & SITH_AF_BOSS ) {
            jk_printf("OpenJKDF2: Boss w/o a blade? Fixing... %p\n", thing);

            jkPlayer_FUN_00404fe0(thing);

            sithThing* saberSparks = sithTemplate_GetEntryByName("+ssparks_saber");
            sithThing* bloodSparks = sithTemplate_GetEntryByName("+ssparks_blood");
            sithThing* wallSparks = sithTemplate_GetEntryByName("+ssparks_wall");
            jkSaber_InitializeSaberInfo(thing, "saberred1.mat", "saberred0.mat", 0.0032, 0.0018, 0.12, wallSparks, bloodSparks, saberSparks);
        }
        return;
    }

    if (!thing->animclass)
        return;

    int primary_mesh = thing->animclass->bodypart_to_joint[JOINTTYPE_PRIMARYWEAP];
    int secondary_mesh = thing->animclass->bodypart_to_joint[JOINTTYPE_SECONDARYWEAP];

    // Attempt to find a proper secondary weapon hand
    if (thing->jkFlags & JKFLAG_DUALSABERS && primary_mesh == secondary_mesh && thing->rdthing.model3) {
        for (int i = 0; i < thing->rdthing.model3->numHierarchyNodes; i++)
        {
            int l = _strlen(thing->rdthing.model3->hierarchyNodes[i].name);
            if (l < 5) continue;

            if (!__strcmpi(thing->rdthing.model3->hierarchyNodes[i].name + (l - 5), "lhand")) {
                secondary_mesh = i;
                break;
            }
        }

        if (primary_mesh != secondary_mesh) {
            thing->animclass->bodypart_to_joint[JOINTTYPE_SECONDARYWEAP] = secondary_mesh;
        }
    }

    rdMatrix34* primaryMat = &thing->rdthing.hierarchyNodeMatrices[primary_mesh];
    rdMatrix34* secondaryMat = &thing->rdthing.hierarchyNodeMatrices[secondary_mesh];

    if (thing->jkFlags & JKFLAG_PERSUASION)
    {
        if ( sithPlayer_pLocalPlayer->iteminfo[SITHBIN_F_SEEING].state & ITEMSTATE_ACTIVATE )
        {
            rdGeoMode_t oldGeoMode = thing->rdthing.curGeoMode;
#ifdef TARGET_TWL
            // Added: Don't draw them twice wtf
            if (thing->rdthing.curGeoMode != thing->rdthing.desiredGeoMode) {
#endif
            thing->rdthing.curGeoMode = thing->rdthing.desiredGeoMode;
            rdVector_Copy3(&thing->lookOrientation.scale, &thing->position);
            rdThing_Draw(&thing->rdthing, &thing->lookOrientation);
#ifdef TARGET_TWL
            }
#endif

            thing->lookOrientation.scale.x = 0.0;
            thing->lookOrientation.scale.y = 0.0;
            thing->lookOrientation.scale.z = 0.0;
            thing->rdthing.curGeoMode = oldGeoMode;

            if (playerInfo->rd_thing.model3)
                rdThing_Draw(&playerInfo->rd_thing, primaryMat);

            if (thing->jkFlags & JKFLAG_SABERON)
            {
                jkSaber_PolylineRand(&playerInfo->polylineThing);
                rdThing_Draw(&playerInfo->polylineThing, primaryMat);
                if ( thing->jkFlags & JKFLAG_DUALSABERS)
                    rdThing_Draw(&playerInfo->polylineThing, secondaryMat);
            }
        }
        else
        {
            jkPlayer_renderSaberTwinkle(thing);
        }
    }
    else if ( thing->rdthing.curGeoMode > RD_GEOMODE_NOTRENDERED)
    {
        if (playerInfo->rd_thing.model3)
            rdThing_Draw(&playerInfo->rd_thing, primaryMat);
        
        if (thing->jkFlags & JKFLAG_SABERON)
        {
            jkSaber_PolylineRand(&playerInfo->polylineThing);
            rdThing_Draw(&playerInfo->polylineThing, primaryMat);
            if (thing->jkFlags & JKFLAG_DUALSABERS)
                rdThing_Draw(&playerInfo->polylineThing, secondaryMat);
        }
    }
}

void jkPlayer_renderSaberTwinkle(sithThing *player)
{
    rdVector3 vTmp;
    rdMatrix34 matTmp;

    jkPlayerInfo* playerInfo = player->playerInfo;
    if ( sithTime_curMs > playerInfo->nextTwinkleRandMs )
    {
        playerInfo->bRenderTwinkleParticle = 1;
        
        //TODO: macro bug?
        if ((_frand() * (flex_d_t)playerInfo->twinkleSpawnRate) <= playerInfo->maxTwinkles )
            playerInfo->numTwinkles = playerInfo->maxTwinkles;
        else
            playerInfo->numTwinkles = (int)(_frand() * (flex_d_t)playerInfo->twinkleSpawnRate);

        playerInfo->nextTwinkleRandMs += 2000;
    }
    if ( playerInfo->bRenderTwinkleParticle )
    {
        if ( sithTime_curMs > playerInfo->nextTwinkleSpawnMs )
        {
            rdThing* rdthing = &playerInfo->actorThing->rdthing;
            playerInfo->nextTwinkleSpawnMs += 40;
            rdModel3* model = rdthing->model3;
            
            // Added: Changed both of these from `_frand() * max` to `_rand() % max`
            // to prevent an off-by-one heap buffer overflow.
            uint32_t meshIdx = model->hierarchyNodes[_rand() % model->numHierarchyNodes].meshIdx;

            if ( meshIdx != -1 && model->geosets[0].meshes[meshIdx].numVertices)
            {
                uint32_t vtxIdx = (_rand() % model->geosets[0].meshes[meshIdx].numVertices);

                rdModel3_GetMeshMatrix(rdthing, &playerInfo->actorThing->lookOrientation, meshIdx, &matTmp);
                rdMatrix_TransformPoint34(&vTmp, &model->geosets[0].meshes[meshIdx].vertices[vtxIdx], &matTmp);

                sithThing_Create(sithTemplate_GetEntryByName("+twinkle"), &vTmp, &matTmp, player->sector, 0);

                playerInfo->numTwinkles--;
                if ( !playerInfo->numTwinkles )
                    playerInfo->bRenderTwinkleParticle = 0;
            }
        }
    }
}

void jkPlayer_SetWaggle(sithThing *player, rdVector3 *waggleVec, flex_t waggleMag)
{
    if ( player == playerThings[playerThingIdx].actorThing )
    {
        rdVector_Copy3(&jkPlayer_waggleVec, waggleVec);
        jkPlayer_waggleMag = waggleMag;
    }
}

int jkPlayer_VerifyWcharName(wchar_t *name)
{
    wchar_t *v1; // edi
    wchar_t v2; // ax
    int v3; // ebx
    int v4; // esi
    int v5; // ecx
    int v7; // ecx
    int v9; // ecx

    v1 = name;
    v2 = *name;
    if ( *name )
    {
        v3 = 1;
        while ( 1 )
        {
            v4 = 0;
            v5 = v2 >= 0x20u && v2 <= 0x7Eu;
            if ( !v5 && v2 != 161 && (v2 < 0xBFu || v2 > 0xC4u) )
            {
                v7 = v2 >= 0xC7u && v2 <= 0xC8u;
                if ( !v7 && v2 != 202 && v2 != 205 && (v2 < 0xD1u || v2 > 0xD2u) )
                {
                    v9 = v2 >= 0xD4u && v2 <= 0xD6u;
                    if ( !v9
                      && v2 != 0xDA
                      && v2 != 220
                      && (v2 < 0xDFu || v2 > 0xE4u)
                      && (v2 < 0xE7u || v2 > 0xEFu)
                      && (v2 < 0xF1u || v2 > 0xF6u)
                      && (v2 < 0xF9u || v2 > 0xFCu) )
                    {
                        break;
                    }
                }
            }
            if ( v2 == '\\' || v2 == '/' || v2 == ':' || v2 == '*' || v2 == '?' || v2 == '"' || v2 == '<' || v2 == '.' || v2 == '>' || v2 == '|' )
                break;
            if ( _iswspace(v2) )
                v4 = 1;
            else
                v3 = 0;
            v2 = v1[1];
            ++v1;
            if ( !v2 )
                return v3 != 1 && v4 != 1;
        }
    }
    return 0;
}

int jkPlayer_VerifyCharName(char *name)
{
    wchar_t tmp[64];

    stdString_CharToWchar(tmp, name, 63);
    tmp[63] = 0;
    return jkPlayer_VerifyWcharName(tmp);
}

void jkPlayer_SetMpcInfo(wchar_t *name, char *model, char *soundclass, char *sidemat, char *tipmat)
{
    jkPlayer_mpcInfoSet = 1;
    
    // TODO macro these
    _strncpy(jkPlayer_model, model, 0x1Fu);
    jkPlayer_model[31] = 0;
    _strncpy(jkPlayer_soundClass, soundclass, 0x1Fu);
    jkPlayer_soundClass[31] = 0;
    _strncpy(jkPlayer_sideMat, sidemat, 0x1Fu);
    jkPlayer_sideMat[31] = 0;
    _strncpy(jkPlayer_tipMat, tipmat, 0x1Fu);
    jkPlayer_tipMat[31] = 0;
    _wcsncpy(jkPlayer_name, name, 0x1Fu);
    jkPlayer_name[31] = 0;
}

void jkPlayer_SetPlayerName(wchar_t *name)
{
    _wcsncpy(jkPlayer_name, name, 0x1Fu);
    jkPlayer_name[31] = 0;
}

int jkPlayer_GetMpcInfo(wchar_t *name, char *model, char *soundclass, char *sidemat, char *tipmat)
{
    _wcsncpy(name, jkPlayer_name, 0x1Fu);
    name[31] = 0;

    if (!jkPlayer_mpcInfoSet)
        return 0;

    _strncpy(model, jkPlayer_model, 0x1Fu);
    model[31] = 0;
    _strncpy(soundclass, jkPlayer_soundClass, 0x1Fu);
    soundclass[31] = 0;
    _strncpy(sidemat, jkPlayer_sideMat, 0x1Fu);
    sidemat[31] = 0;
    _strncpy(tipmat, jkPlayer_tipMat, 0x1Fu);
    tipmat[31] = 0;
    return 1;
}

void jkPlayer_SetChoice(signed int amt)
{
    sithPlayer_SetBinAmt(SITHBIN_CHOICE, (flex_t)amt); // FLEXTODO
}

int jkPlayer_GetChoice()
{
    return (int)sithPlayer_GetBinAmt(SITHBIN_CHOICE);
}

//MOTS altered
flex_t jkPlayer_CalcAlignment(int isMp)
{
    if (jkPlayer_GetChoice() == 1)
        return 100.0;
    if (jkPlayer_GetChoice() == 2)
        return -100.0;

    flex_t alignment = jkPlayer_CalcStarsAlign();

    if (!isMp)
    {
        flex_t pedsKilled = sithPlayer_GetBinAmt(SITHBIN_PEDS_KILLED);
        flex_t totalPeds = sithPlayer_GetBinAmt(SITHBIN_PEDS_TOTAL);

        if (totalPeds <= 0.0) // Prevent div 0
            alignment -= -20.0;
        else
            alignment = (alignment - (pedsKilled / totalPeds) * 100.0) - -20.0;
            //alignment -= (pedsKilled / totalPeds * 100.0) - -20.0;
            // These are different between MinGW and Clang??
    }

    // TODO macro?
    if ( alignment > 100.0 )
        alignment = 100.0;
    if ( alignment < -100.0 )
        alignment = -100.0;

    sithPlayer_SetBinAmt(SITHBIN_ALIGNMENT, alignment);

    return alignment;
}

void jkPlayer_MpcInitBins(sithPlayerInfo* unk)
{
    flex_t alignment; // [esp+8h] [ebp-E8h]
    jkPlayerMpcInfo info; // [esp+Ch] [ebp-E4h] BYREF

    jkPlayer_MPCParse(&info, unk, jkPlayer_playerShortName, jkPlayer_name, 1);
    jkPlayer_InitForceBins();
    if ( (unsigned int)(__int64)sithPlayer_GetBinAmt(SITHBIN_CHOICE) != 1 && (unsigned int)(__int64)sithPlayer_GetBinAmt(SITHBIN_CHOICE) != 2 )
    {
        alignment = jkPlayer_CalcStarsAlign();
        if ( alignment > 100.0 )
            alignment = 100.0;
        if ( alignment < -100.0 )
            alignment = -100.0;
        sithPlayer_SetBinAmt(SITHBIN_ALIGNMENT, alignment);
    }
}

// MOTS altered TODO
int jkPlayer_MPCParse(jkPlayerMpcInfo *info, sithPlayerInfo* unk, wchar_t *fname, wchar_t *name, int hasBins)
{
    int v6; // edi
    flex_t a2; // [esp+Ch] [ebp-CCh] BYREF
    int v8; // [esp+10h] [ebp-C8h] BYREF
    char v9; // [esp+14h] [ebp-C4h] BYREF
    char a1a[32]; // [esp+18h] [ebp-C0h] BYREF
    char v11[32]; // [esp+38h] [ebp-A0h] BYREF
    char jkl_fname[128]; // [esp+58h] [ebp-80h] BYREF

    stdString_WcharToChar(a1a, fname, 31);
    a1a[31] = 0;
    stdString_WcharToChar(v11, name, 31);
    v11[31] = 0;
    _wcsncpy(jkPlayer_name, name, 0x1Fu);
    jkPlayer_name[31] = 0;
    _wcsncpy(info->name, name, 0x1Fu);
    info->name[31] = 0;
    _sprintf(jkl_fname, "player\\%s\\%s.mpc", a1a, v11);

    if (!stdConffile_OpenReadBypass(jkl_fname))
        return 0;

    if ( stdConffile_ReadLine()
      && _sscanf(stdConffile_aLine, "version %d", &v8) == 1
      && v8 == 1
      && stdConffile_ReadLine()
      && _sscanf(stdConffile_aLine, "model: %s", jkPlayer_model) == 1
      && stdConffile_ReadLine()
      && _sscanf(stdConffile_aLine, "soundclass: %s", jkPlayer_soundClass) == 1
      && stdConffile_ReadLine()
      && _sscanf(stdConffile_aLine, "sidemat: %s", jkPlayer_sideMat) == 1
      && stdConffile_ReadLine()
      && _sscanf(stdConffile_aLine, "tipmat: %s", jkPlayer_tipMat) == 1 )
    {
        if (Main_bMotsCompat) {
            if (!stdConffile_ReadLine() || _sscanf(stdConffile_aLine, "personality: %d", &jkPlayer_personality) != 1) {
                stdConffile_Close();
                return 0;
            }
        }
        else {
            jkPlayer_personality = 1; // HACK: JK only has Jedi classes.
        }

        _strncpy(info->model, jkPlayer_model, 0x1Fu);
        info->model[31] = 0;
        _strncpy(info->soundClass, jkPlayer_soundClass, 0x1Fu);
        info->soundClass[31] = 0;
        _strncpy(info->sideMat, jkPlayer_sideMat, 0x1Fu);
        info->sideMat[31] = 0;
        _strncpy(info->tipMat, jkPlayer_tipMat, 0x1Fu);
        info->tipMat[31] = 0;
        info->personality = jkPlayer_personality; // MOTS added
        if ( hasBins )
        {
            jkPlayer_MPCBinRead();
        }
        info->jediRank = jkPlayer_GetJediRank();
        stdConffile_Close();

        
        jkPlayer_SetAmmoMaximums(jkPlayer_personality); // MOTS added
        jkPlayer_mpcInfoSet = 1;
        return 1;
    }
    else
    {
        stdConffile_Close();
        return 0;
    }

    return 0;
}

int jkPlayer_MPCWrite(sithPlayerInfo* unk, wchar_t *mpcName, wchar_t *playerName)
{
    int v4; // esi
    char mpcNameChar[32]; // [esp+10h] [ebp-C0h] BYREF
    char playerNameChar[32]; // [esp+30h] [ebp-A0h] BYREF
    char fpath[128]; // [esp+50h] [ebp-80h] BYREF

    stdString_WcharToChar(playerNameChar, playerName, 31);
    playerNameChar[31] = 0;
    stdString_WcharToChar(mpcNameChar, mpcName, 31);
    mpcNameChar[31] = 0;
    stdString_snprintf(fpath, 128, "player\\%s\\%s.mpc", mpcNameChar, playerNameChar);

    if (!stdConffile_OpenWriteBypass(fpath))
        return 0;

    stdConffile_Printf("version %d\n", 1);
    if ( stdConffile_Printf("model: %s\n", jkPlayer_model)
      && stdConffile_Printf("soundclass: %s\n", jkPlayer_soundClass)
      && stdConffile_Printf("sidemat: %s\n", jkPlayer_sideMat)
      && stdConffile_Printf("tipmat: %s\n", jkPlayer_tipMat))
    {
        if (Main_bMotsCompat) {
            stdConffile_Printf("personality: %d\n", jkPlayer_personality);
        }

        v4 = jkPlayer_MPCBinWrite();
        stdConffile_CloseWrite();
        return v4;
    }
    stdConffile_CloseWrite();
    return 0;
}

int jkPlayer_MPCBinWrite()
{
    int v0; // esi
    flex_d_t v1; // st7
    flex_d_t v2; // st7

    if (!stdConffile_Printf("\nforcepowers:\n") )
        return 0;

    v0 = SITHBIN_FP_START;
    while ( 1 )
    {
        if ( !stdConffile_Printf("bin: %d value: %f\n", v0, sithPlayer_GetBinAmt(v0)) )
            break;

        if ( ++v0 > SITHBIN_FP_END )
        {
            return stdConffile_Printf("spendable stars: %f\n", sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS));
        }
    }

    return 0;
}

// MOTS added: weird xor crypt
int jkPlayer_MPCBinRead()
{
    flex32_t a2;
    int v3;

    stdConffile_ReadLine();
    for (int i = SITHBIN_FP_START; i <= SITHBIN_FP_END; ++i )
    {
        if ( !stdConffile_ReadLine() || _sscanf(stdConffile_aLine, "bin: %d value: %f\n", &v3, &a2) != 2 )
            return 0;

        sithPlayer_SetBinAmt(i, a2);
        sithPlayer_SetBinCarries(i, 1);
    }

    if ( !stdConffile_ReadLine() || _sscanf(stdConffile_aLine, "spendable stars: %f\n", &a2) != 1 )
        return 0;

    sithPlayer_SetBinAmt(SITHBIN_SPEND_STARS, a2);
    return 1;
}

void jkPlayer_InitForceBins()
{
    for (int i = SITHBIN_FP_START; i <= SITHBIN_FP_END; ++i )
    {
        if ( i != SITHBIN_JEDI_RANK )
        {
            if ( sithPlayer_GetBinAmt(i) > 0.0 && jkPlayer_playerInfos[playerThingIdx].iteminfo[i].state & ITEMSTATE_CARRIES)
            {
                jkPlayer_playerInfos[playerThingIdx].iteminfo[i].state |= ITEMSTATE_AVAILABLE;
            }
            else
            {
                jkPlayer_playerInfos[playerThingIdx].iteminfo[i].state &= ~ITEMSTATE_AVAILABLE;
            }
        }
    }
}

int jkPlayer_GetAlignment()
{
    flex_t v4;

    int bHasDarkPowers = 0;
    for (int i = SITHBIN_F_THROW; i <= SITHBIN_F_DESTRUCTION; ++i )
    {
        if ( sithPlayer_GetBinAmt(i) > 0.0 )
            bHasDarkPowers = 1;
    }

    int bHasLightPowers = 0;
    for (int j = SITHBIN_F_HEALING; j <= SITHBIN_F_ABSORB; ++j )
    {
        if ( sithPlayer_GetBinAmt(j) > 0.0 )
            bHasLightPowers = 1;
    }

    if (!bHasDarkPowers && !bHasLightPowers)
        return 0;

    if ( !bHasLightPowers )
    {
        v4 = jkPlayer_CalcAlignment(0); // not mp
        if ( v4 < 0.0 )
            return 2;

        if ( !bHasLightPowers )
            return 0;
    }
    if ( !bHasDarkPowers )
    {
        if ( (unsigned int)(__int64)sithPlayer_GetBinAmt(SITHBIN_CHOICE) == 1 )
        {
            v4 = 100.0;
        }
        else if ( (unsigned int)(__int64)sithPlayer_GetBinAmt(SITHBIN_CHOICE) == 2 )
        {
            v4 = -100.0;
        }
        else
        {
            v4 = jkPlayer_CalcAlignment(0); // not mp
        }
        if ( v4 > 0.0 )
            return 1;
    }
    return 0;
}

void jkPlayer_SetAccessiblePowers(int rank)
{
    //MOTS TODO
    if (Main_bMotsCompat) {
#ifdef DEBUG_QOL_CHEATS
        for (int i = SITHBIN_FP_START; i <= SITHBIN_FP_END; ++i )
        {
            if ( i != SITHBIN_JEDI_RANK )
                jkPlayer_playerInfos[playerThingIdx].iteminfo[i].state |= ITEMSTATE_CARRIES;
        }
#endif
        return;
    }

    for (int i = SITHBIN_FP_START; i <= SITHBIN_FP_END; ++i )
    {
        if ( i != SITHBIN_JEDI_RANK )
            jkPlayer_playerInfos[playerThingIdx].iteminfo[i].state &= ~ITEMSTATE_CARRIES;
    }

    if ( rank )
    {
        for (int j = SITHBIN_FP_START; j <= SITHBIN_F_PULL; ++j )
        {
            if ( j != SITHBIN_JEDI_RANK )
                jkPlayer_playerInfos[playerThingIdx].iteminfo[j].state |= ITEMSTATE_CARRIES;
        }

        if ( rank > 3 )
        {
            jkPlayer_playerInfos[playerThingIdx].iteminfo[SITHBIN_F_HEALING].state |= ITEMSTATE_CARRIES;
            jkPlayer_playerInfos[playerThingIdx].iteminfo[SITHBIN_F_THROW].state |= ITEMSTATE_CARRIES;
            
            if ( rank > 4 )
            {
                jkPlayer_playerInfos[playerThingIdx].iteminfo[SITHBIN_F_PERSUASION].state |= ITEMSTATE_CARRIES;
                jkPlayer_playerInfos[playerThingIdx].iteminfo[SITHBIN_F_GRIP].state |= ITEMSTATE_CARRIES;
                
                if ( rank > 5 )
                {
                    jkPlayer_playerInfos[playerThingIdx].iteminfo[SITHBIN_F_BLINDING].state |= ITEMSTATE_CARRIES;
                    jkPlayer_playerInfos[playerThingIdx].iteminfo[SITHBIN_F_LIGHTNING].state |= ITEMSTATE_CARRIES;
                    if ( rank > 6 )
                    {
                        jkPlayer_playerInfos[playerThingIdx].iteminfo[SITHBIN_F_ABSORB].state |= ITEMSTATE_CARRIES;
                        jkPlayer_playerInfos[playerThingIdx].iteminfo[SITHBIN_F_DESTRUCTION].state |= ITEMSTATE_CARRIES;
                    }
                }
            }
        }
    }
}

void jkPlayer_ResetPowers()
{
    for (int i = SITHBIN_FP_START; i <= SITHBIN_FP_END; ++i )
    {
        if ( i != SITHBIN_JEDI_RANK )
            sithPlayer_SetBinAmt(i, 0.0);
    }
}

int jkPlayer_WriteConfSwap(jkPlayerInfo* unk, int a2, char *a3)
{
    int v3; // ebx
    int v4; // edx
    char *v5; // ebp
    int v7; // eax
    int v8; // ecx
    int v9; // ebp
    char *v10; // edi
    int *v11; // esi
    int v12; // [esp+10h] [ebp-A4h]
    char v13[32]; // [esp+14h] [ebp-A0h] BYREF
    char v14[128]; // [esp+34h] [ebp-80h] BYREF

    v3 = sithControl_IsOpen();
    v12 = v3;
    if ( v3 )
        sithControl_Close();
    jkPlayer_ReadConf(jkPlayer_playerShortName);
    v4 = 0;
    if ( jkPlayer_setNumCutscenes <= 0 )
    {
LABEL_8:
        if ( jkPlayer_setNumCutscenes >= 32 )
            return 0;
        _strncpy(&jkPlayer_cutscenePath[32 * jkPlayer_setNumCutscenes], a3, 0x1Fu);
        v7 = jkPlayer_setNumCutscenes;
        v8 = 32 * jkPlayer_setNumCutscenes;
        jkPlayer_aCutsceneVal[jkPlayer_setNumCutscenes] = a2;
        jkPlayer_setNumCutscenes = v7 + 1;
        jkPlayer_cutscenePath[v8 + 31] = 0;
        stdString_WcharToChar(v13, jkPlayer_playerShortName, 31);
        v13[31] = 0;
        stdString_snprintf(v14, 128, "player\\%s\\%s.plr", v13, v13);
        if ( stdConffile_OpenWriteBypass(v14) )
        {
            stdConffile_Printf("version %d\n", 1);
            stdConffile_Printf("diff %d\n", jkPlayer_setDiff);
            jkPlayer_WriteOptionsConf();
            sithWeapon_WriteConf();
            sithControl_WriteConf();
            if ( stdConffile_Printf("numCutscenes %d\n", jkPlayer_setNumCutscenes) )
            {
                v9 = 0;
                if ( jkPlayer_setNumCutscenes > 0 )
                {
                    v10 = jkPlayer_cutscenePath;
                    v11 = jkPlayer_aCutsceneVal;
                    do
                    {
                        if ( !stdConffile_Printf("%s %d\n", v10, *v11) )
                            break;
                        ++v9;
                        ++v11;
                        v10 += 32;
                    }
                    while ( v9 < jkPlayer_setNumCutscenes );
                }
            }
            stdConffile_CloseWrite();
        }
        if ( v3 )
            sithControl_Open();
    }
    else
    {
        v5 = jkPlayer_cutscenePath;
        while ( _strcmp(v5, a3) )
        {
            ++v4;
            v5 += 32;
            if ( v4 >= jkPlayer_setNumCutscenes )
            {
                v3 = v12;
                goto LABEL_8;
            }
        }
    }
    return 1;
}

int jkPlayer_WriteCutsceneConf()
{
    int v0; // esi
    char *v1; // ebx
    int *i; // edi

    if ( !stdConffile_Printf("numCutscenes %d\n", jkPlayer_setNumCutscenes) )
        return 0;
    v0 = 0;
    if ( jkPlayer_setNumCutscenes > 0 )
    {
        v1 = jkPlayer_cutscenePath;
        for ( i = jkPlayer_aCutsceneVal; stdConffile_Printf("%s %d\n", v1, *i); ++i )
        {
            ++v0;
            v1 += 32;
            if ( v0 >= jkPlayer_setNumCutscenes )
                return 1;
        }
        return 0;
    }
    return 1;
}

int jkPlayer_ReadCutsceneConf()
{
    int v0; // esi
    int *v1; // ebx
    char *i; // edi

    if ( stdConffile_ReadArgs()
      && stdConffile_entry.numArgs
      && !_strcmp(stdConffile_entry.args[0].key, "numcutscenes")
      && _sscanf(stdConffile_entry.args[1].value, "%d", &jkPlayer_setNumCutscenes) == 1 )
    {
        v0 = 0;
        if ( jkPlayer_setNumCutscenes <= 0 )
            return 1;
        v1 = jkPlayer_aCutsceneVal;
        for ( i = jkPlayer_cutscenePath;
              stdConffile_ReadArgs()
           && stdConffile_entry.numArgs >= 2u
           && _sscanf(stdConffile_entry.args[0].key, "%s", i) == 1
           && _sscanf(stdConffile_entry.args[1].value, "%d", v1) == 1;
              i += 32 )
        {
            ++v0;
            ++v1;
            if ( v0 >= jkPlayer_setNumCutscenes )
                return 1;
        }
    }
    return 0;
}

void jkPlayer_FixStars()
{
    int v0; // ebx
    int v1; // esi
    int i; // edi
    int v3; // esi
    __int64 v4; // rax
    __int64 v5; // rax
    __int64 v6; // rax
    __int64 v7; // rax
    __int64 v8; // rax
    __int64 v9; // rax
    __int64 v10; // rax
    __int64 v11; // rax
    __int64 v12; // rax
    __int64 v13; // rax
    __int64 v14; // rax
    __int64 v15; // rax
    __int64 v16; // rax
    flex_t a2; // [esp+0h] [ebp-14h]
    flex_t a2a; // [esp+0h] [ebp-14h]
    flex_t a2b; // [esp+0h] [ebp-14h]
    flex_t a2c; // [esp+0h] [ebp-14h]
    flex_t a2d; // [esp+0h] [ebp-14h]
    flex_t a2e; // [esp+0h] [ebp-14h]
    flex_t a2f; // [esp+0h] [ebp-14h]
    flex_t a2g; // [esp+0h] [ebp-14h]
    flex_t a2h; // [esp+0h] [ebp-14h]
    flex_t a2i; // [esp+0h] [ebp-14h]
    flex_t a2j; // [esp+0h] [ebp-14h]
    flex_t a2k; // [esp+0h] [ebp-14h]
    flex_t a2l; // [esp+0h] [ebp-14h]
    flex_t a2m; // [esp+0h] [ebp-14h]

    // MOTS TODO
    if (Main_bMotsCompat) return;

    v0 = 3 * jkPlayer_GetJediRank();
    v1 = (__int64)sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS);
    for ( i = SITHBIN_FP_START; i <= SITHBIN_FP_END; ++i )
    {
        if ( i != SITHBIN_JEDI_RANK && i != SITHBIN_F_PROTECTION && i != SITHBIN_F_DEADLYSIGHT )
            v1 += (__int64)sithPlayer_GetBinAmt(i);
    }
    if ( v0 > v1 )
    {
        a2 = (flex_t)(v0 - v1); // FLEXTODO
        sithPlayer_SetBinAmt(SITHBIN_SPEND_STARS, a2);
        return;
    }
    if ( v0 < v1 )
    {
        v3 = v1 - v0;
        if ( v3 > 0 )
        {
            while ( 1 )
            {
                // TODO un-inline whatever this is
                v4 = (__int64)sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS);
                if ( (int)v4 > 0 )
                    break;
                v5 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_DESTRUCTION);
                if ( (int)v5 > 0 )
                {
                    a2b = (flex_t)(v5 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_DESTRUCTION, a2b);
                    goto LABEL_37;
                }
                v6 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_ABSORB);
                if ( (int)v6 > 0 )
                {
                    a2c = (flex_t)(v6 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_ABSORB, a2c);
                    goto LABEL_37;
                }
                v7 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_LIGHTNING);
                if ( (int)v7 > 0 )
                {
                    a2d = (flex_t)(v7 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_LIGHTNING, a2d);
                    goto LABEL_37;
                }
                v8 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_BLINDING);
                if ( (int)v8 > 0 )
                {
                    a2e = (flex_t)(v8 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_BLINDING, a2e);
                    goto LABEL_37;
                }
                v9 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_GRIP);
                if ( (int)v9 > 0 )
                {
                    a2f = (flex_t)(v9 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_GRIP, a2f);
                    goto LABEL_37;
                }
                v10 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_PERSUASION);
                if ( (int)v10 > 0 )
                {
                    a2g = (flex_t)(v10 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_PERSUASION, a2g);
                    goto LABEL_37;
                }
                v11 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_THROW);
                if ( (int)v11 > 0 )
                {
                    a2h = (flex_t)(v11 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_THROW, a2h);
                    goto LABEL_37;
                }
                v12 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_HEALING);
                if ( (int)v12 > 0 )
                {
                    a2i = (flex_t)(v12 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_HEALING, a2i);
                    goto LABEL_37;
                }
                v13 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_PULL);
                if ( (int)v13 > 0 )
                {
                    a2j = (flex_t)(v13 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_PULL, a2j);
                    goto LABEL_37;
                }
                v14 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_SEEING);
                if ( (int)v14 > 0 )
                {
                    a2k = (flex_t)(v14 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_SEEING, a2k);
                    goto LABEL_37;
                }
                v15 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_SPEED);
                if ( (int)v15 > 0 )
                {
                    a2l = (flex_t)(v15 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_SPEED, a2l);
                    goto LABEL_37;
                }
                v16 = (__int64)sithPlayer_GetBinAmt(SITHBIN_F_JUMP);
                if ( (int)v16 > 0 )
                {
                    a2m = (flex_t)(v16 - 1); // FLEXTODO
                    sithPlayer_SetBinAmt(SITHBIN_F_JUMP, a2m);
                    goto LABEL_37;
                }
LABEL_38:
                if ( v3 <= 0 )
                    return;
            }
            a2a = (flex_t)(v4 - 1); // FLEXTODO
            sithPlayer_SetBinAmt(SITHBIN_SPEND_STARS, a2a);
LABEL_37:
            --v3;
            goto LABEL_38;
        }
    }
}

flex_t jkPlayer_CalcStarsAlign()
{
    flex_t alignment = 0.0;

    // MOTS: Return 0.0 always
    if (Main_bMotsCompat) return 0.0;

    for (int i = SITHBIN_F_THROW; i <= SITHBIN_F_DESTRUCTION; ++i )
    {
        flex_t amt = sithPlayer_GetBinAmt(i);
        alignment -= amt * 6.25;
    }
    
    for (int j = SITHBIN_F_HEALING; j <= SITHBIN_F_ABSORB; ++j )
    {
        flex_t amt = sithPlayer_GetBinAmt(j);
        alignment -= amt * -6.25;
    }
    
    return alignment;
}

int jkPlayer_SetProtectionDeadlysight()
{
    // MOTS TODO
    if (Main_bMotsCompat) return 0;

    int rank = jkPlayer_GetJediRank();

    int hasNoDarkside = 1;
    for (int i = SITHBIN_F_THROW; i <= SITHBIN_F_DESTRUCTION; ++i )
    {
        if (sithPlayer_GetBinAmt(i) > 0.0)
            hasNoDarkside = 0;
    }

    int hasFullDarkside = 1;
    for (int j = SITHBIN_F_THROW; j <= SITHBIN_F_DESTRUCTION; ++j )
    {
        if (sithPlayer_GetBinAmt(j) < 4.0)
            hasFullDarkside = 0;
    }

    int hasNoLightside = 1;
    for (int k = SITHBIN_F_HEALING; k <= SITHBIN_F_ABSORB; ++k )
    {
        if (sithPlayer_GetBinAmt(k) > 0.0)
            hasNoLightside = 0;
    }

    int hasFullLightside = 1;
    for (int l = SITHBIN_F_HEALING; l <= SITHBIN_F_ABSORB; ++l )
    {
        if (sithPlayer_GetBinAmt(l) < 4.0)
            hasFullLightside = 0;
    }

    int hasNoNeutral = 1;
    for (int m = SITHBIN_FP_START; m <= SITHBIN_F_PULL; ++m )
    {
        if (m == SITHBIN_JEDI_RANK) continue;
        if (sithPlayer_GetBinAmt(m) > 0.0)
            hasNoNeutral = 0;
    }

    if (rank == 8)
    {
        if ( hasFullLightside && hasNoDarkside && hasNoNeutral )
        {
            sithPlayer_SetBinAmt(SITHBIN_F_PROTECTION, 4.0);
            sithPlayer_SetBinCarries(SITHBIN_F_PROTECTION, 1);
            sithPlayer_SetBinAmt(SITHBIN_F_DEADLYSIGHT, 0.0);
            sithPlayer_SetBinCarries(SITHBIN_F_DEADLYSIGHT, 0);
            return 1;
        }
        if ( hasFullDarkside && hasNoLightside && hasNoNeutral )
        {
            sithPlayer_SetBinAmt(SITHBIN_F_DEADLYSIGHT, 4.0);
            sithPlayer_SetBinCarries(SITHBIN_F_DEADLYSIGHT, 1);
            sithPlayer_SetBinAmt(SITHBIN_F_PROTECTION, 0.0);
            sithPlayer_SetBinCarries(SITHBIN_F_PROTECTION, 0);
            return 2;
        }
        sithPlayer_SetBinAmt(SITHBIN_F_PROTECTION, 0.0);
        sithPlayer_SetBinCarries(SITHBIN_F_PROTECTION, 0);
        sithPlayer_SetBinAmt(SITHBIN_F_DEADLYSIGHT, 0.0);
        sithPlayer_SetBinCarries(SITHBIN_F_DEADLYSIGHT, 0);
    }
    return 0;
}

void jkPlayer_DisallowOtherSide(int rank)
{
    // MOTS TODO
    if (Main_bMotsCompat) return;

    flex_t align = jkPlayer_CalcStarsAlign();

    if ( rank < 7 )
        return;

    if ( align <= 0.0 )
    {
        if ( align >= 0.0 )
        {
            for (int i = SITHBIN_F_THROW; i < SITHBIN_F_DESTRUCTION; i++)
                sithPlayer_SetBinCarries(i, 1);
            for (int k = SITHBIN_F_HEALING; k <= SITHBIN_F_ABSORB; ++k )
                sithPlayer_SetBinCarries(k, 1);
        }
        else
        {
            for (int i = SITHBIN_F_THROW; i < SITHBIN_F_DESTRUCTION; i++)
                sithPlayer_SetBinCarries(i, 1);
            for (int l = SITHBIN_F_HEALING; l <= SITHBIN_F_ABSORB; ++l )
                sithPlayer_SetBinCarries(l, 0);
        }
    }
    else
    {
        for (int m = SITHBIN_F_THROW; m <= SITHBIN_F_DESTRUCTION; ++m )
            sithPlayer_SetBinCarries(m, 0);
        for (int n = SITHBIN_F_HEALING; n <= SITHBIN_F_ABSORB; ++n )
            sithPlayer_SetBinCarries(n, 1);
    }
}

int jkPlayer_WriteOptionsConf()
{
    return stdConffile_Printf("fullsubtitles %d\n", jkPlayer_setFullSubtitles)
        && stdConffile_Printf("disablecutscenes %d\n", jkPlayer_setDisableCutscenes)
        && stdConffile_Printf("rotateoverlaymap %d\n", jkPlayer_setRotateOverlayMap)
        && stdConffile_Printf("drawstatus %d\n", jkPlayer_setDrawStatus)
        && stdConffile_Printf("crosshair %d\n", jkPlayer_setCrosshair)
        && stdConffile_Printf("sabercam %d\n", jkPlayer_setSaberCam);
}

int jkPlayer_ReadOptionsConf()
{
    return stdConffile_ReadLine()
        && _sscanf(stdConffile_aLine, "fullsubtitles %d\n", &jkPlayer_setFullSubtitles) == 1
        && stdConffile_ReadLine()
        && _sscanf(stdConffile_aLine, "disablecutscenes %d\n", &jkPlayer_setDisableCutscenes) == 1
        && stdConffile_ReadLine()
        && _sscanf(stdConffile_aLine, "rotateoverlaymap %d\n", &jkPlayer_setRotateOverlayMap) == 1
        && stdConffile_ReadLine()
        && _sscanf(stdConffile_aLine, "drawstatus %d\n", &jkPlayer_setDrawStatus) == 1
        && stdConffile_ReadLine()
        && _sscanf(stdConffile_aLine, "crosshair %d\n", &jkPlayer_setCrosshair) == 1
        && stdConffile_ReadLine()
        && _sscanf(stdConffile_aLine, "sabercam %d\n", &jkPlayer_setSaberCam) == 1;
}

int jkPlayer_GetJediRank()
{
    return (int)(__int64)(sithPlayer_GetBinAmt(SITHBIN_JEDI_RANK));
}

void jkPlayer_SetRank(int rank)
{
    sithPlayer_SetBinAmt(SITHBIN_JEDI_RANK, (flex_t)rank); // FLEXTODO
}

// MOTS added
static char* jkPlayer_aClassNames[5] = {
    "single",
    "jedi",
    "bounty",
    "scout",
    "soldier",
};

// MOTS added
uint32_t jkPlayer_ChecksumExtra(uint32_t hash)
{
    int iVar1;
    uint32_t uVar2;
    int64_t lVar3;
    int local_8c;
    flex32_t local_88;
    int local_84;
    char local_80 [128];
    
    for (uVar2 = 0; uVar2 < 5; uVar2++) 
    {
        stdString_snprintf(local_80, 128, "misc\\per\\%s.per", jkPlayer_aClassNames[uVar2]); // Added: sprintf -> snprintf
        if (stdConffile_OpenRead(local_80)) 
        {
            if ((stdConffile_ReadLine() && (iVar1 = _sscanf(stdConffile_aLine,"version %d",&local_84), iVar1 == 1)) && (local_84 == 2)) {
                hash = hash + 2;
                while (((stdConffile_ReadLine() && (iVar1 = _sscanf(stdConffile_aLine,"param %d: %f",&local_8c,&local_88), iVar1 == 2)) && ((-1 < local_8c && (local_8c < 0x100))))) {
                    iVar1 = local_8c + 1;
                    lVar3 = (int64_t)(local_88 * 1000.0);
                    hash = hash + (iVar1 + uVar2) * ((uint32_t)lVar3 ^ 0x5b32a);
                    iVar1 = stdConffile_ReadLine();
                }
            }
            stdConffile_Close();
        }
    }
    return hash;
}

// MOTS added
jkPlayerInfo* jkPlayer_FUN_00404fe0(sithThing *pPlayerThing)
{
#ifdef JKM_DSS
    int iVar3;
    
    iVar3 = 0;
    for (iVar3 = 0; iVar3 < NUM_JKPLAYER_THINGS; iVar3++) {
        if (jkPlayer_aMotsInfos[iVar3].actorThing)
            continue;

        jkPlayer_aMotsInfos[iVar3].actorThing = pPlayerThing;
        jkPlayer_aMotsInfos[iVar3].thing_id = pPlayerThing->thing_id;
        jkPlayer_aMotsInfos[iVar3].rd_thing.model3 = NULL;

        pPlayerThing->thingflags |= SITH_TF_RENDERWEAPON;
        pPlayerThing->playerInfo = &jkPlayer_aMotsInfos[iVar3];
        
        return pPlayerThing->playerInfo;
    }
#endif
    return NULL;
}

// NOTS added
int jkPlayer_SetAmmoMaximums(int classIdx)
{
    if (!Main_bMotsCompat) return 1;

#ifdef JKM_DSS
    int iVar1;
    flex_t *pfVar2;
    int local_8c;
    flex32_t local_88;
    int local_84;
    char local_80 [128];
    
    if ((classIdx < 0) || (4 < classIdx)) {
        classIdx = 0;
    }
    _sprintf(local_80,"misc\\per\\%s.per", jkPlayer_aClassNames[classIdx]);
    iVar1 = stdConffile_OpenRead(local_80);
    if (iVar1 != 0) {
        iVar1 = stdConffile_ReadLine();
        if (((iVar1 != 0) && (iVar1 = _sscanf(stdConffile_aLine,"version %d",&local_84), iVar1 == 1)) && (local_84 == 2)) {
            pfVar2 = jkPlayer_aMultiParams;
            for (iVar1 = 0x100; iVar1 != 0; iVar1 = iVar1 + -1) {
                *pfVar2 = 0.0;
                pfVar2 = pfVar2 + 1;
            }
            iVar1 = stdConffile_ReadLine();
            while( 1 ) {
                if (iVar1 == 0) {
                    stdConffile_Close();
                    jkHudInv_FixAmmoMaximums();
                    pfVar2 = jkPlayer_aMultiParams + 51;
                    do {
                        iVar1 = (int)pfVar2[-1];
                        if ((-1 < iVar1) && (iVar1 < 200)) {
                            sithInventory_aDescriptors[iVar1].ammoMax = *pfVar2;
                        }
                        pfVar2 = pfVar2 + 2;
                    } while (pfVar2 < &jkPlayer_aMultiParams[61]);
                    return 1;
                }
                iVar1 = _sscanf(stdConffile_aLine,"param %d: %f",&local_8c,&local_88);
                if (((iVar1 != 2) || (local_8c < 0)) || (0xff < local_8c)) break;
                jkPlayer_aMultiParams[local_8c] = local_88;
                iVar1 = stdConffile_ReadLine();
            }
        }
        stdConffile_Close();
        return 0;
    }
#endif
    return 0;
}

// MOTS added
void jkPlayer_idkEndLevel(void)
{
    if (!Main_bMotsCompat) return;

    int lVar3 = (int)sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS);
    int lVar4 = (int)sithPlayer_GetBinAmt(SITHBIN_NEW_STARS);
    int lVar5 = (int)sithPlayer_GetBinAmt(SITHBIN_F_DEFENSE);
    int local_4 = lVar3 + lVar4 + (lVar5 * 2);

    for (int iVar1 = SITHBIN_F_DEFENSE; iVar1 <= SITHBIN_FP_END; iVar1++) {
        if ((iVar1 != SITHBIN_JEDI_RANK) && (iVar1 != SITHBIN_F_DEFENSE)) {
            lVar3 = (int)sithPlayer_GetBinAmt(iVar1);
            local_4 = local_4 + lVar3;
        }
    }

    local_4 = local_4 / 3;
    if (local_4 < 0) {
        local_4 = 0;
    }
    else if (8 < local_4) {
        local_4 = 8;
    }

    sithPlayer_SetBinAmt(SITHBIN_JEDI_RANK,(flex_t)local_4); // FLEXTODO
}

// MOTS added
int jkPlayer_SyncForcePowers(int rank,int bIsMulti)
{
#ifdef DEBUG_QOL_CHEATS
    return 0;
#endif

    int *piVar2;
    int iVar3;
    flex_t *pfVar4;
    int iVar5;
    int iVar6;
    flex_d_t dVar8;
    flex_t fVar9;
    int *local_c;
    int local_8;
    int local_4;
    
    if (rank < 0) {
        rank = 0;
    }
    else if (8 < rank) {
        rank = 8;
    }

    if (bIsMulti == 0) 
    {
        sithPlayer_SetBinAmt(SITHBIN_F_DEFENSE,0.0);
        if (((0 < rank) &&
            (fVar9 = sithPlayer_GetBinAmt(SITHBIN_F_JUMP), fVar9 < 1.0)) &&
           (fVar9 = sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS), 0.0 < fVar9)) 
        {
            sithPlayer_SetBinAmt(SITHBIN_F_JUMP,1.0);
            fVar9 = sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS);
            sithPlayer_SetBinAmt(SITHBIN_SPEND_STARS,(flex_t)(fVar9 - 1.0)); // FLEXTODO
        }

        if (((1 < rank) &&
            (fVar9 = sithPlayer_GetBinAmt(SITHBIN_F_PULL), fVar9 < 1.0)) &&
           (fVar9 = sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS), 0.0 < fVar9)) 
        {
            sithPlayer_SetBinAmt(SITHBIN_F_PULL,1.0);
            fVar9 = sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS);
            sithPlayer_SetBinAmt(SITHBIN_SPEND_STARS,(flex_t)(fVar9 - 1.0)); // FLEXTODO
        }

        if (((3 < rank) && (fVar9 = sithPlayer_GetBinAmt(SITHBIN_F_SEEING), fVar9 < 1.0)) &&
           (fVar9 = sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS), 0.0 < fVar9)) 
        {
            sithPlayer_SetBinAmt(SITHBIN_F_SEEING,1.0);
            fVar9 = sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS);
            sithPlayer_SetBinAmt(SITHBIN_SPEND_STARS,(flex_t)(fVar9 - 1.0)); // FLEXTODO
        }

        iVar3 = rank;
        if (((4 < rank) && (fVar9 = sithPlayer_GetBinAmt(SITHBIN_F_PERSUASION), fVar9 < 1.0)) &&
           (fVar9 = sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS), 0.0 < fVar9)) 
        {
            sithPlayer_SetBinAmt(SITHBIN_F_PERSUASION,1.0);
            fVar9 = sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS);
            sithPlayer_SetBinAmt(SITHBIN_SPEND_STARS,(flex_t)(fVar9 - 1.0)); // FLEXTODO
        }
    }
    else 
    {
        iVar3 = rank * 3 + (int)jkPlayer_aMultiParams[119] * 2;
        pfVar4 = jkPlayer_aMultiParams + 0x77;

        do {
            if ((pfVar4 != jkPlayer_aMultiParams + 0x78) && (pfVar4 != jkPlayer_aMultiParams + 0x77)
               ) {
                iVar3 = iVar3 + (int)*pfVar4;
            }
            pfVar4 = pfVar4 + 1;
        } while (pfVar4 < &jkPlayer_aMultiParams[0x8C]);

        fVar9 = sithPlayer_GetBinAmt(SITHBIN_F_DEFENSE);
        if (fVar9 < jkPlayer_aMultiParams[119]) {
            sithPlayer_SetBinAmt(SITHBIN_F_DEFENSE,jkPlayer_aMultiParams[119]);
        }
        iVar5 = SITHBIN_F_DEFENSE;
        pfVar4 = jkPlayer_aMultiParams + 0x77;

        do {
            if ((pfVar4 != jkPlayer_aMultiParams + 0x78) && (pfVar4 != jkPlayer_aMultiParams + 0x77)
               ) {
                fVar9 = sithPlayer_GetBinAmt(iVar5);
                if (fVar9 < *pfVar4) {
                    sithPlayer_SetBinAmt(iVar5,*pfVar4);
                }
            }
            pfVar4 = pfVar4 + 1;
            iVar5 = iVar5 + 1;
        } while (pfVar4 < &jkPlayer_aMultiParams[0x8C]);
    }

    iVar5 = (int)sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS) + (int)sithPlayer_GetBinAmt(SITHBIN_F_DEFENSE) * 2;
    iVar6 = SITHBIN_F_DEFENSE;
    do 
    {
        if ((iVar6 != SITHBIN_JEDI_RANK) && (iVar6 != SITHBIN_F_DEFENSE)) {
            iVar5 += (int)sithPlayer_GetBinAmt(iVar6);
        }
        iVar6 = iVar6 + 1;
    } while (iVar6 < SITHBIN_BACTATANK);

    int bVar7 = bIsMulti != 0;
    bIsMulti = 0;
    if (bVar7) 
    {
        if (iVar3 < iVar5) {
            iVar5 = iVar5 - iVar3;
            iVar3 = (int)sithPlayer_GetBinAmt(SITHBIN_SPEND_STARS);
            if (iVar3 < iVar5) {
                iVar5 = iVar5 - iVar3;
                sithPlayer_SetBinAmt(SITHBIN_SPEND_STARS,0.0);
                if (0 < iVar5) {
                    do {
                        fVar9 = sithPlayer_GetBinAmt(SITHBIN_F_DEFENSE);
                        if (fVar9 <= jkPlayer_aMultiParams[119]) break;
                        iVar5 = iVar5 + -2;
                        fVar9 = sithPlayer_GetBinAmt(SITHBIN_F_DEFENSE);
                        sithPlayer_SetBinAmt(SITHBIN_F_DEFENSE,(flex_t)(fVar9 - 1.0)); // FLEXTODO
                    } while (0 < iVar5);

                    if (0 < iVar5) {
                        iVar3 = SITHBIN_F_DEFENSE;
                        pfVar4 = jkPlayer_aMultiParams + 0x77;
                        do {
                            if ((pfVar4 != jkPlayer_aMultiParams + 0x78) &&
                               (pfVar4 != jkPlayer_aMultiParams + 0x77)) {
                                while ((0 < iVar5 &&
                                       (fVar9 = sithPlayer_GetBinAmt(iVar3),
                                       *pfVar4 < fVar9))) {
                                    fVar9 = sithPlayer_GetBinAmt(iVar3);
                                    sithPlayer_SetBinAmt(iVar3,(flex_t)(fVar9 - 1.0)); // FLEXTODO
                                    iVar5 = iVar5 + -1;
                                    if (iVar5 == 0) goto LAB_0040747a;
                                }
                            }
                            pfVar4 = pfVar4 + 1;
                            iVar3 = iVar3 + 1;
                        } while (pfVar4 < &jkPlayer_aMultiParams[0x8C]);
LAB_0040747a:
                        bIsMulti = 0;
                        goto LAB_004074a0;
                    }
                }
                bIsMulti = -iVar5;
            }
            else {
                sithPlayer_SetBinAmt(SITHBIN_SPEND_STARS,(flex_t)(iVar3 - iVar5)); // FLEXTODO
                bIsMulti = 0;
            }
        }
        else {
            bIsMulti = iVar3 - iVar5;
        }
    }
LAB_004074a0:
    for (iVar5 = SITHBIN_FP_START; iVar5 <= SITHBIN_FP_END; iVar5++)
    {
        if (iVar5 != SITHBIN_JEDI_RANK) {
            jkPlayer_playerInfos[playerThingIdx].iteminfo[iVar5].state &= ~ITEMSTATE_CARRIES;
        }
    }

    if ((0 < rank) ||
       (fVar9 = sithPlayer_GetBinAmt(SITHBIN_F_DEFENSE), 
       0.0 < fVar9)) 
    {
        jkPlayer_playerInfos[playerThingIdx].iteminfo[SITHBIN_F_DEFENSE].state |= ITEMSTATE_CARRIES;
    }
    local_4 = 3;
    local_c = jkPlayer_aMotsFpBins + 0x18;
    do 
    {
        if (jkPlayer_aMotsFpBins[(int)sithPlayer_GetBinAmt(SITHBIN_F_DEFENSE) + 0x44] < local_4) {
            local_8 = 0;
        }
        else {
            local_8 = jkPlayer_aMotsFpBins[local_4 + rank * 4 + 0x20];
        }
        iVar3 = 0;
        iVar5 = 8;
        piVar2 = local_c;
        do {
            if ((*piVar2 != 0) &&
               (fVar9 = sithPlayer_GetBinAmt(*piVar2), 0.0 < fVar9)) {
                iVar3 = iVar3 + 1;
            }
            iVar6 = playerThingIdx;
            piVar2 = piVar2 + 1;
            iVar5 = iVar5 + -1;
        } while (iVar5 != 0);

        if (iVar3 < local_8) {
            iVar3 = 8;
            piVar2 = local_c;
            do {
                if (*piVar2 != 0) {
                    jkPlayer_playerInfos[playerThingIdx].iteminfo[*piVar2].state |= ITEMSTATE_CARRIES;
                }
                piVar2 = piVar2 + 1;
                iVar3 = iVar3 + -1;
            } while (iVar3 != 0);
        }
        else {
            iVar5 = 0;
            piVar2 = local_c;
            do {
                if (iVar3 <= local_8) break;
                iVar6 = *piVar2;
                if ((iVar6 != 0) && (jkPlayer_aMultiParams[iVar6 + 100] < 1.0)) {
                    bIsMulti = bIsMulti + (int)sithPlayer_GetBinAmt(iVar6);
                    sithPlayer_SetBinAmt(iVar6,0.0);
                }
                iVar5 = iVar5 + 1;
                piVar2 = piVar2 + 1;
            } while (iVar5 < 8);

            iVar3 = 8;
            piVar2 = local_c;
            do {
                iVar5 = *piVar2;
                if ((iVar5 != 0) &&
                   (fVar9 = sithPlayer_GetBinAmt(iVar5), 0.0 < fVar9)) {
                    jkPlayer_playerInfos[playerThingIdx].iteminfo[iVar5].state |= ITEMSTATE_CARRIES;
                }
                piVar2 = piVar2 + 1;
                iVar3 = iVar3 + -1;
            } while (iVar3 != 0);

        }
        local_c = local_c + -8;
        local_4 = local_4 + -1;
        if (local_c < jkPlayer_aMotsFpBins) {
            return bIsMulti;
        }
    } while( 1 );

    return bIsMulti;
}

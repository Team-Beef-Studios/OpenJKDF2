#include "jkGame.h"

#include "General/stdPalEffects.h"
#include "Main/sithMain.h"
#include "Engine/rdroid.h"
#include "Raster/rdCache.h"
#include "Engine/sithRender.h"
#include "World/sithWorld.h"
#include "World/jkPlayer.h"
#include "World/sithSector.h"
#include "Win95/Video.h"
#include "Win95/stdComm.h"
#include "Platform/std3D.h"
#include "Win95/stdDisplay.h"
#include "Main/jkHud.h"
#include "Main/jkHudInv.h"
#include "Main/jkHudScope.h"
#include "Main/jkHudCameraView.h"
#include "Main/jkDev.h"
#include "Main/jkQuakeConsole.h"
#include "Engine/rdColormap.h"
#include "Engine/sithCamera.h"
#include "General/stdString.h"

#include "stdPlatform.h"
#include "jk.h"

#if defined(TARGET_TWL)
#include <nds.h>
#endif

// Added: VR support
#ifdef PLATFORM_VR
#include "Platform/VR/stdVR.h"
#include "Engine/rdCamera.h"
#include "World/sithWeapon.h"
#include "World/sithTemplate.h"
#include "World/sithThing.h"
#include "Gameplay/sithTime.h"
#include "SDL2_helper.h"
#include "Win95/Window.h"  // For Window_VRMirrorPresent (desktop VR mirror)
extern sithThing* sithPlayer_pLocalPlayerThing;
extern flex_t sithTime_deltaSeconds;
#endif

int jkGame_Startup()
{
    stdPlatform_Printf("OpenJKDF2: %s\n", __func__);
    
    sithWorld_SetSectionParser("jk", jkGame_ParseSection);
    jkGame_bInitted = 1;
    return 1;
}

int jkGame_ParseSection(sithWorld* a1, int a2)
{
    return a2 == 0;
}

void jkGame_ForceRefresh()
{
    sithCamera_Close();
    rdCanvas_Free(Video_pCanvas);
#ifdef SDL2_RENDER
    rdCanvas_Free(Video_pCanvasOverlayMap);
#endif
}

void jkGame_Shutdown()
{
    stdPlatform_Printf("OpenJKDF2: %s\n", __func__);
    
    jkGame_bInitted = 0;
}

void jkGame_ScreensizeIncrease()
{
    if ( Video_modeStruct.viewSizeIdx < 0xAu )
    {
        // MOTS added
        if (Main_bMotsCompat) {
            jkHudScope_Close();
            jkHudCameraView_Close();
        }

#ifndef LINUX_TMP
        sithCamera_Close();
        rdCanvas_Free(Video_pCanvas);
#ifdef SDL2_RENDER
        rdCanvas_Free(Video_pCanvasOverlayMap);
#endif
        ++Video_modeStruct.viewSizeIdx;
        Video_camera_related();
#endif
        // MOTS added
        if (Main_bMotsCompat) {
            jkHudScope_Open();
            jkHudCameraView_Open();
        }
    }
}

void jkGame_ScreensizeDecrease()
{
    if ( Video_modeStruct.viewSizeIdx )
    {
        // MOTS added
        if (Main_bMotsCompat) {
            jkHudScope_Close();
            jkHudCameraView_Close();
        }

#ifndef LINUX_TMP
        sithCamera_Close();
        rdCanvas_Free(Video_pCanvas);
#ifdef SDL2_RENDER
        rdCanvas_Free(Video_pCanvasOverlayMap);
#endif
        --Video_modeStruct.viewSizeIdx;
        Video_camera_related();
#endif
        // MOTS added
        if (Main_bMotsCompat) {
            jkHudScope_Open();
            jkHudCameraView_Open();
        }
    }
}

void jkGame_SetDefaultSettings()
{
    jkPlayer_setFullSubtitles = 0;
    jkPlayer_setDisableCutscenes = 0;
    jkPlayer_setRotateOverlayMap = 1;
    jkPlayer_setDrawStatus = 1;
    jkPlayer_setCrosshair = 0;
    jkPlayer_setSaberCam = 0;
}

int jkGame_Update()
{
    int64_t v0; // rcx
    sithThing *v2; // esi
    int v3; // eax
    flex_d_t v4; // st7
    int result; // eax
    int v6; // [esp+1Ch] [ebp-1Ch]

    static int jkGame_Update_Start = 0;
    static int jkGame_Update_ClearScreen = 0;
    static int jkGame_Update_AdvanceFrame = 0;
    static int jkGame_Update_UpdateCamera = 0;
    static int jkGame_Update_DrawPov = 0;
    static int jkGame_Update_HudDrawn = 0;
    static int jkGame_Update_End = 0;

    jkGame_Update_Start = stdPlatform_GetTimeMsec();

    // HACK HACK HACK: Adjust zNear depending on if we're using the scope/camera views
#if defined(SDL2_RENDER) || defined(TARGET_TWL)
    if (sithCamera_cameras[0].rdCam.pClipFrustum) {
        sithCamera_cameras[0].rdCam.pClipFrustum->zNear = SITHCAMERA_ZNEAR_FIRSTPERSON;

        if (Main_bMotsCompat) {
            if (playerThings[playerThingIdx].actorThing->actorParams.typeflags & SITH_AF_SCOPEHUD) {
                sithCamera_cameras[0].rdCam.pClipFrustum->zNear = SITHCAMERA_ZNEAR;
            }
            if ((playerThings[playerThingIdx].actorThing->actorParams.typeflags & SITH_AF_80000000) != 0) {
                sithCamera_cameras[0].rdCam.pClipFrustum->zNear = SITHCAMERA_ZNEAR;
            }
        }
    }
    
#endif

#if defined(SDL2_RENDER) || defined(TARGET_TWL)
    // HACK
    Video_modeStruct.b3DAccel = 1;
#endif

#if !defined(SDL2_RENDER) && !defined(TARGET_TWL)
    if ( Video_modeStruct.Video_8606C0 || Video_modeStruct.geoMode <= 2 )
#endif
#if !defined(TARGET_TWL)
        stdDisplay_VBufferFill(Video_pMenuBuffer, Video_fillColor, 0); // Significant delay on TWL
#endif
    jkDev_DrawLog();
    jkHudInv_ClearRects();
    jkHud_ClearRects(0);
    jkGame_Update_ClearScreen = stdPlatform_GetTimeMsec();

    stdPalEffects_UpdatePalette(stdDisplay_GetPalette());
#if !defined(SDL2_RENDER) && !defined(TARGET_TWL)
    if ( Video_modeStruct.b3DAccel )
#endif
        rdSetColorEffects(&stdPalEffects_state.effect);

#if defined(SDL2_RENDER) || defined(TARGET_TWL)
    _memcpy(stdDisplay_masterPalette, sithWorld_pCurrentWorld->colormaps->colors, 0x300);
#endif

// Added: VR stereo rendering path
#ifdef PLATFORM_VR
    // Forward declarations for VR logging and state
    extern void VR_Log(const char* fmt, ...);
    extern int stdVR_currentEye;

#ifdef VR_WEAPON_ALIGNMENT_TOOL
    // Update the weapon alignment tool (processes controller input for adjustment)
    if (stdVR_bEnabled) {
        stdVR_AlignmentTool_Update(sithTime_deltaSeconds);
    }
#endif

    if (stdVR_bEnabled && stdVR_IsSessionRunning() && jkGame_isDDraw) {
        static int vrRenderCount = 0;
        vrRenderCount++;

        // If no frame is pending, we need to call WaitFrame ourselves
        // This happens in 3D game mode where Window_Main_Loop doesn't manage VR frames
        if (!stdVR_IsFramePending()) {
            stdVR_WaitFrame();

            // Update VR input after WaitFrame (in 3D mode, Window_Main_Loop skips this)
            stdVR_UpdateInput();
            stdVR_MapInputToGame();
        }

        // Check if we have a VR frame pending to work with
        if (!stdVR_IsFramePending()) {
            goto non_vr_path;
        }

        // VR frame timing - BeginFrame must be called first
        if (!stdVR_BeginFrame()) {
            goto non_vr_path;  // Fall back to non-VR if frame begin fails
        }

        // Update tracking
        stdVR_UpdateTracking();

        // Update screen layer state (will be false for 3D gameplay, true for menus)
        // This ensures bUseScreenLayer is updated when transitioning from menu to gameplay
        stdVR_UseScreenLayer();

        // Only render if the runtime tells us to (headset visible, etc.)
        if (stdVR_clientInfo.bShouldRender) {
            // Do per-frame setup ONCE before eye loop
            rdAdvanceFrame();  // This calls std3D_StartScene via rdCache_AdvanceFrame
            sithCamera_PrepareFrameVR();  // Updates camera position, sets rdCamera

            // Added: Update 3D map geometry (collects sector edges once per frame)
            stdVR_Map3D_Update();

#if defined(MULTIVIEW_ENABLED)  // Single-pass MultiView stereo (desktop PCVR + Quest)
            // === MultiView Path (single-pass stereo rendering) ===
            // Uses GL_OVR_multiview2 to render both eyes in a single draw call.
            // Since engine uses CPU projection, shader applies IPD offset in screen-space.
            // On desktop this is the only supported path (VR init hard-errors without
            // GL_OVR_multiview2); on Android the per-eye else below remains as a fallback.
            if (stdVR_IsMultiViewSupported()) {
                if (stdVR_PrepareMultiViewBuffer()) {
                    // Clear internal render target
                    std3D_ClearMainFbo();

                    // Upload both eye view/projection matrices to UBOs
                    // The shader will use gl_ViewID_OVR to select the correct matrices
                    stdVR_SetMultiViewMatrices(0.01f, 1000.0f);  // zNear, zFar

                    // Set up center camera (MultiView handles eye separation in shader)
                    sithCamera_SetVRViewMultiView();

                    // Added: Update weapon crosshair position (creates/moves a sithThing)
                    // Must be before sithRender_Draw so the thing is visible this frame
                    stdVR_DrawWeaponCrosshair();

                    // Altered: advance the render tick IMMEDIATELY before the traversal that
                    // depends on it. sithRender_lastRenderTick is a generation counter shared
                    // with the AI, the automap and sithMap, and sector->renderTick is the field
                    // they all stamp. Anything that runs between the bump and sithRender_Draw -
                    // a thing spawn, a COG, an AI sight check - can stamp sectors at the current
                    // generation, and the renderer then treats them as already drawn and skips
                    // them, leaving a hole for one frame.
                    sithMain_sub_4C4D80();

                    // Render scene once - GPU renders to both eye layers
                    sithRender_Draw();
                    jkPlayer_DrawPov();

                    // Added: Render 3D map overlay (uses custom shader for VR stereo)
                    stdVR_Map3D_Render(-1);  // -1 = MultiView mode, shader uses gl_ViewID_OVR

                    // Flush render cache (includes crosshair sprite)
                    rdCache_Flush();
					sithCamera_RestoreVRView();

                    // Resolve to VR swapchain (both eyes)
                    // (In MultiView mode this is a no-op — scene already rendered to swapchain)
                    std3D_DrawSceneFbo();

                    // Bake the HUD into the multiview eye buffer (BOTH eyes), instead of a
                    // separate quad layer — not all OpenXR runtimes (e.g. SteamVR) composite
                    // the quad, but they all show what's in the projection. std3D_multiViewActive
                    // is still set and the multiview FBO is still bound, so the UI flush uses
                    // the MultiView UI program (num_views=2) to reach both eye layers.
                    {
                        int hudW = stdVR_clientInfo.renderWidth;
                        int hudH = stdVR_clientInfo.renderHeight;

                        // Draw HUD elements (hidden when weapon/force wheel is active)
                        if (!stdVR_WeaponWheel_IsActive()) {
                            if (!Main_bMotsCompat) {
                                if ((playerThings[playerThingIdx].actorThing->actorParams.typeflags & SITH_AF_NOHUD) == 0) {
                                    jkHud_Draw();
                                }
                            }
                            else {
                                if (playerThings[playerThingIdx].actorThing->actorParams.typeflags & SITH_AF_SCOPEHUD) {
                                    jkHudScope_Draw();
                                }
                                if ((playerThings[playerThingIdx].actorThing->actorParams.typeflags & SITH_AF_80000000) == 0) {
                                    if ((playerThings[playerThingIdx].actorThing->actorParams.typeflags & SITH_AF_NOHUD) == 0) {
                                        jkHud_Draw();
                                    }
                                }
                                else {
                                    jkHudCameraView_Draw();
                                }
                            }
                            jkHudInv_Draw();

                            // Added: bake the message log (COG jkPrintUNIString, item/key
                            // messages, VR prompts) into the eye buffer. The desktop path
                            // does this further down; without it these are invisible in VR.
                            // Push the column down from the canvas top so it lands in a
                            // comfortable part of the HUD rect rather than on its edge.
                            jkDev_msgTopY = (int)((flex_t)Window_ySize * 0.28f);
                            jkDev_BlitLogToScreen();
                            jkDev_msgTopY = 4;  // restore for the desktop/mirror path
                        }

#ifdef VR_WEAPON_ALIGNMENT_TOOL
                        if (stdVR_AlignmentTool_IsActive()) {
                            stdVR_AlignmentTool_DrawOverlay();
                        }
#endif

                        // Draw weapon/force wheel overlay
                        if (stdVR_WeaponWheel_IsActive()) {
                            stdVR_WeaponWheel_Draw(hudW, hudH);
                        }

                        // Destination rect for the UI flush.
                        float dstX, dstY, dstW, dstH;
                        if (stdVR_WeaponWheel_IsActive()) {
                            // Weapon/force selector: keep it CENTRED at a fixed comfortable size,
                            // NOT subject to the HUD X/Y/size tuning (that misaligns the radial
                            // selector and makes it hard to use). Aspect-preserving, centred, and
                            // pushed further back than the HUD so it doesn't crowd the view.
                            dstH = (float)hudH * 0.45f;
                            dstW = dstH * ((float)Window_xSize / (float)Window_ySize);
                            if (dstW > (float)hudW) { dstW = (float)hudW; dstH = dstW * ((float)Window_ySize / (float)Window_xSize); }
                            dstX = ((float)hudW - dstW) * 0.5f;
                            dstY = ((float)hudH - dstH) * 0.5f;
                            stdVR_SetHudOffsetForDepth(1.5f);  // selector sits ~1.5m back
                        } else {
                            // VR HUD placement: map the 2D HUD into an NDC rect centred at (PosX,PosY)
                            // with half-extents (Width,Height), all tunable live in the VR Options menu.
                            // Convert NDC -> eye-buffer pixels (NDC [-1,1] spans the buffer; Y flipped).
                            // The multiview UI shader adds per-eye forward-centering + depth convergence.
                            extern float jkPlayer_vrHudWidth, jkPlayer_vrHudHeight, jkPlayer_vrHudPosX, jkPlayer_vrHudPosY;
                            float hHalfW = jkPlayer_vrHudWidth,  hHalfH = jkPlayer_vrHudHeight;
                            float hPosX  = jkPlayer_vrHudPosX,   hPosY  = jkPlayer_vrHudPosY;
                            if (hHalfW < 0.02f) hHalfW = 0.02f; if (hHalfW > 1.60f) hHalfW = 1.60f;
                            if (hHalfH < 0.02f) hHalfH = 0.02f; if (hHalfH > 1.60f) hHalfH = 1.60f;
                            if (hPosX < -1.05f) hPosX = -1.05f; if (hPosX > 1.05f) hPosX = 1.05f;
                            if (hPosY < -1.35f) hPosY = -1.35f; if (hPosY > 1.05f) hPosY = 1.05f;
                            dstW = hHalfW * (float)hudW;
                            dstH = hHalfH * (float)hudH;
                            dstX = ((float)hudW * 0.5f) * (1.0f + hPosX - hHalfW);
                            dstY = ((float)hudH * 0.5f) * (1.0f - hPosY - hHalfH);
                        }

                        // Flush queued UI + overlay into the (bound) multiview eye buffer.
                        // The HUD UI list is multi-textured; rendering it directly with multiview
                        // desyncs per eye on Adreno (left eye 100/100, right eye 050). Render it to
                        // a single texture first, then composite as one single-texture MV draw.
                        std3D_RenderVRHudViaTexture(hudW, hudH, dstX, dstY, dstW, dstH);
                        std3D_DrawOverlayToCurrentFBO(hudW, hudH);
                    }

                    // Reset render lists
                    rdCache_ResetRenderList();

                    // Release MultiView buffer
                    stdVR_FinishMultiViewBuffer();
                }
            }
#endif // MULTIVIEW_ENABLED
        }

        // Clear VR projection state before potentially falling through to non-VR code
        rdCamera_ClearVRProjection();
        stdVR_ClearCurrentEyeViewMatrix();  // Added: Clear per-eye view matrix

        // Flush and clear render state (equivalent to what rdFinishFrame does for non-VR)
        rdCache_Flush();
        rdCache_ClearFrameCounters();

        // End VR frame
        stdVR_EndFrame();

        // Present the desktop mirror for the in-game path. In VR the normal window swap is
        // skipped (OpenXR composits to the HMD), and in-game does not go through
        // Window_SdlUpdate, so without this the desktop window would keep showing the last
        // menu frame.
        Window_VRMirrorPresent();

        jkGame_Update_AdvanceFrame = stdPlatform_GetTimeMsec();
        jkGame_Update_UpdateCamera = stdPlatform_GetTimeMsec();
        jkGame_Update_DrawPov = stdPlatform_GetTimeMsec();
        return 1;  // Skip non-VR rendering path when VR is active
    }
non_vr_path:
    ;  // Label needs a statement
#endif // PLATFORM_VR

    // Non-VR rendering path
    {
        rdAdvanceFrame();
        jkGame_Update_AdvanceFrame = stdPlatform_GetTimeMsec();
#if !defined(SDL2_RENDER) && !defined(TARGET_TWL)
        if ( Video_modeStruct.b3DAccel )
#endif
        {
            sithMain_UpdateCamera();
        }
#if !defined(SDL2_RENDER) && !defined(TARGET_TWL)
        else
        {
            stdDisplay_VBufferLock(Video_pMenuBuffer);
            stdDisplay_VBufferLock(Video_pVbufIdk);
            sithMain_UpdateCamera();
            stdDisplay_VBufferUnlock(Video_pVbufIdk);
            stdDisplay_VBufferUnlock(Video_pMenuBuffer);
        }
#endif
        jkGame_Update_UpdateCamera = stdPlatform_GetTimeMsec();
        jkPlayer_DrawPov();
        jkGame_Update_DrawPov = stdPlatform_GetTimeMsec();
    }

#if 1
    //if (Main_bMotsCompat)
    ++Video_dword_5528A0; // MOTS added
    if ( Main_bDispStats )
    {
        v2 = sithWorld_pCurrentWorld->playerThing;
        //++Video_dword_5528A0; // MOTS removed
        v3 = stdPlatform_GetTimeMsec();
        v0 = v3 - Video_lastTimeMsec;
        Video_dword_5528A8 = v3;
        if ( (unsigned int)(v3 - Video_lastTimeMsec) > 0x3E8 )
        {
            if ( Main_bDispStats )
            {
                v6 = v2->sector->id;
                Video_flt_55289C = (flex_d_t)(Video_dword_5528A0 - Video_dword_5528A4) * 1000.0 / (flex_d_t)v0;
                _sprintf(
                    std_genBuffer,
                    "%02.3f (%02d%%)f %3ds %3da %3dz %4dp %3d curSector %3d fo",
                    Video_flt_55289C,
                    (unsigned int)(__int64)((flex_d_t)(unsigned int)jkGame_updateMsecsTotal / (flex_d_t)(int)v0 * 100.0),
                    sithRender_sectorsDrawn,
                    sithRender_geoThingsDrawn,
                    sithRender_nongeoThingsDrawn,
                    rdCache_drawnFaces,
                    v6,
                    sithNet_thingsIdx);
                if ( sithNet_isMulti )
                    _sprintf(&std_genBuffer[_strlen(std_genBuffer)], " %d m %d b", stdComm_dword_8321F4, stdComm_dword_8321F0);
                jkDev_sub_41FC40(100, std_genBuffer);
                v3 = Video_dword_5528A8;
            }
            Video_lastTimeMsec = v3;
            Video_dword_5528A4 = Video_dword_5528A0;
            jkGame_dword_552B5C = 0;
            jkGame_updateMsecsTotal = 0;
            stdComm_dword_8321F0 = 0;
            stdComm_dword_8321F4 = 0;
        }
    }
    else if ( Main_bFrameRate )
    {
        //++Video_dword_5528A0; // MOTS removed
        Video_dword_5528A8 = stdPlatform_GetTimeMsec();
        if ( (unsigned int)(Video_dword_5528A8 - Video_lastTimeMsec) > 1000 )
        {
            v4 = (flex_d_t)(Video_dword_5528A0 - Video_dword_5528A4) * 1000.0 / (flex_d_t)(unsigned int)(Video_dword_5528A8 - Video_lastTimeMsec);
            Video_flt_55289C = v4;
            _sprintf(std_genBuffer, "%02.3f", v4);
            jkDev_sub_41FC40(100, std_genBuffer);
            Video_lastTimeMsec = Video_dword_5528A8;
            Video_dword_5528A4 = Video_dword_5528A0;
        }
    }
#endif

#if defined(SDL2_RENDER)
    stdVBuffer* pOverlayBuffer = Video_pCanvasOverlayMap->vbuffer;
    stdDisplay_VBufferLock(pOverlayBuffer);
    stdDisplay_VBufferFill(pOverlayBuffer, Video_fillColor, 0);
    stdDisplay_VBufferUnlock(pOverlayBuffer);
#endif

    // MOTS added: scope/security cam overlays
    // VR HUD rendering is handled in the VR render path above;
    // skip here to avoid drawing HUD into the 3D eye buffer.
#ifdef QOL_IMPROVEMENTS
    if (!(stdVR_bEnabled && stdVR_IsSessionRunning()))
#endif
    {
        if (!Main_bMotsCompat) {
            if ( (playerThings[playerThingIdx].actorThing->actorParams.typeflags & SITH_AF_NOHUD) == 0 ) {
                jkHud_Draw();
            }
        }
        else {
            if (playerThings[playerThingIdx].actorThing->actorParams.typeflags & SITH_AF_SCOPEHUD) {
                jkHudScope_Draw();
            }
            if ((playerThings[playerThingIdx].actorThing->actorParams.typeflags & SITH_AF_80000000) == 0) {
                if ((playerThings[playerThingIdx].actorThing->actorParams.typeflags & SITH_AF_NOHUD) == 0) {
                    jkHud_Draw();
                }
            }
            else {
                jkHudCameraView_Draw();
            }
        }

        jkDev_BlitLogToScreen();
        jkHudInv_Draw();
    }

    jkGame_Update_HudDrawn = stdPlatform_GetTimeMsec();
#if !defined(SDL2_RENDER) && !defined(TARGET_TWL)
    if ( Video_modeStruct.b3DAccel )
        std3D_DrawOverlay();
#endif

    // MOTS added
    /*
    if (Main_bRecord != 0) {
        jkGame_Screenshot();
    }
    */

#if defined(SDL2_RENDER)
    jkQuakeConsole_Render();
#endif

#if defined(SDL2_RENDER) || defined(TARGET_TWL)
    std3D_DrawMenu();
    rdFinishFrame();
#endif

// VR frames are ended in the VR render paths (gameplay or menu). Avoid calling
// xrEndFrame here without a matching BeginFrame.

    // MOTS removed
    if ( Video_modeStruct.b3DAccel )
        result = stdDisplay_DDrawGdiSurfaceFlip();
    else
        result = stdDisplay_VBufferCopy(Video_pOtherBuf, Video_pMenuBuffer, 0, 0, 0, 0);
    // end MOTS removed

    // MOTS added
    /*
    if ((Video_modeStruct.Video_motsNew1 != 0) && (Video_modeStruct.b3DAccel == 0)) {
        result = stdDisplay_VBufferCopy(Video_pOtherBuf,Video_pMenuBuffer,0,0,NULL,0);
        return result;
    }
    result = stdDisplay_DDrawGdiSurfaceFlip();
    */

    jkGame_Update_End = stdPlatform_GetTimeMsec();

#if defined(TARGET_TWL)
    int jkGame_Delta_Start_ClearScreen = jkGame_Update_ClearScreen - jkGame_Update_Start;
    int jkGame_Delta_ClearScreen_AdvanceFrame = jkGame_Update_AdvanceFrame - jkGame_Update_ClearScreen;
    int jkGame_Delta_AdvanceFrame_UpdateCamera = jkGame_Update_UpdateCamera - jkGame_Update_AdvanceFrame;
    int jkGame_Delta_UpdateCamera_DrawPov = jkGame_Update_DrawPov - jkGame_Update_UpdateCamera;
    int jkGame_Delta_DrawPov_HudDrawn = jkGame_Update_HudDrawn - jkGame_Update_DrawPov;
    int jkGame_Delta_HudDrawn_End = jkGame_Update_End - jkGame_Update_HudDrawn;
    
    static int last_time_ms = 0;
    int now_ms = stdPlatform_GetTimeMsec();
    int total_delta = now_ms - last_time_ms;
    last_time_ms = now_ms;
    extern int std3D_timeWastedWaitingAround;
    extern int32_t sithRender_numSectors;

    int healthNum = 0;
    int shieldsNum = 0;
    int forceNum = 0;
    int ammoNum = 0;
    int currentItemBin = 0;
    int currentForceBin = 0;
    int bHasSuperShields = 0;
    int bHasSuperWeapon = 0;
    int bHasForceSurge = 0;
    int bHasFieldLight = 0;

    if (sithWorld_pCurrentWorld) {
        sithThing* pPlayer = sithWorld_pCurrentWorld->playerThing;
        if ( pPlayer->type == SITH_THING_PLAYER ) {
            healthNum = pPlayer->actorParams.health;
            shieldsNum = (int32_t)sithInventory_GetBinAmount(pPlayer, SITHBIN_SHIELDS);
            forceNum = (int32_t)sithInventory_GetBinAmount(pPlayer, SITHBIN_FORCEMANA);
            ammoNum = jkHud_GetWeaponAmmo(pPlayer);

            bHasSuperShields = playerThings[playerThingIdx].bHasSuperShields;
            bHasSuperWeapon = playerThings[playerThingIdx].bHasSuperWeapon;
            bHasForceSurge = playerThings[playerThingIdx].bHasForceSurge;
            bHasFieldLight = sithInventory_GetActivate(pPlayer, SITHBIN_FIELDLIGHT);
        }
    }

    char resetConsole[16];
    int consoleX, consoleY;
    consoleGetCursor(NULL, &consoleX, &consoleY);
    snprintf(resetConsole, sizeof(resetConsole)-1, "\x1b[%d;%dH\x1b[97m", consoleY, consoleX);
    stdPlatform_Printf("\x1b[0;0H                                \r\x1b[0;0H\x1b[%d;1m%cHLTH %03d \x1b[%d;1m%cSHLD %03d \x1b[39;0m%c\n                               \n", (bHasSuperShields ? 33 : 31), (bHasSuperShields ? '*' : ' '), healthNum, (bHasSuperShields ? 33 : 32), (bHasSuperShields ? '*' : ' '), shieldsNum, (bHasFieldLight ? '*' : ' '));
    stdPlatform_Printf(resetConsole);
    if (ammoNum < 0) {
        stdPlatform_Printf("\x1b[1;0H                                \r\x1b[1;0H\x1b[%d;1m%cAMMO --- \x1b[%d;1m%cMANA %03d   \n                               \n\x1b[39;0m", (bHasSuperWeapon ? 33 : 39), (bHasSuperWeapon ? '*' : ' '), (bHasForceSurge ? 33 : 39), (bHasForceSurge ? '*' : ' '), forceNum);
    }
    else {
        stdPlatform_Printf("\x1b[1;0H                                \r\x1b[1;0H\x1b[33;%dm%cAMMO %03d \x1b[%d;1m%cMANA %03d   \n                               \n\x1b[39;0m", (bHasSuperWeapon ? 1 : 0), (bHasSuperWeapon ? '*' : ' '), ammoNum, (bHasForceSurge ? 33 : 36), (bHasForceSurge ? '*' : ' '), forceNum);
    }
    stdPlatform_Printf("\x1b[6;0H                                \r");
    stdPlatform_Printf("\x1b[5;0H                                \r");
    stdPlatform_Printf("\x1b[4;0H                                \r");
    stdPlatform_Printf("\x1b[3;0H                                \r");
    stdPlatform_Printf("\x1b[2;0H                                \r\x1b[2;0H");
    
    jkDev_UpdateEntries();
    jkDev_PrintfLog();
    stdPlatform_Printf("\x1b[7;0H                                \r");
    stdPlatform_Printf(resetConsole);
    stdPlatform_Printf("\x1b[10;0H                               \rdlt all=%d mn=%d %d wrld=%d\n                               \r pov=%d hud=%d drw=%d wst=%d %d \n                               \n                               \n", total_delta-std3D_timeWastedWaitingAround, sithMain_tickEndMs-sithMain_tickStartMs, jkGame_Delta_ClearScreen_AdvanceFrame, jkGame_Delta_AdvanceFrame_UpdateCamera, jkGame_Delta_UpdateCamera_DrawPov, jkGame_Delta_DrawPov_HudDrawn, jkGame_Delta_HudDrawn_End - std3D_timeWastedWaitingAround, std3D_timeWastedWaitingAround, sithRender_numSectors);
    stdPlatform_Printf(resetConsole);
    stdPlatform_Printf("\x1b[13;0H                               \r");
    stdPlatform_PrintHeapStats();
    stdPlatform_Printf(resetConsole);
    //world=28 drw=15 emu
    //world=48 drw=33 dsi, 33 down to 25 with jank phys?
#endif

    return result;
}

#ifdef SDL2_RENDER
void jkGame_Screenshot()
{
    //stdPlatform_Printf("TODO: Implement screenshots\n");
    char local_80[128];
    int bVar2 = 0;
    do {
        stdString_snprintf(local_80, sizeof(local_80), "SHOT%04d.PNG", Video_dword_5528B0);
        stdFile_t fp = pHS->fileOpen(local_80, "r");
        if (fp == 0) {
            bVar2 = 1;
        }
        else {
            pHS->fileClose(fp);
        }
        Video_dword_5528B0++;
        if (Video_dword_5528B0 > 9999) {
            bVar2 = 1;
        }
    } while (!bVar2);

    std3D_Screenshot(local_80);
}
#endif

void jkGame_Gamma()
{
    int v0; // eax
    char *v1; // eax

    v0 = ++Video_modeStruct.Video_8606A4;
    if ( Video_modeStruct.Video_8606A4 >= 0xAu )
    {
        v0 = 0;
        Video_modeStruct.Video_8606A4 = 0;
    }
    stdDisplay_GammaCorrect3(v0);
#if !defined(SDL2_RENDER) && !defined(TARGET_TWL)
    stdPalEffects_RefreshPalette();
    if ( Video_modeStruct.b3DAccel )
    {
        v1 = stdDisplay_GetPalette();
        sithRender_SetPalette(v1);
    }
#endif
}

void jkGame_PrecalcViewSizes(int width, int height, jkViewSize *aOut)
{
    flex_d_t v5; // st7
    flex_d_t v6; // st6
    flex_t v7; // [esp+4h] [ebp-Ch]
    flex_t v8;
    flex_t widtha; // [esp+14h] [ebp+4h]
    flex_t widthb; // [esp+14h] [ebp+4h]
    flex_t heighta; // [esp+18h] [ebp+8h]

    v5 = (flex_d_t)width;

    widtha = (flex_t)height;
    heighta = widtha;
    v6 = widtha * 0.5;
    widthb = v5 * 0.5;
    v8 = v6;
    v7 = heighta * 0.36000001;
    aOut[10].xMax = widthb;
    aOut[10].yMax = v6;
    aOut[9].xMax = widthb;
    aOut[9].yMax = v8;
    aOut[10].xMin = width;
    aOut[10].yMin = height;
    aOut[9].xMin = width;
    aOut[9].yMin = height;
    aOut[8].xMin = (__int64)(v5 * 0.9375 - -0.5);
    aOut[8].xMax = widthb;
    aOut[8].yMax = v8;
    aOut[8].yMin = (__int64)(heighta * 0.9375 - -0.5);
    aOut[7].xMin = (__int64)(v5 * 0.875 - -0.5);
    aOut[7].xMax = widthb;
    aOut[7].yMax = v8;
    aOut[7].yMin = (__int64)(heighta * 0.875 - -0.5);
    aOut[6].xMin = (__int64)(v5 * 0.8125 - -0.5);
    aOut[6].xMax = widthb;
    aOut[6].yMax = v8;
    aOut[6].yMin = (__int64)(heighta * 0.8125 - -0.5);
    aOut[5].xMin = (__int64)(v5 * 0.71875 - -0.5);
    aOut[5].xMax = widthb;
    aOut[5].yMax = v7;
    aOut[5].yMin = (__int64)(heighta * 0.71875 - -0.5);
    aOut[4].xMin = (__int64)(v5 * 0.625 - -0.5);
    aOut[4].xMax = widthb;
    aOut[4].yMax = v7;
    aOut[4].yMin = (__int64)(heighta * 0.625 - -0.5);
    aOut[3].xMin = (__int64)(v5 * 0.53125 - -0.5);
    aOut[3].xMax = widthb;
    aOut[3].yMax = v7;
    aOut[3].yMin = (__int64)(heighta * 0.53125 - -0.5);
    aOut[2].xMin = (__int64)(v5 * 0.4375 - -0.5);
    aOut[2].xMax = widthb;
    aOut[2].yMax = v7;
    aOut[2].yMin = (__int64)(heighta * 0.4375 - -0.5);
    aOut[1].xMin = (__int64)(v5 * 0.34375 - -0.5);
    aOut[1].xMax = widthb;
    aOut[1].yMax = v7;
    aOut[1].yMin = (__int64)(heighta * 0.34375 - -0.5);
    aOut->xMin = (__int64)(v5 * 0.25 - -0.5);
    aOut->yMin = (__int64)(heighta * 0.25 - -0.5);
    aOut->xMax = widthb;
    aOut->yMax = v7;
}

void jkGame_ddraw_idk_palettes()
{
    if ( Video_bOpened )
    {
        stdDisplay_VBufferFill(Video_pMenuBuffer, Video_fillColor, 0);
        stdDisplay_DDrawGdiSurfaceFlip();
        stdDisplay_ddraw_surface_flip2();
        stdDisplay_VBufferFill(Video_pMenuBuffer, Video_fillColor, 0);
        sithRender_SetPalette(stdDisplay_GetPalette());
    }
}

void jkGame_nullsub_36()
{
    ;
}

#include "stdVR_AlignmentTool.h"

// Added: VR weapon alignment tool for real-time offset adjustment

#ifdef VR_WEAPON_ALIGNMENT_TOOL

#include "Platform/VR/stdVR.h"
#include "Platform/VR/stdVR_WeaponOffsets.h"
#include "General/stdFont.h"
#include "World/jkPlayer.h"
#include "stdPlatform.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

// Tool state
static int stdVR_alignmentTool_bActive = 0;
static int stdVR_alignmentTool_bInitted = 0;
static stdVR_AlignmentMode stdVR_alignmentTool_mode = STDVR_ALIGN_POSITION;

// Input state tracking for debouncing
static int stdVR_alignmentTool_bAPressed = 0;

// Adjustment speeds
#define STDVR_ALIGN_POS_SPEED   0.2f    // meters per second
#define STDVR_ALIGN_SCALE_SPEED 0.5f    // scale units per second
#define STDVR_ALIGN_PITCH_SPEED 30.0f   // degrees per second

// Stick deadzone
#define STDVR_ALIGN_DEADZONE    0.2f

// External font for overlay (from jkHud)
extern stdFont* jkHud_pMsgFontSft;
extern flex_t jkPlayer_hudScale;

void stdVR_AlignmentTool_Startup(void)
{
    if (stdVR_alignmentTool_bInitted) {
        return;
    }

    stdVR_alignmentTool_bActive = 0;
    stdVR_alignmentTool_mode = STDVR_ALIGN_POSITION;
    stdVR_alignmentTool_bAPressed = 0;

    stdVR_alignmentTool_bInitted = 1;
    stdPlatform_Printf("stdVR_AlignmentTool: Initialized\n");
}

void stdVR_AlignmentTool_Toggle(void)
{
    stdVR_alignmentTool_bActive = !stdVR_alignmentTool_bActive;

    if (stdVR_alignmentTool_bActive) {
        stdPlatform_Printf("stdVR_AlignmentTool: Activated\n");
    } else {
        // Save offsets when deactivating
        stdVR_WeaponOffsets_Save();
        stdPlatform_Printf("stdVR_AlignmentTool: Deactivated and saved\n");
    }
}

int stdVR_AlignmentTool_IsActive(void)
{
    return stdVR_alignmentTool_bActive;
}

stdVR_AlignmentMode stdVR_AlignmentTool_GetMode(void)
{
    return stdVR_alignmentTool_mode;
}

static float stdVR_AlignmentTool_ApplyDeadzone(float value)
{
    if (fabsf(value) < STDVR_ALIGN_DEADZONE) {
        return 0.0f;
    }
    // Remap value from deadzone..1.0 to 0.0..1.0
    float sign = value < 0 ? -1.0f : 1.0f;
    float absVal = fabsf(value);
    return sign * (absVal - STDVR_ALIGN_DEADZONE) / (1.0f - STDVR_ALIGN_DEADZONE);
}

void stdVR_AlignmentTool_Update(float deltaSeconds)
{
    if (!stdVR_bEnabled) {
        return;
    }

    // Altered: activation is the cvar g_vrAlignTool, not the old both-grips + B chord. Those
    // inputs all have jobs now - each grip opens a wheel and B is alt-fire - so the chord
    // would open a wheel, drop time to 0.1x and fire the weapon on the way in. A cvar also
    // keeps a dev tool out of reach of a player mashing buttons.
    // Clearing the cvar toggles off, which is what writes the JSON.
    extern int jkPlayer_vrAlignTool;
    int bWantActive = (jkPlayer_vrAlignTool != 0);
    if (bWantActive != stdVR_alignmentTool_bActive) {
        stdVR_AlignmentTool_Toggle();
    }

    if (!stdVR_alignmentTool_bActive) {
        return;
    }

    // Check for mode cycle: A button
    int bAButton = (stdVR_clientInfo.buttonState & STDVR_BTN_A) != 0;
    if (bAButton && !stdVR_alignmentTool_bAPressed) {
        stdVR_alignmentTool_mode = (stdVR_alignmentTool_mode + 1) % STDVR_ALIGN_MODE_COUNT;
        stdPlatform_Printf("stdVR_AlignmentTool: Mode changed to %d\n", stdVR_alignmentTool_mode);
    }
    stdVR_alignmentTool_bAPressed = bAButton;

    // Get current weapon offset
    stdVR_WeaponOffset* pOffset = stdVR_GetCurrentWeaponOffset();
    if (!pOffset) {
        return;
    }

    // Get stick inputs
    float leftStickX = stdVR_AlignmentTool_ApplyDeadzone(stdVR_clientInfo.analogMove[0]);
    float leftStickY = stdVR_AlignmentTool_ApplyDeadzone(stdVR_clientInfo.analogMove[1]);

    float rightStickY = stdVR_AlignmentTool_ApplyDeadzone(stdVR_clientInfo.analogTurn[1]);

    // Added: the stored offsets are always in right-hand sense; the renderer mirrors X, yaw
    // and roll for the left hand. Invert those sticks here so the weapon moves the way the
    // tester pushes, and the saved file stays hand-neutral.
    float mirror = (stdVR_GetDominantHand() == STDVR_CONTROLLER_LEFT) ? -1.0f : 1.0f;

    // Apply adjustments based on mode
    switch (stdVR_alignmentTool_mode) {
        case STDVR_ALIGN_POSITION:
            // Left stick X/Y = offset X/Y, Right stick Y = offset Z
            pOffset->offsetX += mirror * leftStickX * STDVR_ALIGN_POS_SPEED * deltaSeconds;
            pOffset->offsetY += leftStickY * STDVR_ALIGN_POS_SPEED * deltaSeconds;
            pOffset->offsetZ += rightStickY * STDVR_ALIGN_POS_SPEED * deltaSeconds;
            break;

        case STDVR_ALIGN_SCALE:
            // Left stick Y = scale adjustment
            pOffset->modelScale += leftStickY * STDVR_ALIGN_SCALE_SPEED * deltaSeconds;
            // Clamp scale to reasonable range
            if (pOffset->modelScale < 0.1f) pOffset->modelScale = 0.1f;
            if (pOffset->modelScale > 5.0f) pOffset->modelScale = 5.0f;
            break;

        case STDVR_ALIGN_ROTATION:
            // Left stick Y = pitch, left stick X = yaw, right stick Y = roll. This matches
            // the layout of POSITION mode, so all three axes need no extra mode change.
            pOffset->pitchAdjust += leftStickY * STDVR_ALIGN_PITCH_SPEED * deltaSeconds;
            pOffset->yawAdjust += mirror * leftStickX * STDVR_ALIGN_PITCH_SPEED * deltaSeconds;
            pOffset->rollAdjust += mirror * rightStickY * STDVR_ALIGN_PITCH_SPEED * deltaSeconds;

            // Clamp each axis to a sensible range
            if (pOffset->pitchAdjust < -180.0f) pOffset->pitchAdjust = -180.0f;
            if (pOffset->pitchAdjust > 180.0f) pOffset->pitchAdjust = 180.0f;
            if (pOffset->yawAdjust < -180.0f) pOffset->yawAdjust = -180.0f;
            if (pOffset->yawAdjust > 180.0f) pOffset->yawAdjust = 180.0f;
            if (pOffset->rollAdjust < -180.0f) pOffset->rollAdjust = -180.0f;
            if (pOffset->rollAdjust > 180.0f) pOffset->rollAdjust = 180.0f;
            break;

        default:
            break;
    }

    // Mark this weapon as configured
    pOffset->bConfigured = 1;
}

void stdVR_AlignmentTool_DrawOverlay(void)
{
    if (!stdVR_alignmentTool_bActive) {
        return;
    }

    if (!jkHud_pMsgFontSft) {
        return;
    }

    int binIdx = stdVR_GetCurrentWeaponBin();
    stdVR_WeaponOffset* pOffset = stdVR_GetCurrentWeaponOffset();

    char buf[256];
    int y = 30;
    flex_t scale = jkPlayer_hudScale;

    // The overlay needs about 16 lines. Cap the scale so a large HUD scale cannot push the
    // last lines off the bottom of the canvas. 2.0 is the default, so this changes nothing
    // for most users.
    if (scale > 2.0) {
        scale = 2.0;
    }

    // Altered: the text draws at jkPlayer_hudScale, but the line pitch used the UNSCALED font
    // height, so at the default scale of 2.0 each line drew over the line above it. Scale the
    // pitch by the same factor and add a small gap.
    int fontHeight = (int)((flex_t)stdFont_GetHeight(jkHud_pMsgFontSft) * scale) + 4;
    if (fontHeight < 8) {
        fontHeight = 8;
    }

    // Title
    stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, "=== VR WEAPON ALIGNMENT ===", 1, scale);
    y += fontHeight;

    // Current weapon bin
    snprintf(buf, sizeof(buf), "Weapon Bin: %d", binIdx);
    stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, buf, 1, scale);
    y += fontHeight;

    // Current mode
    const char* modeNames[] = { "POSITION", "SCALE", "ROTATION" };
    snprintf(buf, sizeof(buf), "Mode: %s (A to cycle)",
        stdVR_alignmentTool_mode < STDVR_ALIGN_MODE_COUNT ? modeNames[stdVR_alignmentTool_mode] : "???");
    stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, buf, 1, scale);
    y += fontHeight;
    y += fontHeight; // Extra spacing

    if (pOffset) {
        // Position values
        snprintf(buf, sizeof(buf), "Offset X: %.3f m", pOffset->offsetX);
        stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, buf, 1, scale);
        y += fontHeight;

        snprintf(buf, sizeof(buf), "Offset Y: %.3f m", pOffset->offsetY);
        stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, buf, 1, scale);
        y += fontHeight;

        snprintf(buf, sizeof(buf), "Offset Z: %.3f m", pOffset->offsetZ);
        stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, buf, 1, scale);
        y += fontHeight;

        snprintf(buf, sizeof(buf), "Scale: %.2f", pOffset->modelScale);
        stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, buf, 1, scale);
        y += fontHeight;

        snprintf(buf, sizeof(buf), "Pitch: %.1f  Yaw: %.1f  Roll: %.1f deg",
                 pOffset->pitchAdjust, pOffset->yawAdjust, pOffset->rollAdjust);
        stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, buf, 1, scale);
        y += fontHeight;

        snprintf(buf, sizeof(buf), "Configured: %s", pOffset->bConfigured ? "Yes" : "No");
        stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, buf, 1, scale);
        y += fontHeight;
    } else {
        stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, "No weapon selected", 1, scale);
        y += fontHeight;
    }

    y += fontHeight; // Extra spacing

    // Controls help
    stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, "Controls:", 1, scale);
    y += fontHeight;
    stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, "  VR Options menu: turn the tool off to save", 1, scale);
    y += fontHeight;
    stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, "  A: Cycle mode   Triggers: Prev/Next weapon", 1, scale);
    y += fontHeight;

    switch (stdVR_alignmentTool_mode) {
        case STDVR_ALIGN_POSITION:
            stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, "  Left Stick: X/Y offset", 1, scale);
            y += fontHeight;
            stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, "  Right Stick Y: Z offset", 1, scale);
            break;
        case STDVR_ALIGN_SCALE:
            stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, "  Left Stick Y: Scale", 1, scale);
            break;
        case STDVR_ALIGN_ROTATION:
            stdFont_DrawAsciiGPU(jkHud_pMsgFontSft, 10, y, 640, "  L Stick: Pitch/Yaw   R Stick Y: Roll", 1, scale);
            break;
        default:
            break;
    }
}

#endif // VR_WEAPON_ALIGNMENT_TOOL

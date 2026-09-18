#include "stdVR_Input.h"

// Added: VR controller input mapping implementation

#ifdef PLATFORM_VR

#include "stdVR.h"
#include "stdVR_Types.h"
#include "stdVR_Map3D.h"
#include "stdVR_Prompts.h"
#include "Platform/stdControl.h"
#include "stdPlatform.h"
#include "General/stdMath.h"
#include "Primitives/rdVector.h"
#include "Cog/sithCog.h"

#include "Main/jkHud.h"
#include "Main/jkMain.h"
#include "Main/jkSmack.h"
#include "Main/jkDev.h"
#include "Main/jkStrings.h"
#include "Gui/jkGUIRend.h"

#include <math.h>

// Snap turn state
static int stdVR_snapTurnPending = 0;
static float stdVR_lastSnapTurnInput = 0.0f;

// Menu button state for long-press detection
static int stdVR_menuButtonHeld = 0;
static uint32_t stdVR_menuButtonHoldStart = 0;
#define STDVR_MENU_LONGPRESS_MS 1000  // Hold menu 1 second to recenter

// Run toggle (off-hand thumbstick click)
static int stdVR_runToggled = 0;  // 0 = walk (default), 1 = run

// Weapon switching via dominant hand thumbstick up
static int stdVR_weaponSwitchState = 0;  // 0 = neutral, 1 = up triggered, -1 = down triggered
static int stdVR_nextWeaponTriggered = 0;
static int stdVR_prevWeaponTriggered = 0;
#define STDVR_WEAPON_SWITCH_THRESHOLD 0.7f
// Crouch shares the turn stick, so it needs a deeper, more deliberate push than a weapon
// switch, and both need the vertical axis to dominate the horizontal by this ratio before
// they count as an up/down gesture at all.
#define STDVR_CROUCH_THRESHOLD 0.9f
#define STDVR_STICK_AXIS_DOMINANCE 2.0f

// Alignment tool: trigger edge state for weapon stepping
static int stdVR_alignNextTriggerHeld = 0;
static int stdVR_alignPrevTriggerHeld = 0;
#define STDVR_ALIGN_TRIGGER_THRESHOLD 0.7f

// Crouch toggle via right thumbstick down
static int stdVR_crouchToggled = 0;
static int stdVR_crouchToggleState = 0;  // debounce: 0 = neutral, -1 = already toggled this push

// Fist punch state per controller (fists fire on a forward thrust of EITHER hand). Edge-detected
// once per frame in stdVR_Input_MapToGame so the haptic buzzes once per punch; the level value is
// read by stdVR_Input_IsPunchActive for FIRE1.
static int stdVR_punchActive[STDVR_CONTROLLER_COUNT] = {0};
static int stdVR_punchActiveLast[STDVR_CONTROLLER_COUNT] = {0};

// Deadzone for thumbsticks
#define STDVR_THUMBSTICK_DEADZONE 0.2f
#define STDVR_SNAP_TURN_THRESHOLD 0.7f

// VR Cheat combo state (both grips + both triggers held)
static int stdVR_cheatComboHeld = 0;
static uint32_t stdVR_cheatComboHoldStart = 0;
static int stdVR_cheatComboActivated = 0;  // Prevents repeated activation
#define STDVR_CHEAT_COMBO_HOLD_MS 1500     // Hold for 1.5 seconds to activate
#define STDVR_CHEAT_TRIGGER_THRESHOLD 0.8f // Trigger/grip must be at least 80% pressed

// External: get current time in ms
extern uint32_t stdPlatform_GetTimeMsec(void);
// External: controller escape key hook (handled in Window.c)
extern int stdControl_bControllerEscapeKey;

// Apply deadzone to thumbstick input
static float ApplyDeadzone(float value, float deadzone)
{
    if (value > deadzone) {
        return (value - deadzone) / (1.0f - deadzone);
    } else if (value < -deadzone) {
        return (value + deadzone) / (1.0f - deadzone);
    }
    return 0.0f;
}

void stdVR_Input_ProcessSnapTurn(void)
{
    if (!stdVR_bEnabled) {
        return;
    }

    // Added: Suppress turning while weapon wheel is active
    if (stdVR_WeaponWheel_IsActive() || stdVR_AlignmentTool_IsActive()) {
        stdVR_snapTurnPending = 0;
        return;
    }

    float turnInput = stdVR_clientInfo.analogTurn[0];

    // Apply deadzone
    turnInput = ApplyDeadzone(turnInput, STDVR_THUMBSTICK_DEADZONE);

    if (stdVR_config.turnMode == STDVR_TURN_SNAP) {
        // Snap turn logic
        if (fabsf(turnInput) > STDVR_SNAP_TURN_THRESHOLD && fabsf(stdVR_lastSnapTurnInput) <= STDVR_SNAP_TURN_THRESHOLD) {
            // Trigger snap turn (negate: positive X = right on stick, but positive yaw = left in JKDF2)
            stdVR_snapTurnPending = (turnInput > 0) ? -1 : 1;
        } else if (fabsf(turnInput) <= STDVR_SNAP_TURN_THRESHOLD) {
            // Reset when returning to center
            stdVR_snapTurnPending = 0;
        }
        stdVR_lastSnapTurnInput = turnInput;
    } else {
        stdVR_snapTurnPending = 0;
    }
}

// Added: which physical stick currently carries movement / turning. The axes themselves are
// already routed by hand in stdVR_OpenXR_UpdateInput; these are for the stick-CLICK actions,
// which have to follow the same stick.
uint32_t stdVR_Input_GetMoveStickButton(void)
{
    return stdVR_config.bSwapSticks ? STDVR_BTN_THUMBSTICK_R : STDVR_BTN_THUMBSTICK_L;
}

uint32_t stdVR_Input_GetTurnStickButton(void)
{
    return stdVR_config.bSwapSticks ? STDVR_BTN_THUMBSTICK_L : STDVR_BTN_THUMBSTICK_R;
}

void stdVR_Input_GetMovementDirection(float* pMoveX, float* pMoveY)
{
    if (!stdVR_bEnabled || !pMoveX || !pMoveY) {
        return;
    }

    // Added: no locomotion while a wheel is open. Time is already slowed to 0.1x and the
    // off-hand stick is the wheel's page-flip control, so letting it also strafe would send
    // the player sliding sideways every time they change page. Turning is suppressed the
    // same way in stdVR_Input_GetSmoothTurnSpeed.
    if (stdVR_WeaponWheel_IsActive() || stdVR_AlignmentTool_IsActive()) {
        *pMoveX = 0.0f;
        *pMoveY = 0.0f;
        return;
    }

    // Get raw thumbstick input with deadzone
    // OpenXR left stick X is inverted relative to JK strafe (left reports negative),
    // so flip it here to keep "left = strafe left".
    float moveX = -ApplyDeadzone(stdVR_clientInfo.analogMove[0], STDVR_THUMBSTICK_DEADZONE);
    float moveY = ApplyDeadzone(stdVR_clientInfo.analogMove[1], STDVR_THUMBSTICK_DEADZONE);

    // If 3D map is visible, use left stick for map rotation instead of movement
    if (stdVR_Map3D_IsVisible()) {
        float deltaTime = 1.0f / 72.0f;  // Approximate frame time

        // Two-handed gesture control: both grips held
        int bothGripsHeld = (stdVR_clientInfo.buttonState & STDVR_BTN_GRIP_L) &&
                           (stdVR_clientInfo.buttonState & STDVR_BTN_GRIP_R);
        if (bothGripsHeld) {
            rdVector3 leftPos, rightPos;
            stdVR_GetControllerPose(STDVR_CONTROLLER_LEFT, &leftPos, NULL);
            stdVR_GetControllerPose(STDVR_CONTROLLER_RIGHT, &rightPos, NULL);
            stdVR_Map3D_ProcessGestures(1, &leftPos, &rightPos);
        } else {
            stdVR_Map3D_ProcessGestures(0, NULL, NULL);

            // Thumbstick controls only when grips not held
            // Rotate map with left thumbstick left/right
            float rotationSpeed = 120.0f;  // Degrees per second at full deflection
            stdVR_Map3D_Rotate(-moveY * rotationSpeed * deltaTime);

            // Zoom map with right thumbstick Y axis
            float zoomInput = ApplyDeadzone(stdVR_clientInfo.analogTurn[1], STDVR_THUMBSTICK_DEADZONE);
            float zoomSpeed = 1.5f;  // Zoom rate per second at full deflection
            stdVR_Map3D_Zoom(zoomInput * zoomSpeed * deltaTime);
        }

        // Don't pass movement input when map is visible
        *pMoveX = 0.0f;
        *pMoveY = 0.0f;
        return;
    }

    // Added: Suppress movement while weapon wheel is active
    if (stdVR_WeaponWheel_IsActive() || stdVR_AlignmentTool_IsActive()) {
        *pMoveX = 0.0f;
        *pMoveY = 0.0f;
        return;
    }

    // Movement is already oriented based on controller/head direction
    // The actual transformation happens in sithControl when we apply moveYaw
    *pMoveX = moveX;
    *pMoveY = moveY;
}

// Track if menu button triggered escape this frame
static int stdVR_menuTriggeredThisFrame = 0;

void stdVR_Input_MapToGame(void)
{
    if (!stdVR_bEnabled) {
        return;
    }

    stdVR_menuTriggeredThisFrame = 0;
    stdVR_nextWeaponTriggered = 0;
    stdVR_prevWeaponTriggered = 0;

    // Process snap turn
    stdVR_Input_ProcessSnapTurn();

    // Update weapon/force wheel (before weapon switch logic)
    stdVR_WeaponWheel_Update();

    // Get movement input
    float moveX = 0.0f, moveY = 0.0f;
    stdVR_Input_GetMovementDirection(&moveX, &moveY);

    // Added: while the alignment tool is open the triggers step through the arsenal (right =
    // next, left = previous), so a tuning pass can reach every weapon without closing the
    // tool. The triggers are free there because firing is suppressed, and it keeps the sticks
    // entirely for the offsets.
    if (stdVR_AlignmentTool_IsActive()) {
        int bNext = (stdVR_clientInfo.triggerRight > STDVR_ALIGN_TRIGGER_THRESHOLD);
        int bPrev = (stdVR_clientInfo.triggerLeft  > STDVR_ALIGN_TRIGGER_THRESHOLD);

        if (bNext && !stdVR_alignNextTriggerHeld) {
            stdVR_nextWeaponTriggered = 1;
            stdVR_TriggerHaptic(STDVR_CONTROLLER_RIGHT, 0.3f, 0.05f, 100.0f);
        }
        if (bPrev && !stdVR_alignPrevTriggerHeld) {
            stdVR_prevWeaponTriggered = 1;
            stdVR_TriggerHaptic(STDVR_CONTROLLER_LEFT, 0.3f, 0.05f, 100.0f);
        }
        stdVR_alignNextTriggerHeld = bNext;
        stdVR_alignPrevTriggerHeld = bPrev;
    }
    else {
        stdVR_alignNextTriggerHeld = 0;
        stdVR_alignPrevTriggerHeld = 0;
    }

    // Turn stick DOWN toggles crouch. Pushing UP does nothing: weapon selection is the
    // wheel's job, and a nudged stick flick kept switching weapons by accident. Disabled
    // while a wheel or the alignment tool is up - both own the sticks.
    if (!stdVR_WeaponWheel_IsActive() && !stdVR_AlignmentTool_IsActive()) {
        // Use the turn stick's Y axis (right stick by default, left if the sticks are swapped
        // - analogTurn is already routed by stdVR_OpenXR_UpdateInput)
        float weaponSwitchY = stdVR_clientInfo.analogTurn[1];  // Y axis of turn stick

        // Altered: turning lives on this same stick, so a diagonal push used to crouch by
        // accident constantly. Require a deliberate, near-vertical push: the vertical axis
        // must clearly dominate the horizontal, and crouch needs a deeper deflection than
        // weapon-switch because it is the one that was being triggered unintentionally.
        float weaponSwitchXMag = stdVR_clientInfo.analogTurn[0];
        if (weaponSwitchXMag < 0.0f) {
            weaponSwitchXMag = -weaponSwitchXMag;
        }
        float weaponSwitchYMag = (weaponSwitchY < 0.0f) ? -weaponSwitchY : weaponSwitchY;
        int bVerticalDominant = (weaponSwitchYMag > weaponSwitchXMag * STDVR_STICK_AXIS_DOMINANCE);

        if (!bVerticalDominant) {
            // Mostly a turn, not an up/down gesture - ignore, but let the state reset below.
            weaponSwitchY = 0.0f;
        }

        if (weaponSwitchY < -STDVR_CROUCH_THRESHOLD) {
            // Thumbstick pushed down - toggle crouch
            if (stdVR_crouchToggleState != -1) {
                stdVR_crouchToggled = !stdVR_crouchToggled;
                stdVR_crouchToggleState = -1;
                stdVR_weaponSwitchState = -1;
                stdVR_TriggerHaptic(stdVR_config.dominantHand, 0.3f, 0.05f, 100.0f);
            }
        } else if (weaponSwitchY > -0.3f && weaponSwitchY < 0.3f) {
            // Thumbstick returned to center - reset state
            stdVR_weaponSwitchState = 0;
            stdVR_crouchToggleState = 0;
        }
    }

    // Map movement axes
    // INPUT_FUNC_FORWARD = 0, INPUT_FUNC_TURN = 1, INPUT_FUNC_SLIDE = 2, etc.
    // These will be handled in sithControl_PlayerMovement when VR is enabled

    // Handle turn input
    if (stdVR_config.turnMode == STDVR_TURN_SNAP) {
        // Snap turn is handled separately via stdVR_snapTurnPending
        // Return the pending snap turn angle
        if (stdVR_snapTurnPending != 0) {
            // Snap turn will be applied in sithControl
        }
    } else {
        // Smooth turn - let the axis value pass through
    }

    // Handle menu button (escape/pause)
    // Short press = menu/escape, Long press = recenter
    // Accept Y button as menu on Quest (the system menu button isn't accessible)
    int menuButtonDown = (stdVR_clientInfo.buttonState & STDVR_BTN_MENU) != 0;
    if (menuButtonDown) {
        if (!stdVR_menuButtonHeld) {
            // Button just pressed
            stdVR_menuButtonHeld = 1;
            stdVR_menuButtonHoldStart = stdPlatform_GetTimeMsec();
        } else if (stdVR_menuButtonHeld == 1) {
            // Check for long press (recenter)
            uint32_t holdTime = stdPlatform_GetTimeMsec() - stdVR_menuButtonHoldStart;
            if (holdTime >= STDVR_MENU_LONGPRESS_MS) {
                // Long press detected - recenter view
                stdVR_RecenterView();
                stdVR_TriggerHaptic(STDVR_CONTROLLER_LEFT, 0.8f, 0.2f, 200.0f);
                stdVR_menuButtonHeld = 2;  // Mark as handled (prevent repeated recenter)
            }
        }
    } else {
        // Button released
        if (stdVR_menuButtonHeld == 1) {
            // Short press - trigger escape/menu
            stdVR_menuTriggeredThisFrame = 1;
            stdVR_TriggerHaptic(STDVR_CONTROLLER_LEFT, 0.3f, 0.1f, 100.0f);
        }
        stdVR_menuButtonHeld = 0;
    }

    // Added: the off-hand upper face button also opens the in-game pause menu. Uses the
    // rising-edge bit so a single press fires once - no long-press-to-recenter, unlike the
    // headset's own menu button.
    // Altered: this was hard-wired to Y. Alt-fire has to sit on the weapon hand, so the two
    // upper buttons trade with handedness and the menu takes whichever one is left.
    {
        uint32_t btnMenu = stdVR_GetMenuButton();
        if (stdVR_clientInfo.buttonPressed & btnMenu) {
            stdVR_menuTriggeredThisFrame = 1;
            stdVR_TriggerHaptic(stdVR_GetButtonHand(btnMenu), 0.3f, 0.1f, 100.0f);
        }
    }

    // Mirror JKXR-style logic: use screen layer when UI/cinematics/menus are active
    int guiState = jkSmack_GetCurrentGuiState();
    int inGameplay = (guiState == JK_GAMEMODE_GAMEPLAY);
    int bMenuActive = jkGuiRend_IsMenuActive();  // a GUI menu (pause/options/dialog) is showing

    if (stdVR_menuTriggeredThisFrame && bMenuActive) {
        // A menu is already open: a second menu-button press closes it (triggers the menu's
        // "Return to Game" / back shortcut), so the user doesn't have to point at and click
        // "Return to Game" to get back into the game.
        jkGuiRend_TriggerEscape();
    }
    else if (inGameplay && stdVR_menuTriggeredThisFrame) {
        if (Main_bMotsCompat) {
            if (!jkGuiMultiplayer_mpcInfo.pCutsceneCog) {
                if (jkHud_bChatOpen)
                    jkHud_idk_time();
                else
                    jkMain_do_guistate6();
            } else {
                sithCog_SendMessage(jkGuiMultiplayer_mpcInfo.pCutsceneCog, SITH_MESSAGE_ESCAPED, 0,
                                    0, 0, 0, 0);
            }
        } else {
            if (jkHud_bChatOpen)
                jkHud_idk_time();
            else
                jkMain_do_guistate6();
        }
    }

    // Simulate a controller escape key press on short menu release (one-frame pulse). Suppressed
    // when a menu was open (we closed it directly above) to avoid a double-escape.
    stdControl_bControllerEscapeKey = (stdVR_menuTriggeredThisFrame && !bMenuActive) ? 1 : 0;

    // VR Cheat combo: Hold both grips + both triggers for 1.5 seconds
    // This gives all weapons and force powers
    {
        int bothTriggersHeld = (stdVR_clientInfo.triggerLeft >= STDVR_CHEAT_TRIGGER_THRESHOLD) &&
                               (stdVR_clientInfo.triggerRight >= STDVR_CHEAT_TRIGGER_THRESHOLD);
        int bothGripsHeld = (stdVR_clientInfo.gripLeft >= STDVR_CHEAT_TRIGGER_THRESHOLD) &&
                            (stdVR_clientInfo.gripRight >= STDVR_CHEAT_TRIGGER_THRESHOLD);
        int cheatComboDown = bothTriggersHeld && bothGripsHeld;

        if (cheatComboDown) {
            if (!stdVR_cheatComboHeld) {
                // Combo just started
                stdVR_cheatComboHeld = 1;
                stdVR_cheatComboHoldStart = stdPlatform_GetTimeMsec();
                stdVR_cheatComboActivated = 0;
            } else if (!stdVR_cheatComboActivated) {
                // Check for long press (cheat activation)
                uint32_t holdTime = stdPlatform_GetTimeMsec() - stdVR_cheatComboHoldStart;
                if (holdTime >= STDVR_CHEAT_COMBO_HOLD_MS) {
                    // Activate cheats!
                    jkDev_CmdAllWeapons(NULL, NULL);  // Give all weapons
                    jkDev_CmdUberJedi(NULL, NULL);    // Give all force powers maxed
                    jkDev_CmdHeal(NULL, NULL);        // Full health and shields

                    // Strong haptic feedback on both controllers
                    stdVR_TriggerHaptic(STDVR_CONTROLLER_LEFT, 1.0f, 0.3f, 250.0f);
                    stdVR_TriggerHaptic(STDVR_CONTROLLER_RIGHT, 1.0f, 0.3f, 250.0f);

                    stdVR_cheatComboActivated = 1;  // Prevent repeated activation
                }
            }
        } else {
            // Combo released
            stdVR_cheatComboHeld = 0;
            stdVR_cheatComboActivated = 0;
        }
    }

    // Map buttons to game actions using stdControl functions
    // Note: We don't call stdControl_SetKey directly here because
    // the actual input processing happens in the sithControl tick.
    // Instead, the VR button state is read directly by the modified
    // sithControl code when PLATFORM_VR is defined.

    // Removed: fire/alt-fire/force haptics used to buzz on the raw button press, so the
    // controller vibrated whether or not anything happened - out of ammo, mid fire-wait,
    // while switching weapons, or with a weapon that does not shoot. The weapon buzz now
    // comes from sithWeapon_FireProjectile, once a shot has actually been created.

    // Fist punch detection (both hands) + haptics. With the fists equipped, a forward thrust of
    // either controller fires a punch (see stdVR_Input_IsPunchActive -> FIRE1) and buzzes the hand
    // that threw it. Only active for the fists; other weapons fire via the trigger as normal.
    {
        extern sithPlayerInfo* sithPlayer_pLocalPlayer;
        int weap = sithPlayer_pLocalPlayer ? sithPlayer_pLocalPlayer->curWeapon : -1;
        int bFists = (weap == SITHBIN_FISTS || weap == SITHBIN_MOTS_FISTS);


        float punchThreshold = stdVR_motionConfig.weaponVelocityTrigger;
        if (punchThreshold <= 0.0f) punchThreshold = 2.0f;

        for (int h = 0; h < STDVR_CONTROLLER_COUNT; h++) {
            stdVR_ControllerState* pCtrl = &stdVR_clientInfo.controllers[h];
            int active = (bFists && pCtrl->bTracking
                          && pCtrl->motion.forwardSpeed >= punchThreshold) ? 1 : 0;
            if (active && !stdVR_punchActiveLast[h]) {
                // Punch just started — buzz the punching hand.
                stdVR_TriggerHaptic(h, 0.6f, 0.12f, 100.0f);
            }
            stdVR_punchActive[h] = active;
            stdVR_punchActiveLast[h] = active;
        }
    }

    // Move-stick click = toggle run mode (INPUT_FUNC_FAST).
    // Altered: follows the movement stick rather than a fixed side, so the run toggle stays
    // under the thumb that is doing the walking when the sticks are swapped.
    // Gated on inGameplay so a thumbstick click in a menu/intro/cutscene doesn't get acted on
    // (otherwise the action "buffers" into gameplay — e.g. clicking the stick in the menu would
    // leave the holomap open once gameplay starts).
    if (inGameplay && (stdVR_clientInfo.buttonPressed & stdVR_Input_GetMoveStickButton())) {
        stdVR_runToggled = !stdVR_runToggled;
        int moveHand = stdVR_config.bSwapSticks ? STDVR_CONTROLLER_RIGHT : STDVR_CONTROLLER_LEFT;
        // Haptic feedback: longer pulse for run, short pulse for walk
        if (stdVR_runToggled) {
            // Run mode: one longer pulse
            stdVR_TriggerHaptic(moveHand, 0.5f, 0.15f, 150.0f);
        } else {
            // Walk mode: short pulse
            stdVR_TriggerHaptic(moveHand, 0.3f, 0.1f, 100.0f);
        }
    }

    // Added: turn-stick click = toggle 3D map (gameplay only; see note above).
    if (inGameplay && (stdVR_clientInfo.buttonPressed & stdVR_Input_GetTurnStickButton())) {
        stdVR_Map3D_Toggle();
        // Haptic feedback on both controllers
        stdVR_TriggerHaptic(STDVR_CONTROLLER_LEFT, 0.4f, 0.15f, 120.0f);
        stdVR_TriggerHaptic(STDVR_CONTROLLER_RIGHT, 0.4f, 0.15f, 120.0f);
    }

    // Added: onboarding prompts. Reads the same toggles/wheel state resolved above, so it
    // runs last.
    stdVR_Prompts_Tick(inGameplay);
}

// Check if menu button just triggered escape (short press release)
int stdVR_Input_IsMenuPressed(void)
{
    return stdVR_menuTriggeredThisFrame;
}

// Get current snap turn angle (called from sithControl)
int stdVR_Input_GetSnapTurnAngle(void)
{
    if (!stdVR_bEnabled || stdVR_config.turnMode != STDVR_TURN_SNAP) {
        return 0;
    }

    if (stdVR_snapTurnPending != 0) {
        int angle = stdVR_snapTurnPending * stdVR_config.snapTurnAngle;
        stdVR_snapTurnPending = 0; // Consume the snap turn
        return angle;
    }

    return 0;
}

// Get smooth turn speed (called from sithControl)
float stdVR_Input_GetSmoothTurnSpeed(void)
{
    if (!stdVR_bEnabled || stdVR_config.turnMode != STDVR_TURN_SMOOTH) {
        return 0.0f;
    }

    // Added: Suppress smooth turn while weapon wheel is active
    if (stdVR_WeaponWheel_IsActive() || stdVR_AlignmentTool_IsActive()) {
        return 0.0f;
    }

    float turnInput = ApplyDeadzone(stdVR_clientInfo.analogTurn[0], STDVR_THUMBSTICK_DEADZONE);
    // Negate because positive X = right on stick, but positive yaw = left in JKDF2
    return -turnInput * stdVR_config.smoothTurnSpeed;
}

// Check if a VR button is currently pressed
int stdVR_Input_IsButtonDown(uint32_t button)
{
    if (!stdVR_bEnabled) {
        return 0;
    }
    return (stdVR_clientInfo.buttonState & button) != 0;
}

// Check if a VR button was just pressed this frame
int stdVR_Input_IsButtonPressed(uint32_t button)
{
    if (!stdVR_bEnabled) {
        return 0;
    }
    return (stdVR_clientInfo.buttonPressed & button) != 0;
}

// Check if a VR button was just released this frame
int stdVR_Input_IsButtonReleased(uint32_t button)
{
    if (!stdVR_bEnabled) {
        return 0;
    }
    return (stdVR_clientInfo.buttonReleased & button) != 0;
}

// Get trigger value (0-1)
float stdVR_Input_GetTrigger(int hand)
{
    if (!stdVR_bEnabled) {
        return 0.0f;
    }
    return (hand == STDVR_CONTROLLER_LEFT) ? stdVR_clientInfo.triggerLeft : stdVR_clientInfo.triggerRight;
}

// Get grip value (0-1)
float stdVR_Input_GetGrip(int hand)
{
    if (!stdVR_bEnabled) {
        return 0.0f;
    }
    return (hand == STDVR_CONTROLLER_LEFT) ? stdVR_clientInfo.gripLeft : stdVR_clientInfo.gripRight;
}

// Check if run mode is toggled on (off-hand thumbstick click)
int stdVR_Input_IsRunToggled(void)
{
    if (!stdVR_bEnabled) {
        return 0;
    }
    return stdVR_runToggled;
}

// Reset run toggle (e.g. on level load)
void stdVR_Input_ResetRunToggle(void)
{
    stdVR_runToggled = 0;
}

// Check if next weapon was triggered this frame (thumbstick up flick)
int stdVR_Input_IsNextWeaponTriggered(void)
{
    if (!stdVR_bEnabled) {
        return 0;
    }
    // Altered: weapon switching is now the ONE game action allowed while the alignment tool
    // is open, so a tuning pass can step through weapons without closing the tool.
    return stdVR_nextWeaponTriggered;
}

// Check if previous weapon was triggered this frame (thumbstick down flick)
int stdVR_Input_IsPrevWeaponTriggered(void)
{
    if (!stdVR_bEnabled) {
        return 0;
    }
    // Altered: weapon stepping is allowed while the alignment tool is open - see the trigger
    // handling in stdVR_Input_MapToGame.
    return stdVR_prevWeaponTriggered;
}

// Check if crouch toggle is active
int stdVR_Input_IsCrouchToggled(void)
{
    if (!stdVR_bEnabled) {
        return 0;
    }
    return stdVR_crouchToggled;
}

// Check if the dominant hand is thrusting forward fast enough to register a melee punch (fists).
// Uses the signed forward-axis velocity so a forward jab fires but the backswing/retract does not;
// the weapon's own fire rate gates repeats, giving roughly one hit per punch.
int stdVR_Input_IsPunchActive(void)
{
    if (!stdVR_bEnabled) {
        return 0;
    }
    // Either hand punching counts (computed per-frame in stdVR_Input_MapToGame).
    return (stdVR_punchActive[STDVR_CONTROLLER_LEFT] || stdVR_punchActive[STDVR_CONTROLLER_RIGHT]) ? 1 : 0;
}

// Reset crouch toggle (e.g. on level transitions)
void stdVR_Input_ResetCrouchToggle(void)
{
    stdVR_crouchToggled = 0;
    stdVR_crouchToggleState = 0;
}

#endif // PLATFORM_VR

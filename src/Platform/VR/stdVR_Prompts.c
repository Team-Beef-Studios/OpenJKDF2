#include "stdVR_Prompts.h"

#ifdef PLATFORM_VR

#include "stdVR.h"
#include "stdVR_Types.h"
#include "stdVR_Input.h"
#include "stdVR_WeaponWheel.h"
#include "stdVR_Map3D.h"
#include "Main/jkDev.h"
#include "stdPlatform.h"
#include "Main/jkStrings.h"
#include "World/jkPlayer.h"
#include "Gameplay/sithInventory.h"
#include "Gameplay/sithPlayer.h"
#include "World/sithWeapon.h"
#include "Main/Main.h"
#include "jk.h"

// Pacing. The first prompt waits for the level fade/briefing to be out of the way, and each
// step leaves a gap after the player performs the action so they see the result before the
// next instruction lands.
#define STDVR_PROMPT_INTRO_DELAY_MS (5000)
#define STDVR_PROMPT_STEP_GAP_MS    (2000)

// Intro chain: crouch -> stand -> run -> weapon wheel. Show steps are even, wait steps odd,
// so a prompt already taught on a previous playthrough can skip straight to the next show
// step (+2) instead of stalling the chain on an action the player was never asked for.
enum
{
    STDVR_INTRO_SHOW_CROUCH = 0,
    STDVR_INTRO_WAIT_CROUCH,
    STDVR_INTRO_SHOW_STAND,
    STDVR_INTRO_WAIT_STAND,
    STDVR_INTRO_SHOW_RUN,
    STDVR_INTRO_WAIT_RUN,
    // Queues the weapon wheel prompt plus the remaining basics, which have no action to
    // wait on - the prompt queue paces them.
    STDVR_INTRO_SHOW_WEAPONS,
    STDVR_INTRO_DONE
};

enum
{
    STDVR_FORCE_WAIT_POWER = 0,
    STDVR_FORCE_WAIT_WHEEL,
    STDVR_FORCE_SHOW_USE,
    STDVR_FORCE_DONE
};

static int stdVR_promptsWasInGameplay = 0;
static int stdVR_promptIntroStep = STDVR_INTRO_SHOW_CROUCH;
static int stdVR_promptForceStep = STDVR_FORCE_WAIT_POWER;
static int stdVR_promptRunStateAtPrompt = 0;
static int stdVR_promptLastWeapon = -2;
static uint32_t stdVR_promptNextMs = 0;
// Snapshot of the taught-prompt mask when the session's chain started, so a prompt taught on
// an earlier playthrough is skipped while one taught just now can be waited on.
static uint32_t stdVR_promptsShownAtStart = 0;

// ============================================================================
// Control-scheme aware button naming
// ============================================================================

static const wchar_t* stdVR_Prompts_HandWord(int hand)
{
    return (hand == STDVR_CONTROLLER_LEFT) ? L"left" : L"right";
}

// Movement and turning live on fixed sticks unless the player swapped them; the trigger and
// grip live on the weapon hand. See stdVR_OpenXR_UpdateInput and sithControl_ReadFunctionMap.
static const wchar_t* stdVR_Prompts_MoveStickWord(void)
{
    return stdVR_Prompts_HandWord(stdVR_config.bSwapSticks ? STDVR_CONTROLLER_RIGHT : STDVR_CONTROLLER_LEFT);
}

static const wchar_t* stdVR_Prompts_TurnStickWord(void)
{
    return stdVR_Prompts_HandWord(stdVR_config.bSwapSticks ? STDVR_CONTROLLER_LEFT : STDVR_CONTROLLER_RIGHT);
}

// Face-button letters. Both pairs move with the control scheme, so name them from the live
// mapping rather than literally - see stdVR_GetJumpButton and friends.
static const wchar_t* stdVR_Prompts_ButtonWord(uint32_t btn)
{
    switch (btn) {
        case STDVR_BTN_A: return L"A";
        case STDVR_BTN_B: return L"B";
        case STDVR_BTN_X: return L"X";
        case STDVR_BTN_Y: return L"Y";
        default:          return L"?";
    }
}

static const wchar_t* stdVR_Prompts_JumpButtonWord(void)
{
    return stdVR_Prompts_ButtonWord(stdVR_GetJumpButton());
}

static const wchar_t* stdVR_Prompts_ActivateButtonWord(void)
{
    return stdVR_Prompts_ButtonWord(stdVR_GetActivateButton());
}

static const wchar_t* stdVR_Prompts_AltFireButtonWord(void)
{
    return stdVR_Prompts_ButtonWord(stdVR_GetAltFireButton());
}

static const wchar_t* stdVR_Prompts_DominantWord(void)
{
    return stdVR_Prompts_HandWord(stdVR_config.dominantHand);
}

static const wchar_t* stdVR_Prompts_OffHandWord(void)
{
    return stdVR_Prompts_HandWord(1 - stdVR_config.dominantHand);
}

// ============================================================================
// Formatting
// ============================================================================

// One prompt on screen at a time. Several triggers can come due at once - equipping a weapon
// while the intro chain advances, picking up an item, and so on - and the message log stacks
// five entries, so without this the player gets a wall of text scrolling past faster than it
// can be read. Requests queue and are released one per STDVR_PROMPT_SPACING_MS.
#define STDVR_PROMPT_SPACING_MS (STDVR_PROMPT_DWELL_MS + 1500)
#define STDVR_PROMPT_QUEUE_MAX  8

static int stdVR_promptQueue[STDVR_PROMPT_QUEUE_MAX];
static wchar_t stdVR_promptQueueText[STDVR_PROMPT_QUEUE_MAX][192];
static int stdVR_promptQueueCount = 0;
static uint32_t stdVR_promptQueueNextMs = 0;

static void stdVR_Prompts_Format(wchar_t* pOut, const char* pStrKey, const wchar_t* pFallback,
                                 const wchar_t* pArg1, const wchar_t* pArg2)
{
    const wchar_t* pFmt = jkStrings_GetUniString(pStrKey);
    if (!pFmt) {
        pFmt = pFallback;
    }

    jk_snwprintf(pOut, 192, pFmt, pArg1, pArg2);
    pOut[191] = 0;
}

static void stdVR_Prompts_ShowOnce(int promptId, const char* pStrKey, const wchar_t* pFallback,
                                   const wchar_t* pArg1, const wchar_t* pArg2)
{
    if (stdVR_WasPromptShown(promptId)) {
        return;
    }

    // Already waiting to be shown?
    for (int i = 0; i < stdVR_promptQueueCount; i++) {
        if (stdVR_promptQueue[i] == promptId) {
            return;
        }
    }
    if (stdVR_promptQueueCount >= STDVR_PROMPT_QUEUE_MAX) {
        return;
    }

    // Formatted at QUEUE time so the text reflects the control scheme as it was when the
    // moment happened, and so a config change mid-queue cannot leave a dangling argument.
    int slot = stdVR_promptQueueCount++;
    stdVR_Prompts_Format(stdVR_promptQueueText[slot], pStrKey, pFallback, pArg1, pArg2);
    stdVR_promptQueue[slot] = promptId;
}

// The prompt currently on screen, so it can be retired the moment the player does what it
// asked. Held for a minimum time first: some conditions (crouched already, run already on)
// can be true the instant the prompt appears, and a message that flashes and vanishes is
// worse than one that lingers.
#define STDVR_PROMPT_MIN_ONSCREEN_MS (1200)
#define STDVR_PROMPT_AFTER_ACTION_MS (1200)

static int stdVR_promptDisplayedId = -1;
static uint32_t stdVR_promptDisplayedAtMs = 0;
static wchar_t stdVR_promptDisplayedText[192];

// Has the player done the thing the prompt asked for? Prompts with no cheap observable
// condition simply time out.
static int stdVR_Prompts_IsSatisfied(int promptId)
{
    switch (promptId)
    {
        case STDVR_PROMPT_CROUCH:       return stdVR_Input_IsCrouchToggled();
        case STDVR_PROMPT_STAND:        return !stdVR_Input_IsCrouchToggled();
        case STDVR_PROMPT_RUN:          return stdVR_Input_IsRunToggled() != stdVR_promptRunStateAtPrompt;
        case STDVR_PROMPT_WEAPON_WHEEL: return stdVR_WeaponWheel_GetActiveWheel() == STDVR_WHEEL_WEAPON;
        case STDVR_PROMPT_FORCE_WHEEL:  return stdVR_WeaponWheel_GetActiveWheel() == STDVR_WHEEL_FORCE;
        case STDVR_PROMPT_ITEMS:        return stdVR_WeaponWheel_GetActiveWheel() == STDVR_WHEEL_ITEMS;
        case STDVR_PROMPT_HOLOMAP:      return stdVR_Map3D_IsVisible();
        // Satisfied by the two-handed gesture itself, or by closing the map again.
        case STDVR_PROMPT_HOLOMAP_GRAB: return !stdVR_Map3D_IsVisible()
                                            || (stdVR_Input_IsButtonDown(STDVR_BTN_GRIP_L)
                                             && stdVR_Input_IsButtonDown(STDVR_BTN_GRIP_R));
        case STDVR_PROMPT_HOLOMAP_CLOSE: return !stdVR_Map3D_IsVisible();
        case STDVR_PROMPT_JUMP:         return stdVR_Input_IsButtonDown(stdVR_GetJumpButton());
        case STDVR_PROMPT_ACTIVATE:     return stdVR_Input_IsButtonDown(stdVR_GetActivateButton());
        case STDVR_PROMPT_ALT_FIRE:     return stdVR_Input_IsButtonDown(stdVR_GetAltFireButton());
        default:                        return 0;
    }
}

static void stdVR_Prompts_TickDismiss(uint32_t now)
{
    if (stdVR_promptDisplayedId < 0) {
        return;
    }

    if ((now - stdVR_promptDisplayedAtMs) < STDVR_PROMPT_MIN_ONSCREEN_MS) {
        return;
    }

    if (!stdVR_Prompts_IsSatisfied(stdVR_promptDisplayedId)) {
        return;
    }

    jkDev_ExpireEntryByText(stdVR_promptDisplayedText);
    stdVR_promptDisplayedId = -1;

    // Acting on a prompt should move things along rather than leaving a long dead gap.
    if (stdVR_promptQueueNextMs > now + STDVR_PROMPT_AFTER_ACTION_MS) {
        stdVR_promptQueueNextMs = now + STDVR_PROMPT_AFTER_ACTION_MS;
    }
}

static void stdVR_Prompts_TickQueue(uint32_t now)
{
    if (stdVR_promptQueueCount <= 0 || now < stdVR_promptQueueNextMs) {
        return;
    }

    stdVR_ShowPromptOnce(stdVR_promptQueue[0], stdVR_promptQueueText[0]);
    stdVR_promptQueueNextMs = now + STDVR_PROMPT_SPACING_MS;

    stdVR_promptDisplayedId = stdVR_promptQueue[0];
    stdVR_promptDisplayedAtMs = now;
    _memcpy(stdVR_promptDisplayedText, stdVR_promptQueueText[0], sizeof(stdVR_promptDisplayedText));

    stdVR_promptQueueCount--;
    for (int i = 0; i < stdVR_promptQueueCount; i++) {
        stdVR_promptQueue[i] = stdVR_promptQueue[i + 1];
        _memcpy(stdVR_promptQueueText[i], stdVR_promptQueueText[i + 1],
                sizeof(stdVR_promptQueueText[i]));
    }
}

// Show a prompt straight away, ahead of anything queued. For prompts that only make sense
// while the player is doing the thing right now: by the time a queued prompt reached the
// front, the player had usually closed the map again.
static void stdVR_Prompts_ShowOnceNow(int promptId, const char* pStrKey, const wchar_t* pFallback,
                                      const wchar_t* pArg1, const wchar_t* pArg2)
{
    if (stdVR_WasPromptShown(promptId)) {
        return;
    }

    wchar_t text[192];
    stdVR_Prompts_Format(text, pStrKey, pFallback, pArg1, pArg2);

    // Retire whatever is on screen, so the two do not stack.
    if (stdVR_promptDisplayedId >= 0) {
        jkDev_ExpireEntryByText(stdVR_promptDisplayedText);
    }

    uint32_t now = stdPlatform_GetTimeMsec();
    stdVR_ShowPromptOnce(promptId, text);
    stdVR_promptDisplayedId = promptId;
    stdVR_promptDisplayedAtMs = now;
    _memcpy(stdVR_promptDisplayedText, text, sizeof(stdVR_promptDisplayedText));
    stdVR_promptQueueNextMs = now + STDVR_PROMPT_SPACING_MS;
}

// ============================================================================
// Game state the chains wait on
// ============================================================================

static int stdVR_Prompts_HasAnyForcePower(void)
{
    if (!sithPlayer_pLocalPlayerThing || !sithPlayer_pLocalPlayerThing->actorParams.playerinfo) {
        return 0;
    }

    sithPlayerInfo* pPlayerInfo = sithPlayer_pLocalPlayerThing->actorParams.playerinfo;
    int fpEnd = Main_bMotsCompat ? SITHBIN_F_CHAINLIGHT : SITHBIN_F_DEADLYSIGHT;

    for (int bin = SITHBIN_F_JUMP; bin <= fpEnd; bin++) {
        sithItemDescriptor* pDesc = &sithInventory_aDescriptors[bin];
        sithItemInfo* pInfo = &pPlayerInfo->iteminfo[bin];
        if ((pInfo->state & ITEMSTATE_AVAILABLE) && (pDesc->flags & ITEMINFO_POWER) && pInfo->ammoAmt > 0.0f) {
            return 1;
        }
    }

    return 0;
}

static int stdVR_Prompts_HasAnyItem(void)
{
    if (!sithPlayer_pLocalPlayerThing || !sithPlayer_pLocalPlayerThing->actorParams.playerinfo) {
        return 0;
    }

    sithPlayerInfo* pPlayerInfo = sithPlayer_pLocalPlayerThing->actorParams.playerinfo;

    for (int bin = 0; bin < SITHBIN_NUMBINS; bin++) {
        sithItemDescriptor* pDesc = &sithInventory_aDescriptors[bin];
        sithItemInfo* pInfo = &pPlayerInfo->iteminfo[bin];
        if ((pDesc->flags & ITEMINFO_ITEM) && (pDesc->flags & ITEMINFO_VALID)
            && (pInfo->state & ITEMSTATE_AVAILABLE)) {
            return 1;
        }
    }

    return 0;
}

// A weapon advertises a secondary fire by answering the AUTOSELECT message for mode 1; the
// engine uses the same query when picking a weapon, so this is the game's own notion of
// "has alt-fire" rather than a hardcoded list that mods would break.
static int stdVR_Prompts_WeaponHasAltFire(int binIdx)
{
    if (!sithPlayer_pLocalPlayerThing || binIdx <= 0) {
        return 0;
    }

    return sithWeapon_GetPriority(sithPlayer_pLocalPlayerThing, binIdx, 1) >= 0.0;
}

// ============================================================================
// Chains
// ============================================================================

// Was this prompt already taught before the current session? Those are skipped outright.
// A prompt taught DURING this session has been seen, so the chain may wait on its action.
static int stdVR_Prompts_TaughtBefore(int promptId)
{
    return (stdVR_promptsShownAtStart & (1u << promptId)) != 0;
}

// One show-step: skip if already taught, otherwise queue it and stay put until the queue
// actually puts it on screen - never wait on an action the player was not shown.
static int stdVR_Prompts_StepShow(int promptId, const char* pStrKey, const wchar_t* pFallback,
                                  const wchar_t* pArg, int nextIfTaught, int nextWhenShown)
{
    if (stdVR_Prompts_TaughtBefore(promptId)) {
        return nextIfTaught;
    }

    stdVR_Prompts_ShowOnce(promptId, pStrKey, pFallback, pArg, NULL);

    return stdVR_WasPromptShown(promptId) ? nextWhenShown : -1;
}

static void stdVR_Prompts_TickIntro(uint32_t now)
{
    if (now < stdVR_promptNextMs) {
        return;
    }

    int next = -1;

    switch (stdVR_promptIntroStep)
    {
        case STDVR_INTRO_SHOW_CROUCH:
            next = stdVR_Prompts_StepShow(STDVR_PROMPT_CROUCH, "GUIEXT_VR_PROMPT_CROUCH",
                                          L"Push the %ls thumbstick down to crouch",
                                          stdVR_Prompts_TurnStickWord(),
                                          STDVR_INTRO_SHOW_STAND, STDVR_INTRO_WAIT_CROUCH);
            break;

        case STDVR_INTRO_WAIT_CROUCH:
            if (stdVR_Input_IsCrouchToggled()) {
                next = STDVR_INTRO_SHOW_STAND;
            }
            break;

        case STDVR_INTRO_SHOW_STAND:
            next = stdVR_Prompts_StepShow(STDVR_PROMPT_STAND, "GUIEXT_VR_PROMPT_STAND",
                                          L"Push the %ls thumbstick down again to stand up",
                                          stdVR_Prompts_TurnStickWord(),
                                          STDVR_INTRO_SHOW_RUN, STDVR_INTRO_WAIT_STAND);
            break;

        case STDVR_INTRO_WAIT_STAND:
            if (!stdVR_Input_IsCrouchToggled()) {
                next = STDVR_INTRO_SHOW_RUN;
            }
            break;

        case STDVR_INTRO_SHOW_RUN:
            next = stdVR_Prompts_StepShow(STDVR_PROMPT_RUN, "GUIEXT_VR_PROMPT_RUN",
                                          L"Click the %ls thumbstick to toggle run",
                                          stdVR_Prompts_MoveStickWord(),
                                          STDVR_INTRO_SHOW_WEAPONS, STDVR_INTRO_WAIT_RUN);
            if (next == STDVR_INTRO_WAIT_RUN) {
                stdVR_promptRunStateAtPrompt = stdVR_Input_IsRunToggled();
            }
            break;

        case STDVR_INTRO_WAIT_RUN:
            if (stdVR_Input_IsRunToggled() != stdVR_promptRunStateAtPrompt) {
                next = STDVR_INTRO_SHOW_WEAPONS;
            }
            break;

        case STDVR_INTRO_SHOW_WEAPONS:
            // The remaining basics have no action to wait for, so queue them all here and let
            // the queue pace them one at a time.
            stdVR_Prompts_ShowOnce(STDVR_PROMPT_WEAPON_WHEEL, "GUIEXT_VR_PROMPT_WEAPON_WHEEL",
                                   L"Hold the %ls grip to see your weapons",
                                   stdVR_Prompts_DominantWord(), NULL);
            // Face buttons never swap with handedness, but jump and activate do follow the
            // thumbstick swap, so name them from the live mapping.
            stdVR_Prompts_ShowOnce(STDVR_PROMPT_JUMP, "GUIEXT_VR_PROMPT_JUMP",
                                   L"Press %ls to jump",
                                   stdVR_Prompts_JumpButtonWord(), NULL);
            stdVR_Prompts_ShowOnce(STDVR_PROMPT_ACTIVATE, "GUIEXT_VR_PROMPT_ACTIVATE",
                                   L"Press %ls to open doors and use switches",
                                   stdVR_Prompts_ActivateButtonWord(), NULL);
            // The holomap prompt is deliberately last in this batch, so the first thing shown
            // after the player opens the map is STDVR_PROMPT_HOLOMAP_GRAB.
            stdVR_Prompts_ShowOnce(STDVR_PROMPT_HOLOMAP, "GUIEXT_VR_PROMPT_HOLOMAP",
                                   L"Click the %ls thumbstick for the 3D map",
                                   stdVR_Prompts_TurnStickWord(), NULL);
            next = STDVR_INTRO_DONE;
            break;

        default:
            break;
    }

    if (next >= 0) {
        stdVR_promptIntroStep = next;
    }
}

// First usable inventory item, and first death. Both are one-shot observations rather than
// chains, so they live outside the sequenced steps.
static void stdVR_Prompts_TickEvents(void)
{
    if (!sithPlayer_pLocalPlayerThing || !sithPlayer_pLocalPlayerThing->actorParams.playerinfo) {
        return;
    }

    // The map's grab/rotate/scale gesture is not signposted anywhere, so teach it the first
    // time the map is up. The HUD (and so the message log) still draws over the holomap.
    if (!stdVR_WasPromptShown(STDVR_PROMPT_HOLOMAP_GRAB) && stdVR_Map3D_IsVisible()) {
        stdVR_Prompts_ShowOnceNow(STDVR_PROMPT_HOLOMAP_GRAB, "GUIEXT_VR_PROMPT_HOLOMAP_GRAB",
                               L"Use the grip buttons to grab, rotate and scale the map",
                               NULL, NULL);
    }

    // Follows the grab prompt, while the map is still open. Two ways in: the player performed
    // the grab gesture, which retires the grab prompt and leaves stdVR_promptDisplayedId
    // pointing elsewhere, or the normal prompt gap has passed. Without the second test a
    // player who never grabs would never be told how to close the map.
    if (!stdVR_WasPromptShown(STDVR_PROMPT_HOLOMAP_CLOSE)
        && stdVR_WasPromptShown(STDVR_PROMPT_HOLOMAP_GRAB)
        && stdVR_Map3D_IsVisible()
        && (stdVR_promptDisplayedId != STDVR_PROMPT_HOLOMAP_GRAB
            || stdPlatform_GetTimeMsec() >= stdVR_promptQueueNextMs)) {
        stdVR_Prompts_ShowOnceNow(STDVR_PROMPT_HOLOMAP_CLOSE, "GUIEXT_VR_PROMPT_HOLOMAP_CLOSE",
                                  L"Click the %ls thumbstick again to close the map",
                                  stdVR_Prompts_TurnStickWord(), NULL);
    }

    // Gated on the intro chain being finished: the player starts holding the field light, so
    // HasAnyItem() is true from the first tick and this would otherwise be the very first prompt
    // of the game - before crouch, run or the weapon wheel have been taught.
    if (!stdVR_WasPromptShown(STDVR_PROMPT_ITEMS)
        && stdVR_promptIntroStep == STDVR_INTRO_DONE
        && stdVR_Prompts_HasAnyItem()) {
        stdVR_Prompts_ShowOnce(STDVR_PROMPT_ITEMS, "GUIEXT_VR_PROMPT_ITEMS",
                               L"Hold the %ls grip and push the stick to reach your items",
                               stdVR_Prompts_OffHandWord(), NULL);
    }

    if (!stdVR_WasPromptShown(STDVR_PROMPT_DEATH_LOAD)
        && (sithPlayer_pLocalPlayerThing->thingflags & SITH_TF_DEAD)) {
        stdVR_Prompts_ShowOnce(STDVR_PROMPT_DEATH_LOAD, "GUIEXT_VR_PROMPT_DEATH_LOAD",
                               L"Press Y for the menu, then Load to restore your last save",
                               NULL, NULL);
    }
}

static void stdVR_Prompts_TickForce(uint32_t now)
{
    switch (stdVR_promptForceStep)
    {
        case STDVR_FORCE_WAIT_POWER:
            if (!stdVR_Prompts_HasAnyForcePower()) {
                break;
            }
            if (stdVR_WasPromptShown(STDVR_PROMPT_FORCE_WHEEL)) {
                stdVR_promptForceStep = STDVR_FORCE_SHOW_USE;
                break;
            }
            stdVR_Prompts_ShowOnce(STDVR_PROMPT_FORCE_WHEEL, "GUIEXT_VR_PROMPT_FORCE_WHEEL",
                                   L"Hold the %ls grip to see and select your Force powers",
                                   stdVR_Prompts_OffHandWord(), NULL);
            stdVR_promptForceStep = STDVR_FORCE_WAIT_WHEEL;
            break;

        case STDVR_FORCE_WAIT_WHEEL:
            if (stdVR_WeaponWheel_GetActiveWheel() == STDVR_WHEEL_FORCE) {
                stdVR_promptForceStep = STDVR_FORCE_SHOW_USE;
                stdVR_promptNextMs = now + STDVR_PROMPT_STEP_GAP_MS;
            }
            break;

        case STDVR_FORCE_SHOW_USE:
            // Wait for the wheel to close so the instruction is not hidden behind it.
            if (stdVR_WeaponWheel_IsActive() || now < stdVR_promptNextMs) {
                break;
            }
            stdVR_Prompts_ShowOnce(STDVR_PROMPT_FORCE_USE, "GUIEXT_VR_PROMPT_FORCE_USE",
                                   L"Press the %ls trigger to use the selected Force power",
                                   stdVR_Prompts_OffHandWord(), NULL);
            stdVR_promptForceStep = STDVR_FORCE_DONE;
            break;

        default:
            break;
    }
}

static void stdVR_Prompts_TickWeapon(void)
{
    int weap = sithPlayer_pLocalPlayer ? sithPlayer_pLocalPlayer->curWeapon : -1;
    if (weap == stdVR_promptLastWeapon) {
        return;
    }
    stdVR_promptLastWeapon = weap;

    // The melee weapons do not fire from the trigger in VR - that is not discoverable, so say
    // it the first time each is equipped.
    if (weap == SITHBIN_FISTS || weap == SITHBIN_MOTS_FISTS) {
        stdVR_Prompts_ShowOnce(STDVR_PROMPT_FISTS_PUNCH, "GUIEXT_VR_PROMPT_FISTS",
                               L"Punch forward with either hand to attack", NULL, NULL);
    }
    else if (weap == SITHBIN_LIGHTSABER || weap == SITHBIN_MOTS_LIGHTSABER) {
        stdVR_Prompts_ShowOnce(STDVR_PROMPT_SABER_SWING, "GUIEXT_VR_PROMPT_SABER",
                               L"Swing the controller to use the lightsaber", NULL, NULL);
    }
    else if (stdVR_Prompts_WeaponHasAltFire(weap)) {
        stdVR_Prompts_ShowOnce(STDVR_PROMPT_ALT_FIRE, "GUIEXT_VR_PROMPT_ALT_FIRE",
                               L"Fire with the %ls trigger, alt-fire with %ls",
                               stdVR_Prompts_DominantWord(), stdVR_Prompts_AltFireButtonWord());
    }
}

// ============================================================================
// Entry points
// ============================================================================

void stdVR_Prompts_Reset(void)
{
    stdVR_promptsShownAtStart = (uint32_t)jkPlayer_vrPromptsShown;
    stdVR_promptQueueCount = 0;
    stdVR_promptQueueNextMs = 0;
    stdVR_promptDisplayedId = -1;
    stdVR_promptIntroStep = STDVR_INTRO_SHOW_CROUCH;
    stdVR_promptForceStep = STDVR_FORCE_WAIT_POWER;
    stdVR_promptLastWeapon = -2;
    stdVR_promptNextMs = stdPlatform_GetTimeMsec() + STDVR_PROMPT_INTRO_DELAY_MS;
}

void stdVR_Prompts_Tick(int bInGameplay)
{
    if (!stdVR_bEnabled) {
        return;
    }

    // Onboarding is for a first-time player, which means DF2. Nobody starts on MOTS, so by
    // the time they get there they already know the controls and the prompts would just be
    // noise. Nothing is marked as taught either, so a later DF2 playthrough still teaches.
    if (Main_bMotsCompat) {
        return;
    }

    // Flush only when the gui state has genuinely left gameplay (menu, briefing, tally). A
    // level load keeps the gui state AT gameplay, so this cannot fire while sithWorld_Load
    // owns the conffile - which is exactly what made the profile write crash the loader.
    if (!bInGameplay) {
        if (stdVR_promptsWasInGameplay) {
            stdVR_promptsWasInGameplay = 0;
            stdVR_FlushPromptsShown();
        }
        return;
    }

    // In gameplay by gui state, but the world may still be loading: the state flips to
    // GAMEPLAY before sithMain_Mode1Init runs, and the loader pumps VR frames between level
    // sections ("VR KeepAlive"), so this tick can land mid-load. jkGame_isDDraw only goes up
    // once the world is built and the video mode is set. Touching playerinfo or the COGs
    // before that reads a half-built world.
    extern int jkGame_isDDraw;
    if (!jkGame_isDDraw) {
        return;
    }

    if (!stdVR_promptsWasInGameplay) {
        stdVR_promptsWasInGameplay = 1;
        stdVR_Prompts_Reset();
    }

    uint32_t now = stdPlatform_GetTimeMsec();

    stdVR_Prompts_TickDismiss(now);
    stdVR_Prompts_TickQueue(now);
    stdVR_Prompts_TickIntro(now);
    stdVR_Prompts_TickForce(now);
    stdVR_Prompts_TickWeapon();
    stdVR_Prompts_TickEvents();
}

#endif // PLATFORM_VR

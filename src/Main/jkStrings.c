#include "jkStrings.h"

#include "General/stdStrTable.h"
#include "General/stdString.h"
#include "General/stdHashTable.h"
#include "Cog/jkCog.h"
#include "../jk.h"

// Added: Register a string directly into a string table without file loading.
// This is used as a fallback when .uni files can't be loaded from the filesystem
// (e.g., on Android where APK assets aren't accessible via fopen).
#ifdef QOL_IMPROVEMENTS
static void jkStrings_RegisterString(stdStrTable* pTable, const char* key, const wchar_t* value)
{
    if (!pTable || !key || !value) return;
    if (!pTable->hashtable) return;

    // Check if already registered
    if (stdHashTable_GetKeyVal(pTable->hashtable, key)) return;

    stdStrMsg* pMsg = (stdStrMsg*)std_pHS->alloc(sizeof(stdStrMsg));
    if (!pMsg) return;
    _memset(pMsg, 0, sizeof(stdStrMsg));

    pMsg->key = (char*)std_pHS->alloc(_strlen(key) + 1);
    if (!pMsg->key) { std_pHS->free(pMsg); return; }
    _strcpy(pMsg->key, key);
    pMsg->uniStr = stdString_FastWCopy(value);
    pMsg->field_8 = 0;

    stdHashTable_SetKeyVal(pTable->hashtable, pMsg->key, pMsg);
    pTable->numMsgs++;
}

static void jkStrings_RegisterVRStrings(stdStrTable* pTable)
{
    jkStrings_RegisterString(pTable, "GUIEXT_VR_OPTIONS",              L"VR Options");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_OPTIONS_HINT",         L"Configure VR settings");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_DOMINANT_HAND",        L"Right-Handed");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_DOMINANT_HAND_HINT",   L"Which hand holds the weapon");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_SWAP_STICKS",          L"Swap Thumbsticks");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_SWAP_STICKS_HINT",     L"Move with the right stick and turn with the left");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_ALIGN_TOOL",              L"Weapon Alignment (dev)");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_ALIGN_TOOL_HINT",         L"In-headset weapon offset editor. Unticking saves the offsets.");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_REFRESH_RATE_HINT",       L"Display refresh rate. Higher rates use more battery.");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_SABER",         L"Swing the saber to attack");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_FISTS",         L"Punch forward with either hand to attack");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_CROUCH",           L"Push the %ls thumbstick down to crouch");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_STAND",            L"Push the %ls thumbstick down again to stand up");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_RUN",              L"Click the %ls thumbstick to toggle run");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_WEAPON_WHEEL",     L"Hold the %ls grip to see your weapons");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_FORCE_WHEEL",      L"Hold the %ls grip to see and select your Force powers");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_FORCE_USE",        L"Press the %ls trigger to use the selected Force power");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_ALT_FIRE",         L"Fire with the %ls trigger, alt-fire with %ls");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_JUMP",                 L"Press %ls to jump");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_ACTIVATE",             L"Press %ls to open doors and use switches");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_HOLOMAP",              L"Click the %ls thumbstick for the 3D map");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_ITEMS",                L"Hold the %ls grip and push the stick to reach your items");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_DEATH_LOAD",           L"Press Y for the menu, then Load to restore your last save");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_HOLOMAP_GRAB",         L"Use the grip buttons to grab, rotate and scale the map");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_PROMPT_HOLOMAP_CLOSE",    L"Click the %ls thumbstick again to close the map");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_MOVE_DIRECTION",       L"Controller-Relative Move");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_MOVE_DIRECTION_HINT",  L"Move relative to controller instead of head direction");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_SNAP_TURN",            L"Snap Turn");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_SNAP_TURN_HINT",       L"Use snap turning instead of smooth turning");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_SNAP_ANGLE",           L"Snap Turn Angle");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_SNAP_ANGLE_HINT",      L"Angle of each snap turn step");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_SMOOTH_SPEED",         L"Smooth Turn Speed");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_SMOOTH_SPEED_HINT",    L"Speed of smooth turning in degrees per second");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_HEIGHT_OFFSET",        L"Height Offset");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_HEIGHT_OFFSET_HINT",   L"Adjust player height in meters");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_SUPERSAMPLING",        L"Supersampling:");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_SUPERSAMPLING_HINT",   L"Render scale multiplier (1.0 = native resolution)");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_CROUCH_MODE",          L"Toggle Crouch");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_CROUCH_MODE_HINT",     L"Use toggle crouch instead of hold to crouch");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_WEAPON_CROSSHAIR",     L"Weapon Crosshair");
    jkStrings_RegisterString(pTable, "GUIEXT_VR_WEAPON_CROSSHAIR_HINT",L"Show a laser dot where the weapon is aimed");
}
#endif

static int jkStrings_bInitialized = 0;
static stdStrTable jkStrings_table;
#ifdef QOL_IMPROVEMENTS
static stdStrTable jkStrings_tableExt;
stdStrTable jkStrings_tableExtOver;
#endif // QOL_IMPROVEMENTS

int jkStrings_Startup()
{
    stdPlatform_Printf("OpenJKDF2: %s\n", __func__);
    
    // Added: clean reset
    _memset(&jkStrings_table, 0, sizeof(jkStrings_table));

    int result = stdStrTable_Load(&jkStrings_table, "ui\\jkstrings.uni");

    // Added: OpenJKDF2 i8n
#ifdef QOL_IMPROVEMENTS
    _memset(&jkStrings_tableExtOver, 0, sizeof(jkStrings_tableExtOver));
    _memset(&jkStrings_tableExt, 0, sizeof(jkStrings_tableExt));

    stdStrTable_Load(&jkStrings_tableExtOver, "ui\\openjkdf2_i8n.uni");
    stdStrTable_Load(&jkStrings_tableExt, "ui\\openjkdf2.uni");

    // Altered: If the extension string table failed to load (e.g., Android where APK
    // assets aren't accessible via fopen), register VR strings directly in code.
    // This ensures VR option labels always work regardless of filesystem access.
#ifdef PLATFORM_VR
    if (!jkStrings_tableExt.hashtable) {
        // Create a hash table for the extension strings
        jkStrings_tableExt.hashtable = stdHashTable_New(64);
        jkStrings_tableExt.magic_sTbl = 0x5454626c; // sTbl
    }
    jkStrings_RegisterVRStrings(&jkStrings_tableExt);
#endif
#endif // QOL_IMPROVEMENTS

    jkStrings_bInitialized = 1;
    return result;
}

void jkStrings_Shutdown()
{
    stdPlatform_Printf("OpenJKDF2: %s\n", __func__);

    // Added: OpenJKDF2 i8n
#ifdef QOL_IMPROVEMENTS
    stdStrTable_Free(&jkStrings_tableExtOver);
    stdStrTable_Free(&jkStrings_tableExt);
    _memset(&jkStrings_tableExtOver, 0, sizeof(jkStrings_tableExtOver));
    _memset(&jkStrings_tableExt, 0, sizeof(jkStrings_tableExt));
#endif

    stdStrTable_Free(&jkStrings_table);
    jkStrings_bInitialized = 0;

    // Added: clean reset
    _memset(&jkStrings_table, 0, sizeof(jkStrings_table));
}

wchar_t* jkStrings_GetUniString(const char *key)
{
    wchar_t *result; // eax

    // Added: Allow openjkdf2_i8n.uni to override everything
#ifdef QOL_IMPROVEMENTS
    result = stdStrTable_GetUniString(&jkStrings_tableExtOver, key);
    if ( !result )
#endif
    result = stdStrTable_GetUniString(&jkStrings_table, key);
    if ( !result )
        result = stdStrTable_GetUniString(&jkCog_strings, key);
#ifdef QOL_IMPROVEMENTS
    if ( !result )
        result = stdStrTable_GetUniString(&jkStrings_tableExt, key);
#endif
    return result;
}

wchar_t* jkStrings_GetUniStringWithFallback(const char *key)
{
    wchar_t *result; // eax

    // Added: Allow openjkdf2_i8n.uni to override everything
#ifdef QOL_IMPROVEMENTS
    result = stdStrTable_GetUniString(&jkStrings_tableExtOver, key);
    if ( !result )
#endif
    result = stdStrTable_GetUniString(&jkStrings_table, key);

    // Added: OpenJKDF2 i8n -- stdStrTable_GetStringWithFallback must always be the last lookup
    // because it always succeeds.
#ifdef QOL_IMPROVEMENTS
    if ( !result )
        result = stdStrTable_GetUniString(&jkCog_strings, (char *)key);
    if ( !result )
        result = stdStrTable_GetStringWithFallback(&jkStrings_tableExt, key);
#else
        if ( !result )
        result = stdStrTable_GetStringWithFallback(&jkCog_strings, (char *)key);
#endif
    return result;
}

int jkStrings_unused_sub_40B490()
{
    return 1;
}

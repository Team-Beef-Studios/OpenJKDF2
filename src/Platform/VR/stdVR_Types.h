#ifndef _STDVR_TYPES_H
#define _STDVR_TYPES_H

// Added: VR support types

#include "types.h"
#include "Primitives/rdVector.h"
#include "Primitives/rdMatrix.h"

#ifdef __cplusplus
extern "C" {
#endif

// VR Eye indices
#define STDVR_EYE_LEFT   0
#define STDVR_EYE_RIGHT  1
#define STDVR_EYE_COUNT  2

// VR Controller indices
#define STDVR_CONTROLLER_LEFT   0
#define STDVR_CONTROLLER_RIGHT  1
#define STDVR_CONTROLLER_COUNT  2

// Display refresh rates that the code keeps from XR_FB_display_refresh_rate. Quest 3 reports
// 5 rates (72/80/90/120 Hz and similar), so 16 is enough.
#define STDVR_MAX_REFRESH_RATES 16

// VR Button bit flags
#define STDVR_BTN_TRIGGER_R    0x0001
#define STDVR_BTN_TRIGGER_L    0x0002
#define STDVR_BTN_GRIP_R       0x0004
#define STDVR_BTN_GRIP_L       0x0008
#define STDVR_BTN_A            0x0010
#define STDVR_BTN_B            0x0020
#define STDVR_BTN_X            0x0040
#define STDVR_BTN_Y            0x0080
#define STDVR_BTN_MENU         0x0100
#define STDVR_BTN_THUMBSTICK_L 0x0200
#define STDVR_BTN_THUMBSTICK_R 0x0400

// VR movement direction modes
#define STDVR_MOVE_HEAD       0  // Movement follows head orientation
#define STDVR_MOVE_CONTROLLER 1  // Movement follows controller orientation

// VR turn modes
#define STDVR_TURN_SMOOTH     0
#define STDVR_TURN_SNAP       1

// Motion tracking constants
#define STDVR_MOTION_HISTORY_SIZE   10
#define STDVR_OFFHAND_HISTORY_SIZE  5

// Angle set indices (different interpretations of controller orientation)
typedef enum stdVR_AngleSet {
    STDVR_ANGLES_DEFAULT = 0,   // Raw controller orientation
    STDVR_ANGLES_ADJUSTED = 1,  // Ergonomic pitch offset for weapons
    STDVR_ANGLES_SABER = 2,     // Grip-aligned for saber
    STDVR_ANGLES_COUNT = 3
} stdVR_AngleSet;

// Swing type detection for melee/saber
typedef enum stdVR_SwingType {
    STDVR_SWING_NONE = 0,
    STDVR_SWING_HORIZONTAL_LEFT,
    STDVR_SWING_HORIZONTAL_RIGHT,
    STDVR_SWING_VERTICAL_DOWN,
    STDVR_SWING_VERTICAL_UP,
    STDVR_SWING_DIAGONAL_DL,    // Down-left
    STDVR_SWING_DIAGONAL_DR,    // Down-right
    STDVR_SWING_DIAGONAL_UL,    // Up-left
    STDVR_SWING_DIAGONAL_UR,    // Up-right
    STDVR_SWING_STAB
} stdVR_SwingType;

// Gesture types for Force powers
typedef enum stdVR_GestureType {
    STDVR_GESTURE_NONE = 0,
    STDVR_GESTURE_PUSH,
    STDVR_GESTURE_PULL,
    STDVR_GESTURE_GRIP,         // Force grip (hand closing)
    STDVR_GESTURE_LIGHTNING,    // Palm forward, fingers spread
    STDVR_GESTURE_WAVE          // Mind trick wave
} stdVR_GestureType;

// Motion state for a single controller
typedef struct stdVR_MotionState {
    // Velocity tracking (from OpenXR)
    rdVector3 linearVelocity;       // Linear velocity in m/s
    rdVector3 angularVelocity;      // Angular velocity in rad/s
    float swingSpeed;               // Magnitude of linear velocity
    float forwardSpeed;             // Signed velocity along controller forward (m/s); +ve = forward thrust/punch

    // Multiple angle interpretations
    rdVector3 angles[STDVR_ANGLES_COUNT];       // Current angles per set
    rdVector3 anglesLast[STDVR_ANGLES_COUNT];   // Previous frame angles
    rdVector3 anglesDelta[STDVR_ANGLES_COUNT];  // Frame-to-frame change

    // Position relative to HMD (for gesture detection)
    rdVector3 offset;               // Position relative to HMD
    rdVector3 offsetHistory[STDVR_MOTION_HISTORY_SIZE];
    uint32_t offsetTimestamps[STDVR_MOTION_HISTORY_SIZE];
    int offsetHistoryIndex;

    // Attack state
    int bVelocityTriggeredAttack;       // Swing exceeded threshold this frame
    int bVelocityTriggeredAttackLast;   // Previous frame state
    stdVR_SwingType currentSwing;       // Detected swing type

    // Saber-specific
    int bSaberBlockDebounce;
    uint32_t saberBlockTime;
    rdVector3 saberBounceAngles;
} stdVR_MotionState;

// Gesture state for Force powers
typedef struct stdVR_GestureState {
    stdVR_GestureType activeGesture;
    float gestureStrength;      // 0.0 - 1.0
    rdVector3 gestureDirection;
    int gestureStartFrame;
    rdVector3 gestureStartPosition;
} stdVR_GestureState;

// Per-eye view information
typedef struct stdVR_EyeView {
    rdMatrix34 viewMatrix;          // Eye view matrix (camera space)
    float projectionMatrix[16];     // 4x4 asymmetric projection matrix
    float fovLeft;                  // Left tangent of FOV
    float fovRight;                 // Right tangent of FOV
    float fovUp;                    // Up tangent of FOV
    float fovDown;                  // Down tangent of FOV
} stdVR_EyeView;

// Controller pose and state
typedef struct stdVR_ControllerState {
    rdVector3 position;             // Position in tracking space
    rdVector3 orientation;          // Pitch, Yaw, Roll in degrees
    rdMatrix34 poseMatrix;          // Aim pose matrix (where controller points)
    rdMatrix34 gripPoseMatrix;      // Grip pose matrix (where hand holds)
    int bTracking;                  // Is controller being tracked
    int bActive;                    // Is controller active/connected

    // Added: this hand's own thumbstick, before the move/turn role routing, in the plain
    // convention: [0] = horizontal (left/right), [1] = vertical (up/down). Consumers that
    // care about a physical hand rather than a role (the wheel's page flip) use this.
    float thumbstick[2];

    // Motion tracking data
    stdVR_MotionState motion;       // Velocity and swing tracking
} stdVR_ControllerState;

// Main VR client information structure
typedef struct stdVR_ClientInfo {
    // HMD tracking
    rdVector3 hmdPosition;          // HMD position in world space
    rdVector3 hmdOrientation;       // HMD orientation (Pitch, Yaw, Roll)
    rdMatrix34 hmdPoseMatrix;       // Full HMD pose matrix

    // Per-eye view data
    stdVR_EyeView eyes[STDVR_EYE_COUNT];

    // Controller tracking
    stdVR_ControllerState controllers[STDVR_CONTROLLER_COUNT];

    // Decoupled movement direction (for comfort)
    rdVector3 moveForward;          // Forward direction for movement
    float moveYaw;                  // Yaw angle for movement calculations
    float movePitch;                // Added: pitch of the same source, for swimming/flying

    // Analog input state
    float analogMove[2];            // Left stick: [0]=X (strafe), [1]=Y (forward)
    float analogTurn[2];            // Right stick: [0]=X (turn), [1]=Y (unused)
    float triggerLeft;              // Left trigger value (0-1)
    float triggerRight;             // Right trigger value (0-1)

    // Added: display refresh rates that the runtime reports, in Hz. The list is empty when the
    // runtime does not support XR_FB_display_refresh_rate. Meta Quest supports it. Some other
    // runtimes, for example Pico, can report no support.
    float aRefreshRates[STDVR_MAX_REFRESH_RATES];
    int numRefreshRates;            // Number of valid entries in aRefreshRates
    float currentRefreshRate;       // Rate that the runtime uses now, 0 if unknown
    float gripLeft;                 // Left grip value (0-1)
    float gripRight;                // Right grip value (0-1)

    // Button state
    uint32_t buttonState;           // Currently pressed buttons
    uint32_t buttonPressed;         // Buttons pressed this frame
    uint32_t buttonReleased;        // Buttons released this frame

    // Runtime info
    int bSessionRunning;            // Is VR session active
    int bShouldRender;              // Should we render this frame
    int64_t predictedDisplayTime;   // Predicted display time for this frame

    // Render target info
    int renderWidth;                // Per-eye render width
    int renderHeight;               // Per-eye render height

    // Screen layer mode (for menus/cinematics)
    int bUseScreenLayer;            // True when showing 2D content in quad layer
    rdVector3 screenLayerSnapPos;   // HMD position when entering screen layer mode
    float screenLayerSnapYaw;       // HMD yaw when entering screen layer mode
    float screenLayerDistance;      // Distance of screen from player (meters)
    float screenLayerWidth;         // Screen width in meters
    float screenLayerHeight;        // Screen height in meters

    // VR Menu cursor state
    float menuCursorX;              // Normalized cursor X (0.0 - 1.0)
    float menuCursorY;              // Normalized cursor Y (0.0 - 1.0)
    int menuCursorScreenX;          // Screen-space cursor X
    int menuCursorScreenY;          // Screen-space cursor Y
    int bMenuCursorActive;          // Is the VR menu cursor active
    int bMenuTriggerDown;           // Is trigger currently held
    int bMenuTriggerPressed;        // Was trigger pressed this frame
    int bMenuTriggerReleased;       // Was trigger released this frame
} stdVR_ClientInfo;

// VR configuration settings
typedef struct stdVR_Config {
    // Movement settings
    int moveDirection;              // STDVR_MOVE_HEAD or STDVR_MOVE_CONTROLLER
    int turnMode;                   // STDVR_TURN_SMOOTH or STDVR_TURN_SNAP
    int snapTurnAngle;              // Snap turn angle in degrees (30, 45, 90)
    float smoothTurnSpeed;          // Smooth turn speed in degrees/sec

    // Speed
    float walkSpeedScale;           // Walk speed multiplier (default 1.0, applied to base thrust)

    // 6DoF head-driven body movement: scale factor mapping physical head metres to in-game
    // movement (0 = disabled, body stays put and the camera floats with the head as before).
    float sixDoFScale;

    // Scale and comfort
    float worldScale;               // World scale multiplier (default 1.0)
    float heightOffset;             // Player height offset in meters
    float fixedHeightAdjustment;          // Adjustment to make floor line up

    // Handedness
    int dominantHand;               // 0=left, 1=right
    int bSwapSticks;                // Move on the right stick and turn on the left

    // Crosshair
    int bWeaponCrosshair;           // Show in-world weapon crosshair dot

    // Quality settings
    float supersampling;            // Render scale multiplier
} stdVR_Config;

// Motion controls configuration
typedef struct stdVR_MotionConfig {
    // Velocity thresholds (m/s)
    float weaponVelocityTrigger;    // Swing speed for melee attack (default: 2.0)
    float saberVelocityTrigger;     // Swing speed for saber attack (default: 2.5)
    float forceVelocityTrigger;     // Speed for force gesture (default: 1.5)
    float forceDistanceTrigger;     // Distance movement for push/pull (default: 0.3m)

    // Ergonomic offsets (degrees)
    float weaponPitchAdjust;        // Controller pitch offset for weapon model
    float saberPitchAdjust;         // Controller pitch offset for saber

    // Weapon position offset (meters, in controller local space)
    float weaponOffsetX;            // Left/right offset
    float weaponOffsetY;            // Forward/back offset (negative = closer to player)
    float weaponOffsetZ;            // Up/down offset

    // Weapon model scale (default 1.0, increase for larger weapon model)
    float weaponModelScale;

    // Fire position offset (meters, in world space - adjusts where bullets spawn)
    // These are applied ON TOP of weapon offsets, only affecting fire position not weapon visual
    float fireOffsetX;              // Right offset (positive = right)
    float fireOffsetY;              // Forward offset (positive = forward)
    float fireOffsetZ;              // Up offset (positive = up)

    // Feature toggles
    int bMotionAimEnabled;          // Aim with controller instead of HMD
    int bMotionSaberEnabled;        // Swing to attack with saber
    int bMotionForceEnabled;        // Gestures for Force powers
    int bTwoHandedEnabled;          // Off-hand for rifle grip stabilization
    int bDisablePovAnims;           // Disable weapon recoil/fire animations in VR

    // Smoothing
    int positionSmoothingSamples;   // 1-10 samples (default: 3)
    float velocitySmoothingFactor;  // 0.0-1.0 (default: 0.5)
} stdVR_MotionConfig;

#ifdef __cplusplus
}
#endif

#endif // _STDVR_TYPES_H

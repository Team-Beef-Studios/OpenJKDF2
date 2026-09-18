# JKDF2-XR VR Controls

The diagram and tables below show the default layout: **Dominant Hand = Right**,
**Swap Thumbsticks = off**.

Two options in the VR options menu change the layout. They are independent:

- **Dominant Hand** moves the *triggers*, the *grips* and **Alt Fire** to the other
  controller. The weapon model follows the dominant controller and projectiles leave
  its barrel. Alt fire has to follow the weapon hand - you do not alt fire with your
  off hand. **Menu** takes whichever upper button alt fire did not.
- **Swap Thumbsticks** exchanges the *move* and *turn* sticks, the actions on their
  clicks, and the **Jump** and **Activate** buttons. Jump has to follow the movement
  stick, or both land on the same thumb and you cannot move and jump at once.

The face buttons move in pairs, each for its own reason:

| Pair | Follows | Default (right-handed, sticks unswapped) |
|---|---|---|
| Lower: A / X | the **movement stick** | A = Jump, X = Activate |
| Upper: B / Y | the **weapon hand** | B = Alt Fire, Y = Menu |

Jump must not share a thumb with movement, and alt fire must sit under the hand holding
the weapon. The menu is always reachable from the headset's own menu button as well, so
it takes whichever upper button is left over.

One thing never moves:

- **Thumbstick roles.** Move is the left stick and turn is the right stick, in
  **both** handedness modes. Only **Swap Thumbsticks** changes this.

```
LEFT CONTROLLER                           RIGHT CONTROLLER
==============================            ================================

    [Y] Open / Close Menu                     [B] Alt Fire
    [X] Activate / Use                        [A] Jump / Swim Up

    [Trigger] Use Force Power                 [Trigger] Primary Fire
    [Grip]    Force / Items Wheel             [Grip]    Weapon Wheel
              (hold)                                    (hold)

        /---\                                     /---\
       /     \  Move                             /     \  Turn
      | Stick |  (forward/back/                 | Stick |  (snap or smooth)
       \     /    strafe)                        \     /
        \---/                                     \---/
       [Click]  Toggle Walk/Run                  [Click]  Toggle 3D HoloMap

          Down: (movement)                          Down: Toggle Crouch

    [Menu] Short Press = Menu / Close Menu
           Long Press  = Recenter View
```

## Right Controller (Dominant Hand by default)

| Input | Action |
|-------|--------|
| **Trigger** | Primary Fire. Does nothing with the lightsaber or the fists — see *Motion Melee* |
| **Grip** | Weapon Wheel (hold to open, point to highlight, release to select) |
| **A Button** | Jump / Swim Up |
| **B Button** | Alt Fire (secondary fire mode) |
| **Thumbstick Left/Right** | Turn (snap or smooth, configurable) |
| **Thumbstick Down** | Toggle Crouch / Swim Down |
| **Thumbstick Click** | Toggle 3D HoloMap |

The trigger and the grip move to the left controller when **Dominant Hand = Left**.
The thumbstick actions move to the left stick when **Swap Thumbsticks** is on.

There is no thumbstick flick for the next weapon. The weapon wheel is the only way
to change weapon. A nudged stick used to switch weapon by accident.

## Left Controller (Off-Hand by default)

| Input | Action |
|-------|--------|
| **Trigger** | Use Force Power (the selected power) |
| **Grip** | Force / Items Wheel (hold to open, point to highlight, release to select) |
| **X Button** | Activate / Use |
| **Y Button** | Open Menu, or close an open menu |
| **Thumbstick** | Move (forward / back / strafe) |
| **Thumbstick Click** | Toggle Walk / Run |

## Motion Melee

Melee weapons do not fire from the trigger. They read controller motion.

| Weapon | Action |
|--------|--------|
| **Lightsaber** | Swing the dominant controller to attack. The trigger does nothing |
| **Fists** | Punch forward with **either** controller. The hand that throws the punch buzzes |

The punch threshold is the `weaponVelocityTrigger` motion setting. It defaults to
2.0. Motion saber attacks can be turned off, and the trigger then fires the saber.

## Menu Button

The Quest system menu button belongs to the OS, so the **Y button** opens the
in-game menu as well.

| Input | Action |
|-------|--------|
| **Y Button** | Open the menu. Press again to close an open menu |
| **Short Press (system menu button)** | Same as Y |
| **Long Press, 1 second (system menu button)** | Recenter View |

Only the system menu button recenters. Y never recenters.

## Weapon & Force Wheels

Hold a **Grip** to open a wheel. Point the controller to highlight a segment.
Release the grip to select it. The game runs at 10% speed while a wheel is open.
Movement, turning and the sticks all stop feeding the game while a wheel is open.

| Grip | Wheel |
|------|-------|
| **Dominant Grip** | Weapon Wheel. Shows every weapon you carry as a 3D model |
| **Off-Hand Grip** | A wheel with two pages: **Force** and **Items** |

The off-hand wheel opens on the Force page. It opens on the Items page instead when
you have no force powers yet. Push the **off-hand thumbstick** left or right to flip
the page. One push is one flip. Tabs at the top of the wheel show the current page.

Force powers are **selected** on release. Items are **used** on release — the wheel
is the whole interaction, so there is no separate "use item" button.

The wheels do not open while the HoloMap is showing.

## 3D HoloMap

Click the **turn stick** (the right stick by default) to open the map. Click it
again to close it.

While the map is visible:

| Input | Action |
|-------|--------|
| **Both Grips (hold)** | Grab the map: move the controllers to rotate, scale and reposition it |
| **Move stick Left/Right** | Rotate the map (only while the grips are not held) |
| **Turn stick Up/Down** | Zoom in and out (only while the grips are not held) |

Movement stops while the map is up.

## Cheat Combo

| Input | Action |
|-------|--------|
| **Both Grips + Both Triggers (hold 1.5 sec)** | All weapons, force powers, full health |

## Weapon Alignment Tool (Development Only)

The tool is compiled out by default. To build it, uncomment
`VR_WEAPON_ALIGNMENT_TOOL` in `src/engine_config.h`. The tool takes over the whole
controller, so a shipping build must never include it.

**Turn it on** with the **Weapon Alignment (dev)** checkbox in the VR options menu,
or with the `g_vrAlignTool` cvar. **Turn it off the same way** — that is what writes
the offsets to `jkdf2xr_vr_weapons.json`.

While the tool is open, no VR input reaches the game except the two triggers.

| Input | Action |
|-------|--------|
| **A Button** | Cycle mode: Position -> Scale -> Rotation |
| **Right Trigger** | Next weapon |
| **Left Trigger** | Previous weapon |

| Mode | Move Stick | Turn Stick |
|------|-----------|------------|
| **Position** | X and Y offset | Y axis: Z offset |
| **Scale** | One axis: model scale | — |
| **Rotation** | Pitch and yaw | Y axis: roll |

The tool reads the **move** and **turn** sticks, not fixed sides, so
**Swap Thumbsticks** moves its controls too. The overlay in the headset names the
axis for the current mode.

The tool draws the dominant controller's axes as world rays. The long **green** ray
is the direction a projectile takes. Red is right and blue is up. Line the barrel of
the weapon model up along the green ray.

The alignment angles move the weapon **model** only. They never move the point that
projectiles leave from, so a model can be posed freely without changing where the
weapon shoots.

Saved offsets live in `jkdf2xr_vr_weapons.json` and apply to normal builds that do
not compile the tool in.

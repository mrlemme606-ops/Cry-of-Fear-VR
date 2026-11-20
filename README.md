# Cry of Fear VR

An unofficial VR port of **Cry of Fear** for SteamVR headsets, built on the **Xash3D FWGS** engine. It plays the original campaign from start to finish in room-scale VR with motion controllers.

> **This is an early release.** The campaign is playable from beginning to end, but expect rough edges. Please read **Known issues** before you start, and feel free to report other issues.

---

## What you need

- **Cry of Fear, installed from Steam** (it is free on Steam). This mod does **not** include any game files. Without the Steam version it will not run.
- **SteamVR**, running before you start the game.
- A **Windows PC** and a VR headset that works with SteamVR. Developed and tested on a **Meta Quest 2** (via Link or Virtual Desktop). Other headsets are untested, see below.

## Installation

1. Install **Cry of Fear** on Steam.
2. Find the game folder. In Steam: right-click Cry of Fear, **Manage**, **Browse local files**. A typical path is `C:\Program Files (x86)\Steam\steamapps\common\Cry of Fear`.
3. **Drag and drop all files from this release into your Cry of Fear folder**, and agree to merge or overwrite if Windows asks.
4. Start **SteamVR**.
5. Run **`cofvr.exe`** from your Cry of Fear folder.
6. Stand where you want to play and **click both thumbsticks together** to recenter.

## Controls

Basic controls:

| Action | Control |
| --- | --- |
| Walk | Left thumbstick |
| Turn | Right thumbstick (snap or smooth, set in the options menu) |
| Jump | A (hold it to crouch-jump) |
| Crouch | Physically crouch, or press the right thumbstick down |
| Recenter | Click both thumbsticks at the same time |
| Reload | B |
| Menu | Y |
| Inventory | X |
| Hotkey Selector | Right Joystick up |

## Working features

- Full campaign playable in VR with room-scale movement.
- All base campaign weapons function. (Secret Room Weapons Untested)
- Snap and smooth turning, and a recenter shortcut.
- Physical crouching, plus hold-A crouch-jump for tight ledges.
- Weapons held in your hand with hand-relative aiming, and a two-hand grip for rifles and shotguns.
- Dual wielding.
- Rifle scope view on the sniper rifle and other scoped weapons.
- Phone flashlight, flare and lantern lights.
- Health and ammo displayed on your left hand.

## Known issues

**Melee**
- Melee hits are not reliable. Enemies are hit most of the time, but swings at planks and other breakable objects can still miss.

**Lighting**
- The glow of the phone flashlight can flicker or change colour (yellow, orange, green, etc.).

**Visuals**
- Notes and some UI elements can be small and hard to read.

**Dual wielding and weapons**
- Arm and sleeve cutting on the left hand is not perfect. A nightstick in the left hand shows the whole arm.
- Dual Wielding may provide some unaccounted for jank.

**Movement**
- Walking can feel slow for a few seconds after saving at a tape recorder.
- Not so smooth walking with a bit of jank

**Hardware**
- Only tested on a Quest 2 through SteamVR. Other headsets and controllers are untested. (feel free to report results)
- Windows only.

## Performance and settings tips

All settings are console commands (open the console in-game) and are saved.

## Disclaimers

- This is an **unofficial, fan-made, non-commercial** project. It is **not affiliated with, endorsed by, or supported by** the Cry of Fear developers or any rights holder.
- **Cry of Fear** was created by Team Psykskallar. All of its content, characters, music and assets belong to their owners. This release contains **no Cry of Fear game files**; you must own and install the game from Steam.
- The mod works by loading the game's own libraries and **modifying them in memory while the game runs**. It does not change the original files, but it is only expected to work with the Steam version of the game that it was tested on.
- Use at your own risk. The software is provided **as is**, without warranty. Back up your saves before installing.

## Credits and licences

- **Cry of Fear** by Team Psykskallar.
- **Xash3D FWGS** engine by the Xash3D FWGS team, licensed under the **GNU GPL v3**.
- **OpenVR** by Valve Software is used to talk to SteamVR.
- **cof-enhanced** by hajdawery for a headstart into porting Cry Of Fear into the Xash3D engine
- VR port and changes by **thumbgaming**
- Made with the help of AI (Claude)

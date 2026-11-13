# Changelog

## 1.2.0 (2026-11)
- **ROLL**: one tap for a random setting that still sounds good: attack, sustain, speed, focus and mix. **MILD**, **MEDIUM** or **WILD** under it sets how far it goes (MILD adds a little snap, WILD can carve the hits right up). OUTPUT is kept at 0 dB. Keep rolling until you like it, then fine-tune.
- The preset keys are a little narrower to make room for it. Two parameters were added at the end of the list (Roll, Roll Wildness); saved projects open as before.

## 1.1.2 (2026-11)
- **CPU figure recorded.** Measured on an MPC One with the Open MPC bench: **PASS**, 3.0% of an audio block at the 99th percentile and 4.7% at worst, so it runs comfortably alongside a full project. It shows on the catalog card and in INSTALL.md.
- Nothing else changed: same sound, controls and saved projects as 1.1.1.

## 1.1.1 (2026-11)
- **Ready for MPC OS 3.10.1's plugin folders.** Every download now carries the `plugin.json` that MPC OS 3.10.1 looks for, so the plugin folder can be copied by hand straight into `VST2 Plugins`.
- **Gen2 MPCs in the Open MPC catalog.** A separate 64-bit download (`-mpc-aarch64.zip`), so the Plugin Manager can install it on Gen2 MPCs, and the catalog's 3.10.1 download (`-mpc-sdk.zip`).
- Built with the current Open MPC kit. Sound, controls and saved projects are unchanged.

## 1.1.0 (2026-11)
- **Three looks.** A new default look, **Print Plate** (a warm printed panel), plus **Night Rack** (dark rack unit) and **Colour Block** (flat, big type), each in PotentialPunch's own colour. Pick one with the big LOOK keys on the SETTINGS page; it shows the next time the plugin loads (take it off and add it again, or load your project again), and it is remembered on your MPC for every project.
- Sound, controls and saved projects are unchanged. One parameter was added at the end of the list (LOOK); it is never saved in a project.
- **Akai's plugin folder format** for MPC OS 3.10.1 and later: a second download (`-akai-vst2.zip`) with 32-bit and 64-bit builds, so it now also runs on Gen2 MPCs. Copy the folder into `VST2 Plugins` and turn on External Plugin Support: no hacking needed.

## 1.0.2 (2026-11)
- Fixed: pressing **STOP** or **Overdub** while the MPC was playing could reload the preset and undo your ATTACK / SUSTAIN / MIX tweaks. The MPC's host briefly changes a plugin's first parameter when it re-prepares audio, and that was PRESET. That first slot now does nothing; PRESET is a new parameter at the end (same menu, same Q-Link). Saved projects open with their preset and settings as before.

## 1.0.1 (2026-11)
- Fixed: the MPC's PRESET menu kept closing (and its list flickered) while the plugin was open. The live meter no longer makes the MPC redraw its screen. The line above the meter is now a short tip instead of live numbers; the meter shows the same thing.

## 1.0.0 (2026-11)
- First release: ATTACK and SUSTAIN shaping (level-independent, up to ±18 dB), SPEED, FOCUS, soft CLIP, MIX and OUTPUT, a live meter and readout, and 12 one-tap presets.

# Changelog

## 1.0.2 (2026-11)
- Fixed: pressing **STOP** or **Overdub** while the MPC was playing could reload the preset and undo your ATTACK / SUSTAIN / MIX tweaks. The MPC's host briefly changes a plugin's first parameter when it re-prepares audio, and that was PRESET. That first slot now does nothing; PRESET is a new parameter at the end (same menu, same Q-Link). Saved projects open with their preset and settings as before.

## 1.0.1 (2026-11)
- Fixed: the MPC's PRESET menu kept closing (and its list flickered) while the plugin was open. The live meter no longer makes the MPC redraw its screen. The line above the meter is now a short tip instead of live numbers; the meter shows the same thing.

## 1.0.0 (2026-11)
- First release: ATTACK and SUSTAIN shaping (level-independent, up to ±18 dB), SPEED, FOCUS, soft CLIP, MIX and OUTPUT, a live meter and readout, and 12 one-tap presets.

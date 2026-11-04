# v1.1.3
- Packs can hide parts of a menu (`hide-nodes`), e.g. the treasure room floor so the pack background shows.
- Removed menu transitions (most players use Fast Menu Transitions anyway).

# v1.1.2
- (Menu transitions test - removed again in v1.1.3.)

# v1.1.1
- New name: The Flame Engine.

# v1.1.0
- New name: Flameperfect Pack Engine.
- Lists, comments and profiles can be recoloured from the pack (`list-colors`, `background-color`), and profile boxes are recoloured once a profile finishes loading. Packs no longer need Happy Textures for this.
- One mod now covers everything Flameperfect packs used other mods for.

# v1.0.16
- `ui-brown-to` now also recolours the brown outlines around lists (profile, messages, friends, comments).

# v1.0.15
- Pack-maker debug option now also reports where GD's brown tints come from.

# v1.0.14
- Packs can recolour GD's brown list outlines and text boxes (`ui-brown-to`), which can't be changed with textures alone.

# v1.0.13
- Startup intros: packs can play their own animated intro (with sound) when GD opens (`startup-intro`, `startup-sound`). No extra mod needed. Click to skip, or turn it off in the mod's settings.

# v1.0.12
- Packs can remove the dark panel behind the pause menu (`pause-hide-background`).

# v1.0.11
- Packs can tint the end-screen and pause-menu panels (`end-screen-color`, `pause-color`).
- Optional bubbles drifting up while paused (`pause-bubbles`).

# v1.0.10
- The pack background now also replaces other mods' menu backgrounds (like BetterInfo's).
- Bubbles can rise through the end screen when it opens (`end-screen-bubbles`).

# v1.0.9
- One end-screen icon now sits attached in front of LEVEL COMPLETE (same height as the text). Two icons sit either side, sized to the text.

# v1.0.8
- Fixed end-screen icons not appearing.

# v1.0.7
- Animated loading screens (`loading-shader`).
- Custom icons beside LEVEL COMPLETE on the end screen (`end-screen-icons`).

# v1.0.6
- The "get the mod" banner can now hang off the More Games button, so it only shows on the main menu (not the loading screen). The mod hides it.

# v1.0.5
- The "get the mod" banner now hangs off the GEOMETRY DASH logo instead of the Daily button, so it no longer moves when you press Daily. The mod hides it on the main menu and the loading screen.

# v1.0.4
- Reads the pack's settings once and re-reads only when they change (faster menus).
- Colours in pack settings are clamped to 0-255.

# v1.0.3
- Node IDs now use the mod's ID prefix, as Geode recommends.

# v1.0.2
- Hides the new top-right "get the mod" banner that Flameperfect packs show when the mod is missing.

# v1.0.1
- Menus that use the pack background (like the main levels screen) now hide their ground strip too, so the background goes all the way down.

# v1.0.0
- First release: merges Flameperfect Pack Helper and Flameperfect Menu Icons into one per-pack mod.
- Built-in menu background (replaces Menu Shaders for Flameperfect packs) and loading screen (image, colour, messages).
- Own icons on the title screen, level-complete fly-in, themed chest glow, social links, node recolours.

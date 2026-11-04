# The Flame Engine

This is the mod behind my Flameperfect texture packs.

Texture packs can only swap out pictures, so anything that moves or changes colour in code needs a mod. That's what this is. It reads a little settings file inside the pack and only turns on the stuff that pack uses.

If you're not using one of my packs, it just sits there and does nothing, so it's fine to leave it installed.

## What it does

**When GD opens**
- Plays the pack's own intro animation (with sound). Click to skip it, or turn it off in the mod settings.
- Animated loading screen, or a picture/colour behind the loading bar, plus custom loading messages.

**Menus**
- The pack's background (animated or still) on the main menu and the other menus, including other mods' menus like BetterInfo.
- Hides the ground strip and floors (like the treasure room one) so the background goes all the way down.
- The icons running across the title screen use your own equipped icons and colours.
- The social buttons open the pack maker's links, and the logo button opens their profile.

**Lists and popups**
- Recolours GD's brown lists, comments, messages, friends and profiles to match the pack, outlines and text boxes too.
- Tints the pause menu and end screen, with bubbles (or whatever the pack uses) floating up through them. Can also remove the dark panel behind the pause menu.
- LEVEL COMPLETE flies into the end screen, and packs can put icons next to it.
- Chest openings glow in the pack's colour.
- Fixes colours other mods set in code (like the Integrated Demonlist search bar).

**The reminder note**
- My packs show a small "Get The Flame Engine on Geode" note in the top-right if you don't have the mod. Install it and the note goes away.

## How to use it

1. Install **Texture Loader** and this mod.
2. Apply one of my packs in *Settings > Graphics > Textures*.
3. Restart GD.

If you have **Menu Shaders**, **LoadingScreenTweaks**, **Custom Startups**, **Flameperfect Menu Icons** or **Flameperfect Loading BG** turned on, turn them off, they'll draw over this mod. You don't need **Happy Textures** for my packs anymore either.

## Settings

- **Play pack intros** - turn the startup intro on or off.

## Packs

Grab a pack or get your own custom one made: [ko-fi.com/flameperfect](https://ko-fi.com/flameperfect)

## Making your own pack for it

Put a `flameperfect.json` in your pack. Keys: `own-menu-icons`, `menu-shader`, `menu-hide-ground`, `all-menus`, `background-color`, `hide-nodes`, `loading-background`, `loading-color`, `loading-messages`, `loading-shader`, `startup-intro`, `startup-sound`, `level-complete-flyin`, `chest-glow`, `end-screen-icons`, `end-screen-bubbles`, `end-screen-color`, `pause-color`, `pause-bubbles`, `pause-hide-background`, `ui-brown-to`, `list-colors`, `node-colors`, `links`, `debug-layout`. There's a short explanation of each one at the top of `src/main.cpp`.

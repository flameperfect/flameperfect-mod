# Flameperfect

The mod that powers **Flameperfect texture packs**.

It only switches on when a Flameperfect pack is applied with **Texture Loader**. With any other pack (or none) it does nothing, so it's safe to leave installed.

## What it does

Each pack picks which of these it uses:

- **Your own icons on the title screen.** The icons running across the main menu use *your* equipped icons and colours.
- **Custom menu backgrounds.** The pack's background (still or animated) shows on the main menu and the other menus, including other mods' menus like BetterInfo.
- **No ground strip.** GD's ground is hidden on the menus so the background fills the whole screen.
- **Custom loading screen.** An animated scene, a background picture or a colour, plus the pack's own loading messages.
- **Social buttons.** The menu buttons open the pack maker's links, and the logo button opens their profile.
- **LEVEL COMPLETE fly-in.** The text flies into the end-screen popup.
- **Themed popups.** The end screen and pause menu can be tinted to the pack, with bubbles rising through them.
- **Icons on the end screen.** One custom icon attached to LEVEL COMPLETE, or two either side.
- **Themed chest glow.** Chest openings glow in the pack's colour instead of blue.
- **Colour fixes for other mods.** Things other mods colour themselves (like the Integrated Demonlist search bar) are recoloured to match the pack.
- **Removes the mod reminder.** Flameperfect packs show a "Get the Flameperfect mod" banner in the top-right corner when the mod is missing. Installing the mod hides it.

## How to use it

1. Install **Texture Loader** and this mod.
2. Apply a Flameperfect pack in *Settings → Graphics → Textures*.
3. Restart GD.

If you also have **Menu Shaders** or **LoadingScreenTweaks**, turn them off. They draw over this mod.

## Get packs

Get packs or order your own custom pack: [ko-fi.com/flameperfect](https://ko-fi.com/flameperfect)

## For pack makers

Add a `flameperfect.json` to your pack. Keys: `own-menu-icons`, `menu-shader`, `menu-hide-ground`, `all-menus`, `loading-background`, `loading-color`, `loading-messages`, `loading-shader`, `level-complete-flyin`, `chest-glow`, `end-screen-icons`, `end-screen-bubbles`, `end-screen-color`, `pause-color`, `pause-bubbles`, `node-colors`, `links`. See the comment at the top of `src/main.cpp`.

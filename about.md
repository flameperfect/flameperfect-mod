# The Flame Engine

**The mod that brings Flameperfect texture packs to life.**

A texture pack can only swap pictures. This mod adds everything a picture can't do: animated backgrounds, startup intros, themed lists and popups, and more. Each Flameperfect pack switches on the parts it uses.

It only does anything while a Flameperfect pack is applied with **Texture Loader**. With any other pack (or none) it stays out of the way, so it's safe to leave installed.

## What it does

**Startup and loading**
- **Startup intro.** The pack's own animated intro (with sound) plays when GD opens. Click to skip it, or turn it off in this mod's settings.
- **Animated loading screen.** A moving scene, a picture or a colour behind the loading bar, plus the pack's own loading messages.

**Menus**
- **Animated menu backgrounds.** The pack's background (still or animated) fills the main menu and the other menus, including other mods' menus like BetterInfo. The ground strip is hidden so it reaches the bottom.
- **Your own icons on the title screen.** The icons running across the main menu use *your* equipped icons and colours.
- **Social buttons.** The menu buttons open the pack maker's links, and the logo button opens their profile.

**Lists, profiles and popups**
- **Themed lists.** GD's brown lists, comments, messages, friends and profiles take on the pack's colours, outlines and text boxes included.
- **Themed pause menu and end screen.** Tinted to match the pack, with bubbles (or the pack's own effect) rising through them. The dark panel behind the pause menu can be removed.
- **LEVEL COMPLETE fly-in.** The text flies into the end-screen popup.
- **Themed chest glow.** Chest openings glow in the pack's colour.
- **Colour fixes for other mods.** Things other mods colour in code (like the Integrated Demonlist search bar) are recoloured to match.

**No more reminder**
- Flameperfect packs show a small "Get the Flameperfect mod" note in the top-right corner when this mod is missing. Installing it removes the note.

## How to use it

1. Install **Texture Loader** and this mod.
2. Apply a Flameperfect pack in *Settings → Graphics → Textures*.
3. Restart GD.

This one mod replaces the others Flameperfect packs used to need. If you have **Menu Shaders**, **LoadingScreenTweaks**, **Custom Startups**, **Flameperfect Menu Icons** or **Flameperfect Loading BG**, turn them off: they draw over this mod. **Happy Textures** isn't needed for Flameperfect packs any more.

## Settings

- **Play pack intros:** turn the startup intro on or off.

## Get packs

Get packs or order your own custom pack: [ko-fi.com/flameperfect](https://ko-fi.com/flameperfect)

## For pack makers

Add a `flameperfect.json` to your pack. Keys: `own-menu-icons`, `menu-shader`, `menu-hide-ground`, `all-menus`, `background-color`, `loading-background`, `loading-color`, `loading-messages`, `loading-shader`, `startup-intro`, `startup-sound`, `level-complete-flyin`, `chest-glow`, `end-screen-icons`, `end-screen-bubbles`, `end-screen-color`, `pause-color`, `pause-bubbles`, `pause-hide-background`, `ui-brown-to`, `list-colors`, `node-colors`, `links`, `debug-layout`. Each one is explained at the top of `src/main.cpp`.

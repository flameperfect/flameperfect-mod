# Flameperfect

The mod that powers **Flameperfect texture packs**.

On its own it does nothing - it only switches features on when a Flameperfect pack (with a `flameperfect.json` inside) is applied with **Texture Loader**. Depending on the pack, it adds:

- **Your own icons on the title screen** - the running menu icons use your equipped icons and colours
- **Custom menu backgrounds** - static or animated, on the main menu and the other menus
- **Custom loading screen** - background image or colour, plus the pack's own loading messages
- **Themed effects** - chest-opening glow in the pack's colours, LEVEL COMPLETE! flying into the end screen
- **Pack maker links** on the main-menu social buttons
- Colour fixes for other mods' menus so everything matches the pack

Get packs or order your own custom pack: [ko-fi.com/flameperfect](https://ko-fi.com/flameperfect)

### For pack makers
Add a `flameperfect.json` to your pack. Keys: `own-menu-icons`, `menu-shader`, `menu-hide-ground`, `all-menus`, `loading-background`, `loading-color`, `loading-messages`, `level-complete-flyin`, `chest-glow`, `node-colors`, `links`. See the comment at the top of `src/main.cpp`.

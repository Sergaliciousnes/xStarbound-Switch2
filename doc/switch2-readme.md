# xStarbound for Nintendo Switch 2 — Initial Release

## REQUIRED FIRST: copy packed.pak from your own Steam installation

**You must own a legitimate retail copy of Starbound on Steam. The retail game assets are NOT included in this package. The game will not run until you supply `packed.pak`.**

1. Install [Starbound through Steam](https://store.steampowered.com/app/211820/Starbound/) on your computer.
2. In your Steam Library, right-click **Starbound → Manage → Browse local files**.
3. Open the game's **assets** folder and locate **packed.pak**.
4. Extract this release package and copy its **switch** folder to the root of the SD card used by your Switch 2 homebrew setup.
5. Copy your retail **packed.pak** into **switch/xstarbound/assets/** on that card.

The final destination must be:

```
sdmc:/switch/xstarbound/assets/packed.pak
```

Keep the supplied `xsb-assets/xSBassets.pak` too: it contains required xStarbound support resources and is separate from the retail `assets/packed.pak` you provide. Do not rename one to replace the other.

Your SD card should look like this:

```
switch/
  xstarbound/
    xStarbound.nro
    xsbinit.config
    assets/
      packed.pak           <- copy this from your own Steam installation
    xsb-assets/
      xSBassets.pak         <- included; keep this file
    mods/                  <- optional; initially empty
    storage/               <- your saves, settings and game log
```

## Controls

Mappings below assume the default in-game keyboard bindings.

| Switch 2 control | Action |
| --- | --- |
| Left stick or D-pad | Move left/right/up; down to crouch |
| D-pad Down | Crouch and hold Left Shift; left-stick Down does not press Shift |
| Right stick | Move the cursor (12% radial deadzone; fine center response) |
| ZR | Left click / primary action; hold to keep pressed |
| ZL | Right click / secondary action; hold to keep pressed |
| A | Jump |
| B | Ability / tech action (keyboard F) |
| Y | Interact (keyboard E) |
| Hold Y longer than 2 seconds | Hold the Left Alt modifier until Y is released; the initial interaction still occurs |
| L / R | Previous / next hotbar slot |
| X | Escape / pause / back |
| Minus (-) | Open the system keyboard for chat or a slash command during gameplay |
| Touchscreen | Position the cursor; a quick, stationary tap acts as left click |
| Plus (+) | Unassigned |

The right stick uses a 12% circular deadzone and a gradual response near the center for small cursor adjustments. Full-stick speed is unchanged. Left-stick movement thresholds are unchanged.

Use the cursor with ZR or a quick touch tap to operate menus and inventory buttons. A tap must be released within roughly half a second; swiping or holding the screen moves the cursor without clicking.

For chat, press **Minus**, enter a message or command, and choose **Done** to send it. Cancel or an empty entry sends nothing. The chat closes afterward. The chat shortcut is available during gameplay. Rebinding the corresponding keyboard actions also changes what these controller mappings do.

## Prerequisites and launch

- **Nintendo Switch 2** with [KaiHEN](https://github.com/ouncebrew/kaihen/) installed and a working homebrew menu. This is a native Horizon homebrew NRO; it is not a Switch 1 or Linux executable.
- A runtime compatible with **ounce-sdk 20261113**. The build was prepared against libounce `db3f88a3`, OunceVK `791c34e` and OunceHEN `9451c44`. Use the matching console-side loader/services or a compatible release.
- Launch through your homebrew setup's **full-memory application/title mode**. Restricted applet mode is not suitable for this game.
- Enough free SD-card space for your retail assets, optional mods and growing saves.

After copying the files, launch **xStarbound** (`switch/xstarbound/xStarbound.nro`) from the homebrew menu. You do not need to install devkitPro, a PC SDK, Docker or a compiler to play this package.

The folder name and location must remain **switch/xstarbound**. The game uses that folder as its working directory.

## About this build and source

This is an **unofficial Switch 2 port** based on **xStarbound 4.7.0.2**, itself a fork of **Starbound 1.4.4**. It is not an official Nintendo or Chucklefish release.

- **Switch 2 port and packaging:** Sergalicious.
- **Original/upstream xStarbound source:** https://github.com/xStarbound/xStarbound
- **Original Starbound game:** https://store.steampowered.com/app/211820/Starbound/
- Credit for Starbound goes to **Chucklefish** and its original authors; credit for xStarbound goes to its contributors. Release packages include `CREDITS.txt` for additional project and font acknowledgments; the upstream README and dependency source notices also retain their credits.

Switch 2 port source: https://github.com/Sergaliciousnes/xStarbound-Switch2/tree/switch2 . [Build instructions](https://github.com/Sergaliciousnes/xStarbound-Switch2/blob/switch2/doc/switch2-build.md) explain installing KaiHEN's SDK into devkitPro before compiling. The upstream link identifies the original foundation. Release packages include build identifiers and SHA-256 checksums.

The game renders at **1920×1080** using Vulkan/OunceVK. It retains the tested performance improvements, including rpmalloc, asset/texture caches, bootstrap bytecode caching and RAM-backed temporary shipworld working storage. Persistent saves still go to the SD card.

Extended profiling, allocation tracing, exploration/server captures, startup asset tracing, diagnostic watchdogs and startup regression tests are **disabled**. The small **FPS / frame-time display remains enabled** for performance feedback. Normal startup, error and game logs are retained.

Single-player is the tested focus. Multiplayer and every mod combination have not been validated on this port. Steam platform integration and automatic Workshop downloading are disabled.

## Installing mods

Steam Workshop mods must finish downloading on the computer where you installed Starbound through Steam. This Switch 2 build does not download Workshop subscriptions itself.

### Copy Workshop mods manually

1. Open **Steam/steamapps/workshop/content/211820/** in the Steam library holding your Starbound Workshop downloads. The usual Windows location is `C:\Program Files (x86)\Steam\steamapps\workshop\content\211820`.
2. Each numbered folder is a **Workshop ID**. Inside it, locate the mod's **packed.pak**.
3. **Copy** that file directly into **switch/xstarbound/mods/** on the SD card, renaming the copy to **<workshop_id>.pak**.
4. Repeat for the other mods. Rename the copies so the many files called `packed.pak` do not overwrite one another; leave the Steam originals unchanged.

Example:

```
Steam/steamapps/workshop/content/211820/729558042/packed.pak
    -> SD:/switch/xstarbound/mods/729558042.pak
```

These Workshop packs belong directly in `mods/`, with distinct names. Keep the retail game's `packed.pak` in `assets/` as described at the top of this README.

### Copy all downloaded Workshop mods automatically (Windows)

1. Extract the whole release ZIP on the computer with Steam and Starbound installed, then double-click **Copy All Mods.cmd** beside this README.
2. The helper checks the default Steam installation for downloaded Starbound Workshop packs. If none are found, enter your **Steam folder or Steam library folder**, for example `D:\SteamLibrary`. You can also enter the `211820` folder directly. Enter **Q** to cancel.
3. Choose the output mods folder. Press **Enter** to use this extracted package's `switch/xstarbound/mods/`, or enter your SD card's mods path, for example `E:\switch\xstarbound\mods`.
4. The helper copies each available Workshop `packed.pak` as **<workshop_id>.pak** and verifies the copy. If you used the default PC output, copy the package's `switch` folder to the SD card afterward, or transfer the exported mod files over FTP after the game exits.

The helper uses built-in Windows PowerShell; no Python, compiler or administrator access is required. It copies every available packed Starbound Workshop mod in the selected library. It does not choose a compatible modpack or resolve dependencies. Numbered folders without a downloaded `packed.pak` are skipped. Run it again with another library path if you keep Workshop downloads across multiple libraries.

Re-running skips identical files and updates changed files with the same Workshop ID. Steam source files are never renamed or modified. Other destination mods are not deleted, so unsubscribe/remove unwanted mods manually. Close the game before updating its mod files.

Unpacked mods are also supported: copy the entire mod folder into `switch/xstarbound/mods/`, preserving its contents and directory structure. The helper handles packed Workshop downloads only.

This package includes **no third-party gameplay mods**. Test a new installation without mods first, then add your chosen mods. Large NPC populations, heavy scripts or complex objects can slow simulation even when the FPS counter looks healthy.

## Saves and updates

- Saves and generated settings live in **switch/xstarbound/storage/**. Back up that folder before testing with existing characters/worlds or changing a modpack.
- When updating, preserve your retail `assets/packed.pak`, `mods/` and `storage/` folders. The supplied storage directory is empty; this package does not contain somebody else's character, settings or universe.
- Use **Save & Quit**, then exit the game normally before copying saves or retrieving logs.

## Troubleshooting and feedback

If the game does not start, first check the exact folder layout above, especially both asset-pack locations, then confirm your console runtime matches the prerequisites. For mod-related problems, compare with a clean no-mod launch using a separate test save.

Please report the build identifier, handheld/docked mode, mod list, what you were doing, approximate FPS, and whether NPC/world movement was slow independently of FPS.

Useful logs:

- **switch/xstarbound/storage/xclient.log** — main game log.
- **/xstarbound-startup.log** — startup output at the SD root.
- **/xstarbound-stderr.log** — runtime/driver errors at the SD root.

Copy logs after the game exits and before launching it again, since a new launch can replace them. This build does not generate the extended performance-capture files used during development. If reporting a loader-cleanup issue, include `/ouncehen/nro-cleanup.txt` and check which program its `program:` line names.

# Building the Switch 2 port

## Prerequisites

- devkitPro devkitA64, Switch tools/portlibs and its MSYS2 environment on Windows (or an equivalent Unix devkitPro environment).
- **KaiHEN's Switch 2 SDK must be installed into devkitPro before building.** Grab ounce-devkit from https://github.com/ouncebrew/ounce-devkit . Standard devkitPro Switch 1 support alone is insufficient. This release was built against ounce-sdk **20261113**, with libounce db3f88a3, OunceVK 791c34e and OunceHEN 9451c44. The SDK is not vendored here; use the matching/compatible KaiHEN and hbloader on the console.
- Host CMake and Ninja, plus host `glslangValidator` on PATH. On Windows the Vulkan SDK's Bin directory is supported through VULKAN_SDK.
- Switch portlibs including SDL2, zlib, libpng, FreeType, Ogg, Vorbis and Opus and their dependencies. Install with devkitPro pacman. These are target libraries; host Windows/Linux packages are not substitutes.
- Git, with submodules initialized if using the full upstream desktop source. The Switch 2 client uses installed Opus and disables Tracy.

## Install KaiHEN's SDK into devkitPro

In the devkitPro MSYS2 shell on Windows, set DEVKITPRO and run the install.sh supplied in KaiHEN's SDK directory:

```sh
export DEVKITPRO=/c/devkitPro
cd /path/to/kaihen/sdk
sh install.sh
cat "$DEVKITPRO/ounce-sdk-version.txt"
```

Replace the example SDK path with the directory containing the install.sh from your KaiHEN source checkout or release package. The installer must place libounce, Switch 2 portlibs and build rules under the same DEVKITPRO directory used for the game build. On Unix, use your actual devkitPro path instead of /c/devkitPro. KaiHEN source: https://github.com/ouncebrew/kaihen/ .

Installing the SDK on the build PC and installing KaiHEN on the console are separate requirements. A player using the prebuilt release needs the compatible console runtime, but does not need devkitPro on their PC.

## Build

Run in devkitPro MSYS2 on Windows, not ordinary PowerShell or native Windows CMake:

```sh
git clone --recurse-submodules --branch switch2 https://github.com/Sergaliciousnes/xStarbound-Switch2.git
cd xStarbound-Switch2
export DEVKITPRO=/c/devkitPro
# If glslangValidator is not already on PATH, set your actual installation:
# export VULKAN_SDK=/c/VulkanSDK/<installed-version>
STAR_SWITCH2_RENDER_HEIGHT=1080 sh scripts/switch2/build.sh
```

On Unix, set DEVKITPRO to the actual installation directory, commonly /opt/devkitpro. The script derives the project path from its own location and does not require a copied CMake cache. It uses the project Switch2 toolchain to select libounce and Switch 2 Vulkan libraries and reads the SDK's Cortex-A78C flags.

The default build directory is `build-switch2-clean`. Output is `build-switch2-clean/source/client/xclient.nro`. The launcher title is xStarbound, the author line is Sergalicious, and the existing xStarbound desktop icon is embedded as a JPEG. The current source builds game and launcher version 4.7.0.3. The latest published Switch 2 release remains switch2-v1.0.1 (based on 4.7.0.2).

The script enables the Switch 2 rpmalloc integration, bootstrap bytecode cache and RAM-backed temporary shipworld storage. It disables Steam/Discord integration, desktop allocators, Qt tools, Tracy and heavyweight diagnostic captures. RAM-backed working files do not replace persistent SD saves. Set STAR_SWITCH2_RENDER_HEIGHT=720 to test the alternative render size; analog cursor speed scales with resolution.

Use JOBS=4 (the default); a Windows compiler process has crashed at higher parallelism in this environment. STAR_BUILD_DIR and STAR_BUILD_TYPE can override output directory and build type. RelWithDebInfo is the tested default. This retains ELF symbols for debugging; the generated NRO is the console executable.

## Runtime files

An NRO build alone does not include game assets. Use the same `sdmc:/switch/xstarbound` installation layout described in [the player README](switch2-readme.md):

- xStarbound.nro: rename/copy the generated xclient.nro.
- xsbinit.config: copy the generated build-directory install configuration.
- assets/packed.pak: from a legitimate retail Steam installation; not distributed here.
- xsb-assets/xSBassets.pak: support assets from this source tree, packed using a host-runnable xStarbound asset_packer.
- mods/ and storage/: optional user mods and persistent saves.

Cross-compilation sets PACKAGE_XSB_ASSETS=OFF because the target asset_packer cannot run on the build PC. The release package includes the prepacked support assets. To regenerate them, use a host xStarbound asset_packer with scripts/packing.config and the assets/xSBassets directory after CMake has generated its _metadata. Follow the upstream host build instructions for that tool.

The SDK is installed externally; no local modifications to the SDK are needed for the game's rpmalloc integration. It is implemented in the repository's source/extern/rpmalloc-switch2 and ownership-routing source files. The SDK allocator remains available for fallback allocations.

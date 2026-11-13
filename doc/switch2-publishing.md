# Publishing the Switch 2 fork

## Repository layout

Create an actual GitHub fork of https://github.com/xStarbound/xStarbound using https://github.com/xStarbound/xStarbound/fork . Suggested owner: Sergaliciousnes; repository name: xStarbound-Switch2. A custom fork name preserves the upstream relationship.

Keep the fork's main branch aligned with upstream, and publish the current port on a switch2 branch. Set switch2 as the fork's default branch once its source and documentation are ready. The existing local checkout already contains upstream history; do not initialize a new repository or upload the working folder through the website. Its current base is 551562cf6cf59be0a0e9caea41e846d15febbda7. Preserve that tested base for the first port release; handle newer upstream changes separately.

After the fork exists, configure the local repository (commands shown for review; not yet executed):

```sh
git remote rename origin upstream
git remote add origin https://github.com/Sergaliciousnes/xStarbound-Switch2.git
git switch -c switch2
```

## Prepare the source commit

Review the modified tracked files and new source files, then stage an explicit list of source, build configuration, documentation, tests, shaders, icon, and the pinned rpmalloc port. Preserve upstream credits and dependency notices. Do not use an unchecked `git add .`.

The local .gitignore excludes build-switch2 variants, dist-switch2 packages/logs, storage, mods, retail asset packs and root PDB files. Ignore rules do not remove previously tracked files, so inspect the staged list too.

Before committing:

```sh
git diff --cached --stat
git diff --cached --check
git diff --cached --name-only
```

Confirm no saves, test modpacks, retail packed.pak, SDK archives, credentials, local caches or generated binary/debug files are staged. SDK installation remains an external prerequisite rather than a repository payload.

Update the main README with the Switch 2 port status, legitimate Steam retail-assets requirement, controls, release instructions, upstream link and credits. Keep historical diagnostic notes distinct from current build instructions. The current doc/switch2-port.md includes early bring-up statements that need refreshing before it serves as the public landing page.

The checked-in scripts/switch2/build.sh is the portable entry point. Document SDK 20261113 / compatible kaiHEN 0.0.4, devkitPro portlibs, host CMake/Ninja and glslangValidator prerequisites. Set STAR_SWITCH2_RENDER_HEIGHT=1080 explicitly for the current tester resolution. Do not publish the local build-switch2 initial-cache files as build instructions: they contain machine-specific paths. Validate a fresh source checkout with submodules before calling the source release reproducible.

Then commit the reviewed source and push only the port branch:

```sh
git commit -m "Add native Switch 2 Vulkan port"
git push -u origin switch2
```

This pushes to the personal fork, not upstream. Do not force-push or merge upstream updates into the initial tested port as part of publication.

## Tester download

Create a GitHub Release from the exact published source commit. Attach the existing cleaned tester ZIP and its .sha256 file as release assets rather than committing them to source history. The ZIP contains the NRO, support assets, README, credits/licenses and Workshop-copy helper; it excludes retail assets, mods and saves. Keep the legitimate retail packed.pak requirement first in the release instructions.

Only describe performance and compatibility already confirmed on hardware. Identify optional profiling as disabled in the shared build. If publication preparation changes runtime source or build options, rebuild and test that revision before tagging it.

References:
- https://docs.github.com/en/pull-requests/how-tos/work-with-forks/fork-a-repo
- https://docs.github.com/en/pull-requests/how-tos/work-with-forks/configuring-a-remote-repository-for-a-fork

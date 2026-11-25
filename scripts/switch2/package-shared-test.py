#!/usr/bin/env python3
"""Package the opt-in shared runtime candidate without touching a release."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile


def host_path(value):
    # A native Windows Python may be invoked from the MSYS shell.
    if os.name == 'nt' and re.match(r'^/[a-zA-Z]/', value):
        value = value[1] + ':' + value[2:]
    return Path(value).resolve()


parser = argparse.ArgumentParser(description=__doc__)
root = Path(__file__).resolve().parents[2]
parser.add_argument('--sdk-root', required=True, type=host_path)
parser.add_argument('--build-dir', type=host_path, default=root / 'build-switch2-shared')
parser.add_argument('--output-dir', type=host_path, default=root / 'dist-switch2')
parser.add_argument('--base-release', type=host_path, help='Existing release ZIP from which to retain credits and dependency notices')
args = parser.parse_args()
cache = (args.build_dir / 'CMakeCache.txt').read_text()
for flag in ('OUNCE_SHARED:BOOL=ON', 'STAR_SWITCH2_GAME_RPMALLOC:BOOL=ON',
             'BUILD_SHARED_LIBS:BOOL=OFF'):
    if flag not in cache:
        raise SystemExit('Unexpected build configuration: missing ' + flag)

readelf = args.sdk_root / 'devkitA64/bin' / ('aarch64-none-elf-readelf.exe' if os.name == 'nt' else 'aarch64-none-elf-readelf')


def elf_info(path, option):
    return subprocess.check_output([str(readelf), option, '--wide', str(path)], text=True)

game = 'switch/xstarbound/'
check = 'switch/xstarbound-shared-check/'
files = {
    game + 'xStarbound.nro': args.build_dir / 'source/client/xclient.nro',
    check + 'xallocator-routing-test.nro': args.build_dir / 'source/application/xallocator-routing-test.nro',
    check + 'libxallocator-boundary.so.1': args.build_dir / 'source/application/nro/libxallocator-boundary.so.1',
}
for folder in (game, check):
    for name in ('libounce.so.1', 'libstdc++.so.6'):
        files[folder + name] = args.sdk_root / 'libounce/lib/shared/nro' / name
files[game + 'libouncevk.so.1'] = args.sdk_root / 'portlibs/switch2/lib/shared/nro/libouncevk.so.1'
# SDL2's static portlib also links EGL, even when our renderer uses Vulkan.
files[game + 'libouncevk-gl.so.1'] = args.sdk_root / 'portlibs/switch2/lib/shared/nro/libouncevk-gl.so.1'

# Validate the complete dependency closure before producing a runnable package.
elfs = {
    game + 'xStarbound.nro': args.build_dir / 'source/client/xclient.elf',
    check + 'xallocator-routing-test.nro': args.build_dir / 'source/application/xallocator-routing-test.elf',
    check + 'libxallocator-boundary.so.1': args.build_dir / 'source/application/libxallocator-boundary.so.1',
}
for name, path in files.items():
    if name not in elfs:
        elfs[name] = path.parent.parent / path.name
dependencies = {}
for name, elf in elfs.items():
    needed = re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', elf_info(elf, '-d'))
    dependencies[name] = needed
    folder = name.rsplit('/', 1)[0] + '/'
    for dependency in needed:
        if folder + dependency not in files:
            raise SystemExit('Missing package dependency: ' + folder + dependency)
for folder, name in ((game, 'xStarbound.nro'), (check, 'xallocator-routing-test.nro')):
    symbols = elf_info(elfs[folder + name], '--dyn-syms')
    for operator in ('_Znwm', '_Znam', '_ZdlPv', '_ZdaPv', '_ZdlPvm', '_ZdaPvm'):
        if not re.search(r'FUNC\s+GLOBAL\s+DEFAULT\s+(?!UND)\S+\s+' + operator + r'\s*$', symbols, re.M):
            raise SystemExit('Missing exported game allocator: ' + name + ':' + operator)
    for c_allocator in ('malloc', 'free', 'realloc'):
        if not re.search(r'\bUND\s+' + c_allocator + r'\s*$', symbols, re.M):
            raise SystemExit('Expected shared SDK allocator import: ' + name + ':' + c_allocator)

payload = {}
for name, path in files.items():
    data = path.read_bytes()
    if data[16:20] != b'NRO0':
        raise SystemExit('Expected NRO-format runtime file: ' + str(path))
    payload[name] = data

for label, directory in (
    ('libounce', args.sdk_root / 'libounce/licenses'),
    ('ouncevk', args.sdk_root / 'portlibs/switch2/share/licenses/ouncevk'),
):
    for path in directory.rglob('*'):
        if path.is_file():
            payload['licenses/' + label + '/' + path.relative_to(directory).as_posix()] = path.read_bytes()
if args.base_release:
    with zipfile.ZipFile(args.base_release) as z:
        for name in z.namelist():
            if not name.endswith('/') and (name.startswith('licenses/') or name == 'CREDITS.txt'):
                payload[name] = z.read(name)

payload['README.txt'] = b'''xStarbound - Experimental shared SDK runtime

Requires an existing working xStarbound Switch 2 installation with legally
obtained Steam assets, and KaiHEN 20261123 or newer with a compatible loader.
This is an experimental overlay, not a new published release.

1. Back up your existing switch/xstarbound/xStarbound.nro and storage folder.
2. Copy this ZIP's switch folder to the SD root, replacing only the game NRO.
   Leave configuration, assets, mods and saves in place. The supplied shared
   libraries belong beside xStarbound.nro; do not copy them to kaihen/lib.
3. Launch "Game allocator routing test" from xstarbound-shared-check first.
   It returns to the menu when finished. Check /xstarbound-shared-routing.log:
   shared-boundary-tests=verified, routing-tests=verified, and
   late-static-destructor=verified should all appear, with no FAILED entries.
   If the diagnostic fails or cannot launch, stop and send its logs.
4. Launch xStarbound. Test menu, character loading, ship, planet, outpost,
   mods, Joy-Con mouse, multiplayer, save/quit, and a second launch.
   Compare FPS, hitches and loading against the static build in the same area.
5. Send xclient.log, startup/stderr logs if present, the routing diagnostic
   log, and /ouncehen/nro-cleanup.txt after quitting both tests normally.

To roll back, restore the old xStarbound.nro. The adjacent .so files can remain
unused or be removed. This package does not alter global runtime libraries.

Engine, Lua and rpmalloc remain linked into the game. libounce, libstdc++ and
the Vulkan driver are loaded from separate libraries. SDL2 also requires the
OpenGL/EGL companion library; the game still renders through Vulkan. Heavy profiling stays
disabled. Successful compilation is not console validation.
'''
hashes = {name: hashlib.sha256(data).hexdigest() for name, data in payload.items()}
payload['BUILD-INFO.json'] = (json.dumps({
    'build_kind': 'experimental shared SDK runtime',
    'hardware_validation': 'pending',
    'shared_runtime': ['libounce.so.1', 'libstdc++.so.6', 'libouncevk.so.1', 'libouncevk-gl.so.1'],
    'engine_allocator': 'ownership-routed rpmalloc',
    'elf_dependencies': dependencies,
    'sha256': hashes,
}, indent=2) + '\n').encode()
payload['SHA256SUMS.txt'] = ''.join(
    hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
    for name, data in sorted(payload.items())
).encode()
args.output_dir.mkdir(parents=True, exist_ok=True)
archive = args.output_dir / 'xStarbound-Switch2-Shared-Runtime-Test.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for name, data in payload.items():
        z.writestr(name, data)
with zipfile.ZipFile(archive) as z:
    if z.testzip() is not None:
        raise SystemExit('ZIP validation failed')
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
Path(str(archive) + '.sha256').write_text(digest + '  ' + archive.name + '\n')
print(json.dumps({'archive': str(archive), 'bytes': archive.stat().st_size,
                  'sha256': digest, 'console_validation': 'pending'}, indent=2))

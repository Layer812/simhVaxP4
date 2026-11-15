#!/usr/bin/env python3
"""R80 FIX28 source-to-native-build entry. Fail closed, never flash.

The public Git branch currently has only the preserved R80 overlays and staging
patches. A complete project, components and exact compiled SIMH port must be
added under full_project/ before this command may build.  Never fabricate them
from the linked PDP-7 simhP4 or a similarly named VAX project.
"""
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
OVERLAY = HERE / 'source'
LEGACY = HERE / 'legacy_staging'
FULL = HERE / 'full_project'
BASE_FILE = LEGACY / 'BASE_SOURCE_SHA256.json'
PKG_FILE = LEGACY / 'PACKAGE_SHA256.json'
APPNAME = 'simhp4_vax630_visual_media_probe_v12'

# Necessary, but not sufficient: a complete independent ESP-IDF checkout also
# needs all source dependencies declared by the real CMakeLists and sdkconfig.
MUST_EXIST = [
    'project/CMakeLists.txt',
    'project/main/CMakeLists.txt',
    'project/main/simhp4_vax630_io_esp_r14e.c',
    'project/main/simhp4_vax_terminal.h',
    'project/main/simhp4_vax_keyboard.h',
    'project/main/simhp4_wifi_overlay.h',
    'project/main/simhp4_wifi_backend.h',
    'project/main/simh_wifi_boot_gate.h',
    'project/main/simhp4_wifi_badge.h',
    'project/main/simhp4_sd.h',
    'third_party/simh_vax630_87eb/scp.c',
    'third_party/simh_vax630_87eb/VAX/vax_cpu1.c',
    'components',
]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def fail(message, code=2):
    print('STOP: ' + message)
    print('NO FLASH / NO COM9 / NO DSK WRITES')
    return code


def publish_build_log(log: Path, *, failed: bool) -> None:
    """Keep original build evidence outside the disposable staging directory.

    Do not suppress the first compiler error. This function performs no serial
    access, image writes, flashing, disk-image access or git publication.
    """
    try:
        report_dir = Path(os.environ.get('SIMHP4_BUILD_REPORT_DIR', r'F:\build\tmp'))
        report_dir.mkdir(parents=True, exist_ok=True)
        target = report_dir / 'SIMHP4_R80_FIX28_LAST_BUILD.log'
        shutil.copyfile(log, target)
        print('BUILD LOG SAVED: ' + str(target), flush=True)
    except OSError as exc:
        print('BUILD LOG COPY FAILED: ' + str(exc), flush=True)
        print('BUILD LOG STILL AT: ' + str(log), flush=True)
    if not failed:
        return
    try:
        lines = log.read_text('utf-8', errors='replace').splitlines()
        keys = re.compile(r'error:|fatal error:|CMake Error|undefined reference|FAILED:|ninja: error:|No such file or directory|Traceback|build stopped', re.I)
        hits = [(i, line) for i, line in enumerate(lines) if keys.search(line)]
        print('FIRST BUILD FAILURE DETAILS (from real ESP-IDF log):', flush=True)
        if hits:
            for i, _ in hits[:8]:
                lo, hi = max(0, i - 2), min(len(lines), i + 4)
                for line in lines[lo:hi]:
                    print(line[:1200], flush=True)
                print('---', flush=True)
        else:
            print('(No common compiler-error pattern; showing log tail.)', flush=True)
            for line in lines[-80:]:
                print(line[:1200], flush=True)
    except OSError as exc:
        print('LOG READ FAILED: ' + str(exc), flush=True)


def main():
    if not BASE_FILE.is_file() or not PKG_FILE.is_file():
        return fail('Missing pinned FIX28 source manifests')
    expected = json.loads(PKG_FILE.read_text(encoding='utf-8'))
    for rel, digest in expected.items():
        f = (OVERLAY / rel[7:]) if rel.startswith('source/') else (LEGACY / rel)
        if not f.is_file() or sha(f) != digest:
            return fail('Original FIX28 package integrity mismatch: ' + rel)
    missing = [rel for rel in MUST_EXIST if not (FULL / rel).exists()]
    if missing or not (FULL / 'project' / 'main').is_dir():
        print('INCOMPLETE_GIT_SOURCE: required custom firmware source is not yet published:')
        for name in missing:
            print('  MISSING ' + str(FULL / name))
        return fail('Source tree incomplete; no build attempted', 4)
    source = json.loads(BASE_FILE.read_text(encoding='utf-8'))
    for filename, base_sha in source.items():
        baseline = FULL / 'project' / 'main' / filename
        if not baseline.is_file():
            return fail('Missing complete project source: ' + str(baseline))
        # Require a known identity; either original FIX4 baseline or already
        # identical FIX28 source, never overwrite a different user revision.
        if sha(baseline) not in (base_sha, sha(OVERLAY / filename)):
            return fail('Unrecognized project source revision: ' + filename)
    idf_root = Path(os.environ.get('IDF_PATH', r'G:\esp-idf-5.5.4'))
    idf_py = idf_root / 'tools' / 'idf.py'
    if not idf_py.is_file():
        return fail('ESP-IDF 5.5.4 Python build tool not found: ' + str(idf_py))
    # The IDF component manager default cache path on Windows is excessively
    # long for esp_hosted 3.0.9's nested examples/ tree. On the owner's machine
    # the unpack failed with FileNotFoundError before CMake configured the build.
    # IDF_COMPONENT_CACHE_PATH is the documented environment override; isolate
    # this R80 cache under a short path on the existing F: development volume.
    # No Windows registry changes, system cache cleanup, or library replacement.
    component_cache = Path(r'F:\build\tmp\r80cm')
    if sys.platform == 'win32':
        if not Path('F:/build/tmp').is_dir():
            return fail('Required F:/build/tmp source development volume is missing')
        component_cache.mkdir(parents=True, exist_ok=True)
        print('ESP-IDF component cache: ' + str(component_cache), flush=True)
    build_env = os.environ.copy()
    if sys.platform == 'win32':
        build_env['IDF_COMPONENT_CACHE_PATH'] = str(component_cache)
    stage = Path(tempfile.mkdtemp(prefix='simhvaxp4_fix28_build_'))
    print('Build staging (isolated): ' + str(stage))
    try:
        for part in ('project', 'components', 'third_party'):
            original = FULL / part
            if not original.is_dir():
                return fail('Missing source sub-tree: ' + str(original))
            def ign(directory, names):
                return {n for n in names if n in ('build', '.git', '__pycache__') or n.endswith('.dsk') or n.endswith('.DSK')}
            shutil.copytree(original, stage / part, ignore=ign)
        for filename in source:
            shutil.copy2(OVERLAY / filename, stage / 'project' / 'main' / filename)
        # Import precisely the original FIX28 patch functions.  Module import
        # does not execute its old CLI (protected by __main__).  No flash API
        # or boot-slot assumptions are invoked from this workflow.
        sys.path.insert(0, str(LEGACY))
        spec = importlib.util.spec_from_file_location('simhp4_fix28_legacy', LEGACY / 'RUN_RELEASE_UI.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for patch in (module.patch_actual_qba, module.patch_qba_file_metadata,
                      module.patch_scb_fault_provenance):
            patch(stage)
        run = [sys.executable, str(idf_py), '-B', str(stage / 'project' / 'build'), 'build']
        log = stage / 'BUILD.log'
        print('ESP-IDF build only: ' + ' '.join(run))
        with log.open('w', encoding='utf-8') as out:
            result = subprocess.run(run, cwd=stage / 'project', env=build_env,
                                    stdout=out, stderr=subprocess.STDOUT, check=False)
        publish_build_log(log, failed=(result.returncode != 0))
        if result.returncode != 0:
            print('BUILD FAILED; log: ' + str(log))
            return fail('Native compilation returned ' + str(result.returncode))
        elf = stage / 'project' / 'build' / (APPNAME + '.elf')
        app = stage / 'project' / 'build' / (APPNAME + '.bin')
        if not elf.is_file() or not app.is_file():
            return fail('Build returned success but matching APP/ELF are missing')
        bin_bytes = app.read_bytes()
        if bytes.fromhex(sha(elf)) not in bin_bytes[:512]:
            return fail('APP header does not embed the exact ELF SHA-256')
        for marker in (b'SIMHP4_FIX28_SCB_CAPTURE_LINKED',
                       b'SIMHP4_FIX24_QBMAP_COMPILED_GUARD',
                       b'SIMHP4_FIX24_QBA_FLUSH_GUARD'):
            if marker not in bin_bytes:
                return fail('Linked APP missing regression marker: ' + marker.decode())
        result_zip = stage / 'R80_FIX28_BUILD_ONLY_RESULT.zip'
        with zipfile.ZipFile(result_zip, 'w', zipfile.ZIP_DEFLATED) as z:
            for item in (log, elf, app):
                z.write(item, item.name)
        print('SOURCE + BUILD PASS (hardware not yet tested). NO FLASH.')
        print('RESULT ZIP: ' + str(result_zip))
        return 0
    except Exception as e:
        return fail('Source preparation failure: ' + repr(e))

if __name__ == '__main__':
    sys.exit(main())

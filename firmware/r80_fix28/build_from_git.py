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


def patch_unresolved_r79_hosted_autoinit(stage):
    """Remove only the stale R79 glue call from the isolated build copy.

    ESP-Hosted 3.0.9 already schedules its own 'eh_auto_init' worker from the
    SDIO INIT event once TX becomes active. The pinned sdkconfig enables both
    HOST_AUTO_FEAT_INIT and WIFI_AUTO_INIT. Running the feature walker again
    from our Wi-Fi worker would risk duplicate/concurrent RPC registrations.
    NEVER manufacture a dummy success return value.
    """
    root = stage / 'project'
    f = root / 'main' / 'simhp4_wifi_backend.cpp'
    config = root / 'sdkconfig'
    component = root / 'main' / 'idf_component.yml'
    if not f.is_file() or not config.is_file() or not component.is_file():
        raise RuntimeError('FIX28 Wi-Fi auto-init gate missing pinned sources')
    cfg = config.read_text(encoding='utf-8')
    for required in ('CONFIG_ESP_HOSTED_HOST_AUTO_FEAT_INIT=y',
                     'CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_AUTO_INIT=y'):
        if required not in cfg.splitlines():
            raise RuntimeError('FIX28 requires vendor Wi-Fi auto-init: ' + required)
    if 'version: "==3.0.9"' not in component.read_text(encoding='utf-8'):
        raise RuntimeError('FIX28 ESP-Hosted version changed; review auto-init semantics')
    old = f.read_text(encoding='utf-8')
    declaration = 'extern "C" int simhp4_r79_run_auto_init_on_wifi_worker(void);'
    call = '''    /* P4: feature auto-init in existing Wi-Fi worker, never on SDIO RX. */
    const int auto_rc=simhp4_r79_run_auto_init_on_wifi_worker();
    if (auto_rc != 0) {
        ESP_LOGE(LOG,"SIMHP4_R79_AUTOINIT_WORKER rc=%d",auto_rc);
        update_state("HOSTED_AUTO_INIT",SIMHP4_WIFI_DISCONNECTED);
        return false;
    }
'''
    expected_hosted = ('esp_hosted_init()', 'esp_hosted_connect_to_slave()',
                       'esp_netif_create_default_wifi_sta()', 'esp_wifi_init(&config)')
    if old.count(declaration) != 1 or old.count(call) != 1:
        raise RuntimeError('FIX28 Wi-Fi auto-init glue anchor changed; no build')
    if any(old.count(token) != 1 for token in expected_hosted):
        raise RuntimeError('FIX28 Hosted bring-up source changed; no build')
    header = '''/* FIX28: ESP-Hosted 3.0.9 schedules feature auto-init on its own worker,
 * never inline on SDIO RX. Require that exact build configuration.
 * See host/mcu/eh_host_mcu_transport/src/eh_host_mcu_transport_init.c. */
#include "sdkconfig.h"
#if !defined(CONFIG_ESP_HOSTED_HOST_AUTO_FEAT_INIT) || !CONFIG_ESP_HOSTED_HOST_AUTO_FEAT_INIT || !defined(CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_AUTO_INIT) || !CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_AUTO_INIT
#error "FIX28: ESP-Hosted Wi-Fi auto-init must remain enabled"
#endif
'''
    replacement = '''    /* FIX28: no second feature-init walker. ESP-Hosted starts eh_auto_init
     * after C6 transport INIT and TX_ACTIVE; do not run another walker here.
     * Host init, netif/Wi-Fi setup and worker scan/Join remain unchanged. */
'''
    updated = old.replace(declaration, header, 1).replace(call, replacement, 1)
    if 'simhp4_r79_run_auto_init_on_wifi_worker' in updated:
        raise RuntimeError('FIX28 stale undefined R79 symbol remains')
    for required in expected_hosted:
        if updated.count(required) != 1:
            raise RuntimeError('FIX28 Wi-Fi Hosted bring-up was modified: ' + required)
    f.write_text(updated, encoding='utf-8')
    print('PASS FIX28: stale undefined R79 Wi-Fi init glue removed in isolated build only; vendor auto-init retained', flush=True)


def main():
    if not BASE_FILE.is_file() or not PKG_FILE.is_file():
        return fail('Missing pinned FIX28 source manifests')
    # Verify every original Git source against its import SHA-256 and require
    # the exact donor .inl that had been silently excluded in the first import.
    # Fail BEFORE copying 228MB of source or invoking CMake/ESP-IDF.
    gate = HERE / 'source_completion_gate.py'
    if not gate.is_file():
        return fail('Git source completion auditor missing; no native build', 4)
    print('Preflight: validate original 1,777 Git source hashes and recovered include', flush=True)
    checked = subprocess.run([sys.executable, '-B', str(gate), '--verify'],
                             cwd=HERE, check=False)
    if checked.returncode != 0:
        return fail('Git source audit did not pass; no native compilation', 4)
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
    # Component Manager first unpacks hosted in the cache, then copies its
    # entire examples/ tree into project/managed_components.  The first FIX28
    # retry shortened the cache, but the default %TEMP% project destination
    # still measured 265 chars for a real esp_hosted 3.0.9 file and failed
    # with WinError 3. Shorten the *destination* as well (no global settings).
    if sys.platform == 'win32':
        stage_parent = Path(r'F:\build\tmp')
        if not stage_parent.is_dir():
            return fail('Short F: build-stage parent is missing: ' + str(stage_parent))
        stage = Path(tempfile.mkdtemp(prefix='b28_', dir=str(stage_parent)))
        # Check the exact previous failure path before invoking native build.
        # Leave margin below Win32 MAX_PATH for intermediate extraction.
        previous_failed_subpath = Path(
            'project/managed_components/espressif__esp_hosted/'
            'examples/power_save/host+cp/'
            'network_split__host_deep_sleep_cp_light_sleep/'
            'cp/components/esp_hosted_examples_common/include/'
            'esp_hosted_examples_common.h'
        )
        previous_failed_target = stage / previous_failed_subpath
        print('ESP Hosted nested target length: ' + str(len(str(previous_failed_target))), flush=True)
        if len(str(previous_failed_target)) >= 240:
            return fail('ESP Hosted managed-components target still too long')
    else:
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
        patch_unresolved_r79_hosted_autoinit(stage)
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

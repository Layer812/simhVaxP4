"""Host-only regression test for R80 FIX28 Wi-Fi auto-init link gate.

Run with Python; does not compile ESP-IDF, access serial devices or flash.
"""
from __future__ import annotations

import importlib.util
import shutil
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE / 'build_from_git.py'
ORIGINAL = HERE / 'full_project' / 'project'
SPEC = importlib.util.spec_from_file_location('simhp4_build_only', BUILD)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

DECL = 'extern "C" int simhp4_r79_run_auto_init_on_wifi_worker(void);'
OLD = '''    /* P4: feature auto-init in existing Wi-Fi worker, never on SDIO RX. */
    const int auto_rc=simhp4_r79_run_auto_init_on_wifi_worker();
    if (auto_rc != 0) {
        ESP_LOGE(LOG,"SIMHP4_R79_AUTOINIT_WORKER rc=%d",auto_rc);
        update_state("HOSTED_AUTO_INIT",SIMHP4_WIFI_DISCONNECTED);
        return false;
    }
'''
PRESERVE = (
    'esp_hosted_init()', 'esp_hosted_connect_to_slave()',
    'esp_netif_create_default_wifi_sta()', 'esp_wifi_init(&config)',
    'esp_wifi_set_storage(WIFI_STORAGE_RAM)', 'esp_wifi_set_mode(WIFI_MODE_STA)',
    'esp_wifi_start()', 'esp_wifi_scan_start(&cfg,true)',
    'esp_wifi_set_config(WIFI_IF_STA,&cfg)',
)


def fixture(folder: Path) -> Path:
    p = folder / 'project'
    target = p / 'main'
    target.mkdir(parents=True)
    for rel in ('main/simhp4_wifi_backend.cpp', 'main/idf_component.yml',
                'sdkconfig'):
        dest = p / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ORIGINAL / rel, dest)
    return folder


def must_stop(folder: Path, expected: str) -> None:
    try:
        MODULE.patch_unresolved_r79_hosted_autoinit(folder)
    except RuntimeError as exc:
        assert expected in str(exc), str(exc)
        return
    raise AssertionError('unsafe source accepted: ' + expected)


with tempfile.TemporaryDirectory(prefix='simhp4_wifi_link_gate_') as td:
    parent = Path(td)
    p = fixture(parent / 'ok')
    base = (ORIGINAL / 'main/simhp4_wifi_backend.cpp').read_text('utf-8')
    assert base.count(DECL) == 1 and base.count(OLD) == 1
    assert all(base.count(s) >= 1 for s in PRESERVE)
    MODULE.patch_unresolved_r79_hosted_autoinit(p)
    updated = (p / 'project/main/simhp4_wifi_backend.cpp').read_text('utf-8')
    assert 'simhp4_r79_run_auto_init_on_wifi_worker' not in updated
    assert 'FIX28: ESP-Hosted Wi-Fi auto-init must remain enabled' in updated
    assert all(updated.count(s) == base.count(s) for s in PRESERVE)
    assert (ORIGINAL / 'main/simhp4_wifi_backend.cpp').read_text('utf-8') == base
    must_stop(p, 'anchor changed')
    print('PASS: exact isolated source patch, existing Hosted/Wi-Fi calls preserved, no double-patch')

    p = fixture(parent / 'disabled')
    cfg = p / 'project/sdkconfig'
    cfg.write_text(cfg.read_text('utf-8').replace(
        'CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_AUTO_INIT=y',
        '# CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_AUTO_INIT is not set'), 'utf-8')
    must_stop(p, 'requires vendor Wi-Fi auto-init')
    print('PASS: vendor auto-init disabled => safe stop')

    p = fixture(parent / 'version')
    yml = p / 'project/main/idf_component.yml'
    yml.write_text(yml.read_text('utf-8').replace(
        'version: "==3.0.9"', 'version: "==3.1.0"'), 'utf-8')
    must_stop(p, 'version changed')
    print('PASS: unreviewed ESP-Hosted version => safe stop')

    p = fixture(parent / 'drift')
    file = p / 'project/main/simhp4_wifi_backend.cpp'
    file.write_text(file.read_text('utf-8').replace(DECL, ''), 'utf-8')
    must_stop(p, 'anchor changed')
    print('PASS: changed original Wi-Fi source => safe stop')

print('PASS: FIX28 Wi-Fi link correction is host-static only; NO FLASH')

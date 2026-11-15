"""Host-only safety tests. No Git network, no COM9, no DSK writes."""
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT = Path(__file__).with_name('import_full_source.py')


def fake(root: Path, secret=False):
    unit = root / 'firmware' / 'r80_fix28'
    donor = root / 'donor'
    (unit / 'legacy_staging').mkdir(parents=True)
    (donor / 'project' / 'main').mkdir(parents=True)
    (donor / 'components').mkdir()
    (donor / 'third_party' / 'simh_vax630_87eb' / 'VAX').mkdir(parents=True)
    (donor / 'project' / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\n')
    (donor / 'project' / 'main' / 'CMakeLists.txt').write_text('idf_component_register(SRCS "v13_main.cpp")\n')
    (donor / 'project' / 'main' / 'simhp4_vax630_io_esp_r14e.c').write_text('int port(void){return 1;}\n')
    (donor / 'third_party' / 'simh_vax630_87eb' / 'scp.c').write_text('int scp(void){return 1;}\n')
    (donor / 'third_party' / 'simh_vax630_87eb' / 'VAX' / 'vax_cpu1.c').write_text('int vax(void){return 1;}\n')
    files = {}
    for i in range(7):
        name = f'file{i}.c'
        blob = (f'int file{i}(void){{return {i};}}\n').encode()
        (donor / 'project' / 'main' / name).write_bytes(blob)
        files[name] = hashlib.sha256(blob).hexdigest()
    (unit / 'legacy_staging' / 'BASE_SOURCE_SHA256.json').write_text(json.dumps(files))
    (donor / 'project' / 'main' / 'QUASWORK.DSK').write_bytes(b'PRIVATE_DSK')
    (donor / 'project' / 'main' / 'firmware.bin').write_bytes(b'PRIVATE_BIN')
    (donor / 'components' / 'example.c').write_text('int example(void) {return 7;}\n')
    if secret:
        (donor / 'components' / 'secret.h').write_text('#define WIFI_PASSWORD "my-real-password"\n')
    shutil.copy2(SCRIPT, unit / 'import_full_source.py')
    return unit, donor


def invoke(unit: Path, donor: Path, *args):
    return subprocess.run([sys.executable, '-B', str(unit / 'import_full_source.py'),
                           '--donor', str(donor), *args], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)


def tests():
    with tempfile.TemporaryDirectory(prefix='simhp4_test_') as d:
        unit, donor = fake(Path(d))
        dry = invoke(unit, donor, '--dry-run')
        assert dry.returncode == 0, dry.stdout
        assert not (unit / 'full_project').exists(), 'dry run modified checkout'
        bad = donor / 'project' / 'main' / 'file4.c'
        bad.write_text('wrong\n')
        wrong = invoke(unit, donor, '--dry-run')
        assert wrong.returncode == 4, wrong.stdout
        bad.write_text('int file4(void){return 4;}\n')
        write = invoke(unit, donor)
        assert write.returncode == 0, write.stdout
        project = unit / 'full_project'
        assert (project / 'project' / 'main' / 'file4.c').exists()
        assert not (project / 'project' / 'main' / 'QUASWORK.DSK').exists()
        assert not (project / 'project' / 'main' / 'firmware.bin').exists()
        manifest = json.loads((project / 'SOURCE_IMPORT_MANIFEST.json').read_text())
        assert len(manifest['donor_source_sha256']) == 7
        assert any(x['path'].endswith('QUASWORK.DSK') for x in manifest['excluded'])
        assert invoke(unit, donor).returncode == 3, 'existing full_project overwritten'
        print('PASS: baseline match, SHA mismatch fail-closed, dry-run, one-time copy, DSK/BIN exclusion, no-overwrite')
    with tempfile.TemporaryDirectory(prefix='simhp4_test_secrets_') as d:
        unit, donor = fake(Path(d), secret=True)
        x = invoke(unit, donor)
        assert x.returncode == 3 and 'possible password literal' in x.stdout, x.stdout
        assert not (unit / 'full_project').exists()
        print('PASS: credential signature blocks public source import')

if __name__ == '__main__':
    tests()

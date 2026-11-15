#!/usr/bin/env python3
"""simhVaxP4 FIX28: exact missing source recovery and 1,777-file Git audit.

--publish: one-time recovery from SHA-pinned historical donor; development Git only.
--verify: verify committed source and FIX28 build inputs before native compilation.
No flash/COM port/disk-image reads or writes. No version substitution.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

HERE = Path(__file__).resolve().parent
FULL = HERE / 'full_project'
ORIGINAL = FULL / 'SOURCE_IMPORT_MANIFEST.json'
REPAIRED = FULL / 'SOURCE_COMPLETION_MANIFEST.json'
BASE_SHA = HERE / 'legacy_staging' / 'BASE_SOURCE_SHA256.json'
BRANCH = 'work/r80-regression-handoff-20261115'
REPO_URLS = {
    'https://github.com/Layer812/simhVaxP4.git',
    'https://github.com/Layer812/simhVaxP4',
    'git@github.com:Layer812/simhVaxP4.git',
}
REQUIRED = 'components/M5Unified/src/utility/imu/BMI270_config.inl'
SOURCE_SUFFIXES = {'.inl', '.ipp', '.tpp', '.inc', '.h', '.hpp', '.hxx',
                   '.c', '.cc', '.cpp', '.cxx', '.s', '.asm', '.cmake'}
MUST_EXIST = (
    'project/CMakeLists.txt', 'project/main/CMakeLists.txt',
    'project/main/simhp4_vax630_io_esp_r14e.c',
    'third_party/simh_vax630_87eb/scp.c',
    'third_party/simh_vax630_87eb/VAX/vax_cpu1.c',
    'components/M5Unified/src/utility/imu/BMI270_Class.cpp',
    'components/M5GFX/CMakeLists.txt',
)
BAD_SECRETS = [
    re.compile(rb'-----BEGIN [A-Z ]*PRIVATE KEY-----'),
    re.compile(rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
    re.compile(rb'\b(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,})\b'),
    re.compile(rb'(?im)^\s*(?:#define\s+)?[A-Z0-9_]*(?:PASSWORD|PASSWD|SECRET_KEY|API_KEY)[A-Z0-9_]*\s+(?:\"[^\"\r\n]{4,}\"|\'[^\'\r\n]{4,}\')'),
]


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def fail(msg: str) -> None:
    raise RuntimeError(msg)


def run(*args: str, cwd: Path) -> str:
    p = subprocess.run(args, cwd=cwd, capture_output=True,
                       text=True, errors='replace', check=False)
    if p.returncode:
        fail(f'Git command failed safely: {args[:3]}: {(p.stdout+p.stderr)[-400:]}')
    return p.stdout.strip()


def strict_repo() -> Path:
    repo = HERE.parents[1]
    if not (repo / '.git').exists():
        fail('not a Git checkout')
    if run('git', 'branch', '--show-current', cwd=repo) != BRANCH:
        fail('wrong Git branch: must be the R80 regression handoff branch')
    if run('git', 'remote', 'get-url', 'origin', cwd=repo) not in REPO_URLS:
        fail('unexpected Git origin: publishing forbidden')
    if run('git', 'status', '--porcelain', cwd=repo):
        fail('Git checkout is dirty: do not overwrite or publish')
    remote = run('git', 'ls-remote', 'origin', 'refs/heads/'+BRANCH, cwd=repo).split()
    local = run('git', 'rev-parse', 'HEAD', cwd=repo)
    if not remote or remote[0] != local:
        fail('Git branch not current with remote; pull --ff-only first')
    return repo


def must_be_file(root: Path, rel: str) -> Path:
    p = root / rel
    if not p.is_file() or p.is_symlink():
        fail('missing/unsafe source: ' + rel)
    return p


def verified_git_eol_checkout(source: Path, rel: str, item: dict) -> bool:
    """Accept ONLY a Git-proven LF -> CRLF checkout transformation.

    A nested vendor .gitattributes may say text=auto, overriding the outer
    "-text" rule. Windows checkout then changes LF bytes to CRLF despite
    Git's canonical HEAD blob matching the SHA-256 of the original donor.
    Never silently skip SHA checks or accept arbitrary normalized contents.
    """
    local = source.read_bytes()
    if b'\r\n' not in local:
        return False
    candidate = local.replace(b'\r\n', b'\n')
    if len(candidate) != item['bytes'] or hashlib.sha256(candidate).hexdigest() != item['sha256']:
        return False
    repo = HERE.parents[1]
    if not (repo / '.git').exists():
        return False
    git_rel = 'firmware/r80_fix28/full_project/' + rel
    status = subprocess.run(
        ['git', '-C', str(repo), 'status', '--porcelain', '--', git_rel],
        capture_output=True, check=False)
    if status.returncode or status.stdout or status.stderr:
        return False
    original = subprocess.run(
        ['git', '-C', str(repo), 'show', 'HEAD:' + git_rel],
        capture_output=True, check=False)
    if original.returncode or original.stderr:
        return False
    blob = original.stdout
    # The original SHA-256 remains the acceptance criterion.  Checking the
    # committed Git blob prevents accidental acceptance of a modified file.
    if len(blob) != item['bytes'] or hashlib.sha256(blob).hexdigest() != item['sha256']:
        return False
    if b'\r\n' in blob or local != blob.replace(b'\n', b'\r\n'):
        return False
    print('PASS Git checkout-only CRLF conversion (original SHA-256 retained): ' + rel,
          flush=True)
    return True


def audit_original() -> dict:
    manifest = json.loads(ORIGINAL.read_text(encoding='utf-8'))
    known = json.loads(BASE_SHA.read_text(encoding='utf-8'))
    if manifest.get('status') != 'IMPORTED_FROM_SHA_PINNED_LOCAL_BASE':
        fail('unexpected original source manifest status')
    if len(known) != 7 or manifest.get('donor_source_sha256') != known:
        fail('donor source SHA baseline mismatch')
    inc = manifest.get('included', [])
    exc = manifest.get('excluded', [])
    if len(inc) < 1000 or len(exc) < 1:
        fail('Git original source manifest is incomplete or unrecognized')
    seen = set()
    for item in inc:
        rel = item['path']
        if rel in seen or PurePosixPath(rel).is_absolute() or '..' in PurePosixPath(rel).parts:
            fail('duplicate/unsafe source path: ' + rel)
        seen.add(rel)
        f = must_be_file(FULL, rel)
        if f.stat().st_size != item['bytes'] or digest(f) != item['sha256']:
            if not verified_git_eol_checkout(f, rel, item):
                fail('Git source changed or incomplete: ' + rel)
    for rel in MUST_EXIST:
        must_be_file(FULL, rel)
    skipped_inputs = sorted(item['path'] for item in exc
                            if PurePosixPath(item['path']).suffix.lower() in SOURCE_SUFFIXES)
    if skipped_inputs != [REQUIRED]:
        fail('unexpected excluded compile inputs: ' + repr(skipped_inputs[:12]))
    cpp = must_be_file(FULL, 'components/M5Unified/src/utility/imu/BMI270_Class.cpp')
    if '#include "BMI270_config.inl"' not in cpp.read_text('utf-8', errors='replace'):
        fail('BMI270 compilation unit changed: verify source manually')
    print(f'PASS source audit: {len(inc)} pinned files / '
          f'{sum(item["bytes"] for item in inc)} bytes SHA-256; '
          f'{len(exc)} original exclusions examined', flush=True)
    print('Build-critical exclusion identified: ' + REQUIRED, flush=True)
    return manifest


def verify_completion() -> bool:
    if not REPAIRED.is_file():
        return False
    evidence = json.loads(REPAIRED.read_text('utf-8'))
    item = evidence.get('recovered_source')
    if evidence.get('status') != 'RECOVERED_FROM_SHA_PINNED_LOCAL_DONOR' or not isinstance(item, dict):
        fail('source completion manifest is invalid')
    if item.get('path') != REQUIRED or not re.fullmatch('[0-9a-f]{64}', item.get('sha256', '')):
        fail('source completion path/hash invalid')
    source = must_be_file(FULL, REQUIRED)
    if source.stat().st_size != item['bytes'] or digest(source) != item['sha256']:
        fail('source recovery bytes differ from Git manifest: ' + REQUIRED)
    if b'\0' in source.read_bytes():
        fail('recovered C include is not text')
    print(f'PASS exact vendor source complete: {REQUIRED} ({item["bytes"]} bytes, SHA-256 {item["sha256"]})', flush=True)
    return True


def find_donor(original: dict) -> Path:
    parent = Path(r'F:\build\tmp\s94')
    if not parent.is_dir():
        fail('original SHA-pinned donor directory unavailable; no alternate source substituted')
    known = original['donor_source_sha256']
    found = []
    for p in sorted(parent.iterdir()):
        if not p.is_dir() or p.is_symlink():
            continue
        if not all((p / sub).is_dir() for sub in ('project', 'components', 'third_party')):
            continue
        if all((p / 'project' / 'main' / name).is_file()
               and digest(p / 'project' / 'main' / name) == hashval
               for name, hashval in known.items()):
            found.append(p)
    if len(found) != 1:
        fail(f'expected one original SHA-matched donor; found {len(found)}')
    return found[0]


def publish_once(original: dict) -> None:
    repo = strict_repo()
    if REPAIRED.exists() or (FULL / REQUIRED).exists():
        fail('partial source recovery already exists; do not overwrite')
    donor = find_donor(original)
    original_file = must_be_file(donor, REQUIRED)
    raw = original_file.read_bytes()
    if not raw or len(raw) > 2 * 1024 * 1024 or b'\0' in raw:
        fail('original vendor .inl content size/type not accepted')
    if any(pattern.search(raw) for pattern in BAD_SECRETS):
        fail('source looks sensitive; no Git upload')
    # Keep exact upstream/donor bytes, do not fabricate a replacement array or
    # splice an unmatched M5Unified version. No writes to historical donor.
    target = FULL / REQUIRED
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(raw)
    sha = hashlib.sha256(raw).hexdigest()
    recovery = {
        'status': 'RECOVERED_FROM_SHA_PINNED_LOCAL_DONOR',
        'original_manifest': 'SOURCE_IMPORT_MANIFEST.json',
        'recovered_source': {'path': REQUIRED, 'sha256': sha, 'bytes': len(raw)},
        'note': 'Exact vendor source bytes; only R80 development branch. No flash, NVS or DSK.',
    }
    REPAIRED.write_text(json.dumps(recovery, indent=2) + '\n', encoding='utf-8')
    if not verify_completion():
        fail('source recovery did not verify')
    paths = [f'firmware/r80_fix28/full_project/{REQUIRED}',
             'firmware/r80_fix28/full_project/SOURCE_COMPLETION_MANIFEST.json']
    run('git', '-c', 'core.autocrlf=false', 'add', '-f', '--', *paths, cwd=repo)
    print(run('git', 'commit', '-m', 'source: restore exact excluded M5Unified BMI270 include', cwd=repo))
    print(run('git', 'push', 'origin', 'HEAD:refs/heads/' + BRANCH, cwd=repo))
    print('PASS exact source uploaded to Git; future native builds need no donor.', flush=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--verify', action='store_true')
    group.add_argument('--publish', action='store_true')
    args = parser.parse_args()
    print('R80 FIX28 source-completion gate: NO FLASH / NO COM9 / NO DSK', flush=True)
    try:
        original = audit_original()
        if verify_completion():
            if args.publish:
                # A local, uncommitted recovery must never be mistaken for
                # successfully published Git source. Reusing this path is
                # idempotent only when both files are tracked on remote HEAD.
                repo = strict_repo()
                for item in (REQUIRED, 'SOURCE_COMPLETION_MANIFEST.json'):
                    run('git', 'cat-file', '-e',
                        'HEAD:firmware/r80_fix28/full_project/' + item,
                        cwd=repo)
            print('PASS all Git sources: ready for native build-only.', flush=True)
            return 0
        if args.verify:
            fail('BMI270_config.inl was omitted by historical importer; '
                 'publish exact donor source before any native compilation')
        publish_once(original)
        print('PASS all Git sources: ready for fresh Git checkout and build-only.', flush=True)
        return 0
    except Exception as ex:
        print('SOURCE GATE STOP: ' + str(ex), flush=True)
        print('No build, no flash, no DSK or NVS changes.', flush=True)
        return 4


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
"""Import the exact historical SIMHP4 R80 donor source into this Git checkout.

Source-only. No Flash, COM9 or microSD access. Publish to a development branch
only if source provenance and a conservative public-source filter both pass.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
FULL = HERE / 'full_project'
BASE_SHA = HERE / 'legacy_staging' / 'BASE_SOURCE_SHA256.json'
BRANCH = 'work/r80-regression-handoff-20261115'
REPO_NAME = 'Layer812/simhVaxP4'
PARTS = ('project', 'components', 'third_party')
EXCLUDED_DIRS = {'build', '.git', '.pio', '__pycache__', '.cache', '.venv', 'venv', 'node_modules', 'managed_components', '.vs', '.vscode', '.idea', 'dist', 'out'}
EXCLUDED_SUFFIXES = {'.dsk', '.xdf', '.img', '.adf', '.hdf', '.vhd', '.qcow2', '.zip', '.7z', '.rar', '.bak', '.elf', '.bin', '.hex', '.map', '.o', '.obj', '.a', '.so', '.dll', '.exe', '.pdb', '.db', '.sqlite', '.nvs', '.pem', '.key', '.p12', '.pfx', '.der', '.crt', '.cer', '.log', '.pyc', '.jpg', '.jpeg', '.png', '.gif', '.webp', '.wav', '.mp3', '.ttf', '.otf', '.woff', '.woff2', '.dat'}
SOURCE_SUFFIXES = {'.c', '.cpp', '.cc', '.cxx', '.h', '.hpp', '.hxx', '.s', '.asm', '.in', '.inc', '.i', '.cmake', '.txt', '.md', '.rst', '.py', '.yml', '.yaml', '.json', '.csv', '.ld', '.def', '.cfg', '.ps1', '.cmd', '.bat', '.sh', '.kconfig', '.mk', '.diff', '.patch'}
SOURCE_FILENAMES = {'CMakeLists.txt', 'Kconfig', 'Kconfig.projbuild', 'Makefile', 'LICENSE', 'NOTICE', 'COPYING', '.gitignore', '.gitattributes', '.gitmodules', 'sdkconfig', 'sdkconfig.defaults', 'partitions.csv', 'component.mk'}
# Keep the conservative limit for first-party sources. M5GFX's upstream
# Japanese font is distributed as a very large C translation unit; it must be
# copied byte-for-byte, not silently omitted or replaced by a different font.
# GitHub rejects individual files above 100 MiB; stay below that hard ceiling.
MAX_ITEM = 8 * 1024 * 1024
MAX_M5GFX_ITEM = 96 * 1024 * 1024
MAX_TOTAL = 640 * 1024 * 1024
MAX_FILES = 12000

# Conservative public-repository secrets scanning. Refuse to push if uncertain.
SECRETS = [
    ('private key', re.compile(rb'-----BEGIN [A-Z ]*PRIVATE KEY-----')),
    ('AWS access key', re.compile(rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b')),
    ('GitHub token', re.compile(rb'\b(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,})\b')),
    ('password literal', re.compile(rb'(?im)^\s*(?:(?:#define|const|static|set|export)\s+)?[A-Za-z0-9_]*(?:PASSWORD|PASSWD|WIFI_PASS|API_KEY|SECRET_KEY|TOKEN)[A-Za-z0-9_]*\s*(?:=|\s+)\s*[\"\']([^\"\'\r\n]{4,})[\"\']')),
    ('sdkconfig credential', re.compile(rb'(?im)^\s*CONFIG_[A-Za-z0-9_]*(?:SSID|PASSWORD|PASSWD|API_KEY|SECRET|TOKEN)[A-Za-z0-9_]*\s*=\s*\"[^\"\r\n]+\"')),
]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def run(*cmd: str, cwd: Path, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(list(cmd), cwd=cwd, text=True, encoding='utf-8',
                          errors='replace', stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          check=check)


def find_donors(specified: str | None) -> list[Path]:
    if specified:
        return [Path(specified)]
    root = Path(r'F:\build\tmp\s94')
    if not root.is_dir():
        return []
    candidates = []
    if all((root / p).is_dir() for p in PARTS):
        candidates.append(root)
    for child in root.iterdir():
        if child.is_dir() and not child.is_symlink() and all((child / p).is_dir() for p in PARTS):
            candidates.append(child)
    return candidates


def validate_donor(root: Path, known: dict[str, str]) -> tuple[bool, str]:
    if not root.is_dir() or root.is_symlink():
        return False, 'not an ordinary donor directory'
    for p in PARTS:
        if not (root / p).is_dir():
            return False, 'missing ' + p
    main = root / 'project' / 'main'
    for name, digest in known.items():
        file = main / name
        if not file.is_file() or file.is_symlink() or sha256(file) != digest:
            return False, 'source SHA mismatch or missing: ' + name
    required = ('project/CMakeLists.txt', 'project/main/CMakeLists.txt',
                'project/main/simhp4_vax630_io_esp_r14e.c',
                'third_party/simh_vax630_87eb/scp.c',
                'third_party/simh_vax630_87eb/VAX/vax_cpu1.c')
    for p in required:
        if not (root / p).is_file():
            return False, 'incomplete donor: ' + p
    return True, '7 pinned source SHA-256 and structural requirements PASS'


def check_tree_and_stage(root: Path, stage: Path) -> tuple[list[dict], list[dict]]:
    included, excluded = [], []
    total = 0
    for part in PARTS:
        original = root / part
        for current, subdirs, filenames in os.walk(original, followlinks=False):
            subdirs[:] = sorted(d for d in subdirs
                                if d not in EXCLUDED_DIRS and not (Path(current) / d).is_symlink())
            for name in sorted(filenames):
                old = Path(current) / name
                rel = old.relative_to(root)
                rel_text = rel.as_posix()
                if old.is_symlink():
                    raise RuntimeError('symbolic link blocked: ' + rel_text)
                if name.lower().endswith(tuple(EXCLUDED_SUFFIXES)) or name.startswith('._') or name in {'.env', 'id_rsa', 'id_ed25519'}:
                    excluded.append({'path': rel_text, 'reason': 'non-source or sensitive extension'})
                    continue
                if name not in SOURCE_FILENAMES and old.suffix.lower() not in SOURCE_SUFFIXES:
                    excluded.append({'path': rel_text, 'reason': 'unrecognized extension'})
                    continue
                size = old.stat().st_size
                is_m5gfx_source = (
                    rel.parts[:2] == ('components', 'M5GFX')
                    and old.suffix.lower() in SOURCE_SUFFIXES
                )
                item_limit = MAX_M5GFX_ITEM if is_m5gfx_source else MAX_ITEM
                if size > item_limit:
                    raise RuntimeError(
                        f'file exceeds {item_limit // (1024 * 1024)} MiB source review limit: {rel_text}'
                    )
                # Audit oversized vendor source exactly as any other source.
                # Size exceptions never bypass the NUL/credentials checks or
                # full SHA-256 manifest; no content is dropped or truncated.
                if size > MAX_ITEM:
                    print(f'ALLOW LARGE M5GFX SOURCE: {rel_text} ({size} bytes; exact copy)')
                raw = old.read_bytes()
                if b'\0' in raw:
                    raise RuntimeError(f'binary content in source file: {rel_text}')
                for label, pattern in SECRETS:
                    if pattern.search(raw):
                        raise RuntimeError(f'possible {label} in {rel_text}; refuse public Git upload')
                total += size
                if total > MAX_TOTAL or len(included) >= MAX_FILES:
                    raise RuntimeError('source collection exceeds safety limit; require review')
                target = stage / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(raw)
                included.append({'path': rel_text, 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': size})
    if not included:
        raise RuntimeError('no source files found')
    return included, excluded


def validate_git_checkout() -> Path:
    repo = HERE.parents[1]
    if not (repo / '.git').exists():
        raise RuntimeError('not a Git checkout; clone the R80 development branch first')
    branch = run('git', 'branch', '--show-current', cwd=repo).stdout.strip()
    if branch != BRANCH:
        raise RuntimeError(f'wrong branch {branch!r}; expected {BRANCH}')
    remote = run('git', 'remote', 'get-url', 'origin', cwd=repo).stdout.strip()
    valid = (remote in ('https://github.com/' + REPO_NAME + '.git',
                        'https://github.com/' + REPO_NAME,
                        'git@github.com:' + REPO_NAME + '.git'))
    if not valid:
        raise RuntimeError('unexpected Git origin: ' + remote)
    if run('git', 'status', '--porcelain', cwd=repo).stdout.strip():
        raise RuntimeError('Git working tree is dirty; refuse to publish or overwrite files')
    return repo


def main() -> int:
    ap = argparse.ArgumentParser(description='Import exact R80 FIX4 base source to Git; never flash')
    ap.add_argument('--donor', help='Explicit directory containing project/, components/, third_party/')
    ap.add_argument('--dry-run', action='store_true', help='Validate source without copying or Git push')
    ap.add_argument('--publish', action='store_true', help='Git commit and push only to R80 development branch')
    args = ap.parse_args()
    print('SIMHP4 R80 source import; NO FLASH / NO DSK / NO NVS')
    if args.publish and args.dry_run:
        print('STOP: --publish and --dry-run conflict')
        return 2
    try:
        known = json.loads(BASE_SHA.read_text('utf-8'))
        if len(known) != 7:
            raise RuntimeError('unexpected donor SHA manifest')
        donors = find_donors(args.donor)
        valid, errors = [], []
        for root in donors:
            ok, note = validate_donor(root, known)
            (valid if ok else errors).append((root, note))
        if not valid:
            print('STOP: no exactly SHA-matched R80 FIX4 donor found')
            print('Checked: ' + (str([str(x) for x in donors]) if donors else r'F:\build\tmp\s94 (missing)'))
            for path, reason in errors[:8]:
                print('  ' + str(path) + ': ' + reason)
            return 4
        if len(valid) != 1:
            print('STOP: multiple matching donors; specify exactly one with --donor')
            return 4
        root = valid[0][0]
        print('PINNED BASE VERIFIED: ' + str(root))
        repo = validate_git_checkout() if args.publish else None
        if FULL.exists():
            # A previous successful one-time publication must be idempotent.
            # Do not overwrite, recopy, commit, or push a previously verified
            # source tree.  An untracked/partial tree is still rejected.
            if not FULL.is_dir() or FULL.is_symlink():
                raise RuntimeError('full_project is not an ordinary source directory')
            manifest_path = FULL / 'SOURCE_IMPORT_MANIFEST.json'
            if not manifest_path.is_file() or manifest_path.is_symlink():
                raise RuntimeError('full_project already exists without a valid import manifest; no overwrite')
            manifest = json.loads(manifest_path.read_text('utf-8'))
            if (manifest.get('status') != 'IMPORTED_FROM_SHA_PINNED_LOCAL_BASE'
                    or manifest.get('donor_source_sha256') != known):
                raise RuntimeError('existing full_project manifest does not match the exact R80 source; no overwrite')
            included = manifest.get('included')
            if not isinstance(included, list) or not included:
                raise RuntimeError('existing full_project manifest is empty; no overwrite')
            tracked = run('git', 'cat-file', '-e',
                          'HEAD:firmware/r80_fix28/full_project/SOURCE_IMPORT_MANIFEST.json',
                          cwd=repo, check=False) if repo else None
            if repo and tracked.returncode != 0:
                raise RuntimeError('full_project exists but is not committed in Git; no overwrite')
            for rel in ('project/CMakeLists.txt', 'project/main/CMakeLists.txt',
                        'project/main/simhp4_vax630_io_esp_r14e.c',
                        'third_party/simh_vax630_87eb/scp.c',
                        'third_party/simh_vax630_87eb/VAX/vax_cpu1.c'):
                file = FULL / rel
                if not file.is_file() or file.is_symlink():
                    raise RuntimeError('missing published source file: ' + rel)
            # The tracked source and manifest are immutable after checkout.
            # Make sure the remote branch has this exact commit before
            # declaring public source publication successful.
            if repo:
                local = run('git', 'rev-parse', 'HEAD', cwd=repo).stdout.strip()
                remote = run('git', 'ls-remote', 'origin', 'refs/heads/' + BRANCH,
                             cwd=repo).stdout.split()
                if not remote or remote[0] != local:
                    raise RuntimeError('local committed source is not yet the remote branch HEAD; git pull/verify first')
            print('PASS: full_project already verified and published in Git; no re-import needed.')
            return 0
        with tempfile.TemporaryDirectory(prefix='simhp4_source_stage_') as td:
            stage = Path(td) / 'full_project'
            included, excluded = check_tree_and_stage(root, stage)
            print(f'FULL SOURCE STAGING: {len(included)} files / {sum(x["bytes"] for x in included)} bytes; excluded {len(excluded)}')
            if args.dry_run:
                print('PASS: dry-run. Nothing copied, committed or pushed.')
                return 0
            # Keep original CRLF/LF exactly; no Git attributes translation of donor bytes.
            (stage / '.gitattributes').write_text('* -text\n', encoding='utf-8')
            manifest = {'status': 'IMPORTED_FROM_SHA_PINNED_LOCAL_BASE',
                        'donor_source_sha256': known,
                        'included': included,
                        'excluded': excluded,
                        'notice': 'Development source only. Does not imply native compile or physical acceptance.'}
            (stage / 'SOURCE_IMPORT_MANIFEST.json').write_text(
                json.dumps(manifest, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
            shutil.move(str(stage), str(FULL))
        print('SOURCE IMPORTED (local Git checkout only): ' + str(FULL))
        if not args.publish:
            print('NOT PUSHED. Run with --publish only after checking this public-source tree.')
            return 0
        run('git', '-c', 'core.autocrlf=false', 'add', '-f', '-A',
            'firmware/r80_fix28/full_project', cwd=repo)
        committed = run('git', '-c', 'core.autocrlf=false', 'commit', '-m',
                        'source: import SHA-verified R80 FIX4 complete ESP-IDF base (no media)', cwd=repo)
        print(committed.stdout.strip())
        pushed = run('git', 'push', 'origin', 'HEAD:refs/heads/' + BRANCH, cwd=repo)
        print(pushed.stdout.strip())
        print('PASS: imported source pushed to GitHub development branch. NO FLASH.')
        return 0
    except Exception as exc:
        print('STOP: ' + str(exc))
        print('No Flash, DSK or NVS changed. Review any staged source before retrying.')
        return 3

if __name__ == '__main__':
    sys.exit(main())

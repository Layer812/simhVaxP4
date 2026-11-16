#!/usr/bin/env python3
# R80 FIX11 APP-only flash primitives inherited unchanged from physically verified FIX10 runner.
from __future__ import annotations
import argparse
import binascii
import datetime as dt
import hashlib
from importlib.metadata import version
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import time
import traceback
import zipfile


APP_OFFSET = 0x10000
APP_PARTITION_SIZE = 0x800000
FLASH_SIZE = 0x1000000
TABLE_OFFSET = 0x8000
TABLE_SIZE = 0x1000
PROJECT = 'simhp4_vax630_visual_media_probe_v12'[:31]
EXPECTED_PARTS = [
    ('nvs', 1, 2, 0x9000, 0x6000),
    ('phy_init', 1, 1, 0xf000, 0x1000),
    ('factory', 0, 0, APP_OFFSET, APP_PARTITION_SIZE),
    ('humanxdf', 1, 128, 0x810000, 0x140000),
]
APP_SIZE = 0
APP_SHA256 = ''

class SafeStop(Exception):
    pass

def guard(cond, reason):
    if not cond:
        raise SafeStop(reason)

def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()

def sha256_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()

def candidate_identity(img):
    guard(len(img) >= 176 and img[0] == 0xE9, 'candidate ESP-IDF application header missing')
    guard(int.from_bytes(img[12:14], 'little') == 18, 'candidate is not ESP32-P4 image')
    guard(struct.unpack_from('<I', img, 32)[0] == 0xABCD5432, 'candidate app descriptor invalid')
    proj = img[80:112].split(b'\0', 1)[0].decode('ascii', 'strict')
    guard(proj == PROJECT, 'candidate project does not match simhP4')
    return proj

def parse_partition_entries(blob):
    # Parse *every* entry before deciding whether the application slot is safe.
    # A different but valid partition table must not be treated as corruption.
    guard(len(blob) == TABLE_SIZE, 'partition table sector size mismatch')
    parsed = []
    for off in range(0, TABLE_SIZE, 32):
        row = blob[off:off + 32]
        if row == b'\xff' * 32 or row[:2] == b'\xeb\xeb':
            break
        guard(row[:2] == b'\xaa\x50', f'invalid partition entry magic at {off:#x}')
        _, typ, sub, start, size, raw_name, flags = struct.unpack('<HBBII16sI', row)
        name = raw_name.split(b'\0', 1)[0].decode('ascii', 'strict')
        guard(bool(name) and flags == 0 and size > 0 and start % 4096 == 0
              and size % 4096 == 0 and start >= 0x9000 and start + size <= FLASH_SIZE,
              f'invalid partition bounds/flags: {name!r}')
        parsed.append((name, typ, sub, start, size))
    guard(parsed, 'partition table is empty')
    guard(len(parsed) == len({e[0] for e in parsed}), 'duplicate partition name')
    ordered = sorted(parsed, key=lambda e: e[3])
    for a, b in zip(ordered, ordered[1:]):
        guard(a[3] + a[4] <= b[3], 'overlapping partitions: ' + a[0] + '/' + b[0])
    return parsed


def parse_partition_table(blob):
    # Validate all entries. The app target is selected separately using current
    # boot metadata rather than assuming a factory slot at 0x10000.
    return parse_partition_entries(blob)


def selected_boot_partition(parts, ota_blob, app_size):
    """Determine the *selected* boot slot, never modify OTA metadata.

    Intentionally refuses ambiguous/unsupported layouts. The running bootloader's
    selected partition can be known only with a matching partition table and
    OTA data, and a stable, valid OTA state.
    """
    guard(0 < app_size <= FLASH_SIZE, 'candidate APP size invalid')
    factories = [p for p in parts if p[1] == 0 and p[2] == 0]
    otas = sorted([p for p in parts if p[1] == 0 and 0x10 <= p[2] <= 0x1F],
                  key=lambda p: p[2])
    datas = [p for p in parts if p[1] == 1 and p[2] == 0]
    guard(len(factories) <= 1, 'ambiguous factory partition table; NO FLASH')
    guard(len(datas) <= 1, 'ambiguous otadata partition; NO FLASH')
    guard([p[2] for p in otas] == list(range(0x10,0x10+len(otas))),
          'non-contiguous OTA app slots; NO FLASH')
    guard(not (otas and not datas and not factories),
          'OTA app present but otadata and factory missing; NO FLASH')
    for p in factories + otas:
        guard(p[3] >= 0x10000 and p[3] % 0x10000 == 0 and p[4] % 0x10000 == 0,
              'invalid APP alignment: ' + p[0])
    if not datas:
        guard(ota_blob is None, 'unexpected OTA metadata; NO FLASH')
        guard(len(factories) == 1, 'no unambiguous boot APP partition; NO FLASH')
        selected = factories[0]
        reason = 'factory (no otadata)'
    else:
        guard(ota_blob is not None and len(ota_blob) == 8192,
              'otadata read incomplete; NO FLASH')
        guard(len(otas) > 0 or len(factories) == 1,
              'otadata present but no boot APP; NO FLASH')
        records=[]
        for ix in (0,1):
            block=ota_blob[ix*4096:ix*4096+32]
            seq=int.from_bytes(block[0:4],'little')
            state=int.from_bytes(block[24:28],'little')
            crc=int.from_bytes(block[28:32],'little')
            valid=(seq not in (0,0xffffffff) and state not in (3,4) and
                   crc == (binascii.crc32(block[:4], 0xffffffff) & 0xffffffff))
            records.append({'index':ix,'seq':seq,'state':state,'crc':crc,
                            'valid':valid,'empty':block==b'\xff'*32})
        valid=[p for p in records if p['valid']]
        if valid:
            guard(bool(otas), 'selected OTA entry with no OTA slots; NO FLASH')
            if len(valid)==2:
                guard(abs(valid[0]['seq']-valid[1]['seq']) < 0x80000000,
                      'OTA sequence wraparound ambiguous; NO FLASH')
            active=max(valid,key=lambda p:p['seq'])
            guard(active['state'] in (2,0xffffffff),
                  'active OTA state may roll back or is unverified; NO FLASH')
            slot=(active['seq']-1) % len(otas)
            selected=otas[slot]
            reason=f'OTA selected by otadata seq={active["seq"]} index={active["index"]} slot=ota_{slot}'
        else:
            # A valid ESP-IDF bootloader initially boots factory, or ota_0
            # without factory. A corrupt/modified otadata is NOT initial state.
            guard(all(r['empty'] for r in records),
                  'invalid/nonempty OTA boot selection; NO FLASH')
            if factories:
                selected=factories[0]
                reason='factory (otadata erased)'
            else:
                guard(otas, 'no boot partition; NO FLASH')
                selected=otas[0]
                reason='ota_0 (otadata erased; no factory)'
    guard(selected[4] >= app_size,
          f'selected APP {selected[0]} capacity {selected[4]:#x} smaller than candidate {app_size:#x}; NO FLASH')
    guard(selected[3] + app_size <= selected[3] + selected[4] <= FLASH_SIZE,
          'selected APP bounds invalid; NO FLASH')
    return selected,reason

class Esptool:
    def __init__(self, python, port, logs):
        self.python, self.port, self.logs = python, port, logs
        try:
            release = version('esptool')
        except Exception as exc:
            raise SafeStop('esptool package not available: ' + str(exc)) from exc
        m = re.match(r'^(\d+)\.(\d+)(?:\.|$)', release)
        guard(m and int(m.group(1)) in (4,5), 'unsupported esptool: ' + release)
        self.dash = int(m.group(1)) == 5
        print('esptool:', release, 'syntax:', 'v5' if self.dash else 'v4', flush=True)
        logs.append('esptool distribution version: ' + release)

    def cli(self, s):
        return s.replace('_','-') if self.dash else s

    def command(self, op, args):
        guard(op in ('read_flash', 'write_flash', 'verify_flash'), 'esptool operation forbidden')
        return [self.python, '-m', 'esptool', '--chip', 'esp32p4', '--port', self.port,
                '--baud', '460800', '--before', self.cli('default_reset'),
                '--after', self.cli('hard_reset'), self.cli(op)] + [str(x) for x in args]

    def run(self, op, args):
        cmd = self.command(op,args)
        print('ESP32-P4:', self.cli(op), *(str(x) for x in args[:2]), flush=True)
        proc = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
        output = (proc.stdout or '') + '\n' + (proc.stderr or '')
        self.logs.append('CMD: ' + ' '.join(cmd) + '\nrc=' + str(proc.returncode) + '\n' + output)
        if proc.returncode != 0:
            print('ESPTOOL FAILURE rc=', proc.returncode, '\n', output[-5000:], flush=True)
            raise SafeStop(f'esptool {self.cli(op)} failed: rc={proc.returncode}; see ESPTOOL_LOG.txt')
        # --chip esp32p4 is also enforced by the tool itself.

    def read_partition_table(self, outfile):
        guard(outfile.parent.is_dir() and not outfile.exists(), 'temporary table file invalid')
        self.run('read_flash', [hex(TABLE_OFFSET), hex(TABLE_SIZE), str(outfile)])
        guard(outfile.is_file() and outfile.stat().st_size == TABLE_SIZE,
              'partition table read incomplete')
        return outfile.read_bytes()

    def read_otadata(self, outfile, offset):
        guard(outfile.parent.is_dir() and not outfile.exists(), 'temporary OTA file invalid')
        self.run('read_flash', [hex(offset), hex(0x2000), str(outfile)])
        guard(outfile.is_file() and outfile.stat().st_size == 0x2000, 'OTA read incomplete')
        return outfile.read_bytes()

    def write_app(self, path):
        guard(path.is_file() and path.stat().st_size == APP_SIZE and sha256_file(path) == APP_SHA256,
              'candidate image corrupted before writing')
        guard(APP_OFFSET >= 0x10000 and APP_OFFSET % 0x10000 == 0 and
              APP_SIZE > 0 and APP_SIZE <= APP_PARTITION_SIZE and
              APP_OFFSET + APP_PARTITION_SIZE <= FLASH_SIZE,
              'app-only selected slot bounds invalid')
        opts = ['--flash_mode','dio','--flash_freq','80m','--flash_size','16MB']
        self.run('write_flash', [self.cli(s) for s in opts] + [hex(APP_OFFSET),str(path)])

    def verify_app(self, path):
        self.run('verify_flash', [hex(APP_OFFSET), str(path)])

def capture_serial(port, dest, seconds=100):
    try:
        import serial
        print('Collecting normal BSD/LCD/Hosted log. Tap Wi-Fi icon after BSD prompt.', flush=True)
        with serial.Serial(port, 115200, timeout=0.4) as ser:
            until=time.monotonic()+seconds
            while time.monotonic()<until:
                data=ser.read(8192)
                if data:
                    dest.append(data.decode('utf-8','replace'))
                    if sum(map(len,dest))>1_500_000:
                        break
    except Exception as exc:
        dest.append('SERIAL UNAVAILABLE: ' + repr(exc))

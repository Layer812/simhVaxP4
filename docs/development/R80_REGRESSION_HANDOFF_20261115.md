# simhVaxP4 R80: regression-safe development handoff (2026-11-15)

**Status: development handoff only — NO FLASH / NO RELEASE / NOT HARDWARE-ACCEPTED.**

This document captures verified milestones and minimum regression gates before a new MicroVAX II / 4.3BSD Quasijarus firmware is considered for hardware use. It does **not** claim a repair has been built. The R80 firmware source and exact release artifacts are **not** present in this repository's public main branch; compare the actual local source and binary manifests before applying any changes.

## Protect the known successes

| Baseline | Evidence | Must not regress |
| --- | --- | --- |
| R6R30_NATIVE_IDLE2 | Previously physically accepted production performance baseline | Native SIMH idle behavior, BSD boot responsiveness, RQ disk behavior |
| R74 | Prior physical STA association / IP acquisition | C6 power / SDIO / Hosted startup and scan settings |
| R79 integrated RC1 | Guest `qe0 at uba0 csr 174440 vec 770, ipl 14` and `ra0a` detected | XQ vector-table rebuild and guest IRQ initialization |
| R80 FIX22 lineage | Physical log: 1280×720 panel, BSD shell command `ls`, 12 SSIDs, `STA GOT_IP`, TCP forwarding route registration | LCD / touch / A164 / BSD console / Wi-Fi; forwarding registration is NOT a proven successful remote Telnet session |
| R80 RC14 FIX27R1 | Native build and boot-selected APP-only write/readback reported PASS; BSD kernel enumerates `qe0` and `ra0a` in one boot trace | **Not yet accepted** as a full BSD login/shell or stable reboot baseline |

**Do not conflate:** firmware flash verification, ROM reaching BSD kernel, single-user shell readiness, multi-user login readiness, P4 Wi-Fi connection, guest `qe0` enumeration, guest packet transmission, or completed remote Telnet sessions.

A separate November 15 reboot trace reports `V13 BOOT_CPU returned status=4`, followed by `EVENT_GATE calls=0` and guest PC `2004542c`. Establish the exact build identity and stop/reboot path before attributing it to FIX27R1 or claiming its cause.

## First engineering change

1. Compare the actual locally built **FIX22 → FIX23/FIX24 → FIX25 → FIX26 → FIX27/FIX27R1** source snapshots and their source/BIN SHA manifests. Do not use ZIP names as code provenance.
2. Isolate the QBA/RQ lifecycle, UNIT reset/clear, `SCPE_STOP` handling, metadata checks, and explicit guest reboot/sync gate. Keep the known-good XQ vector rebuild, display/touch ownership, Wi-Fi Hosted sequencing, and R6R30 idle/scheduler behavior unchanged.
3. Identify the precise condition under which `BOOT_CPU` returns status 4. Do not hide it by unconditional retry or reset; preserve the error and require a known valid recovery path.
4. Before claiming a repair, verify startup, `ra0a`, `qe0`, the single-user `#` prompt, interactive commands, and a deliberate guest reset/reboot with both normal and error paths. Only then evaluate multi-user boot and networking.

## Nonnegotiable regression gates

- [ ] Exact, non-substituted golden artifacts: SHA manifest, source revision, dependencies/managed component revision, ESP-IDF version, build config, ELF and APP binary.
- [ ] C6 firmware and on-card BSD source image remain unchanged. Working disk operations are isolated to the authorized work copy. No disk initialization or formatting as a regression test.
- [ ] LCD identified at 1280×720; terminal presents readable glyphs and VT100 cursor/scroll/erase; A164 keyboard and touch both work.
- [ ] Guest recognizes `ra0a` and `qe0` without `didn't interrupt` or stray-vector regression.
- [ ] Single-user shell reachable with a responsive interactive `ls`, without boot loop, unexpected stop, or unbounded restart.
- [ ] Warm reboot/reset has defined, bounded state handling; QBA/RQ resources have exactly one owner and no stale pointer or double release.
- [ ] Wi-Fi tap triggers Hosted; SSID scan and STA/DHCP work; preserve FIX22 behavior and verify guest packets/Telnet separately.
- [ ] BSD console boot is independent of Wi-Fi. MACRO reads optional SD `MACRO.TXT` only on explicit action; no automatic legacy BSD command injection.
- [ ] CPU and disk performance are compared to R6R30 with the same workload, not just a boot-banner screenshot.
- [ ] Distinguish host/mocked tests, compilation, device flash/readback, and **physical functional PASS** in the release manifest.

## Hard stop before every APP-only flash

The active device's **partition table and OTA data must be read and interpreted**. Identify the actual boot-selected APP slot, offset and size; validate chip, APP image/header, signed/encrypted state if relevant, boundaries, and all protected partitions. Do not presume factory mode or that `0x10000` is always the correct app target. Any uncertain or mismatched value means **NO WRITE**. Preserve bootloader, partition table, otadata, NVS, C6 firmware, and SD media; never use erase-flash or full-flash updates.

In particular, the historical **R6R30 golden recovery failed closed** because its accepted manifest and original matching BIN were not found. Do not substitute a different BIN, and do not flash an inferred recovery image.

## Acceptance and release policy

Build and offline source-level regression checks may run in an isolated copy. Do not modify the protected production source tree, or ask for repeated diagnostics-only flashes. Prepare one coherent candidate and one hardware acceptance exercise after static validation passes. A newly flashed APP is **not** a production replacement until the whole relevant regression matrix passes. Record failures as failures, even if other functionality worked.

**Current next action requiring local sources:** obtain the exact FIX22 and FIX27R1 source snapshots (with manifests) from the development machine. This public repository currently does not contain the firmware source needed to construct or verify a corrective patch.


## November 15 forensic update: two distinct failure modes

**Finding A — firmware host crash (recorded in FIX22 lineage and in the latest November 15 trace):** both logs contain ESP32-P4 Core 0 `Guru Meditation Error: Instruction access fault` while BSD is booting, shortly after `Changing root device to ra0a`. The reported bad host instruction fetch addresses differ (FIX22 trace `MEPC=0x7858d65a`; latest trace `MEPC=0x13fd96ce`). FIX22's later successful shell, scan, and IP acquisition occurred **after a software reboot** and therefore do not prove first-attempt or reboot stability. Match each log to its exact ELF/BIN SHA before attributing a cause.

**Finding B — VAX guest stop status 4 on a later startup:** the latest trace reports `V13 BOOT_CPU returned status=4` during KA630 POST and subsequently `EVENT_GATE calls=0`. In upstream Open SIMH VAX, `STOP_ILLVEC=4` is described as `Invalid SCB vector` (`VAX/vax_defs.h`, `VAX/vax_sys.c`). In upstream `VAX/vax_cpu1.c`, `intexc()` reads `newpc = ReadLP((SCBB+vec) & (PAMASK & ~3))` and stops if `newpc & 2`. Verify these definitions against the *exact local fork*: do not treat a VAX-specific stop code 4 as the generic `SCPE_STOP`, and do not automatically retry/reboot through it. Capture offending `SCBB`, `vec`, `newpc`, guest PC and interrupt source in the corrective build's error path (not a diagnostics-only flash).

**Finding C — disk validation was incomplete:** the observed R80 console reports `/sdcard/QUASWORK.DSK`, size `622932480`, `V12 RESULT=PASS`, and first 512-byte sector FNV-1a32 `c6b9e541`. It also explicitly reports `R6R3 STARTUP_DIRECT no_hash=1 no_manifest=1 no_recopy=1`. These logs do **not** establish a full-file hash or UFS filesystem consistency. An unchanged first sector does not rule out changes or damage in root filesystem sectors. Do not erase, recopy, regenerate, repair, or format either disk merely to test a hypothesis.

**Finding D — resource headroom caution:** FIX22 Wi-Fi success traces also include `dma_internal_free=12` bytes in the presenter-stack log immediately before `STA GOT_IP`; this may be a dangerously small remaining DMA-capable pool, but requires the exact heap metric definition and workload context before being labeled an OOM regression.

### Read-only disk forensics gate

Use the offline disk audit script distributed separately with this handoff to compute full-file SHA-256 for both original and work DSK, compare all `1,216,665` 512-byte sectors, produce contiguous changed-LBA ranges, and emit bounded local-only hexadecimal byte excerpts. Confirm both images are immutable throughout reading. Compare the original image's full SHA with a trusted pinned original if available; comparing original and work alone does not prove original provenance. A changed filesystem block is not automatically corruption; UFS integrity needs a filesystem-aware read-only check after obtaining a consistent snapshot. An absent or concurrently changing DSK means **NOT CHECKED**, never PASS.

### Required symbolication of host crashes

Use the ELF whose SHA matches each *specific* UART crash build. Symbolicate host return addresses (`0x4006d8fe` and `0x4006d330` in the two supplied crash records) with that build's `riscv32-esp-elf-addr2line`; the invalid MEPCs themselves will generally not symbolize. Compare the resulting call sites and the QBA/RQ/XQ ownership/lifetime paths. Do not recompile a guessed source tree and use its ELF to symbolize a different flash image.

### Evidence limitation

These are historical UART logs and publicly available upstream reference code, not the exact local FIX22 / FIX27R1 source snapshots. No actual original/work DSK bytes, completed full-disk comparison, matching ESP-IDF ELF, or newly repaired firmware have been verified in this handoff.


## Uploaded offline audit result — 2026-11-15 19:49:41 (local)

The user's read-only audit report archive was inspected, including `AUDIT_SUMMARY.md`, `source_comparison.json`, `source_unified_diff.txt`, `artifact_inventory.json`, and `source_zip_inventory.json`. The ZIP passed integrity testing.

**Verified from report:**
- Exact FIX22 and FIX27R1 source ZIPs were located and hashed. Both contain 13 normalized source-related entries, 12 matching paths; the 3 differing common paths are `simhp4_release_ui/package_sha256.json`, `simhp4_release_ui/run_release_ui.py`, and `source/v13_main.cpp`. These are packaging/staging differences, not a complete compiled-project object comparison.
- The FIX27R1 launcher extends the staging patcher to change *compiled* `third_party/simh_vax630_87eb/scp.c` and `project/main/simhp4_vax630_io_esp_r14e.c`, adding QBA skip/flush/sequential-file guards and address-range checks at the entry to `qbmap_wr` and `qbmap_rd`.
- `v13_main.cpp` adds `extern "C" UNIT qba_unit` and `memset(&qba_unit, 0, sizeof(qba_unit))` immediately before the first guest run, logging prior flags/fileref.
- The code stages a workaround for suspected QBA metadata overwrite. The report **does not prove** that the patched code corresponds byte-for-byte to the flashed APP, or that the cause of the overwrite was resolved.
- The artifact inventory enumerated 2,970 paths but reached its 5,001-directory limit. Its absence of particular files therefore is **not evidence those files do not exist**.
- **No actual DSK comparison was performed:** `QUASDISK.DSK` and `QUASWORK.DSK` were not resolved as one unambiguous readable pair. No DSK hash, changed-LBA listing, sector dumps, or filesystem integrity result exists.

**Additional source-level finding:** pinned upstream Open SIMH `VAX/vax630_io.c` defines `qbmap_rd()` with an unchecked `qb_map[idx]` access and `qbmap_wr()` with only `idx < QBNMAPR` prior to dereferencing. If the computed index can be negative, this is an out-of-bounds risk. See upstream commit `87eb7d5e96f9ce0ee6ac183e20160e5c486b0712`, `VAX/vax630_io.c` lines 445–466. FIX27R1's staging script attempts to add lower and upper physical-address range guards. Whether a negative index actually occurred in the user's image and whether this explains QBA corruption is **not proven**; inspect the locally compiled translation unit and ELF with corresponding checksums.

**Next safe evidence step:** (1) directly inspect actual FIX22/FIX27R1 source package contents and matching ELF/BIN, especially the compiled guards and firmware crash backtrace; (2) compare all sectors of the two DSK files only after confirming both paths and exclusive/unmounted access. No auto repair/re-init/copy/flash.

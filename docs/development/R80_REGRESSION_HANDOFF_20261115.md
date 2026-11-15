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


## Actual FIX22/FIX24R1 ELF and FIX27R1 APP instruction proof

The user's 2026-11-15 forensic archive contains verified FIX22/FIX27R1 source ZIPs, APP images and result archives, plus **FIX22 and FIX24R1** ELFs. **There is no matching FIX27R1 ELF.** The FIX22 APP image embeds the precise FIX22 ELF SHA256 `1afb047785191cd165ac97e2834eb9c49c1bdcd1569f2e7ea7cd3b9d1f062df8` (offset 176).

- FIX22 ELF: `qba_unit=0x4ff82198` size 152; `qb_map=0x4ff82230` size 32,768: **the latter immediately follows the former**. RISC-V `qbmap_wr` (`0x40054a20`) checks only signed `idx < 8192`, not `idx >= 0`; `qbmap_rd` (`0x40054a02`) accesses without any bounds guard. For `QBMAPBASE=0x20088000`, `pa=0x20087fa0` yields `idx=-24` and targets `qba_unit+0x38`, i.e. its `io_flush` field. Similarly, `0x20087f88` targets `flags`, `0x20087f74` targets `fileref`. **This is a code-level out-of-bounds mechanism, not evidence that such a PA was actually supplied.**
- FIX22 crash: `qba_dev=0x4ff28768` matches the crash's S2; `sim_flush_buffered_files=0x4006d898`, return PC `0x4006d8fe` follows an indirect `jalr a4` at `0x4006d8fc`. This coheres with an invalid QBA UNIT callback; exact initiating memory write still unknown.
- FIX24R1 ELF already contains an unsigned `bgeu` bounds check on both `qbmap_rd` (`0x40054a02`) and `qbmap_wr` (`0x40054a42`). The direct, independently parsed FIX27R1 APP binary contains the **identical full 64-byte compiled `qbmap_rd` function** (image offset `0xd4a52`) and **first 174 bytes of the compiled guarded `qbmap_wr` function** (image offset `0xd4a92`). Therefore the compiled FIX27R1 check is verified **in machine code**, not merely inferred from embedded marker strings.
- In the FIX22 and FIX24R1 ELFs themselves, resolving `sim_stop_messages[4]` through the ELF symbol and section bytes gives `Invalid SCB vector`; `status=4` is a VAX-specific reason code, not a generic SCPE_STOP.
- Both result ZIPs contain **actual readback partition-table bytes** and esptool logs showing the **same device MAC** `80:f1:b2:d1:4b:8c`. The table changed between captures: FIX22 app0 `0x10000/0xc00000`, NVS `0x9000/0x5000`, storage `0xc10000/0x3e0000`; FIX27R1 app0 `0x10000/0xfd0000`, NVS `0xfe0000/0x10000`, **no storage**. A separate operation must have altered the partition configuration; the specific operation and timing are not proven. The APP-only updates by themselves do not rewrite it.
- Actual DSK bytes are still absent; prior sector-zero FNV and image-size logs cannot validate `QUASWORK.DSK` or its UFS filesystem. **No disk repair/copy/reinit and no flash.**

**Engineering conclusion:** Keep the QBA map bounds protection and file-device exclusion; do not declare the full UNIT memset a root-cause fix. FIX24R1 already contains range guards, so a remaining corrupt UNIT or guest invalid SCB vector requires examination of other access paths or initialization/lifecycle interactions. Next candidate must be grounded in the complete exact local source and the matching FIX27R1 ELF, with first-boot/reboot and DSK checks separate.


## R80 FIX28 isolated source candidate (2026-11-15, **build-only, NO FLASH**)

After examining the uploaded FIX22/FIX27R1 source packages and matching FIX22/FIX24R1 ELFs, a distinct FIX28 **source-only** package was prepared. It keeps all existing FIX27R1 QBA bounds guards, QBA file exclusion, unit reset, console, LCD, A164 keyboard, Wi-Fi overlay, XQ and RQ disk behavior unchanged. Only the existing error path for the VAX `STOP_ILLVEC=4` is extended:

- In the exact compiled `VAX/vax_cpu1.c` `intexc()` branch checking `newpc & 2`, preserve `ABORT(STOP_ILLVEC)` and capture `SCBB`, `vec`, `newpc`, guest PC, PSL, IPL, and exception/interrupt kind in single-event globals. No serial I/O or additional FreeRTOS tasks are inserted into normal VAX execution.
- In `v13_main.cpp`, when `run_cmd()` returns `STOP_ILLVEC`, emit the captured values once, **without retrying/rebooting**.
- In the fixed-source staging/build script, require a source match and a compile log for `vax_cpu1.c.obj`, require the linked capture marker in the resulting APP, and verify the APP's embedded **exact ELF SHA256**.
- The package runner has had **all COM9/esptool/flash/write steps removed**. It outputs a matched APP.bin+ELF and actual compiled C/C++ sources to one result ZIP. It can be extracted in an arbitrary Windows folder; its output stays under `F:\build\tmp`, with isolated staging under `s114`.
- Offline tests: package hashes/ZIP integrity PASS, unit-test C compile and normal/error-path simulated execution PASS, repeated-apply fails closed, source identity and absence of flash actions in runtime source PASS.
- **Not yet done:** ESP-IDF native build in the user's Windows environment; hardware boot / fault recurrence test; full DSK difference and UFS consistency check.

**This is not a confirmed root-cause repair or hardware-accepted build.** Do not publish binaries or merge to `main` until physical behavior and guest network functionality are demonstrated. If the fault persists, the captured vector values can distinguish spurious XQ/QBA interrupts, corrupted SCB memory and other exception transitions without changing normal timing.


## FIX28 delivery correction — 2026-11-15

**Do not request additional ZIP-download/Windows execution retries from the user.** The user reported repeatedly that the operation cannot be completed. Examination of the actual `SIMHP4_R80_RC14_FIX28_SCB_PROVENANCE_BUILD_ONLY.zip` confirms ZIP CRC integrity but finds it is **not a standalone/portable build package**, contrary to an earlier statement:

- It contains only a small source-overlay plus Python staging/build scripts; it does **not** contain `project/CMakeLists.txt`, third-party compiled VAX `vax_cpu1.c`, or the full ESP-IDF project tree.
- `RUN_RELEASE_UI.py` hardcodes `F:\\build\\tmp` as its project output root and requires a completed, SHA-matching preexisting `F:\\build\\tmp\\s94` donor-source stage, a matching ELF/BIN/build log, and `G:\\esp-idf-5.5.4`.
- Its entry point explicitly requires Windows (`sys.platform == 'win32'`). It cannot be built by this assistant in the current container and cannot be represented as a completed native compile.
- It is **NO FLASH**, but its standalone usability was overstated. This is a handoff blocker, not evidence of user error.

**Revised plan:** Continue offline analysis from the already provided forensic source/ELF/BIN archive. Do not demand more local execution, additional diagnostics-only runs, or re-downloading the same file. Preserve exact known-good binaries and mark FIX28 as unbuilt/unverified until a genuinely reproducible build environment is available. Do not release or flash FIX28.

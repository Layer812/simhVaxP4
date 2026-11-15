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

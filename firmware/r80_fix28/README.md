# simhVaxP4 — R80 FIX28 source recovery branch

**State: SOURCE IMPORT PARTIAL — native ESP-IDF build NOT possible yet / NO FLASH.**

This directory contains the actual source files received in the R80 FIX28 candidate ZIP, not approximations reconstructed from firmware binaries. The seven files under `source/` and the five historical patch/staging files under `legacy_staging/` are SHA-256 pinned in [`SOURCE_STATUS.json`](SOURCE_STATUS.json). The source file Git blob hashes were checked against the local originals after upload.

## What has been published

- `source/v13_main.cpp` — VAX P4 host entry and FIX28 invalid-SCB reporting
- `source/simhp4_sim_console_esp_v14c.c` — SIMH console and guest input
- `source/simhp4_vax_terminal_r14f.cpp` — Tab5 VT100 terminal
- `source/simhp4_a164_keyboard.c` — keyboard
- `source/simhp4_wifi_settings.c` and `.h` — Wi-Fi settings
- `source/simhp4_wifi_overlay.cpp` — Wi-Fi overlay
- `legacy_staging/RUN_RELEASE_UI.py` — exact source-patch functions for QBA and invalid SCB; **historical donor-dependent script, not the Git build command**
- `legacy_staging/R80_REV9_BINARY_PATCH.py`, `R80_FIX11_FLASH_CORE.py`, `BASE_SOURCE_SHA256.json`, `PACKAGE_SHA256.json` — retained for provenance, not run by users as a flashing workflow
- `build_from_git.py` and `BUILD_FROM_GIT.cmd` — fail-closed native-build entry with no COM9 / flashing commands

## What is *not* present

The submitted ZIPs contained only an overlay of changed files, not the complete ESP-IDF `project/`, local `components/` or compiled VAX port sources. In particular, the original source of `simhp4_vax630_io_esp_r14e.c`, the custom headers, the actual build `CMakeLists.txt`, and the compiled SIMH fork are missing. **These are required for the firmware, and the repository does not yet pass the native-build gate.** See the exact list in [`SOURCE_STATUS.json`](SOURCE_STATUS.json).

The Open SIMH upstream source can be downloaded separately by Git from the pin `87eb7d5e96f9ce0ee6ac183e20160e5c486b0712`; however **upstream cannot be substituted unmodified** for the locally ported ESP32-P4 translation unit, simulator integration, or unknown custom components. Previous known-good BSD, LCD, Wi-Fi and QBA code may not be replaced arbitrarily.

## Build flow once all source has been published

```bat
REM No flashing, no direct access to the original Windows source tree:
git clone --branch work/r80-regression-handoff-20261115 https://github.com/Layer812/simhVaxP4.git
cd simhVaxP4
call firmware\r80_fix28\BUILD_FROM_GIT.cmd
```

This command first checks every published file against its recorded SHA-256 and refuses to compile when sources are missing. With the exact complete project present under `full_project/`, it creates a new isolated temporary stage, applies only the pinned FIX28 overlays and QBA/SCB patches and runs the local ESP-IDF compiler (5.5.4). APP/ELF identity and linked markers are checked. **It never flashes, changes OTA data, opens COM9 or writes a BSD DSK.** It is currently expected to stop with `INCOMPLETE_GIT_SOURCE` until the missing files are legitimately supplied.

### Provenance and license

These are development-source artifacts recovered from the user's submitted packages; the candidate has **not** passed a native ESP-IDF build or physical regression acceptance. Third-party SIMH code is not republished as if it were original authoring; preserve its upstream copyright/license when eventually importing a verified port. Do not add BSD disk images, ROMs, passwords, NVS dumps, Flash/NVS backups, or credentials to Git. Keep the public root README's no-firmware-release status in force.


## SHA-pinned import and clean Git build (new)

For the exact source-publication workflow, see [SOURCE_IMPORT_GUIDE.md](SOURCE_IMPORT_GUIDE.md). The new [import_full_source.py](import_full_source.py) does **not** manufacture missing custom VAX files: it checks the historical `F:\build\tmp\s94` source stage against seven exact SHA-256 baselines, screens out binary media and apparent credentials, and may push a complete original source tree only to this development branch. The [PUBLISH_AND_BUILD_FROM_GIT.cmd](PUBLISH_AND_BUILD_FROM_GIT.cmd) path clones a **second clean Git checkout** and runs build-only from that checkout after successful source import.

**Current state remains incomplete until that exact private/local source stage has actually been imported and the resulting Git checkout built.** The Git scripts and offline safety tests are published and checked, but neither a native build nor a hardware functional pass has been claimed.

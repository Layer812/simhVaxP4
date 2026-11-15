# simhVaxP4 — R80 FIX28 source recovery branch

**State: 1,777 source files imported to Git; native ESP-IDF FIX28 build PENDING / NO FLASH.**

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

## Complete source is now in Git

`full_project/` contains the SHA-256-pinned 2026-10-31 historical donor source: **1,777 files (228,831,654 bytes)**, including ESP-IDF `project/CMakeLists.txt`, `project/main/CMakeLists.txt`, custom `simhp4_vax630_io_esp_r14e.c`, all locally imported `components/`, and the pinned SIMH source in `third_party/`. The 29 MB M5GFX IPA C font is included exactly. See [`full_project/SOURCE_IMPORT_MANIFEST.json`](full_project/SOURCE_IMPORT_MANIFEST.json).

**Source publication is verified but native compilation is not yet verified.** The original SIMH port remains unmodified in Git; the FIX28 patches are applied only to a separate isolated build stage. Hardware testing remains pending.

## Git-only native build (source now published)

```bat
REM No flashing, no direct access to the original Windows source tree:
git clone --branch work/r80-regression-handoff-20261115 https://github.com/Layer812/simhVaxP4.git
cd simhVaxP4
call firmware\r80_fix28\BUILD_FROM_GIT.cmd
```

This command first checks every published file against its recorded SHA-256 and refuses to compile when sources are missing. With the exact complete project present under `full_project/`, it creates a new isolated temporary stage, applies only the pinned FIX28 overlays and QBA/SCB patches and runs the local ESP-IDF compiler (5.5.4). APP/ELF identity and linked markers are checked. **It never flashes, changes OTA data, opens COM9 or writes a BSD DSK.** The `INCOMPLETE_GIT_SOURCE` condition for the listed base files is now resolved in Git; other build or linkage issues have not been excluded by an actual native ESP-IDF run.

### Provenance and license

These are development-source artifacts recovered from the user's submitted packages; the candidate has **not** passed a native ESP-IDF build or physical regression acceptance. Third-party SIMH code is not republished as if it were original authoring; preserve its upstream copyright/license when eventually importing a verified port. Do not add BSD disk images, ROMs, passwords, NVS dumps, Flash/NVS backups, or credentials to Git. Keep the public root README's no-firmware-release status in force.


## SHA-pinned import and clean Git build (new)

For the exact source-publication workflow, see [SOURCE_IMPORT_GUIDE.md](SOURCE_IMPORT_GUIDE.md). The new [import_full_source.py](import_full_source.py) does **not** manufacture missing custom VAX files: it checks the historical `F:\build\tmp\s94` source stage against seven exact SHA-256 baselines, screens out binary media and apparent credentials, and may push a complete original source tree only to this development branch. The [PUBLISH_AND_BUILD_FROM_GIT.cmd](PUBLISH_AND_BUILD_FROM_GIT.cmd) path clones a **second clean Git checkout** and runs build-only from that checkout after successful source import.

**The exact local source stage has now been published and its Git manifest checked.** On repeated runs, `PUBLISH_AND_BUILD_FROM_GIT.cmd` skips the one-time importer if the import manifest is already committed; it performs a fresh Git clone then compiles. Native-build PASS and hardware PASS have not been claimed.


## FIX28 native compile failure — evidence required (2026-11-16)

The user's **actual Git-only build** reached `idf.py -B ...\\project\\build build` under ESP-IDF 5.5.4 / Python 3.14.2 but exited with code 2. The complete log is left at:
`C:\\Users\\fooba\\AppData\\Local\\Temp\\simhvaxp4_fix28_build_kw78ogvz\\BUILD.log`.

**Do not guess the compiler fault from return code 2 or trigger another full build just to get diagnostics.** The current failure requires the original `BUILD.log`, e.g. one read-only command:
```bat
powershell -NoProfile -Command "Get-Content 'C:\Users\fooba\AppData\Local\Temp\simhvaxp4_fix28_build_kw78ogvz\BUILD.log' -Tail 100"
```
The Git build driver now prints representative **first error** passages and copies the complete log to `F:\\build\\tmp\\SIMHP4_R80_FIX28_LAST_BUILD.log` on subsequent runs, without modifying the source, invoking COM9 or flashing. **Native build is still FAILED; no physical acceptance.**


## FIX28 first native build: ESP Component Manager long-path error (2026-11-16)

User-supplied BUILD.log proves that CMake stopped during the dependency download/extraction for espressif/esp_hosted==3.0.9, BEFORE MicroVAX C/C++ compilation. The default Windows Component Manager cache rooted under C:\Users\fooba\AppData\Local\Espressif\ComponentManager\Cache\ was too long for a deeply nested esp_hosted examples file, causing Python zipfile.extract FileNotFoundError.

The Git-only native-build driver now sets IDF_COMPONENT_CACHE_PATH=F:\build\tmp\r80cm in the CHILD idf.py process environment, after confirming the development volume exists. This environment override is officially supported by Espressif. No registry change, library deletion or emulator-source change; no Flash/COM9/DSK/NVS actions.

The original build log has been preserved at C:\Users\fooba\AppData\Local\Temp\simhvaxp4_fix28_build_kw78ogvz\BUILD.log. The driver also prints the first real build error and retains a full copy at F:\build\tmp\SIMHP4_R80_FIX28_LAST_BUILD.log on subsequent runs.

**Current state: Git source import PASS; ESP-IDF native build FAIL due to dependency extraction long path; corrected driver published but not yet run on Windows; hardware NOT tested.**


## FIX28 second native build: managed_components destination path corrected (2026-11-16)

The user's **second actual build log** confirms that the short Component Manager cache at F:\build\tmp\r80cm was active, but the independent native build *destination* still used the very long Windows default TEMP directory:
C:\Users\fooba\AppData\Local\Temp\simhvaxp4_fix28_build_a1btgg4i\project\managed_components\espressif__esp_hosted\...

Python Component Manager failed while copying the same deeply nested esp_hosted 3.0.9 example file with shutil.Error / WinError 3. This is not a MicroVAX C compiler error.

The build-only driver now also creates a uniquely named, isolated SHORT STAGING DIRECTORY at F:\build\tmp\b28_<random>\project, while retaining the separate short component cache and the full source/patch SHA gates. A preflight calculates the previously failing managed_components target path and refuses paths >=240 chars before starting the native build. Host-only PureWindowsPath verification of the exact failure path measured:
- Windows default TEMP build destination: **265 characters** (failed)
- New F:\build\tmp\b28_abcdefgh destination: **226 characters**
- Existing short component cache file: **224 characters**

No VAX/QBA, M5GFX/M5Unified, BSD, LCD, Wi-Fi, CMake or flash/disk source was changed by this remediation. No Windows long-path registry changes, removal of existing caches, or modification to the separate TAb500_Improve project. Actual ESP-IDF retest and physical acceptance remain pending.


## FIX28 source completeness gate before any further build (2026-11-16)

The third real Windows build passed the prior ESP Hosted cache/staging path problems and compiled to **step 1106/1306**, but then stopped while compiling \`M5Unified/src/utility/imu/BMI270_Class.cpp\`:

    fatal error: BMI270_config.inl: No such file or directory

This is not a VAX simulator regression. The historical source importer silently excluded the exact vendor \`.inl\` file, and the committed original \`full_project/SOURCE_IMPORT_MANIFEST.json\` confirms this. All **833** excluded-file records were reviewed by extension: the **only** excluded C/C++ compile-input extension (C/C++/headers/inc/inl/etc.) is \`components/M5Unified/src/utility/imu/BMI270_config.inl\`. The others are largely old backups, unrelated simulator examples, binary media and metadata. Do not replace M5Unified with a different version or create substitute IMU arrays.

**New Git-first fix (published, native retest not yet done):**
- \`source_completion_gate.py --publish\` first SHA-256 checks all **1,777 original Git source files**, validates original seven source baselines and manifest, finds the unique exact matching historical donor, and copies **only the exact omitted** \`BMI270_config.inl\` into the existing development branch, SHA-256 records its bytes in \`full_project/SOURCE_COMPLETION_MANIFEST.json\`, commits and pushes. It does not replace any existing source.
- \`PUBLISH_AND_BUILD_FROM_GIT.cmd\` invokes this source gate **before** cloning or compiling. On a partial source/error it stops; successful one-time import is idempotent.
- The independent \`build_from_git.py\` also invokes \`source_completion_gate.py --verify\` **before** any source copying or ESP-IDF/CMake call. It verifies every original source file's hash and exact recovered source bytes, so direct Git-only builds cannot proceed with missing files.
- Host fixture unit tests pass for pristine source, exact donor-byte publication, modified original source rejection, modified recovered source rejection, and an extra excluded compile-input extension rejection. The two gate/test Git files were verified byte-exact by Git blob hashes.

The script cannot read the user's F:\ drive from ChatGPT. On the user's machine use the existing Git checkout **once** for the source repair and clean Git build:

    git -C F:\build\tmp\simhVaxP4_R80_GIT_SOURCE pull --ff-only && call F:\build\tmp\simhVaxP4_R80_GIT_SOURCE\firmware\r80_fix28\PUBLISH_AND_BUILD_FROM_GIT.cmd

This command first verifies/repairs the source; **native build occurs only if audit + Git source publication succeed**. It then clones a new clean Git tree and re-verifies before compiling. Do not run the direct BUILD_FROM_GIT.cmd against the older incomplete checkout before this once-only source publication.

Still **NO FLASH, NO COM9, NO DSK/NVS changes, main unchanged**, no native compilation or hardware PASS claimed.

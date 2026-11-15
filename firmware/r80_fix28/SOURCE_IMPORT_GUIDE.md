# R80 source recovery / Git-only build

## Status

The upstream repository currently holds the exact FIX28 overlay and its guarded builder. The **full ESP-IDF base is not in Git yet**. The only known potential copy is the SHA-pinned, completed historical base stage at `F:\build\tmp\s94` on the user's Windows machine. ChatGPT cannot read that drive directly.

## One-time Git source import (development branch only)

The repository includes these two scripts:

- `PUBLISH_SOURCE_FROM_LOCAL.cmd`: inspect the historical local donor, compare all **seven** original R80 files with `BASE_SOURCE_SHA256.json`, select a unique exact-match source stage, import `project`, `components` and `third_party` into Git, filter DSK/Flash binaries, screen for apparent credentials and push only to the development branch.
- `PUBLISH_AND_BUILD_FROM_GIT.cmd`: run the same one-time import, clone a **new clean checkout from GitHub**, and invoke the **build-only** `BUILD_FROM_GIT.cmd` on the clean checkout. No `idf.py flash`, esptool, COM9 or SD operations.

In Windows CMD, from a location with Git and Python installed:

```bat
mkdir F:\build\tmp 2>nul
git clone --single-branch --branch work/r80-regression-handoff-20261115 https://github.com/Layer812/simhVaxP4.git F:\build\tmp\simhVaxP4_R80_GIT_SOURCE
call F:\build\tmp\simhVaxP4_R80_GIT_SOURCE\firmware\r80_fix28\PUBLISH_AND_BUILD_FROM_GIT.cmd
```

If an existing checkout is present, **do not overwrite it**: use its Git branch directly when clean and up-to-date.

The repository is public. The import script fails closed on source hash mismatch, secret patterns, symbolic links and foreign Git remotes. This automated check is **not a guarantee that all private information is detectable**. Never add passwords, personal Wi-Fi configuration, SD images, NVS, device dumps, ROMs or generated firmware. Third-party copyrights and licenses must be maintained as in the original source tree.

## What the result means

- Git source upload PASS does **not** establish a working firmware or a compiled ESP-IDF image.
- Native build PASS, if achieved, is not a hardware acceptance result. Readback-only image verification and the BSD + display + Wi-Fi functional regression test are separate release gates.
- The source importer does not invent or regenerate unique VAX/QBA platform code. If the exact `s94` donor is absent or fails verification, it stops and reports the missing dependency; no substitute is fetched from the PDP-7 project or the public upstream VAX simulator.
- `SOURCE_STATUS.json` records the initial missing-source situation. `full_project/SOURCE_IMPORT_MANIFEST.json` is produced only after a successful import.


## Existing-checkout restart after M5GFX font size stop (2026-11-15)

The reported console showed `PINNED BASE VERIFIED: F:\build\tmp\s94\20261031_010409_742208`, followed by `file exceeds 8 MiB review limit: components/M5GFX/src/lgfx/Fonts/IPA/lgfx_font_japan.c`. This means donor SHA verification **passed** and the previous importer blocked a large *upstream M5GFX source file*, not the MicroVAX integration.

The importer now allows text C/C++/header source under `components/M5GFX/` up to **96 MiB per file** (below GitHub's 100 MiB per-blob ceiling). It copies and SHA-256-hashes the exact bytes, still checks for NUL and secret signatures, and retains an 8 MiB limit for other sources. The total source review budget was adjusted to 640 MiB to accommodate vendor font sources. The host-side test includes a synthetic 9 MiB M5GFX font accepted intact, a same-size first-party C source rejected, and a credentials rejection.

**Do not rerun `git clone` to the same existing folder.** Update the already-cloned checkout, then invoke the same one-shot source publication and fresh Git-only native compilation:

```bat
git -C F:\build\tmp\simhVaxP4_R80_GIT_SOURCE pull --ff-only && call F:\build\tmp\simhVaxP4_R80_GIT_SOURCE\firmware\r80_fix28\PUBLISH_AND_BUILD_FROM_GIT.cmd
```

The first prior failed run performed no commit or source push and left `full_project/` absent; the existing checkout is the expected restart location. These commands do not operate on COM9, Flash or DSK. A subsequent source security or native-compile failure must be reported as such; do not suppress it by copying unverified files.

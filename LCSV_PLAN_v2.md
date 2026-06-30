# LCSV Prism — Implementation Plan v2

Fork of PrismLauncher. Adds LCSV as a modpack provider: players browse packs hosted on Tower, install, and get selective, orphan-aware auto-updates before each launch.

This revises v1 (`LCSV_PLAN.md`). It keeps the same architecture but fixes eight design gaps found during review. Sections that changed are marked **[FIX]**.

---

## Current State (as of this revision)

- **Phase 2 done** — `launcher/modplatform/lcsv/LCSVPackIndex.{h,cpp}` parses `distribution.json`. Wired into `launcher/CMakeLists.txt` (`LCSV_SOURCES`).
- **Phase 3 done** — `launcher/ui/pages/modplatform/lcsv/LCSVPage.*` + `LCSVListModel.*` render the "LCSV Servers" tab; appended in `NewInstanceDialog.cpp`. Icon metacache `LCSVPacks` registered in `Application.cpp`.
- **Phase 4 NOT done** — page currently installs via raw `InstanceImportTask(packUrl)`. No `.lcsv-meta.json` is written, so the instance is invisible to the update system. **This is the next task and a hard blocker for Phases 5–6.**
- **Phases 5–7 NOT done.** Branding not started (window title still "Prism").

---

## Distribution File Format

### `distribution.json` — Pack Index **[FIX #8: format guard, #2: manifest sidecar]**

Served at `https://mc.lcsv.com.br/distribution.json`.

```json
{
  "formatVersion": 1,
  "packs": [
    {
      "id": "gtnh",
      "name": "GregTech: New Horizons",
      "version": "2.6.2",
      "description": "The hardest modpack known to man.",
      "icon": "https://mc.lcsv.com.br/icons/gtnh.png",
      "packUrl": "https://mc.lcsv.com.br/packs/gtnh-2.6.2.mrpack",
      "manifestUrl": "https://mc.lcsv.com.br/packs/gtnh-2.6.2.index.json",
      "serverAddress": "gtnh.lcsv.com.br",
      "minecraftVersion": "1.7.10",
      "modloader": "forge"
    }
  ]
}
```

- **`formatVersion`** — integer. The launcher defines `LCSV_SUPPORTED_FORMAT = 1`. If `distribution.json` advertises a higher number, the launcher refuses to parse packs and shows "Your launcher is out of date — please update." This prevents old clients silently misreading a future schema.
- **`manifestUrl`** — **[FIX #2]** the raw `modrinth.index.json` for this version, served as a plain file alongside the `.mrpack`. The update check fetches *this*, never the zip. (`.mrpack` is a ZIP; you cannot fetch its inner JSON over HTTP. The sidecar makes update checks a single cheap JSON GET.) The install flow still downloads the full `.mrpack`.

### `.mrpack` per pack — File Manifest

Standard Modrinth format (see v1 for the example). **Rule unchanged:** every real file lives in `files[]` with a Tower URL and SHA-512. The `overrides/` folder is empty or near-empty — we need a hash on everything so the update system can track it. Configs go in `files[]`, not `overrides/`.

#### The zip is an install bootstrap only — never re-fetched on update

The `.mrpack` zip does **not** contain mods or configs. It holds only `modrinth.index.json` (the manifest) and the `overrides/` folder. Every real file is external, fetched individually from `mc.lcsv.com.br/files/{sha512}`. So the zip is a few KB, never gigabytes.

- **Install:** download the tiny zip once → Prism reads MC version + modloader from it and sets up the loader component → then fetches each `files[]` entry by hash. The zip exists *only* to bootstrap loader setup via `ModrinthCreationTask`.
- **Update:** never downloads the zip. It fetches the bare `manifestUrl` sidecar (JSON), diffs hashes, and downloads only the changed files — one file edited means one file downloaded. This is the NeoNebula-style per-file delta.
- **Hard rule — `overrides/` MUST stay empty.** If any file is shipped inside `overrides/` (inside the zip), editing it forces a re-zip on the server *and* makes the client re-download the whole zip — defeating the delta entirely. Everything goes in `files[]` so it has a hash and an external URL.

### Tower File Storage Structure **[FIX #2]**

```
/mnt/user/appdata/nginx/mc/
  distribution.json
  packs/
    gtnh-2.6.2.mrpack          ← full pack (zip: modrinth.index.json + tiny overrides/)
    gtnh-2.6.2.index.json      ← NEW: bare manifest sidecar, byte-identical to the zip's inner json
    survival-1.0.4.mrpack
    survival-1.0.4.index.json
  files/
    {sha512-hash}              ← deduped content store
  icons/
    gtnh.png
```

Nginx still serves everything statically. No server-side logic added.

---

## Instance Metadata **[FIX #1, #3, #5]**

After install, write `.lcsv-meta.json` to the instance root (next to `mmc-pack.json`). This file is the source of truth for "is this an LCSV instance" and the cache that makes updates cheap.

```json
{
  "schema": 1,
  "packId": "gtnh",
  "packVersion": "2.6.2",
  "distributionUrl": "https://mc.lcsv.com.br/distribution.json",
  "manifestUrl": "https://mc.lcsv.com.br/packs/gtnh-2.6.2.index.json",
  "userProtectedFiles": [
    "options.txt",
    "servers.dat",
    "keybinds.txt",
    "config/iris.properties",
    "config/oculus.properties",
    "shaderpacks/"
  ],
  "userKeptFiles": {
    "config/jei/jei.json": "kept-always"
  },
  "installedFiles": [
    { "path": "mods/gregtech-5.09.37.jar", "sha512": "deadbeef...", "size": 12345678, "mtime": 1750000000 }
  ],
  "lastChecked": "2026-06-28T00:00:00Z"
}
```

- **`installedFiles`** **[FIX #3 + #5]** — snapshot of the manifest that produced the current install: every tracked path with its hash, size, and on-disk mtime at write time. Powers two things:
  - **Orphan deletion (#3):** `orphans = installedFiles.paths − newManifest.paths`. Anything in the old install but not the new version gets removed (unless protected/kept), so deleted mods don't linger and crash the game.
  - **Hash fast-path (#5):** on update, a local file is only re-hashed if its current `size`/`mtime` differs from the cached entry. Unchanged files are compared by cached hash against the new manifest hash. Big packs update without rehashing thousands of untouched files.
- **`userProtectedFiles` / `userKeptFiles` matching** **[FIX #4]** — entries match by this rule, not string equality:
  - exact: `entry == path`
  - directory prefix: `entry` ends in `/` and `path` starts with `entry` (e.g. `shaderpacks/` protects everything under it).

---

## Update Strategy **[FIX #2, #3, #4, #5, #6]**

**Hash-based, per-file, orphan-aware, on every launch — but cheap in the common case.**

### Trigger seam **[FIX #6 — grounded in real code]**

`LaunchController` is itself a `Task`. Its `launchInstance()` (`launcher/LaunchController.cpp:368`) builds the real launch task at line 379 (`m_instance->createLaunchTask(...)`). LCSV update slots in **right before** that call — *not* as a custom `LaunchStep` inside the Minecraft launch sequence (which would force a modal dialog onto the launch controller thread).

Flow inside `launchInstance()`:

```
if (instanceRoot has .lcsv-meta.json) and (not already updated this launch):
    run LCSVUpdateTask (async Task, parented to m_parentWidget)
      → on succeeded → continue to createLaunchTask()  (existing line 379)
      → on user-abort → emit launch abort (reuse LaunchDecision::Abort path)
else:
    proceed as today
```

This reuses the existing `LaunchDecision::{Continue,Abort}` machinery the controller already uses for account/profile dialogs (see lines 298/334). The conflict dialog runs on the GUI thread via the parent widget; the launch task is only built after the update resolves.

### LCSVUpdateTask logic

```
load .lcsv-meta.json
fetch distribution.json (5s timeout; on failure → skip update, launch as-is)
  guard formatVersion <= LCSV_SUPPORTED_FORMAT
find our packId → compare distribution.version vs meta.packVersion
  same  → done (no hashing, no manifest fetch) — the cheap common case
  diff  → fetch the DISTRIBUTION entry's manifestUrl (new version, bare index.json, one GET — FIX #2)
          (meta.manifestUrl records the currently-installed version; it is NOT what we fetch here)

build three sets from the new manifest vs disk + meta.installedFiles:
  for each file in new manifest:
      if matchesAny(userProtectedFiles, path) → skip
      if matchesAny(userKeptFiles.keys, path) → skip
      localHash = cached hash if size+mtime unchanged else sha512(disk)   (FIX #5)
      if local missing            → queue download
      elif localHash != manifest  → if path starts with "config/" → conflict list
                                     else                          → queue download
  orphans = meta.installedFiles.paths − newManifest.paths           (FIX #3)
      for each orphan: if not protected/kept → queue delete

if conflict list not empty:
    show LCSVUpdateDialog (per file: [Download new] [Keep mine] [Keep mine, don't ask again])
      "Keep mine, don't ask again" → add to userKeptFiles

apply: delete orphans, download queued files
rewrite .lcsv-meta.json: new packVersion, new manifestUrl, fresh installedFiles snapshot,
                         merged userKeptFiles, lastChecked = now
continue launch
```

### Config-conflict heuristic **[FIX #4]**

A changed file is a "conflict" (prompt the user) only if its path starts with `config/` and it is not protected/kept. Everything else (`mods/`, libraries, `resourcepacks/`, `kubejs/`, …) updates silently. `kubejs/` is treated as code, not user config — silent — matching v1's intent.

### Force Update

Instance context-menu action "Force update LCSV pack": ignores `userProtectedFiles`/`userKeptFiles` and the version-equality shortcut; re-downloads the full latest manifest including `options.txt`.

---

## C++ Code to Write

### `launcher/modplatform/lcsv/`

| File | Status | Purpose |
|------|--------|---------|
| `LCSVPackIndex.{h,cpp}` | **done** | Parse `distribution.json`. **Add:** `formatVersion` guard (#8), read `manifestUrl` (#2). |
| `LCSVInstallTask.{h,cpp}` | **new (Phase 4)** | Wrap `InstanceImportTask` for the `.mrpack`; on success write `.lcsv-meta.json` incl. `installedFiles` snapshot. |
| `LCSVMeta.{h,cpp}` | **new (Phase 5)** | Load/save `.lcsv-meta.json`; `matchesAny()` path-rule helper (#4); hash fast-path using cached size/mtime (#5). (Was `LCSVUserFileManager` in v1.) |
| `LCSVUpdateTask.{h,cpp}` | **new (Phase 5)** | Fetch sidecar manifest, build download+delete queues, orphan diff (#3), drive conflict dialog. |
| `LCSVExportTask.{h,cpp}` | **new (Phase 6)** | Custom manifest builder (see Phase 6). |

### `launcher/ui/pages/modplatform/lcsv/`

| File | Status |
|------|--------|
| `LCSVPage.*`, `LCSVListModel.*` | **done** — but switch install from `InstanceImportTask` to `LCSVInstallTask` in Phase 4. |

### `launcher/ui/dialogs/`

| File | Status | Purpose |
|------|--------|---------|
| `LCSVUpdateDialog.{h,cpp,ui}` | new (Phase 5) | Per-file conflict resolution. |
| `LCSVExportDialog.{h,cpp,ui}` | new (Phase 6) | Developer export form. |

### Existing files to modify

| File | Change |
|------|--------|
| `LCSVPage.cpp` | Phase 4: install via `LCSVInstallTask`, not `InstanceImportTask`. |
| `launcher/LaunchController.cpp` (~line 368/379) | Phase 5: run `LCSVUpdateTask` before `createLaunchTask` when `.lcsv-meta.json` exists (#6). |
| `launcher/CMakeLists.txt` | add each new source to `LCSV_SOURCES`. |
| Settings (Advanced page) + instance context menu | Phase 6: "LCSV Developer Mode" toggle; "Export as LCSV Pack" (dev-mode only). |

---

## Implementation Phases (revised)

### Phase 4 — Install Task + Metadata  *(next; unblocks everything)*
1. `LCSVInstallTask`: own an `InstanceImportTask(packUrl)`, forward progress; on success resolve the created `InstancePtr`, fetch the distribution entry's `manifestUrl`, and write `.lcsv-meta.json` with a full `installedFiles` snapshot (set each entry's `mtime` from the freshly-downloaded file on disk).
2. **Verify first — how does `InstanceImportTask` surface the final instance dir?** It stages into a temp dir, then `InstanceList::commitStagedInstance` returns the new instance id. Confirm whether the dialog flow (`MainWindow`/`NewInstanceDialog` accept path) gives us that id, or whether `LCSVInstallTask` must look the instance up by name in `InstanceList` after commit. Pick the mechanism before writing the meta-write step — this is the one unknown in Phase 4.
3. Switch `LCSVPage::suggestCurrent()` to hand the dialog an `LCSVInstallTask` instead of a bare `InstanceImportTask`.
4. **Done when:** installing a pack yields a working instance whose root contains a correct `.lcsv-meta.json`.

### Phase 5 — Update + File Protection
1. `LCSVMeta` (load/save, `matchesAny`, hash fast-path).
2. `LCSVUpdateTask` (version check → sidecar fetch → diff → orphan delete → conflict dialog → apply → rewrite meta).
3. `LCSVUpdateDialog`.
4. Hook into `LaunchController::launchInstance()` before line 379.
5. **Done when:** no-change launch is silent and does zero hashing; a changed mod downloads only that file; a changed config prompts and "Keep mine" leaves it untouched; a removed mod is deleted; force-update re-pulls everything.

### Phase 6 — Developer Export
1. `LCSVExportTask`: walk the instance dir (ignore `logs/`, `.lcsv-meta.json`, crash reports, etc.), SHA-512 each file (reuse `FileSystem` hashing, **not** `ExportInstanceTask` wholesale — its overrides/ behavior is wrong for us). For each file: emit a `files[]` entry with `downloads:["…/files/{sha512}"]`, copy the file to `output/files/{sha512}`. Emit `.mrpack` (zip with the index + empty overrides), the bare `index.json` sidecar (#2), and `distribution-entry.json`.
2. `LCSVExportDialog` + dev-mode toggle + context-menu action.
3. Deploy via the two `rsync --ignore-existing` commands (unchanged from v1).
4. **Done when:** export → rsync → another client installs/updates the new version end to end.

### Phase 1.5 / 7 — Branding + Polish
Window title/app name/about/update URLs → LCSV; verify "Add Offline" works; optionally hide Modrinth/CurseForge browsers; Linux + Windows release builds.

---

## Open Questions / Later
- Launcher self-update channel (reuse Prism's updater? new URLs).
- A tiny Tower-side script to merge `distribution-entry.json` into `distribution.json` (replaces manual JSON editing).
- Discord Rich Presence with active pack name.
- Optional server-whitelist gate before download.
- Cross-thread `qRegisterMetaType<LCSV::IndexedPack>()` if the struct ever travels through a queued connection (currently only direct `QVariant` use, so not required yet).

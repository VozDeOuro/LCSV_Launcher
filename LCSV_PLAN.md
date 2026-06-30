# LCSV Prism — Implementation Plan

Fork of PrismLauncher. Adds LCSV as a modpack provider: players browse packs hosted on Tower, install, and get selective auto-updates before each launch.

---

## Distribution File Format

### Why `.mrpack` (Modrinth pack format)

PrismLauncher already parses, downloads, and installs `.mrpack` files natively via `ModrinthCreationTask`. We reuse all of that — no custom downloader needed.

`.mrpack` supports external file URLs, so mods can be hosted anywhere and referenced by hash. Same mod in two packs = same SHA-512 = same Tower URL = **stored once on server**.

### File origin doesn't matter — Tower mirrors everything

The `.mrpack` `files[].downloads` field is just a list of URLs. Prism tries each in order. They can point anywhere:

| Source | Approach |
|--------|----------|
| Modrinth mod | Reference Modrinth URL directly, OR mirror on Tower (preferred for reliability) |
| CurseForge mod | Download it → upload to Tower → use Tower URL |
| Custom/private mod | Upload to Tower → use Tower URL |
| Config files | Upload to Tower → use Tower URL (gives us hashes for update comparison) |
| Shaders / resourcepacks | Upload to Tower → use Tower URL |

**Rule: everything goes through Tower.** No runtime dependency on Modrinth or CurseForge availability. Players always download from `mc.lcsv.com.br`. Deduplication is automatic — same SHA-512 = same file = same URL = stored once.

Config files go in `files[]` with Tower URLs (not in the `overrides/` bundle). This gives us per-file hashes so the update system can detect which configs changed vs which the user edited.

---

### `distribution.json` — Pack Index

Served at `https://mc.lcsv.com.br/distribution.json`.

```json
{
  "formatVersion": "1",
  "packs": [
    {
      "id": "gtnh",
      "name": "GregTech: New Horizons",
      "version": "2.6.2",
      "description": "The hardest modpack known to man.",
      "icon": "https://mc.lcsv.com.br/icons/gtnh.png",
      "packUrl": "https://mc.lcsv.com.br/packs/gtnh-2.6.2.mrpack",
      "serverAddress": "gtnh.lcsv.com.br",
      "minecraftVersion": "1.7.10",
      "modloader": "forge"
    },
    {
      "id": "custom-survival",
      "name": "LCSV Survival",
      "version": "1.0.4",
      "description": "Custom survival pack.",
      "icon": "https://mc.lcsv.com.br/icons/survival.png",
      "packUrl": "https://mc.lcsv.com.br/packs/survival-1.0.4.mrpack",
      "serverAddress": "mc.lcsv.com.br",
      "minecraftVersion": "1.21.1",
      "modloader": "neoforge"
    }
  ]
}
```

---

### `.mrpack` per pack — File Manifest

Standard Modrinth format. Files point to Tower's content-addressable storage.

```json
{
  "formatVersion": 1,
  "game": "minecraft",
  "versionId": "2.6.2",
  "name": "GregTech: New Horizons",
  "files": [
    {
      "path": "mods/gregtech-5.09.37.jar",
      "hashes": {
        "sha1": "aabbcc...",
        "sha512": "deadbeef..."
      },
      "downloads": ["https://mc.lcsv.com.br/files/deadbeef..."],
      "fileSize": 12345678,
      "env": { "client": "required", "server": "unsupported" }
    }
  ],
  "dependencies": {
    "minecraft": "1.7.10",
    "forge": "10.13.4.1614"
  }
}
```

---

### Tower File Storage Structure

```
/mnt/user/appdata/nginx/mc/
  distribution.json            ← pack index (updated manually or via lcsv-pack-tool)
  packs/
    gtnh-2.6.2.mrpack          ← pack manifests (zip containing modrinth.index.json + overrides/)
    survival-1.0.4.mrpack
  files/
    {sha512-hash}              ← individual files stored by hash (deduped)
  icons/
    gtnh.png
    survival.png
```

Nginx serves everything statically. Two packs sharing a mod → same SHA-512 → same file → same URL → stored once.

---

### Developer Mode Export (in-launcher, no CLI needed)

Toggle **"LCSV Developer Mode"** in launcher Settings → Advanced.

When enabled, right-clicking any instance shows **"Export as LCSV Pack"**:

1. Small dialog: pack id, version, description, server address, output folder
2. Launcher runs export task:
   - Reuses existing `ExportInstanceTask` logic (already hashes files for mrpack)
   - Rewrites `downloads[]` URLs to `https://mc.lcsv.com.br/files/{sha512}` (no Modrinth/CurseForge dependency)
   - All files go in `files[]` array — **no `overrides/` folder** (need hashes on everything for update tracking)
   - Copies each file to `output/files/{sha512}` (dedup structure, rsync-ready)
3. Output:

```
lcsv-export/
  files/
    {sha512}              ← every file stored by hash, ready to rsync
    {sha512}
  packs/
    gtnh-2.6.2.mrpack    ← manifest with mc.lcsv.com.br/files/{hash} URLs
  distribution-entry.json ← paste into distribution.json on Tower
```

4. Developer rsyncs to Tower:

```bash
rsync -av --ignore-existing lcsv-export/files/ tower:/mnt/user/appdata/nginx/mc/files/
rsync -av lcsv-export/packs/ tower:/mnt/user/appdata/nginx/mc/packs/
# manually merge distribution-entry.json into distribution.json on Tower
```

`--ignore-existing` = dedup: files already on Tower are skipped. No API, no auth, no Tower changes needed. Tower stays pure static nginx.

---

## Instance Metadata

After install, write `.lcsv-meta.json` to instance root (next to `mmc-pack.json`):

```json
{
  "packId": "gtnh",
  "packVersion": "2.6.2",
  "distributionUrl": "https://mc.lcsv.com.br/distribution.json",
  "userProtectedFiles": [
    "options.txt",
    "servers.dat",
    "keybinds.txt",
    "config/iris.properties",
    "config/oculus.properties",
    "shaderpacks/"
  ],
  "userKeptFiles": {
    "config/jei.cfg": "kept-always"
  },
  "lastChecked": "2026-06-28T00:00:00Z"
}
```

`userKeptFiles`: files the user said "keep mine, don't ask again" on a previous update prompt.

---

## Update Strategy

**Hash-based, per-file, on every launch.**

### Flow (pre-launch hook)

```
Launch LCSV instance
  → load .lcsv-meta.json
  → fetch distribution.json (with 5s timeout, skip on failure)
  → compare packVersion vs installed version
    → same: skip (or check anyway if force-update flag set)
    → different: fetch new .mrpack manifest (JSON only, no files yet)
  → for each file in new manifest:
      if path in userProtectedFiles → skip
      if path in userKeptFiles[*] → skip
      if local file missing → queue download
      if SHA-512(local) != manifest hash → queue for update
        if path is a config file → add to "conflict list"
        else → queue for silent download
  → if conflict list not empty:
      show LCSVUpdateDialog:
        "X config files changed. For each:"
          [Download new] [Keep mine] [Keep mine, don't ask again]
  → download queued files
  → update .lcsv-meta.json with new version + any new userKeptFiles
  → launch
```

### Force Update

Button in instance context menu: "Force update LCSV pack". Redownloads everything including protected files.

### Config file detection heuristic

Files matching `config/**` that are NOT in `userProtectedFiles` → treated as conflict candidates (prompt user). Everything else (mods, libraries, resourcepacks, kubejs, etc.) → silent update.

---

## Cracked / Offline Auth

PrismLauncher already supports offline accounts ("Add Offline" in accounts). We just keep that and make sure it's not hidden. No extra work needed.

---

## C++ Code to Write

### New directory: `launcher/modplatform/lcsv/`

| File | Purpose |
|------|---------|
| `LCSVPackIndex.h/.cpp` | Parse `distribution.json`, store pack list |
| `LCSVInstallTask.h/.cpp` | Download `.mrpack`, delegate to `ModrinthCreationTask`, write `.lcsv-meta.json` |
| `LCSVUpdateTask.h/.cpp` | Pre-launch hash diff + selective file download |
| `LCSVUserFileManager.h/.cpp` | Load/save `.lcsv-meta.json`, decide which files to skip/prompt |

### New directory: `launcher/ui/pages/modplatform/lcsv/`

| File | Purpose |
|------|---------|
| `LCSVPage.h/.cpp/.ui` | Pack browser: list with icons, name, version, description, Install button |

### New dialog: `launcher/ui/dialogs/LCSVUpdateDialog.h/.cpp/.ui`

Shows conflicting config files, per-file choices: Download new / Keep mine / Keep mine always.

### New dialog: `launcher/ui/dialogs/LCSVExportDialog.h/.cpp/.ui`

Developer mode only. Fields: pack id, name, version, description, server address, output folder picker. Runs `LCSVExportTask`.

### Existing files to modify

| File | Change |
|------|--------|
| `launcher/ui/dialogs/NewInstanceDialog.cpp` | Add LCSV tab to instance creation dialog |
| `launcher/ui/dialogs/NewInstanceDialog.ui` | Add LCSV tab widget |
| `launcher/CMakeLists.txt` | Add all new source files |
| Instance launch sequence | Add pre-launch LCSV update check hook |

---

## UI Flow

### Install

```
Add Instance
  └── [LCSV Servers] tab
        Fetches distribution.json on open
        Shows: [icon] Pack Name  v2.6.2  "Description..."
                                          [Install]
        Click Install →
          Download .mrpack
          Run ModrinthCreationTask (existing code handles all downloads)
          Write .lcsv-meta.json
          Instance appears in main list tagged as "LCSV"
```

### Launch

```
Double-click LCSV instance
  → LCSVUpdateTask runs (silent if no changes)
  → If changes: shows update dialog
  → Launch Minecraft
```

---

## Implementation Phases

---

### Phase 1 — Build Environment + First Compile

Goal: get LCSV Prism building locally before touching any logic.

1. Install deps: `cmake`, `qt6`, `ninja`, `java`
2. `cmake -B build -G Ninja` from repo root
3. `ninja -C build` — confirm clean build of upstream Prism
4. Run it, verify basic Prism UI works
5. Initial branding: rename window title to "LCSV Launcher", swap app icon

**Done when:** launcher opens with LCSV name, no upstream Prism build errors.

---

### Phase 2 — Distribution Index Parsing

Goal: C++ struct that fetches and parses `distribution.json`.

Files to create:
- `launcher/modplatform/lcsv/LCSVPackIndex.h` — structs: `LCSVDistribution`, `LCSVPack`
- `launcher/modplatform/lcsv/LCSVPackIndex.cpp` — fetch URL, parse JSON into structs
- Add to `CMakeLists.txt`

Test with local mock JSON if Tower isn't set up yet. Debug-print pack names on fetch.

**Done when:** pack list parses without crash.

---

### Phase 3 — LCSV Tab in Add Instance Dialog

Goal: players see LCSV pack list and can click Install.

Files to create:
- `launcher/ui/pages/modplatform/lcsv/LCSVPage.h/.cpp/.ui` — list widget, icon, name, version, description, Install button

Modify:
- `launcher/ui/dialogs/NewInstanceDialog.cpp/.ui` — add LCSV tab

**Done when:** tab loads packs from `distribution.json`, Install button present.

---

### Phase 4 — Install Task

Goal: clicking Install downloads and creates a working instance.

Files to create:
- `launcher/modplatform/lcsv/LCSVInstallTask.h/.cpp`
  - Downloads `.mrpack` from `packUrl`
  - Delegates to `ModrinthCreationTask` (handles all file downloads)
  - After success: writes `.lcsv-meta.json` to instance root

**Done when:** full install + Minecraft launch works end to end.

---

### Phase 5 — User File Protection + Update Task

Goal: pre-launch update check that skips protected files, prompts on config conflicts.

Files to create:
- `launcher/modplatform/lcsv/LCSVUserFileManager.h/.cpp` — load/save `.lcsv-meta.json`, query protected/kept files
- `launcher/modplatform/lcsv/LCSVUpdateTask.h/.cpp` — fetch manifest, hash diff, build download queue
- `launcher/ui/dialogs/LCSVUpdateDialog.h/.cpp/.ui` — per-file conflict prompt (Download new / Keep mine / Keep always)

Modify:
- Instance pre-launch hook → run `LCSVUpdateTask` if `.lcsv-meta.json` exists

Test scenarios:
- No changes → silent launch
- Mod changed → only that file downloads
- Config changed → prompt appears → "Keep mine" → file untouched
- Force update → everything redownloaded including options.txt

**Done when:** all three scenarios work correctly.

---

### Phase 6 — Developer Export Mode

Goal: pack maintainer exports instance → rsyncs to Tower → players get it.

Files to create:
- `launcher/modplatform/lcsv/LCSVExportTask.h/.cpp`
  - Reuse `ExportInstanceTask` hash logic
  - Rewrite all `downloads[]` URLs to `https://mc.lcsv.com.br/files/{sha512}`
  - Copy files to `output/files/{sha512}`
  - Write `.mrpack` + `distribution-entry.json`
- `launcher/ui/dialogs/LCSVExportDialog.h/.cpp/.ui` — fields: id, version, description, server address, output folder

Modify:
- Settings → Advanced: "LCSV Developer Mode" bool toggle
- Instance right-click menu: "Export as LCSV Pack" visible only in developer mode

Deploy:
```bash
rsync -av --ignore-existing lcsv-export/files/ tower:/mnt/user/appdata/nginx/mc/files/
rsync -av lcsv-export/packs/ tower:/mnt/user/appdata/nginx/mc/packs/
# merge distribution-entry.json into distribution.json manually
```

**Done when:** full developer → player loop works without any CLI tool.

---

### Phase 7 — Cracked Auth + Polish

1. Verify offline account ("Add Offline") works — likely needs no changes
2. Replace remaining Prism branding: about dialog, update URLs, window icons
3. Optionally hide Modrinth/CurseForge mod browser (irrelevant for LCSV players)
4. Linux build test → Windows cross-compile test

**Done when:** clean release builds on Linux + Windows, cracked login works.

---

## Open Questions / Later

- Auto-update for the launcher itself (GitHub releases via existing Prism mechanism?)
- Discord Rich Presence showing active pack name
- Small script to merge `distribution-entry.json` into `distribution.json` on Tower (avoid manual JSON editing)
- Server whitelist check before pack download

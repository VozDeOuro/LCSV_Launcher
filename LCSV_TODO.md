# LCSV Prism — Implementation TODO

Build checklist derived from `LCSV_PLAN_v2.md`. Each task names the file and what existing code to reuse, so it can be implemented without re-deciding design. Read `LCSV_PLAN_v2.md` for the *why* behind each item.

## Ground rules (apply to every file)

- Every new `.cpp` / `.h` / `.ui` must be added to `LCSV_SOURCES` in `launcher/CMakeLists.txt` (around line 583).
- Every new file starts with the SPDX header used by the existing LCSV files:
  ```cpp
  // SPDX-License-Identifier: GPL-3.0-only
  /*
   * LCSV Launcher
   * Copyright (C) 2026 LCSV Contributors
   *
   * This program is free software: you can redistribute it and/or modify
   * it under the terms of the GNU General Public License as published by
   * the Free Software Foundation, version 3.
   */
  ```
- Style: members `m_camelCase`, types `PascalCase`, functions `camelCase`, constants `SCREAMING_SNAKE_CASE`. clang-format before commit.
- Commits: `git commit -s`, and add `Assisted-by: Claude:claude-<model>` (do NOT add `Signed-off-by` as an AI).

## Already done (do not redo)

- `launcher/modplatform/lcsv/LCSVPackIndex.{h,cpp}` — parses `distribution.json`.
- `launcher/ui/pages/modplatform/lcsv/LCSVPage.*` + `LCSVListModel.*` — "LCSV Servers" tab.
- Wired into `NewInstanceDialog.cpp` and `Application.cpp` (`LCSVPacks` metacache).

---

## Phase 4 — Install + Metadata  (BLOCKER — do first)

### 4.0 Resolve the one unknown (read-only investigation, no code yet)
- Read `launcher/InstanceImportTask.cpp`. Find how the created instance is reported: look for `InstanceList::commitStagedInstance` and any exposed instance id / final path.
- Confirm how the `NewInstanceDialog` accept flow obtains the new `InstancePtr`.
- **Output a decision:** does the task expose the instance id, or must `LCSVInstallTask` look the instance up by name in `InstanceList` after commit? Pick one before writing 4.2.

### 4.1 `launcher/modplatform/lcsv/LCSVMeta.{h,cpp}`  (new)
- Struct mirroring `.lcsv-meta.json`: `schema`, `packId`, `packVersion`, `distributionUrl`, `manifestUrl`, `userProtectedFiles` (`QStringList`), `userKeptFiles` (`QMap<QString,QString>`), `installedFiles` (`QList` of `{ QString path; QString sha512; qint64 size; qint64 mtime; }`), `lastChecked`.
- `bool load(const QString& instanceRoot)` / `bool save(const QString& instanceRoot) const` using `QJsonDocument` (file = `instanceRoot + "/.lcsv-meta.json"`).
- `static bool matchesAny(const QStringList& entries, const QString& path)` — true if `entry == path`, OR `entry.endsWith('/') && path.startsWith(entry)`.

### 4.2 `launcher/modplatform/lcsv/LCSVInstallTask.{h,cpp}`  (new)
- Subclass `Task`. Constructor takes the selected `LCSV::IndexedPack`.
- Owns an `InstanceImportTask(QUrl(pack.packUrl))`. Forward its `progress` / `status` / `failed` signals.
- On import `succeeded`:
  1. Resolve the instance dir (mechanism from 4.0).
  2. GET `pack.manifestUrl` via `NetJob` + `Net::ApiDownload::makeByteArray` (same pattern as `LCSVListModel::request`).
  3. Parse `files[]` → build `installedFiles`; for each, read on-disk `size` + `mtime` with `QFileInfo` on the downloaded file.
  4. `LCSVMeta::save` to the instance root.
  5. emit `succeeded`.

### 4.3 `launcher/ui/pages/modplatform/lcsv/LCSVPage.cpp`  (edit `suggestCurrent`)
- Replace `new InstanceImportTask(QUrl(m_selected.packUrl), this)` with `new LCSVInstallTask(m_selected, this)`.
- Pass the full `m_selected` pack (install needs packId / version / manifestUrl for the meta file).

### 4.4 `launcher/modplatform/lcsv/LCSVPackIndex.{h,cpp}`  (edit)
- Add `QString manifestUrl;` to `IndexedPack`. Parse it in `loadIndexedPack` (`Json::ensureString(obj, "manifestUrl", "")`).
- In `loadIndexFromBytes`: read root `formatVersion` as int. Define `constexpr int LCSV_SUPPORTED_FORMAT = 1`. If the file's value > that → log "launcher out of date" and return false (parse nothing).

**Phase 4 done when:** installing a pack produces a working instance whose root has a correct `.lcsv-meta.json`.

---

## Phase 5 — Update + File Protection

### 5.1 `launcher/modplatform/lcsv/LCSVUpdateTask.{h,cpp}`  (new)
- Subclass `Task`. Input: instance root (+ optional `force` flag for 5.4).
- Steps:
  1. `LCSVMeta::load`. If no meta → emit `succeeded` (not an LCSV instance).
  2. GET `meta.distributionUrl` with a 5s timeout. On failure → emit `succeeded` (skip update, launch as-is).
  3. Enforce `formatVersion` guard. Find entry by `meta.packId`.
  4. If `entry.version == meta.packVersion` and not `force` → emit `succeeded` (no hashing, no manifest fetch).
  5. GET the **distribution entry's** `manifestUrl` (the new version — NOT `meta.manifestUrl`).
  6. For each file in the new manifest:
     - skip if `LCSVMeta::matchesAny(userProtectedFiles, path)` or `matchesAny(userKeptFiles.keys(), path)` (unless `force`).
     - local hash: reuse cached `installedFiles` hash if current `size`+`mtime` match; else compute SHA-512 (reuse the hashing helper in `launcher/FileSystem.cpp` / `Hashing`).
     - local missing → queue download.
     - hash ≠ manifest hash → if `path.startsWith("config/")` → conflict list; else → queue download.
  7. Orphans: `meta.installedFiles paths − newManifest paths`. Each → queue delete unless protected/kept.
  8. If conflict list non-empty → show `LCSVUpdateDialog` (5.2); apply choices; "keep, don't ask again" → add to `userKeptFiles`.
  9. Apply: delete orphans, then `NetJob` download the queued files.
  10. Rewrite meta: new `packVersion` + `manifestUrl`, fresh `installedFiles` snapshot, merged `userKeptFiles`, `lastChecked = now`.

### 5.2 `launcher/ui/dialogs/LCSVUpdateDialog.{h,cpp,ui}`  (new)
- Shows the conflicting config paths. Per row three choices: **Download new** / **Keep mine** / **Keep mine, don't ask again**.
- Returns a `QMap<QString, Choice>`.

### 5.3 `launcher/LaunchController.cpp`  (edit `launchInstance()`, ~line 368; insert before `createLaunchTask` at line 379)
- If `QFile::exists(m_instance->instanceRoot() + "/.lcsv-meta.json")` and not already updated this launch:
  - run `LCSVUpdateTask` (parent dialogs to `m_parentWidget`).
  - `succeeded` → continue to the existing line-379 `createLaunchTask` call.
  - user abort → use the existing `LaunchDecision::Abort` path (mirror lines 298 / 334).
- Else → behave exactly as today.

### 5.4 Force-update action
- Add "Force update LCSV pack" to the instance context menu (`launcher/ui/MainWindow.cpp` or the instance page actions).
- Runs `LCSVUpdateTask` with `force = true` (ignores protected/kept + the version-equality shortcut; re-pulls everything incl. `options.txt`).

**Phase 5 done when:** no-change launch is silent with zero hashing; one changed mod downloads only that file; a changed config prompts and "Keep mine" leaves it untouched; a removed mod is deleted; force-update re-pulls everything.

---

## Phase 6 — Developer Export

### 6.1 `launcher/modplatform/lcsv/LCSVExportTask.{h,cpp}`  (new)
- Walk the instance's `.minecraft` (or instance) dir. Ignore: `logs/`, `crash-reports/`, `.lcsv-meta.json`.
- SHA-512 each file (reuse `FileSystem` hashing — do **not** reuse `ExportInstanceTask`, its `overrides/` behavior is wrong for us).
- Per file: build a `files[]` entry with `downloads: ["https://mc.lcsv.com.br/files/{sha512}"]`; copy the file to `output/files/{sha512}`.
- Emit three outputs: the `.mrpack` zip (contains `modrinth.index.json` + an **empty** `overrides/`), the bare `index.json` sidecar (byte-identical inner json), and `distribution-entry.json`.

### 6.2 `launcher/ui/dialogs/LCSVExportDialog.{h,cpp,ui}`  (new)
- Fields: pack id, name, version, description, server address, output folder picker. Runs `LCSVExportTask`.

### 6.3 Developer-mode toggle
- Settings → Advanced: add bool "LCSV Developer Mode".
- Instance context menu: "Export as LCSV Pack" visible only when the toggle is on.

**Phase 6 done when:** export → `rsync` to Tower → another client installs/updates the new version end to end.

---

## Phase 7 — Branding + Polish

- `launcher/CMakeLists.txt` / `buildconfig/`: app name, window title, update URLs → LCSV.
- Verify "Add Offline" account works (expected: no code change).
- Optional: hide the Modrinth / CurseForge browser tabs.
- Linux + Windows release builds.

---

## New files summary (all need a `LCSV_SOURCES` entry)

```
launcher/modplatform/lcsv/LCSVMeta.{h,cpp}            (Phase 4)
launcher/modplatform/lcsv/LCSVInstallTask.{h,cpp}     (Phase 4)
launcher/modplatform/lcsv/LCSVUpdateTask.{h,cpp}      (Phase 5)
launcher/ui/dialogs/LCSVUpdateDialog.{h,cpp,ui}       (Phase 5)
launcher/modplatform/lcsv/LCSVExportTask.{h,cpp}      (Phase 6)
launcher/ui/dialogs/LCSVExportDialog.{h,cpp,ui}       (Phase 6)
```

## Edited files summary

```
launcher/modplatform/lcsv/LCSVPackIndex.{h,cpp}   (Phase 4.4: manifestUrl + formatVersion guard)
launcher/ui/pages/modplatform/lcsv/LCSVPage.cpp   (Phase 4.3: use LCSVInstallTask)
launcher/LaunchController.cpp                      (Phase 5.3: pre-launch update hook ~line 368/379)
launcher/ui/MainWindow.cpp                         (Phase 5.4 + 6.3: context menu actions)
launcher/CMakeLists.txt                            (all phases: LCSV_SOURCES entries)
buildconfig/ + settings pages                      (Phase 6.3 toggle, Phase 7 branding)
```

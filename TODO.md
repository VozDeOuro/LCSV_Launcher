# LCSV Launcher TODO

## In Progress / Needs Fix

### Download progress per-file sub-bars
- **What**: `LCSVUpdateTask::startDownloads` needs `stepProgress` signals per file so `ProgressDialog` shows individual bars (filename + speed + ETA) like CurseForge
- **Status**: Code written in `LCSVUpdateTask.cpp` but build was interrupted before verifying
- **File**: `launcher/modplatform/lcsv/LCSVUpdateTask.cpp` — `startDownloads()` method
- **How it works**: Each `Net::ApiDownload` action has `progress/details/succeeded/failed` signals → connect them to emit `stepProgress(TaskStepProgress)` from `UpdateTask` → `ProgressDialog::changeStepProgress` renders sub-bars automatically
- **Verify**: Run `cmake --build build --config Debug` and check for errors

### Icon field compile mismatch
- **What**: Linter reverted `LCSVExportTask.h/.cpp` to use `iconUrl` (plain string), but `LCSVExportDialog.cpp` uses `opts.iconLocalPath`
- **Fix needed**: Either add `iconLocalPath` back to `ExportOptions` struct alongside `iconUrl`, OR change `onAccepted()` in dialog to copy the file and set `opts.iconUrl` directly
- **Files**: `launcher/modplatform/lcsv/LCSVExportTask.h`, `launcher/modplatform/lcsv/LCSVExportTask.cpp`, `launcher/ui/dialogs/LCSVExportDialog.cpp`
- **Recommended fix**: Add both fields to `ExportOptions`:
  - `iconLocalPath` — local file to copy to `{outputDir}/icons/{packId}.png`
  - `iconUrl` is then auto-generated inside `executeTask()` if `iconLocalPath` is set

---

## Pending Features

### Self-update via GitHub Releases (Task #2)
- **What**: Launcher auto-checks for new versions and prompts user to update
- **Steps**:
  1. Create GitHub repo for LCSV_prism fork
  2. Change `Launcher_UPDATER_GITHUB_REPO` default in root `CMakeLists.txt` (line ~198) from PrismLauncher URL to LCSV repo
  3. Set `Launcher_BUILD_ARTIFACT` to a non-empty string (e.g. `lcsv-launcher-linux`) — this enables the updater (`Launcher_BUILD_UPDATER = YES`)
  4. Push tagged releases with attached binary artifacts matching the artifact name
- **Key files**: `CMakeLists.txt` (root), `launcher/updater/prismupdater/PrismUpdater.cpp`
- **Note**: Updater is currently **disabled** — both vars must be set for it to activate

---

## Completed This Session

- [x] `DEFAULT_BLOCKED` list in export dialog — runtime/cache dirs unchecked by default (`xaero`, `.mixin.out`, `ESM`, `debug`, `dynamic-*-cache`, `immersive_paintings_cache`, `downloads`, `texturepacks`, `usercache.json`, `usernamecache.json`, `command_history.txt`)
- [x] `FileIgnoreProxy::ignoreFilesMatchingPattern()` — regex support for dated backup dirs (`XaeroWaypoints_BACKUP240807`, `hs_err_pid*.log`, `replay_pid*.log`)
- [x] All LCSV URLs fixed: `mc.lcsv.com.br/distribution.json` → `mc.lcsv.com.br/lcsv/distribution.json`, base URL → `mc.lcsv.com.br/lcsv`
- [x] Export filter inversion fix — `filterFile()` returns `true` to exclude, was previously `!filter()` (wrong)
- [x] Export config persistence — saves `lcsv-export.json` in instance root, pre-fills dialog on re-export
- [x] Icon picker in export dialog — Browse .png / Use Instance Icon / Clear + 32×32 preview
- [x] Icon copied to `{outputDir}/icons/{packId}.png` on export, URL auto-generated in `distribution.json`
- [x] `distribution.json` upsert merge — re-exporting same packId updates entry instead of overwriting file
- [x] Phase 7 branding — binary `lcsv-launcher`, AppID `com.lcsv.LCSVLauncher`, ENVName `LCSVLAUNCHER`, URLs pointing to lcsv.com.br

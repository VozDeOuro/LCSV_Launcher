// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#pragma once

#include <QJsonObject>
#include <QMap>
#include <QStringList>

#include "LCSVPackIndex.h"

namespace LCSV {

static const QStringList DEFAULT_PROTECTED_FILES = {
    "options.txt",
    "keybinds.txt",
    "servers.dat",
    "config/iris.properties",
    "config/oculus.properties",
    "shaderpacks/",
};

// Per-file snapshot entry stored in .lcsv-meta.json installedFiles (FIX #1, #5)
struct InstalledFileEntry {
    QString sha512;
    qint64 size = 0;
    qint64 mtime = 0;  // seconds since epoch; 0 = not yet cached
};

using InstalledFiles = QMap<QString, InstalledFileEntry>;

class UserFileManager {
   public:
    // Write .lcsv-meta.json (schema v1) with full installedFiles snapshot (FIX #1)
    static void writeMeta(const QString& instanceRoot, const IndexedPack& pack,
                          const InstalledFiles& files = {});

    // Read .lcsv-meta.json — returns false if missing or malformed
    static bool readMeta(const QString& instanceRoot, QJsonObject& out);

    static bool hasMeta(const QString& instanceRoot);

    // Deserialise installedFiles map from a loaded meta object (FIX #1, #5)
    static InstalledFiles readInstalledFiles(const QJsonObject& meta);

    // Persist a user "keep always" choice for a given file path
    static void setKeptFile(const QString& instanceRoot, const QString& relPath);

    // Returns true if file should never be auto-updated
    static bool isProtected(const QJsonObject& meta, const QString& relPath);

    // Returns true if this path is a config-type file (prompt user on change, FIX #4)
    static bool isConfigPath(const QString& relPath);

    // Returns true if on-disk file matches cached size+mtime → skip SHA-512 (FIX #5)
    static bool matchesCached(const InstalledFileEntry& entry, const QString& absolutePath);
};

}  // namespace LCSV

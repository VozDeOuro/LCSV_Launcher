// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#include "LCSVUserFileManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "FileSystem.h"

static QString metaPath(const QString& instanceRoot)
{
    return FS::PathCombine(instanceRoot, ".lcsv-meta.json");
}

namespace LCSV {

void UserFileManager::writeMeta(const QString& instanceRoot, const IndexedPack& pack,
                                const InstalledFiles& files)
{
    QJsonObject meta;
    meta["schema"] = 1;
    meta["packId"] = pack.id;
    meta["packVersion"] = pack.version;
    meta["packUrl"] = pack.packUrl;
    meta["distributionUrl"] = QString("https://mc.lcsv.com.br/lcsv/distribution.json");
    meta["manifestUrl"] = pack.manifestUrl;
    meta["serverAddress"] = pack.serverAddress;
    meta["lastChecked"] = QDateTime::currentSecsSinceEpoch();

    QJsonArray protectedArr;
    for (const auto& f : DEFAULT_PROTECTED_FILES)
        protectedArr.append(f);
    meta["userProtectedFiles"] = protectedArr;
    meta["userKeptFiles"] = QJsonObject{};

    QJsonObject installedObj;
    for (auto it = files.begin(); it != files.end(); ++it) {
        QJsonObject entry;
        entry["sha512"] = it.value().sha512;
        entry["size"] = it.value().size;
        entry["mtime"] = it.value().mtime;
        installedObj[it.key()] = entry;
    }
    meta["installedFiles"] = installedObj;

    QFile f(metaPath(instanceRoot));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(meta).toJson());
}

bool UserFileManager::readMeta(const QString& instanceRoot, QJsonObject& out)
{
    QFile f(metaPath(instanceRoot));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    out = doc.object();
    return true;
}

bool UserFileManager::hasMeta(const QString& instanceRoot)
{
    return QFile::exists(metaPath(instanceRoot));
}

InstalledFiles UserFileManager::readInstalledFiles(const QJsonObject& meta)
{
    InstalledFiles result;
    auto obj = meta["installedFiles"].toObject();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        auto entryObj = it.value().toObject();
        InstalledFileEntry e;
        e.sha512 = entryObj["sha512"].toString();
        e.size = static_cast<qint64>(entryObj["size"].toDouble());
        e.mtime = static_cast<qint64>(entryObj["mtime"].toDouble());
        result[it.key()] = e;
    }
    return result;
}

void UserFileManager::setKeptFile(const QString& instanceRoot, const QString& relPath)
{
    QJsonObject meta;
    if (!readMeta(instanceRoot, meta))
        return;
    auto kept = meta["userKeptFiles"].toObject();
    kept[relPath] = "kept-always";
    meta["userKeptFiles"] = kept;
    QFile f(metaPath(instanceRoot));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(meta).toJson());
}

bool UserFileManager::isProtected(const QJsonObject& meta, const QString& relPath)
{
    auto kept = meta["userKeptFiles"].toObject();
    if (kept.contains(relPath))
        return true;

    auto arr = meta["userProtectedFiles"].toArray();
    for (const auto& v : arr) {
        QString pattern = v.toString();
        if (pattern.endsWith('/')) {
            if (relPath.startsWith(pattern))
                return true;
        } else {
            if (relPath == pattern)
                return true;
        }
    }
    return false;
}

bool UserFileManager::isConfigPath(const QString& relPath)
{
    return relPath.startsWith("config/") || relPath.startsWith("config\\");
}

bool UserFileManager::matchesCached(const InstalledFileEntry& entry, const QString& absolutePath)
{
    if (entry.mtime == 0)
        return false;  // no cached mtime — can't use fast-path
    QFileInfo info(absolutePath);
    if (!info.exists())
        return false;
    return info.size() == entry.size && info.lastModified().toSecsSinceEpoch() == entry.mtime;
}

}  // namespace LCSV

// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#include "LCSVUpdateTask.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include "Application.h"
#include "FileSystem.h"
#include "LCSVUserFileManager.h"
#include "net/ApiDownload.h"
#include "net/NetJob.h"
#include "ui/dialogs/LCSVUpdateDialog.h"

#define LCSV_DIST_URL "https://mc.lcsv.com.br/lcsv/distribution.json"

namespace LCSV {

UpdateTask::UpdateTask(const QString& instanceRoot, const QString& gameRoot, QWidget* parent, bool force)
    : m_instanceRoot(instanceRoot), m_gameRoot(gameRoot), m_parentWidget(parent), m_force(force)
{}

bool UpdateTask::abort()
{
    if (m_job)
        m_job->abort();
    return true;
}

void UpdateTask::executeTask()
{
    if (!UserFileManager::readMeta(m_instanceRoot, m_meta)) {
        emitSucceeded();
        return;
    }

    setStatus(tr("LCSV: Checking for pack updates..."));

    auto job = makeShared<NetJob>("LCSV::UpdateCheck", APPLICATION->network());
    auto [action, response] = Net::ApiDownload::makeByteArray(QUrl(LCSV_DIST_URL));
    job->addNetAction(action);
    m_job = job;

    connect(job.get(), &NetJob::succeeded, this, [this, response] { onDistributionFetched(response); });
    connect(job.get(), &NetJob::failed, this, [this](QString reason) {
        qWarning() << "LCSV: Update check failed (offline?):" << reason;
        emitSucceeded();  // fail open — let the game launch
    });

    job->start();
}

void UpdateTask::onDistributionFetched(QByteArray* dataPtr)
{
    QByteArray data = std::move(*dataPtr);
    m_job.reset();

    QString installedId = m_meta["packId"].toString();
    QString installedVersion = m_meta["packVersion"].toString();

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "LCSV: Could not parse distribution.json";
        emitSucceeded();
        return;
    }

    auto root = doc.object();

    // FIX #8: formatVersion guard
    int fv = root.value("formatVersion").toInt(1);
    if (fv > 1) {
        qWarning() << "LCSV: distribution.json formatVersion" << fv << "is newer than expected (1). Continuing anyway.";
    }

    QString remoteVersion, remotePackUrl, remoteManifestUrl;
    for (const auto& v : root["packs"].toArray()) {
        auto obj = v.toObject();
        if (obj["id"].toString() == installedId) {
            remoteVersion = obj["version"].toString();
            remotePackUrl = obj["packUrl"].toString();
            remoteManifestUrl = obj["manifestUrl"].toString();
            break;
        }
    }

    if (remoteVersion.isEmpty() || remoteVersion == installedVersion) {
        setStatus(tr("LCSV: Pack is up to date."));
        emitSucceeded();
        return;
    }

    setStatus(tr("LCSV: Update available: %1 → %2. Fetching manifest...")
                  .arg(installedVersion, remoteVersion));

    // FIX #2: fetch bare sidecar .index.json instead of full .mrpack
    QString manifestUrl = remoteManifestUrl.isEmpty() ? m_meta["manifestUrl"].toString() : remoteManifestUrl;
    if (manifestUrl.isEmpty()) {
        qWarning() << "LCSV: No manifestUrl for pack" << installedId << "— cannot update";
        emitSucceeded();
        return;
    }

    auto job = makeShared<NetJob>("LCSV::ManifestFetch", APPLICATION->network());
    auto [action, response] = Net::ApiDownload::makeByteArray(QUrl(manifestUrl));
    job->addNetAction(action);
    m_job = job;

    connect(job.get(), &NetJob::succeeded, this,
            [this, response, remoteVersion, remotePackUrl, remoteManifestUrl] {
                onManifestFetched(response, remoteVersion, remotePackUrl, remoteManifestUrl);
            });
    connect(job.get(), &NetJob::failed, this, [this](QString reason) {
        qWarning() << "LCSV: Could not fetch pack manifest:" << reason;
        emitSucceeded();
    });

    job->start();
}

void UpdateTask::onManifestFetched(QByteArray* dataPtr, const QString& remoteVersion,
                                   const QString& remotePackUrl, const QString& remoteManifestUrl)
{
    QByteArray data = std::move(*dataPtr);
    m_job.reset();

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "LCSV: Could not parse pack manifest";
        emitSucceeded();
        return;
    }

    // Parse remote file list from sidecar modrinth.index.json
    QList<RemoteFile> remoteFiles;
    QSet<QString> remotePaths;
    for (const auto& v : doc.object()["files"].toArray()) {
        auto obj = v.toObject();
        RemoteFile f;
        f.path = obj["path"].toString();
        f.sha512 = obj["hashes"].toObject()["sha512"].toString();
        f.fileSize = static_cast<qint64>(obj["fileSize"].toDouble());
        for (const auto& u : obj["downloads"].toArray())
            f.downloads.append(QUrl(u.toString()));
        if (!f.path.isEmpty() && !f.sha512.isEmpty() && !f.downloads.isEmpty()) {
            remoteFiles.append(f);
            remotePaths.insert(f.path);
        }
    }

    // Update meta fields before applying
    m_meta["packVersion"] = remoteVersion;
    m_meta["packUrl"] = remotePackUrl;
    if (!remoteManifestUrl.isEmpty())
        m_meta["manifestUrl"] = remoteManifestUrl;

    applyUpdate(remoteFiles, remoteVersion, remotePackUrl, remoteManifestUrl);
}

void UpdateTask::applyUpdate(const QList<RemoteFile>& remoteFiles, const QString& /*remoteVersion*/,
                             const QString& /*remotePackUrl*/, const QString& /*remoteManifestUrl*/)
{
    auto installedFiles = UserFileManager::readInstalledFiles(m_meta);

    // FIX #3: Orphan detection — files in snapshot but not in new manifest
    QStringList orphans;
    for (const auto& path : installedFiles.keys()) {
        QSet<QString> remotePaths;
        for (const auto& rf : remoteFiles)
            remotePaths.insert(rf.path);
        if (!remotePaths.contains(path) && (m_force || !UserFileManager::isProtected(m_meta, path)))
            orphans.append(path);
    }

    // Build download queue with FIX #4 config-conflict and FIX #5 hash fast-path
    QStringList changedConfigs;
    QList<RemoteFile> toDownload;

    for (const auto& rf : remoteFiles) {
        if (!m_force && UserFileManager::isProtected(m_meta, rf.path))
            continue;

        QString localPath = FS::PathCombine(m_gameRoot, rf.path);

        // FIX #5: Hash fast-path — skip SHA-512 if size+mtime unchanged and sha512 matches
        if (!m_force && installedFiles.contains(rf.path)) {
            const auto& cached = installedFiles[rf.path];
            if (cached.sha512 == rf.sha512 && UserFileManager::matchesCached(cached, localPath))
                continue;
        }

        // Need full hash check
        bool localExists = QFile::exists(localPath);
        QString localHash;
        if (localExists) {
            QFile f(localPath);
            if (f.open(QIODevice::ReadOnly))
                localHash = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha512).toHex();
        }

        if (localExists && localHash == rf.sha512)
            continue;

        // FIX #4: Only config/ paths prompt the user; everything else updates silently
        if (UserFileManager::isConfigPath(rf.path))
            changedConfigs.append(rf.path);

        toDownload.append(rf);
    }

    // Show conflict dialog for config/ files only
    if (!changedConfigs.isEmpty()) {
        auto* dlg = new LCSVUpdateDialog(changedConfigs, m_parentWidget);
        dlg->exec();
        auto choices = dlg->choices();
        dlg->deleteLater();

        QList<RemoteFile> filtered;
        for (const auto& rf : toDownload) {
            if (!changedConfigs.contains(rf.path)) {
                filtered.append(rf);
                continue;
            }
            auto choice = choices.value(rf.path, LCSVUpdateDialog::Choice::DownloadNew);
            if (choice == LCSVUpdateDialog::Choice::KeepAlways) {
                UserFileManager::setKeptFile(m_instanceRoot, rf.path);
                // Re-read meta so setKeptFile change is reflected
                UserFileManager::readMeta(m_instanceRoot, m_meta);
            } else if (choice == LCSVUpdateDialog::Choice::DownloadNew) {
                filtered.append(rf);
            }
            // KeepMine → skip, don't persist
        }
        toDownload = filtered;
    }

    startDownloads(toDownload, orphans);
}

void UpdateTask::startDownloads(const QList<RemoteFile>& toDownload, const QStringList& orphans)
{
    // FIX #3: Delete orphaned files from disk
    for (const auto& path : orphans) {
        QFile::remove(FS::PathCombine(m_gameRoot, path));
    }

    if (toDownload.isEmpty()) {
        writeUpdatedMeta({}, orphans);
        setStatus(tr("LCSV: No files to update."));
        emitSucceeded();
        return;
    }

    setStatus(tr("LCSV: Downloading %1 updated file(s)...").arg(toDownload.size()));
    setProgress(0, toDownload.size());

    auto job = makeShared<NetJob>("LCSV::FileUpdate", APPLICATION->network());
    for (const auto& rf : toDownload) {
        QString dest = FS::PathCombine(m_gameRoot, rf.path);
        FS::ensureFilePathExists(dest);

        auto action = Net::ApiDownload::makeFile(rf.downloads.first(), dest);

        // Per-file step progress bar in ProgressDialog
        auto step = std::make_shared<TaskStepProgress>();
        step->status = rf.path;
        step->state = TaskStepState::Running;
        emit stepProgress(*step);

        connect(action.get(), &Task::progress, this, [this, step](qint64 cur, qint64 total) {
            step->update(cur, total);
            emit stepProgress(*step);
        });
        connect(action.get(), &Task::details, this, [this, step](QString det) {
            step->details = det;
            emit stepProgress(*step);
        });
        connect(action.get(), &Task::succeeded, this, [this, step] {
            step->state = TaskStepState::Succeeded;
            emit stepProgress(*step);
        });
        connect(action.get(), &Task::failed, this, [this, step](QString) {
            step->state = TaskStepState::Failed;
            emit stepProgress(*step);
        });

        job->addNetAction(action);
    }
    m_job = job;

    connect(job.get(), &NetJob::succeeded, this, [this, toDownload, orphans] {
        writeUpdatedMeta(toDownload, orphans);
        setStatus(tr("LCSV: Pack updated successfully."));
        emitSucceeded();
    });
    connect(job.get(), &NetJob::failed, this, [this](QString reason) {
        qWarning() << "LCSV: File download failed:" << reason;
        emitSucceeded();  // fail open
    });

    job->start();
}

void UpdateTask::writeUpdatedMeta(const QList<RemoteFile>& downloadedFiles, const QStringList& removedOrphans)
{
    auto installedFiles = UserFileManager::readInstalledFiles(m_meta);

    // Remove orphans from snapshot
    for (const auto& path : removedOrphans)
        installedFiles.remove(path);

    // Update snapshot for downloaded files with fresh size+mtime (enables fast-path next check)
    for (const auto& rf : downloadedFiles) {
        QString localPath = FS::PathCombine(m_gameRoot, rf.path);
        QFileInfo info(localPath);
        InstalledFileEntry entry;
        entry.sha512 = rf.sha512;
        entry.size = info.size();
        entry.mtime = info.lastModified().toSecsSinceEpoch();
        installedFiles[rf.path] = entry;
    }

    m_meta["lastChecked"] = QDateTime::currentSecsSinceEpoch();

    QJsonObject installedObj;
    for (auto it = installedFiles.begin(); it != installedFiles.end(); ++it) {
        QJsonObject entry;
        entry["sha512"] = it.value().sha512;
        entry["size"] = it.value().size;
        entry["mtime"] = it.value().mtime;
        installedObj[it.key()] = entry;
    }
    m_meta["installedFiles"] = installedObj;

    QFile f(FS::PathCombine(m_instanceRoot, ".lcsv-meta.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(m_meta).toJson());
}

}  // namespace LCSV

// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#include "LCSVUpdateStep.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>

#include "Application.h"
#include "FileSystem.h"
#include "archive/ArchiveReader.h"
#include "launch/LaunchTask.h"
#include "minecraft/MinecraftInstance.h"
#include "modplatform/lcsv/LCSVUserFileManager.h"
#include "net/ApiDownload.h"
#include "net/NetJob.h"
#include "ui/dialogs/LCSVUpdateDialog.h"

#define LCSV_DIST_URL "https://mc.lcsv.com.br/lcsv/distribution.json"

LCSVUpdateStep::LCSVUpdateStep(LaunchTask* parent) : LaunchStep(parent)
{
    auto inst = qobject_cast<MinecraftInstance*>(m_parent->instance());
    m_instanceRoot = inst->instanceRoot();
    m_gameRoot = inst->gameRoot();
}

bool LCSVUpdateStep::abort()
{
    if (m_job)
        m_job->abort();
    return true;
}

void LCSVUpdateStep::executeTask()
{
    if (!LCSV::UserFileManager::readMeta(m_instanceRoot, m_meta)) {
        emitSucceeded();
        return;
    }

    emit logLine("LCSV: Checking for pack updates...", MessageLevel::Launcher);

    auto job = makeShared<NetJob>("LCSV::UpdateCheck", APPLICATION->network());
    auto [action, response] = Net::ApiDownload::makeByteArray(QUrl(LCSV_DIST_URL));
    job->addNetAction(action);
    m_job = job;

    connect(job.get(), &NetJob::succeeded, this, [this, response] { onDistributionFetched(response); });
    connect(job.get(), &NetJob::failed, this, [this](QString reason) {
        emit logLine("LCSV: Update check failed (offline?): " + reason, MessageLevel::Warning);
        emitSucceeded();
    });

    job->start();
}

void LCSVUpdateStep::onDistributionFetched(QByteArray* dataPtr)
{
    QByteArray data = std::move(*dataPtr);
    m_job.reset();

    QString installedId = m_meta["packId"].toString();
    QString installedVersion = m_meta["packVersion"].toString();
    QString packUrl = m_meta["packUrl"].toString();

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit logLine("LCSV: Could not parse distribution.json", MessageLevel::Warning);
        emitSucceeded();
        return;
    }

    QString remoteVersion;
    QString remotePackUrl = packUrl;
    for (const auto& v : doc.object()["packs"].toArray()) {
        auto obj = v.toObject();
        if (obj["id"].toString() == installedId) {
            remoteVersion = obj["version"].toString();
            remotePackUrl = obj["packUrl"].toString();
            break;
        }
    }

    if (remoteVersion.isEmpty() || remoteVersion == installedVersion) {
        emit logLine("LCSV: Pack is up to date.", MessageLevel::Launcher);
        emitSucceeded();
        return;
    }

    emit logLine(
        QString("LCSV: Update available: %1 → %2. Fetching manifest...").arg(installedVersion, remoteVersion),
        MessageLevel::Launcher);

    m_remotePackUrl = remotePackUrl;
    m_meta["packUrl"] = remotePackUrl;

    auto job = makeShared<NetJob>("LCSV::MrpackFetch", APPLICATION->network());
    auto [action, response] = Net::ApiDownload::makeByteArray(QUrl(remotePackUrl));
    job->addNetAction(action);
    m_job = job;

    connect(job.get(), &NetJob::succeeded, this, [this, response, remoteVersion] {
        m_meta["packVersion"] = remoteVersion;
        onMrpackFetched(response);
    });
    connect(job.get(), &NetJob::failed, this, [this](QString reason) {
        emit logLine("LCSV: Could not fetch pack manifest: " + reason, MessageLevel::Warning);
        emitSucceeded();
    });

    job->start();
}

void LCSVUpdateStep::onMrpackFetched(QByteArray* dataPtr)
{
    QByteArray zipData = std::move(*dataPtr);
    m_job.reset();

    QList<LCSVRemoteFile> remoteFiles;
    if (!parseMrpackManifest(zipData, remoteFiles)) {
        emit logLine("LCSV: Could not parse pack manifest", MessageLevel::Warning);
        emitSucceeded();
        return;
    }

    applyUpdate(remoteFiles);
}

bool LCSVUpdateStep::parseMrpackManifest(const QByteArray& zipData, QList<LCSVRemoteFile>& out)
{
    // Write to a temp file so ArchiveReader can open it by path
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    if (!tmp.open())
        return false;
    tmp.write(zipData);
    tmp.flush();

    MMCZip::ArchiveReader reader(tmp.fileName());
    auto file = reader.goToFile("modrinth.index.json");
    if (!file)
        return false;

    QByteArray indexData = file->readAll();

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(indexData, &err);
    if (err.error != QJsonParseError::NoError)
        return false;

    for (const auto& v : doc.object()["files"].toArray()) {
        auto obj = v.toObject();
        LCSVRemoteFile f;
        f.path = obj["path"].toString();
        f.sha512 = obj["hashes"].toObject()["sha512"].toString();
        f.fileSize = static_cast<qint64>(obj["fileSize"].toDouble());
        for (const auto& u : obj["downloads"].toArray())
            f.downloads.append(QUrl(u.toString()));
        if (!f.path.isEmpty() && !f.sha512.isEmpty() && !f.downloads.isEmpty())
            out.append(f);
    }
    return true;
}

void LCSVUpdateStep::applyUpdate(const QList<LCSVRemoteFile>& remoteFiles)
{
    QStringList changedConfigs;
    QList<LCSVRemoteFile> toDownload;

    for (const auto& rf : remoteFiles) {
        if (LCSV::UserFileManager::isProtected(m_meta, rf.path))
            continue;

        QString localPath = FS::PathCombine(m_gameRoot, rf.path);
        bool localExists = QFile::exists(localPath);

        QString localHash;
        if (localExists) {
            QFile f(localPath);
            if (f.open(QIODevice::ReadOnly))
                localHash = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha512).toHex();
        }

        if (localExists && localHash == rf.sha512)
            continue;

        if (LCSV::UserFileManager::isConfigPath(rf.path)) {
            changedConfigs.append(rf.path);
            toDownload.append(rf);
        } else {
            toDownload.append(rf);
        }
    }

    if (!changedConfigs.isEmpty()) {
        auto* dlg = new LCSVUpdateDialog(changedConfigs, nullptr);
        dlg->exec();
        auto choices = dlg->choices();
        dlg->deleteLater();

        QList<LCSVRemoteFile> filtered;
        for (const auto& rf : toDownload) {
            if (!changedConfigs.contains(rf.path)) {
                filtered.append(rf);
                continue;
            }
            auto choice = choices.value(rf.path, LCSVUpdateDialog::Choice::DownloadNew);
            if (choice == LCSVUpdateDialog::Choice::KeepAlways) {
                LCSV::UserFileManager::setKeptFile(m_instanceRoot, rf.path);
            } else if (choice == LCSVUpdateDialog::Choice::DownloadNew) {
                filtered.append(rf);
            }
        }
        toDownload = filtered;
    }

    if (toDownload.isEmpty()) {
        writeMeta();
        emit logLine("LCSV: No files to update.", MessageLevel::Launcher);
        emitSucceeded();
        return;
    }

    emit logLine(QString("LCSV: Downloading %1 updated file(s)...").arg(toDownload.size()), MessageLevel::Launcher);

    auto job = makeShared<NetJob>("LCSV::FileUpdate", APPLICATION->network());
    for (const auto& rf : toDownload) {
        QString dest = FS::PathCombine(m_gameRoot, rf.path);
        FS::ensureFilePathExists(dest);
        job->addNetAction(Net::ApiDownload::makeFile(rf.downloads.first(), dest));
    }
    m_job = job;

    connect(job.get(), &NetJob::succeeded, this, [this] {
        writeMeta();
        emit logLine("LCSV: Pack updated successfully.", MessageLevel::Launcher);
        emitSucceeded();
    });
    connect(job.get(), &NetJob::failed, this, [this](QString reason) {
        emit logLine("LCSV: File download failed: " + reason, MessageLevel::Warning);
        emitSucceeded();
    });

    job->start();
}

void LCSVUpdateStep::writeMeta()
{
    QFile f(FS::PathCombine(m_instanceRoot, ".lcsv-meta.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(m_meta).toJson());
}

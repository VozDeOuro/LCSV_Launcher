// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#include "LCSVInstallTask.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceImportTask.h"
#include "LCSVUserFileManager.h"
#include "icons/IconList.h"
#include "net/ApiDownload.h"
#include "net/NetJob.h"

namespace LCSV {

InstallTask::InstallTask(const IndexedPack& pack, QWidget* parent)
    : InstanceTask(), m_pack(pack), m_parentWidget(parent)
{
    setObjectName(pack.name);
}

void InstallTask::executeTask()
{
    if (!m_pack.iconUrl.isEmpty() && m_instIcon == "default") {
        setStatus(tr("LCSV: Downloading pack icon..."));
        QString iconKey = QString("LCSV_%1_Icon").arg(m_pack.id);
        auto job = makeShared<NetJob>("LCSV::InstallIcon", APPLICATION->network());
        auto [action, response] = Net::ApiDownload::makeByteArray(QUrl(m_pack.iconUrl));
        job->addNetAction(action);
        m_iconJob = job;
        connect(job.get(), &NetJob::succeeded, this, [this, response, iconKey] {
            QString suffix = QFileInfo(QUrl(m_pack.iconUrl).path()).suffix();
            if (suffix.isEmpty())
                suffix = "png";
            QString tmpPath = FS::PathCombine(QDir::tempPath(), iconKey + "." + suffix);
            QFile tmp(tmpPath);
            if (tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                tmp.write(*response);
                tmp.close();
                APPLICATION->icons()->installIcon(tmpPath, iconKey + "." + suffix);
                m_instIcon = iconKey;
            }
            startImport();
        });
        connect(job.get(), &NetJob::failed, this, [this](QString) { startImport(); });
        job->start();
    } else {
        startImport();
    }
}

void InstallTask::startImport()
{
    auto inner = makeShared<InstanceImportTask>(QUrl(m_pack.packUrl), m_parentWidget);
    inner->setParentSettings(m_globalSettings);
    inner->setStagingPath(m_stagingPath);
    inner->setName(*this);
    inner->setIcon(m_instIcon);
    inner->setGroup(m_instGroup);

    connect(inner.get(), &Task::succeeded, this, [this] {
        if (!m_pack.manifestUrl.isEmpty()) {
            fetchManifest();
        } else {
            UserFileManager::writeMeta(m_stagingPath, m_pack, {});
            emitSucceeded();
        }
    });
    connect(inner.get(), &Task::failed, this, [this](QString reason) { emitFailed(reason); });
    connect(inner.get(), &Task::aborted, this, [this] { emitAborted(); });
    connect(inner.get(), &Task::progress, this, &Task::setProgress);
    connect(inner.get(), &Task::status, this, &Task::setStatus);

    m_inner = inner;
    inner->start();
}

void InstallTask::fetchManifest()
{
    setStatus(tr("LCSV: Fetching pack manifest..."));

    auto job = makeShared<NetJob>("LCSV::InstallManifest", APPLICATION->network());
    auto [action, response] = Net::ApiDownload::makeByteArray(QUrl(m_pack.manifestUrl));
    job->addNetAction(action);
    m_manifestJob = job;

    connect(job.get(), &NetJob::succeeded, this, [this, response] {
        InstalledFiles files = parseManifest(*response);
        UserFileManager::writeMeta(m_stagingPath, m_pack, files);
        emitSucceeded();
    });
    connect(job.get(), &NetJob::failed, this, [this](QString) {
        UserFileManager::writeMeta(m_stagingPath, m_pack, {});
        emitSucceeded();
    });

    job->start();
}

InstalledFiles InstallTask::parseManifest(const QByteArray& data)
{
    InstalledFiles result;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return result;

    for (const auto& v : doc.object()["files"].toArray()) {
        auto obj = v.toObject();
        QString path = obj["path"].toString();
        QString sha512 = obj["hashes"].toObject()["sha512"].toString();
        qint64 size = static_cast<qint64>(obj["fileSize"].toDouble());
        if (path.isEmpty() || sha512.isEmpty())
            continue;
        InstalledFileEntry entry;
        entry.sha512 = sha512;
        entry.size = size;
        entry.mtime = 0;  // not on disk yet; fast-path activates after first update
        result[path] = entry;
    }
    return result;
}

}  // namespace LCSV

// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QWidget>

#include "tasks/Task.h"

namespace LCSV {

struct RemoteFile {
    QString path;
    QString sha512;
    QList<QUrl> downloads;
    qint64 fileSize = 0;
};

// Async Task (FIX #6) — checks for LCSV pack update and applies it before launch.
// Replaces the old LaunchStep approach. Hooked into LaunchController::launchInstance().
class UpdateTask : public Task {
    Q_OBJECT
   public:
    explicit UpdateTask(const QString& instanceRoot, const QString& gameRoot, QWidget* parent = nullptr,
                        bool force = false);

    bool abort() override;

   protected:
    void executeTask() override;

   private slots:
    void onDistributionFetched(QByteArray* data);
    void onManifestFetched(QByteArray* data, const QString& remoteVersion, const QString& remotePackUrl,
                           const QString& remoteManifestUrl);

   private:
    void applyUpdate(const QList<RemoteFile>& remoteFiles, const QString& remoteVersion,
                     const QString& remotePackUrl, const QString& remoteManifestUrl);
    void startDownloads(const QList<RemoteFile>& toDownload, const QStringList& orphans);
    void writeUpdatedMeta(const QList<RemoteFile>& downloadedFiles, const QStringList& removedOrphans);

    QString m_instanceRoot;
    QString m_gameRoot;
    QWidget* m_parentWidget;
    bool m_force = false;
    QJsonObject m_meta;
    Task::Ptr m_job;
};

}  // namespace LCSV

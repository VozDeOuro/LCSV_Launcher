// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

#include "launch/LaunchStep.h"
#include "tasks/Task.h"

struct LCSVRemoteFile {
    QString path;    // relative to game root, e.g. "mods/gregtech.jar"
    QString sha512;
    QList<QUrl> downloads;
    qint64 fileSize = 0;
};

class LCSVUpdateStep : public LaunchStep {
    Q_OBJECT
   public:
    explicit LCSVUpdateStep(LaunchTask* parent);
    void executeTask() override;
    bool abort() override;

   private slots:
    void onDistributionFetched(QByteArray* data);
    void onMrpackFetched(QByteArray* data);

   private:
    bool parseMrpackManifest(const QByteArray& zipData, QList<LCSVRemoteFile>& out);
    void applyUpdate(const QList<LCSVRemoteFile>& remoteFiles);
    void writeMeta();

    QString m_instanceRoot;
    QString m_gameRoot;
    QJsonObject m_meta;
    QString m_remotePackUrl;
    Task::Ptr m_job;
};

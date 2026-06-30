// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <functional>

#include "minecraft/MinecraftInstance.h"
#include "tasks/Task.h"

namespace LCSV {

struct ExportOptions {
    QString packId;
    QString name;
    QString version;
    QString description;
    QString iconLocalPath;  // optional; local file to copy into {outputDir}/icons/
    QString iconUrl;        // set by task after copying icon; or pre-set to skip copy
    QString serverAddress;
    QString outputDir;
    // Returns true if file should be included. nullptr = include everything.
    std::function<bool(const QFileInfo&)> filter;
};

class ExportTask : public Task {
    Q_OBJECT
   public:
    explicit ExportTask(MinecraftInstance* instance, const ExportOptions& opts, QObject* parent = nullptr);

   protected:
    void executeTask() override;

   private:
    struct FileEntry {
        QString path;
        QString sha512;
        qint64 size = 0;
    };

    QByteArray buildIndex(const QList<FileEntry>& files) const;
    bool buildMrpack(const QByteArray& indexJson, const QString& mrpackPath) const;
    bool writeDistributionEntry(const QString& mrpackFilename, const QString& indexFilename) const;

    MinecraftInstance* m_instance;
    ExportOptions m_opts;
};

}  // namespace LCSV

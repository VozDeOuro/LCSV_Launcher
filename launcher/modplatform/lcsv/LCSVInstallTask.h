// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#pragma once

#include <QWidget>
#include "InstanceTask.h"
#include "LCSVPackIndex.h"
#include "LCSVUserFileManager.h"
#include "tasks/Task.h"

namespace LCSV {

// Wraps InstanceImportTask; fetches sidecar manifest and writes .lcsv-meta.json (FIX #1)
class InstallTask : public InstanceTask {
    Q_OBJECT
   public:
    explicit InstallTask(const IndexedPack& pack, QWidget* parent = nullptr);

   protected:
    void executeTask() override;

   private:
    void fetchManifest();
    void startImport();

    static InstalledFiles parseManifest(const QByteArray& data);

    IndexedPack m_pack;
    QWidget* m_parentWidget;
    Task::Ptr m_inner;
    Task::Ptr m_manifestJob;
    Task::Ptr m_iconJob;
};

}  // namespace LCSV

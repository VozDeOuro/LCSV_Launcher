// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#pragma once

#include <QDialog>
#include "FastFileIconProvider.h"
#include "FileIgnoreProxy.h"
#include "minecraft/MinecraftInstance.h"
#include "modplatform/lcsv/LCSVExportTask.h"

namespace Ui { class LCSVExportDialog; }

class LCSVExportDialog : public QDialog {
    Q_OBJECT
   public:
    explicit LCSVExportDialog(MinecraftInstance* instance, QWidget* parent = nullptr);
    ~LCSVExportDialog() override;

   private slots:
    void onBrowse();
    void onBrowseIcon();
    void onUseInstanceIcon();
    void onClearIcon();
    void onAccepted();

   private:
    void setIconPath(const QString& path);
    void saveConfig(const LCSV::ExportOptions& opts) const;

    Ui::LCSVExportDialog* ui;
    MinecraftInstance* m_instance;
    FileIgnoreProxy* m_proxy = nullptr;
    FastFileIconProvider m_icons;
    QString m_iconLocalPath;
};

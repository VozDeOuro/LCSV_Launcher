// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#pragma once

#include <QDialog>
#include <QStringList>
#include <QMap>

namespace Ui { class LCSVUpdateDialog; }

// Shown when config files changed server-side and user must decide per-file.
class LCSVUpdateDialog : public QDialog {
    Q_OBJECT
   public:
    enum class Choice { DownloadNew, KeepMine, KeepAlways };

    explicit LCSVUpdateDialog(const QStringList& changedConfigs, QWidget* parent = nullptr);
    ~LCSVUpdateDialog() override;

    // Returns per-file choice after exec()
    QMap<QString, Choice> choices() const { return m_choices; }

   private slots:
    void onAccepted();

   private:
    Ui::LCSVUpdateDialog* ui;
    QStringList m_files;
    QMap<QString, Choice> m_choices;
};

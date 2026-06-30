// SPDX-License-Identifier: GPL-3.0-only
/*
 * LCSV Launcher
 * Copyright (C) 2026 LCSV Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#pragma once

#include <QWidget>

#include "LCSVListModel.h"
#include "modplatform/lcsv/LCSVPackIndex.h"
#include "ui/pages/modplatform/ModpackProviderBasePage.h"

namespace Ui {
class LCSVPage;
}
class NewInstanceDialog;

class LCSVPage : public QWidget, public ModpackProviderBasePage {
    Q_OBJECT

   public:
    explicit LCSVPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);
    ~LCSVPage() override;

    QString displayName() const override { return "LCSV Servers"; }
    QIcon icon() const override { return QIcon::fromTheme("grass"); }
    QString id() const override { return "lcsv"; }
    QString helpPage() const override { return {}; }
    bool shouldDisplay() const override { return true; }
    void retranslate() override;
    void openedImpl() override;

    void setSearchTerm(QString term) override;
    QString getSerachTerm() const override;

   private slots:
    void onSelectionChanged(const QModelIndex& current, const QModelIndex& previous);
    void triggerSearch();

   private:
    void suggestCurrent();

   private:
    Ui::LCSVPage* ui = nullptr;
    NewInstanceDialog* m_dialog = nullptr;
    LCSV::ListModel* m_model = nullptr;

    LCSV::IndexedPack m_selected;
    bool m_hasSelection = false;
    bool m_initialized = false;
};

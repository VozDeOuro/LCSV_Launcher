// SPDX-License-Identifier: GPL-3.0-only
/*
 * LCSV Launcher
 * Copyright (C) 2026 LCSV Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "LCSVPage.h"
#include "ui_LCSVPage.h"

#include <StringUtils.h>

#include "modplatform/lcsv/LCSVInstallTask.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/widgets/ProjectItem.h"

LCSVPage::LCSVPage(NewInstanceDialog* dialog, QWidget* parent)
    : QWidget(parent), ui(new Ui::LCSVPage), m_dialog(dialog)
{
    ui->setupUi(this);

    m_model = new LCSV::ListModel(this);
    ui->packView->setModel(m_model);
    ui->packView->setItemDelegate(new ProjectItemDelegate(this));

    connect(ui->packView->selectionModel(), &QItemSelectionModel::currentChanged, this, &LCSVPage::onSelectionChanged);
    connect(ui->searchEdit, &QLineEdit::textChanged, this, &LCSVPage::triggerSearch);
    connect(m_model, &LCSV::ListModel::packListUpdated, this, [this] {
        // auto-select first pack when list loads
        if (m_model->rowCount() > 0 && !m_hasSelection)
            ui->packView->setCurrentIndex(m_model->index(0, 0));
    });
}

LCSVPage::~LCSVPage()
{
    delete ui;
}

void LCSVPage::retranslate()
{
    ui->retranslateUi(this);
}

void LCSVPage::openedImpl()
{
    if (!m_initialized) {
        m_model->request();
        m_initialized = true;
    }
    suggestCurrent();
}

void LCSVPage::onSelectionChanged(const QModelIndex& current, [[maybe_unused]] const QModelIndex& previous)
{
    if (!current.isValid()) {
        m_hasSelection = false;
        m_dialog->setSuggestedPack();
        return;
    }

    QVariant raw = m_model->data(current, Qt::UserRole);
    if (!raw.canConvert<LCSV::IndexedPack>())
        return;

    m_selected = raw.value<LCSV::IndexedPack>();
    m_hasSelection = true;

    QString desc = QString("<b>%1</b> &nbsp; <i>v%2</i><br><br>%3")
                       .arg(m_selected.name.toHtmlEscaped())
                       .arg(m_selected.version.toHtmlEscaped())
                       .arg(m_selected.description.toHtmlEscaped().replace("\n", "<br>"));
    if (!m_selected.minecraftVersion.isEmpty()) {
        desc += QString("<br><br><b>Minecraft:</b> %1 &nbsp; <b>Loader:</b> %2")
                    .arg(m_selected.minecraftVersion.toHtmlEscaped())
                    .arg(m_selected.modloader.toHtmlEscaped());
    }
    if (!m_selected.serverAddress.isEmpty()) {
        desc += QString("<br><b>Server:</b> %1").arg(m_selected.serverAddress.toHtmlEscaped());
    }
    ui->packDescription->setHtml(desc);

    suggestCurrent();
}

void LCSVPage::suggestCurrent()
{
    if (!isOpened || !m_hasSelection)
        return;

    m_dialog->setSuggestedPack(m_selected.name, m_selected.version,
                               new LCSV::InstallTask(m_selected, this));
}

void LCSVPage::triggerSearch()
{
    // Simple client-side filter on the list view
    QString term = ui->searchEdit->text().toLower();
    for (int i = 0; i < m_model->rowCount(); i++) {
        auto idx = m_model->index(i, 0);
        bool match = m_model->data(idx, Qt::DisplayRole).toString().toLower().contains(term);
        ui->packView->setRowHidden(i, !match);
    }
}

void LCSVPage::setSearchTerm(QString term)
{
    ui->searchEdit->setText(term);
}

QString LCSVPage::getSerachTerm() const
{
    return ui->searchEdit->text();
}

// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#include "LCSVUpdateDialog.h"
#include "ui_LCSVUpdateDialog.h"

#include <QComboBox>

LCSVUpdateDialog::LCSVUpdateDialog(const QStringList& changedConfigs, QWidget* parent)
    : QDialog(parent), ui(new Ui::LCSVUpdateDialog), m_files(changedConfigs)
{
    ui->setupUi(this);

    ui->tableWidget->setRowCount(m_files.size());
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    for (int i = 0; i < m_files.size(); i++) {
        auto* nameItem = new QTableWidgetItem(m_files[i]);
        nameItem->setFlags(Qt::ItemIsEnabled);
        ui->tableWidget->setItem(i, 0, nameItem);

        auto* combo = new QComboBox(this);
        combo->addItem("Download new (pack version)", 0);
        combo->addItem("Keep mine", 1);
        combo->addItem("Keep mine, don't ask again", 2);
        ui->tableWidget->setCellWidget(i, 1, combo);
    }

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &LCSVUpdateDialog::onAccepted);
}

LCSVUpdateDialog::~LCSVUpdateDialog()
{
    delete ui;
}

void LCSVUpdateDialog::onAccepted()
{
    m_choices.clear();
    for (int i = 0; i < m_files.size(); i++) {
        auto* combo = qobject_cast<QComboBox*>(ui->tableWidget->cellWidget(i, 1));
        int val = combo ? combo->currentData().toInt() : 0;
        m_choices[m_files[i]] = static_cast<Choice>(val);
    }
    accept();
}

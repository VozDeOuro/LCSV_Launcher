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

#include <QAbstractListModel>
#include <QIcon>
#include <QList>
#include <QMap>
#include <QStringList>

#include "modplatform/lcsv/LCSVPackIndex.h"
#include "tasks/Task.h"

namespace LCSV {

class ListModel : public QAbstractListModel {
    Q_OBJECT

   public:
    explicit ListModel(QObject* parent = nullptr);
    ~ListModel() override = default;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    void request();

    const IndexedPack& at(int row) const { return m_packs.at(row); }

   signals:
    void packListUpdated();

   private slots:
    void requestFinished(QByteArray* response);
    void requestFailed(QString reason);
    void logoLoaded(QString id, QIcon icon);
    void logoFailed(QString id);

   private:
    void requestLogo(const QString& id, const QString& url);

   private:
    QList<IndexedPack> m_packs;
    Task::Ptr m_jobPtr;

    QMap<QString, QIcon> m_logoMap;
    QStringList m_loadingLogos;
    QStringList m_failedLogos;
};

}  // namespace LCSV

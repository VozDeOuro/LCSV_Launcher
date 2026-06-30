// SPDX-License-Identifier: GPL-3.0-only
/*
 * LCSV Launcher
 * Copyright (C) 2026 LCSV Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "LCSVListModel.h"

#include <Application.h>
#include <QIcon>

#include "net/ApiDownload.h"
#include "net/NetJob.h"
#include "ui/widgets/ProjectItem.h"

#define LCSV_DISTRIBUTION_URL "https://mc.lcsv.com.br/lcsv/distribution.json"

namespace LCSV {

ListModel::ListModel(QObject* parent) : QAbstractListModel(parent) {}

int ListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_packs.size();
}

QVariant ListModel::data(const QModelIndex& index, int role) const
{
    int row = index.row();
    if (!index.isValid() || row < 0 || row >= m_packs.size())
        return {};

    const auto& pack = m_packs.at(row);

    switch (role) {
        case Qt::DisplayRole:
        case UserDataTypes::TITLE:
            return pack.name;
        case Qt::ToolTipRole:
        case UserDataTypes::DESCRIPTION:
            return pack.description;
        case Qt::DecorationRole: {
            if (m_logoMap.contains(pack.id))
                return m_logoMap.value(pack.id);
            auto placeholder = QIcon::fromTheme("grass");
            if (!pack.iconUrl.isEmpty())
                const_cast<ListModel*>(this)->requestLogo(pack.id, pack.iconUrl);
            return placeholder;
        }
        case Qt::UserRole: {
            QVariant v;
            v.setValue(pack);
            return v;
        }
        case Qt::SizeHintRole:
            return QSize(0, 58);
        case UserDataTypes::INSTALLED:
            return false;
        default:
            break;
    }
    return {};
}

void ListModel::request()
{
    beginResetModel();
    m_packs.clear();
    endResetModel();

    auto job = makeShared<NetJob>("LCSV::DistributionFetch", APPLICATION->network());
    auto [action, response] = Net::ApiDownload::makeByteArray(QUrl(LCSV_DISTRIBUTION_URL));
    job->addNetAction(action);
    m_jobPtr = job;
    m_jobPtr->start();

    connect(job.get(), &NetJob::succeeded, this, [this, response] { requestFinished(response); });
    connect(job.get(), &NetJob::failed, this, &ListModel::requestFailed);
}

void ListModel::requestFinished(QByteArray* responsePtr)
{
    QByteArray data = std::move(*responsePtr);
    m_jobPtr.reset();

    QList<IndexedPack> packs;
    if (!loadIndexFromBytes(data, packs)) {
        qWarning() << "LCSV: could not load distribution index";
        return;
    }

    beginInsertRows({}, m_packs.size(), m_packs.size() + packs.size() - 1);
    m_packs.append(packs);
    endInsertRows();

    emit packListUpdated();
}

void ListModel::requestFailed(QString reason)
{
    m_jobPtr.reset();
    qWarning() << "LCSV: distribution fetch failed:" << reason;
}

void ListModel::requestLogo(const QString& id, const QString& url)
{
    if (m_loadingLogos.contains(id) || m_failedLogos.contains(id))
        return;

    auto entry = APPLICATION->metacache()->resolveEntry("LCSVPacks", QString("icons/%1.png").arg(id));
    auto job = new NetJob(QString("LCSV::Icon::%1").arg(id), APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(Net::ApiDownload::makeCached(QUrl(url), entry));

    auto fullPath = entry->getFullPath();
    connect(job, &NetJob::succeeded, this, [this, id, fullPath, job] {
        job->deleteLater();
        emit logoLoaded(id, QIcon(fullPath));
    });
    connect(job, &NetJob::failed, this, [this, id, job] {
        job->deleteLater();
        emit logoFailed(id);
    });

    job->start();
    m_loadingLogos.append(id);
}

void ListModel::logoLoaded(QString id, QIcon icon)
{
    m_loadingLogos.removeAll(id);
    m_logoMap.insert(id, icon);
    for (int i = 0; i < m_packs.size(); i++) {
        if (m_packs[i].id == id)
            emit dataChanged(createIndex(i, 0), createIndex(i, 0), { Qt::DecorationRole });
    }
}

void ListModel::logoFailed(QString id)
{
    m_failedLogos.append(id);
    m_loadingLogos.removeAll(id);
}

}  // namespace LCSV

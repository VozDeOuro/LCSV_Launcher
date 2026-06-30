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

#include <QList>
#include <QMetaType>
#include <QString>

namespace LCSV {

struct IndexedPack {
    QString id;
    QString name;
    QString version;
    QString description;
    QString iconUrl;
    QString packUrl;
    QString manifestUrl;  // sidecar .index.json URL (FIX #2)
    QString serverAddress;
    QString minecraftVersion;
    QString modloader;
};

void loadIndexedPack(IndexedPack& pack, const QJsonObject& obj);
bool loadIndexFromBytes(const QByteArray& data, QList<IndexedPack>& out);

}  // namespace LCSV

Q_DECLARE_METATYPE(LCSV::IndexedPack)

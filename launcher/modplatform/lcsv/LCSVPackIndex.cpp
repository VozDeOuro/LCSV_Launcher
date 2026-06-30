// SPDX-License-Identifier: GPL-3.0-only
/*
 * LCSV Launcher
 * Copyright (C) 2026 LCSV Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "LCSVPackIndex.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "Json.h"

namespace LCSV {

void loadIndexedPack(IndexedPack& pack, const QJsonObject& obj)
{
    pack.id = Json::requireString(obj, "id");
    pack.name = Json::requireString(obj, "name");
    pack.version = Json::requireString(obj, "version");
    pack.description = obj.value("description").toString("");
    pack.iconUrl = obj.value("icon").toString("");
    pack.packUrl = Json::requireString(obj, "packUrl");
    pack.manifestUrl = obj.value("manifestUrl").toString("");
    pack.serverAddress = obj.value("serverAddress").toString("");
    pack.minecraftVersion = obj.value("minecraftVersion").toString("");
    pack.modloader = obj.value("modloader").toString("");
}

bool loadIndexFromBytes(const QByteArray& data, QList<IndexedPack>& out)
{
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "LCSV: failed to parse distribution.json:" << err.errorString();
        return false;
    }

    auto root = doc.object();

    // FIX #8: formatVersion guard
    int fv = root.value("formatVersion").toInt(1);
    if (fv > 1) {
        qWarning() << "LCSV: distribution.json formatVersion" << fv << "is newer than expected (1). Continuing anyway.";
    }

    auto packsArr = root.value("packs").toArray();
    for (const auto& raw : packsArr) {
        auto obj = raw.toObject();
        IndexedPack pack;
        try {
            loadIndexedPack(pack, obj);
        } catch (const JSONValidationError& e) {
            qWarning() << "LCSV: skipping pack due to parse error:" << e.cause();
            continue;
        }
        out.append(pack);
    }
    return true;
}

}  // namespace LCSV

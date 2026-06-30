// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#include "LCSVExportTask.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "FileSystem.h"
#include "archive/ArchiveWriter.h"
#include "minecraft/PackProfile.h"

#define LCSV_BASE_URL "https://mc.lcsv.com.br/lcsv"

namespace LCSV {

static const QStringList IGNORED_DIRS = { "logs", "crash-reports", "screenshots", "replay_recordings" };
static const QStringList IGNORED_FILES = { ".lcsv-meta.json" };

ExportTask::ExportTask(MinecraftInstance* instance, const ExportOptions& opts, QObject* parent)
    : Task(parent), m_instance(instance), m_opts(opts)
{}

void ExportTask::executeTask()
{
    setStatus(tr("LCSV Export: scanning files..."));

    QString gameRoot = m_instance->gameRoot();
    QDir gameDir(gameRoot);

    QList<FileEntry> files;
    QString filesOutDir = FS::PathCombine(m_opts.outputDir, "files");
    if (!FS::ensureFolderPathExists(filesOutDir)) {
        emitFailed(tr("Could not create output/files directory."));
        return;
    }

    QDirIterator it(gameRoot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo info = it.fileInfo();
        if (!info.isFile())
            continue;

        // skip ignored top-level dirs
        QString relPath = gameDir.relativeFilePath(info.absoluteFilePath());
        bool ignored = false;
        for (const auto& dir : IGNORED_DIRS) {
            if (relPath.startsWith(dir + "/") || relPath.startsWith(dir + "\\")) {
                ignored = true;
                break;
            }
        }
        if (ignored)
            continue;
        if (IGNORED_FILES.contains(relPath)) {
            ignored = true;
        }
        if (ignored)
            continue;

        if (m_opts.filter && m_opts.filter(info))
            continue;

        QFile f(info.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning() << "LCSV Export: cannot read" << info.absoluteFilePath();
            continue;
        }
        QByteArray data = f.readAll();
        QString hash = QCryptographicHash::hash(data, QCryptographicHash::Sha512).toHex();

        // copy to content store
        QString dest = FS::PathCombine(filesOutDir, hash);
        if (!QFile::exists(dest)) {
            QFile out(dest);
            if (!out.open(QIODevice::WriteOnly)) {
                emitFailed(tr("Could not write to output/files/%1").arg(hash));
                return;
            }
            out.write(data);
        }

        FileEntry entry;
        entry.path = relPath;
        entry.sha512 = hash;
        entry.size = info.size();
        files.append(entry);

        setProgress(files.size(), -1);
    }

    setStatus(tr("LCSV Export: building pack manifest..."));

    // Copy icon into {outputDir}/icons/{packId}.{ext} and derive iconUrl
    if (!m_opts.iconLocalPath.isEmpty() && QFile::exists(m_opts.iconLocalPath)) {
        QString ext = QFileInfo(m_opts.iconLocalPath).suffix();
        if (ext.isEmpty())
            ext = "png";
        QString iconsDir = FS::PathCombine(m_opts.outputDir, "icons");
        if (FS::ensureFolderPathExists(iconsDir)) {
            QString iconDest = FS::PathCombine(iconsDir, m_opts.packId + "." + ext);
            QFile::remove(iconDest);
            QFile::copy(m_opts.iconLocalPath, iconDest);
            m_opts.iconUrl = QString("%1/icons/%2.%3").arg(LCSV_BASE_URL, m_opts.packId, ext);
        }
    }

    QString packsOutDir = FS::PathCombine(m_opts.outputDir, "packs");
    if (!FS::ensureFolderPathExists(packsOutDir)) {
        emitFailed(tr("Could not create output/packs directory."));
        return;
    }

    QByteArray indexJson = buildIndex(files);

    QString base = m_opts.packId + "-" + m_opts.version;
    QString indexFilename = base + ".index.json";
    QString mrpackFilename = base + ".mrpack";

    // Write bare sidecar index
    QString indexPath = FS::PathCombine(packsOutDir, indexFilename);
    {
        QFile f(indexPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emitFailed(tr("Could not write %1").arg(indexFilename));
            return;
        }
        f.write(indexJson);
    }

    // Write .mrpack zip
    QString mrpackPath = FS::PathCombine(packsOutDir, mrpackFilename);
    if (!buildMrpack(indexJson, mrpackPath)) {
        emitFailed(tr("Could not create %1").arg(mrpackFilename));
        return;
    }

    // Write distribution-entry.json
    if (!writeDistributionEntry(mrpackFilename, indexFilename)) {
        emitFailed(tr("Could not write distribution-entry.json"));
        return;
    }

    emitSucceeded();
}

QByteArray ExportTask::buildIndex(const QList<FileEntry>& files) const
{
    QJsonObject root;
    root["formatVersion"] = 1;
    root["game"] = "minecraft";
    root["versionId"] = m_opts.version;
    root["name"] = m_opts.name;
    if (!m_opts.description.isEmpty())
        root["summary"] = m_opts.description;

    // collect loader components from the instance
    auto* profile = m_instance->getPackProfile();
    QJsonObject dependencies;
    auto addComp = [&](const QString& uid, const QString& key) {
        auto comp = profile->getComponent(uid);
        if (comp)
            dependencies[key] = comp->m_version;
    };
    addComp("net.minecraft", "minecraft");
    addComp("org.quiltmc.quilt-loader", "quilt-loader");
    addComp("net.fabricmc.fabric-loader", "fabric-loader");
    addComp("net.minecraftforge", "forge");
    addComp("net.neoforged", "neoforge");
    root["dependencies"] = dependencies;

    QJsonArray filesArr;
    for (const auto& fe : files) {
        QJsonObject obj;
        obj["path"] = fe.path;
        obj["fileSize"] = fe.size;

        QJsonObject hashes;
        hashes["sha512"] = fe.sha512;
        obj["hashes"] = hashes;

        QJsonArray downloads;
        downloads.append(QString("%1/files/%2").arg(LCSV_BASE_URL, fe.sha512));
        obj["downloads"] = downloads;

        QJsonObject env;
        env["client"] = "required";
        env["server"] = "required";
        obj["env"] = env;

        filesArr.append(obj);
    }
    root["files"] = filesArr;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool ExportTask::buildMrpack(const QByteArray& indexJson, const QString& mrpackPath) const
{
    MMCZip::ArchiveWriter writer(mrpackPath);
    if (!writer.open())
        return false;

    // Add the index
    if (!writer.addFile("modrinth.index.json", indexJson))
        return false;

    // Add an empty overrides dir marker so the zip has the entry
    if (!writer.addFile("overrides/.gitkeep", QByteArray()))
        return false;

    return writer.close();
}

bool ExportTask::writeDistributionEntry(const QString& mrpackFilename, const QString& indexFilename) const
{
    QJsonObject newEntry;
    newEntry["id"] = m_opts.packId;
    newEntry["name"] = m_opts.name;
    newEntry["version"] = m_opts.version;
    newEntry["description"] = m_opts.description;
    if (!m_opts.iconUrl.isEmpty())
        newEntry["icon"] = m_opts.iconUrl;
    newEntry["packUrl"] = QString("%1/packs/%2").arg(LCSV_BASE_URL, mrpackFilename);
    newEntry["manifestUrl"] = QString("%1/packs/%2").arg(LCSV_BASE_URL, indexFilename);
    if (!m_opts.serverAddress.isEmpty())
        newEntry["serverAddress"] = m_opts.serverAddress;

    // Read existing distribution.json and merge (upsert by packId)
    QString distPath = FS::PathCombine(m_opts.outputDir, "distribution.json");
    QJsonArray packs;
    {
        QFile existing(distPath);
        if (existing.open(QIODevice::ReadOnly)) {
            auto doc = QJsonDocument::fromJson(existing.readAll());
            if (doc.isObject())
                packs = doc.object()["packs"].toArray();
        }
    }

    // Replace existing entry with same id, or append
    bool replaced = false;
    for (int i = 0; i < packs.size(); ++i) {
        if (packs[i].toObject()["id"].toString() == m_opts.packId) {
            packs[i] = newEntry;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        packs.append(newEntry);

    QJsonObject root;
    root["formatVersion"] = 1;
    root["packs"] = packs;

    QFile f(distPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

}  // namespace LCSV

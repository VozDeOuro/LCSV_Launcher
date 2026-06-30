// SPDX-License-Identifier: GPL-3.0-only
// LCSV Launcher - Copyright (C) 2026 LCSV Contributors

#include "LCSVExportDialog.h"
#include "ui_LCSVExportDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPixmap>

#include "Application.h"
#include "FileSystem.h"
#include "icons/IconList.h"
#include "modplatform/lcsv/LCSVExportTask.h"
#include "ui/dialogs/ProgressDialog.h"

static const QStringList DEFAULT_BLOCKED = {
    // Runtime / player data — never ship
    "logs",
    "crash-reports",
    "screenshots",
    "saves",
    "backups",
    "replay_recordings",
    "replay_videos",
    // Map / chunk cache — generated per player
    "Distant_Horizons_server_data",
    "Distant_Horizons_Level_Data",
    ".bobby",
    "journeymap/data",
    "xaero",
    // Mod runtime caches — regenerated on launch
    ".mixin.out",
    "ESM",
    "debug",
    "dynamic-data-pack-cache",
    "dynamic-resource-pack-cache",
    "immersive_paintings_cache",
    "downloads",
    // User-specific generated files
    "texturepacks",
    "usercache.json",
    "usernamecache.json",
    "command_history.txt",
};

static QString exportConfigPath(MinecraftInstance* instance)
{
    return FS::PathCombine(instance->instanceRoot(), "lcsv-export.json");
}

LCSVExportDialog::LCSVExportDialog(MinecraftInstance* instance, QWidget* parent)
    : QDialog(parent), ui(new Ui::LCSVExportDialog), m_instance(instance)
{
    ui->setupUi(this);

    // Pre-fill from last export if available
    QFile cfg(exportConfigPath(instance));
    if (cfg.open(QIODevice::ReadOnly)) {
        auto obj = QJsonDocument::fromJson(cfg.readAll()).object();
        ui->packIdEdit->setText(obj.value("packId").toString());
        ui->nameEdit->setText(obj.value("name").toString(instance->name()));
        ui->versionEdit->setText(obj.value("version").toString());
        ui->descriptionEdit->setText(obj.value("description").toString());
        ui->serverAddressEdit->setText(obj.value("serverAddress").toString());
        ui->outputDirEdit->setText(obj.value("outputDir").toString());
        QString savedIcon = obj.value("iconLocalPath").toString();
        if (!savedIcon.isEmpty() && QFile::exists(savedIcon))
            setIconPath(savedIcon);
    } else {
        ui->nameEdit->setText(instance->name());
    }

    // File tree setup
    auto* model = new QFileSystemModel(this);
    model->setIconProvider(&m_icons);

    m_proxy = new FileIgnoreProxy(instance->gameRoot(), this);
    m_proxy->ignoreFilesWithName().append({ ".DS_Store", "thumbs.db", "Thumbs.db" });
    m_proxy->ignoreFilesWithSuffix().append(".pw.toml");
    m_proxy->ignoreFilesMatchingPattern().append(
        QRegularExpression(R"(^Xaero(?:Waypoints|WorldMap)_[Bb]ackup.+$)", QRegularExpression::CaseInsensitiveOption));
    m_proxy->ignoreFilesMatchingPattern().append(QRegularExpression(R"(^hs_err_pid\d+\.log$)"));
    m_proxy->ignoreFilesMatchingPattern().append(QRegularExpression(R"(^replay_pid\d+\.log$)"));
    m_proxy->setSourceModel(model);
    m_proxy->setBlockedPaths(DEFAULT_BLOCKED);

    ui->files->setModel(m_proxy);
    ui->files->setRootIndex(m_proxy->mapFromSource(model->index(instance->gameRoot())));
    ui->files->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->files->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->files->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->files->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    model->setRootPath(instance->gameRoot());

    connect(ui->outputDirBtn,    &QPushButton::clicked, this, &LCSVExportDialog::onBrowse);
    connect(ui->iconBrowseBtn,   &QPushButton::clicked, this, &LCSVExportDialog::onBrowseIcon);
    connect(ui->iconInstanceBtn, &QPushButton::clicked, this, &LCSVExportDialog::onUseInstanceIcon);
    connect(ui->iconClearBtn,    &QPushButton::clicked, this, &LCSVExportDialog::onClearIcon);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &LCSVExportDialog::onAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

LCSVExportDialog::~LCSVExportDialog()
{
    delete ui;
}

void LCSVExportDialog::setIconPath(const QString& path)
{
    m_iconLocalPath = path;
    QPixmap px(path);
    if (!px.isNull()) {
        ui->iconPreview->setPixmap(px.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        ui->iconPathLabel->setText(QFileInfo(path).fileName());
    } else {
        ui->iconPreview->clear();
        ui->iconPathLabel->setText(tr("(invalid image)"));
    }
}

void LCSVExportDialog::onBrowseIcon()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Select Icon"), QDir::homePath(),
                                                tr("Images (*.png *.jpg *.jpeg *.webp)"));
    if (!path.isEmpty())
        setIconPath(path);
}

void LCSVExportDialog::onUseInstanceIcon()
{
    // Save the instance icon to a temp PNG in the instance root, then use it
    QString destPath = FS::PathCombine(m_instance->instanceRoot(), "lcsv-icon-export.png");
    APPLICATION->icons()->saveIcon(m_instance->iconKey(), destPath, "PNG");
    if (QFile::exists(destPath))
        setIconPath(destPath);
    else
        QMessageBox::warning(this, tr("Icon"), tr("Could not export instance icon."));
}

void LCSVExportDialog::onClearIcon()
{
    m_iconLocalPath.clear();
    ui->iconPreview->clear();
    ui->iconPathLabel->setText(tr("(none)"));
}

void LCSVExportDialog::onBrowse()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Folder"), QDir::homePath());
    if (!dir.isEmpty())
        ui->outputDirEdit->setText(dir);
}

void LCSVExportDialog::saveConfig(const LCSV::ExportOptions& opts) const
{
    QJsonObject obj;
    obj["packId"] = opts.packId;
    obj["name"] = opts.name;
    obj["version"] = opts.version;
    obj["description"] = opts.description;
    obj["iconLocalPath"] = opts.iconLocalPath;
    obj["serverAddress"] = opts.serverAddress;
    obj["outputDir"] = opts.outputDir;

    QFile f(exportConfigPath(m_instance));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(obj).toJson());
}

void LCSVExportDialog::onAccepted()
{
    if (ui->packIdEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Field"), tr("Pack ID is required."));
        return;
    }
    if (ui->versionEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Field"), tr("Version is required."));
        return;
    }
    if (ui->outputDirEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Field"), tr("Please select an output folder."));
        return;
    }

    LCSV::ExportOptions opts;
    opts.packId = ui->packIdEdit->text().trimmed();
    opts.name = ui->nameEdit->text().trimmed();
    opts.version = ui->versionEdit->text().trimmed();
    opts.description = ui->descriptionEdit->text().trimmed();
    opts.iconLocalPath = m_iconLocalPath;
    opts.serverAddress = ui->serverAddressEdit->text().trimmed();
    opts.outputDir = ui->outputDirEdit->text().trimmed();
    opts.filter = std::bind(&FileIgnoreProxy::filterFile, m_proxy, std::placeholders::_1);

    auto task = makeShared<LCSV::ExportTask>(m_instance, opts);

    ProgressDialog dlg(this);
    dlg.setSkipButton(true, tr("Abort"));
    if (dlg.execWithTask(task.get()) == QDialog::Accepted) {
        saveConfig(opts);
        QMessageBox::information(this, tr("Export Complete"),
                                 tr("Pack exported to:\n%1").arg(opts.outputDir));
        accept();
    }
}

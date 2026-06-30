#include "OfflineSkinStep.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

#include "Application.h"
#include "minecraft/auth/Parsers.h"

OfflineSkinStep::OfflineSkinStep(AccountData* data) : AuthStep(data) {}

QString OfflineSkinStep::describe()
{
    return tr("Getting offline account skin.");
}

void OfflineSkinStep::perform()
{
    if (m_data->minecraftProfile.name.isEmpty()) {
        finish(tr("Offline account has no username."));
        return;
    }

    startNameLookup();
}

void OfflineSkinStep::abort()
{
    if (m_task) {
        m_task->abort();
    }
}

void OfflineSkinStep::finish(QString message)
{
    emit finished(AccountTaskState::STATE_WORKING, message);
}

void OfflineSkinStep::startNameLookup()
{
    const QString encodedName = QString::fromLatin1(QUrl::toPercentEncoding(m_data->minecraftProfile.name));
    QUrl url(QStringLiteral("https://api.mojang.com/users/profiles/minecraft/%1").arg(encodedName));

    auto [request, response] = Net::Download::makeByteArray(url);
    m_request = request;
    m_request->enableAutoRetry(true);

    m_task.reset(new NetJob("OfflineSkinNameLookup", APPLICATION->network()));
    m_task->setAskRetry(false);
    m_task->addNetAction(m_request);

    connect(m_task.get(), &Task::finished, this, [this, response] { onNameLookupDone(response); });
    m_task->start();
}

void OfflineSkinStep::onNameLookupDone(QByteArray* response)
{
    if (m_request->error() != QNetworkReply::NoError) {
        finish(tr("No online skin profile found for offline account."));
        return;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(*response, &jsonError);
    if (jsonError.error || !doc.isObject()) {
        finish(tr("Offline skin profile response could not be parsed."));
        return;
    }

    const QJsonObject obj = doc.object();
    const QString uuid = obj.value("id").toString();
    if (uuid.isEmpty()) {
        finish(tr("Offline skin profile has no UUID."));
        return;
    }

    QTimer::singleShot(0, this, [this, uuid] { startSessionLookup(uuid); });
}

void OfflineSkinStep::startSessionLookup(const QString& uuid)
{
    QUrl url(QStringLiteral("https://sessionserver.mojang.com/session/minecraft/profile/%1?unsigned=false").arg(uuid));

    auto [request, response] = Net::Download::makeByteArray(url);
    m_request = request;
    m_request->enableAutoRetry(true);

    m_task.reset(new NetJob("OfflineSkinSessionLookup", APPLICATION->network()));
    m_task->setAskRetry(false);
    m_task->addNetAction(m_request);

    connect(m_task.get(), &Task::finished, this, [this, response] { onSessionLookupDone(response); });
    m_task->start();
}

void OfflineSkinStep::onSessionLookupDone(QByteArray* response)
{
    if (m_request->error() != QNetworkReply::NoError) {
        finish(tr("Offline skin texture lookup failed."));
        return;
    }

    MinecraftProfile onlineProfile;
    if (!Parsers::parseMinecraftProfileMojang(*response, onlineProfile) || onlineProfile.skin.url.isEmpty()) {
        finish(tr("Offline skin texture response could not be parsed."));
        return;
    }

    QTimer::singleShot(0, this, [this, skin = onlineProfile.skin] { startSkinDownload(skin); });
}

void OfflineSkinStep::startSkinDownload(const Skin& skin)
{
    m_skin = skin;
    QUrl url(m_skin.url);

    auto [request, response] = Net::Download::makeByteArray(url);
    m_request = request;
    m_request->enableAutoRetry(true);

    m_task.reset(new NetJob("OfflineSkinDownload", APPLICATION->network()));
    m_task->setAskRetry(false);
    m_task->addNetAction(m_request);

    connect(m_task.get(), &Task::finished, this, [this, response] { onSkinDownloadDone(response); });
    m_task->start();
}

void OfflineSkinStep::onSkinDownloadDone(QByteArray* response)
{
    if (m_request->error() == QNetworkReply::NoError) {
        m_skin.data = *response;
        m_data->minecraftProfile.skin = m_skin;
        finish(tr("Got offline account skin."));
        return;
    }

    finish(tr("Offline skin download failed."));
}

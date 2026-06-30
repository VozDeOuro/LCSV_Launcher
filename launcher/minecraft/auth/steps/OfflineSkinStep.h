#pragma once

#include "minecraft/auth/AuthStep.h"
#include "net/Download.h"
#include "net/NetJob.h"

class OfflineSkinStep : public AuthStep {
    Q_OBJECT

   public:
    explicit OfflineSkinStep(AccountData* data);
    virtual ~OfflineSkinStep() noexcept = default;

    void perform() override;
    void abort() override;

    QString describe() override;

   private:
    void finish(QString message);
    void startNameLookup();
    void startSessionLookup(const QString& uuid);
    void startSkinDownload(const Skin& skin);
    void onNameLookupDone(QByteArray* response);
    void onSessionLookupDone(QByteArray* response);
    void onSkinDownloadDone(QByteArray* response);

   private:
    Net::Download::Ptr m_request;
    NetJob::Ptr m_task;
    Skin m_skin;
};

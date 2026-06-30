#include "LoginWizardPage.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "ui/dialogs/ChooseOfflineNameDialog.h"
#include "ui/dialogs/MSALoginDialog.h"
#include "ui_LoginWizardPage.h"

#include "Application.h"

LoginWizardPage::LoginWizardPage(QWidget* parent) : BaseWizardPage(parent), ui(new Ui::LoginWizardPage)
{
    ui->setupUi(this);
}

LoginWizardPage::~LoginWizardPage()
{
    delete ui;
}

void LoginWizardPage::initializePage() {}

bool LoginWizardPage::validatePage()
{
    return true;
}

void LoginWizardPage::retranslate()
{
    ui->retranslateUi(this);
}

void LoginWizardPage::on_pushButton_clicked()
{
    wizard()->hide();
    auto account = MSALoginDialog::newAccount(nullptr);
    wizard()->show();
    if (account) {
        APPLICATION->accounts()->addAccount(account);
        APPLICATION->accounts()->setDefaultAccount(account);
        if (wizard()->currentId() == wizard()->pageIds().last()) {
            wizard()->accept();
        } else {
            wizard()->next();
        }
    }
}

void LoginWizardPage::on_pushButtonOffline_clicked()
{
    ChooseOfflineNameDialog dialog(tr("Enter a username for your offline account."), this);
    if (dialog.exec() == QDialog::Accepted && !dialog.getUsername().isEmpty()) {
        auto account = MinecraftAccount::createOffline(dialog.getUsername());
        APPLICATION->accounts()->addAccount(account);
        APPLICATION->accounts()->setDefaultAccount(account);
        account->login()->start();  // Best-effort fetches skin head for matching online username.
        if (wizard()->currentId() == wizard()->pageIds().last()) {
            wizard()->accept();
        } else {
            wizard()->next();
        }
    }
}

#include "ui/AboutDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("applicationInfoDialog"));
    setWindowTitle(tr("关于 PAT"));
    resize(680, 520);

    auto* layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("infoTabs"));

    auto* versionPage = new QWidget(tabs_);
    auto* versionLayout = new QVBoxLayout(versionPage);
    auto* title = new QLabel(tr("PAT 飞参解析工具"), versionPage);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    title->setFont(titleFont);
    versionLayout->addWidget(title);

    auto* details = new QFormLayout;
    auto* version = new QLabel(QStringLiteral(PAT_APP_VERSION), versionPage);
    version->setObjectName(QStringLiteral("versionValue"));
    details->addRow(tr("软件版本"), version);
    details->addRow(tr("发布状态"), new QLabel(tr("开发构建（尚未发布）"), versionPage));
    details->addRow(tr("Qt 运行版本"), new QLabel(QString::fromLatin1(qVersion()), versionPage));
    details->addRow(tr("操作系统"), new QLabel(QSysInfo::prettyProductName(), versionPage));
    versionLayout->addLayout(details);
    versionLayout->addStretch();
    tabs_->addTab(versionPage, tr("版本信息"));

    auto* changelog = new QTextBrowser(tabs_);
    changelog->setObjectName(QStringLiteral("changelogView"));
    changelog->setOpenExternalLinks(false);
    QFile changelogFile(QStringLiteral(":/pat/changelog.md"));
    if (changelogFile.open(QIODevice::ReadOnly)) {
        changelog->setMarkdown(QString::fromUtf8(changelogFile.readAll()));
    } else {
        changelog->setPlainText(tr("更新日志无法读取。"));
    }
    tabs_->addTab(changelog, tr("更新日志"));

    layout->addWidget(tabs_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void AboutDialog::SelectPage(Page page) {
    tabs_->setCurrentIndex(page == Page::Version ? 0 : 1);
}

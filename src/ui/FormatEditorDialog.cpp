#include "ui/FormatEditorDialog.h"

#include "core/FormatDefinition.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

FormatEditorDialog::FormatEditorDialog(const QString& title, const QString& initialText, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(title);
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    editor_ = new QTextEdit(this);
    editor_->setPlainText(initialText);
    editor_->setTabStopDistance(24);
    layout->addWidget(editor_, /*stretch=*/1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* validateBtn = buttons->addButton(tr("验证"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(validateBtn, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!ValidateText(editor_->toPlainText(), error)) {
            QMessageBox::warning(this, tr("格式不合法"), error);
            return;
        }
        QMessageBox::information(this, tr("验证通过"), tr("格式校验通过"));
    });

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        QString error;
        if (!ValidateText(editor_->toPlainText(), error)) {
            QMessageBox::warning(this, tr("格式不合法"), error);
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(900, 700);
}

bool FormatEditorDialog::Run(QString& outText) {
    if (exec() != QDialog::Accepted) {
        return false;
    }
    outText = editor_->toPlainText();
    return true;
}

bool FormatEditorDialog::ValidateText(const QString& text, QString& errorMessage) const {
    pat::FormatDefinition tmp;
    return pat::LoadFormatFromJsonData(text.toUtf8(), tmp, errorMessage);
}

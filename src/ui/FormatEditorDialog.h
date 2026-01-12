#pragma once

#include <QDialog>
#include <QString>

class QTextEdit;

class FormatEditorDialog : public QDialog {
    Q_OBJECT

public:
    FormatEditorDialog(const QString& title, const QString& initialText, QWidget* parent = nullptr);
    bool Run(QString& outText);

private:
    bool ValidateText(const QString& text, QString& errorMessage) const;

    QTextEdit* editor_ = nullptr;
};

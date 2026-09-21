#pragma once

#include <QDialog>

class QTabWidget;

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    enum class Page { Version, Changelog };

    explicit AboutDialog(QWidget* parent = nullptr);
    void SelectPage(Page page);

private:
    QTabWidget* tabs_ = nullptr;
};

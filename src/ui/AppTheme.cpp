#include "ui/AppTheme.h"

#include <QCoreApplication>

#include <utility>

namespace {

QString CssColor(const QColor& color) {
    return color.name(QColor::HexRgb);
}

AppTheme IndoorDarkTheme() {
    return {
        AppThemeId::IndoorDark, QStringLiteral("indoor_dark"),
        QColor("#171c22"), QColor("#202b37"), QColor("#1a2128"),
        QColor("#17252c"), QColor("#1d2b37"), QColor("#2a3b48"),
        QColor("#48606a"), QColor("#315d70"), QColor("#e5eef2"),
        QColor("#aebdc5"), QColor("#2d4b57"), QColor("#386170"),
        QColor("#1a2128"), QColor("#516979"),
        QColor("#19252e"), QColor("#1e2b32"), QColor("#4d6d7e"),
        QColor("#5c757b"), QColor("#141d24"), QColor("#182127"),
        QColor("#d2d2d2"), QColor("#8c8c8c"), QColor("#3c3c3c"),
        QColor(20, 20, 22, 220), QColor(90, 90, 90, 220),
        QColor("#ff3030"), QColor("#ffd700"), QColor("#5a5a5a"),
        QColor("#ffe35a"), QColor("#091016"),
        {QColor("#5ac8ff"), QColor("#ff8c00"), QColor("#78dc78"),
         QColor("#dc78dc"), QColor("#ff5a5a"), QColor("#78a0ff"),
         QColor("#f1d84b"), QColor("#3dd6c6")},
        1.6, 2.5
    };
}

AppTheme IndoorLightTheme() {
    return {
        AppThemeId::IndoorLight, QStringLiteral("indoor_light"),
        QColor("#e8edf1"), QColor("#dce5eb"), QColor("#f3f6f8"),
        QColor("#ffffff"), QColor("#edf2f5"), QColor("#cbd8e0"),
        QColor("#9aadb9"), QColor("#91c7e4"), QColor("#17232b"),
        QColor("#52646f"), QColor("#c5d6df"), QColor("#b4ccd8"),
        QColor("#e6ecef"), QColor("#9fb3bf"),
        QColor("#ffffff"), QColor("#f5f8fa"), QColor("#8fa7b5"),
        QColor("#9aacb6"), QColor("#ffffff"), QColor("#f8fafb"),
        QColor("#17232b"), QColor("#4d5b63"), QColor("#ced8de"),
        QColor(255, 255, 255, 235), QColor(117, 132, 142, 220),
        QColor("#c1121f"), QColor("#8a3f00"), QColor("#78868e"),
        QColor("#d05a00"), QColor("#ffffff"),
        {QColor("#0067b9"), QColor("#d04a00"), QColor("#008552"),
         QColor("#7b3fb2"), QColor("#c21f30"), QColor("#007b83"),
         QColor("#785400"), QColor("#a62773")},
        1.8, 2.7
    };
}

AppTheme OutdoorHighContrastTheme() {
    return {
        AppThemeId::OutdoorHighContrast, QStringLiteral("outdoor_high_contrast"),
        QColor("#ffffff"), QColor("#ffffff"), QColor("#fffef8"),
        QColor("#ffffff"), QColor("#f0f0f0"), QColor("#d8d8d8"),
        QColor("#525252"), QColor("#ffcf40"), QColor("#000000"),
        QColor("#303030"), QColor("#ffd34f"), QColor("#ffc21a"),
        QColor("#ffffff"), QColor("#303030"),
        QColor("#ffffff"), QColor("#fff8d9"), QColor("#202020"),
        QColor("#3a3a3a"), QColor("#ffffff"), QColor("#fffdf2"),
        QColor("#000000"), QColor("#000000"), QColor("#a0a0a0"),
        QColor(255, 255, 255, 245), QColor(20, 20, 20, 235),
        QColor("#d00000"), QColor("#000000"), QColor("#676767"),
        QColor("#ff9f00"), QColor("#000000"),
        {QColor("#0047ab"), QColor("#c43b00"), QColor("#006b3c"),
         QColor("#6f2da8"), QColor("#c00000"), QColor("#00737a"),
         QColor("#6b3e00"), QColor("#9b005d")},
        2.4, 3.2
    };
}

}  // namespace

const AppTheme& ThemeFor(AppThemeId id) {
    static const AppTheme indoorDark = IndoorDarkTheme();
    static const AppTheme indoorLight = IndoorLightTheme();
    static const AppTheme outdoor = OutdoorHighContrastTheme();
    switch (id) {
    case AppThemeId::IndoorLight: return indoorLight;
    case AppThemeId::OutdoorHighContrast: return outdoor;
    case AppThemeId::IndoorDark: return indoorDark;
    }
    return indoorDark;
}

QVector<AppThemeId> AvailableThemeIds() {
    return {AppThemeId::IndoorDark, AppThemeId::IndoorLight,
            AppThemeId::OutdoorHighContrast};
}

QString ThemeKey(AppThemeId id) {
    return ThemeFor(id).key;
}

AppThemeId ThemeIdFromKey(const QString& key) {
    const QString normalized = key.trimmed().toLower();
    for (AppThemeId id : AvailableThemeIds()) {
        if (ThemeKey(id) == normalized) return id;
    }
    return AppThemeId::IndoorDark;
}

QString ThemeDisplayName(AppThemeId id) {
    switch (id) {
    case AppThemeId::IndoorLight:
        return QCoreApplication::translate("AppTheme", "室内亮色");
    case AppThemeId::OutdoorHighContrast:
        return QCoreApplication::translate("AppTheme", "户外高对比");
    case AppThemeId::IndoorDark:
        return QCoreApplication::translate("AppTheme", "室内深色");
    }
    return {};
}

QString BuildApplicationStyleSheet(const AppTheme& theme) {
    QString style = QStringLiteral(R"(
        QMainWindow, QDialog { background: @WINDOW@; color: @TEXT@; }
        QWidget#mainCentral { background: @WINDOW@; }
        QWidget#signalPane { background: @PANEL@; }
        QWidget#chartPane, QWidget#trajectoryView { background: @CONTENT@; }
        QWidget#serialPanel { background: @PANEL@; color: @TEXT@; }
        QLabel, QCheckBox, QRadioButton, QGroupBox, QDockWidget { color: @TEXT@; }
        QMenuBar, QMenu { background: @PANEL@; color: @TEXT@; }
        QMenuBar::item:selected, QMenu::item:selected { background: @SELECTION@; }
        QTreeWidget#signalTree, QTreeWidget#signalSearchResults, QTableWidget, QTextBrowser {
            background: @INPUT@; color: @TEXT@; alternate-background-color: @ALTERNATE@;
            border: 1px solid @BORDER@; selection-background-color: @SELECTION@;
        }
        QHeaderView::section { background: @HEADER@; color: @TEXT@;
                               border: 0; padding: 4px; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
            background: @INPUT@; color: @TEXT@; border: 1px solid @BORDER@;
            border-radius: 3px; padding: 3px;
        }
        QComboBox QAbstractItemView { background: @INPUT@; color: @TEXT@;
                                      selection-background-color: @SELECTION@; }
        QPushButton { background: @BUTTON@; color: @TEXT@; border: 1px solid @BORDER@;
                      border-radius: 4px; padding: 4px 8px; }
        QPushButton:hover { background: @BUTTON_HOVER@; }
        QPushButton:disabled { color: @SUBDUED@; }
        QTabWidget::pane { border: 1px solid @BORDER@; background: @CONTENT@; }
        QTabBar::tab { background: @PANEL@; color: @TEXT@; border: 1px solid @BORDER@;
                       padding: 5px 12px; }
        QTabBar::tab:selected { background: @SELECTION@; }
        QStatusBar { background: @PANEL@; color: @TEXT@; }
        QToolTip { background: @INPUT@; color: @TEXT@; border: 1px solid @BORDER@; }
        QSplitter::handle { background: @SPLITTER@; }
    )");
    const std::pair<const char*, QColor> replacements[] = {
        {"@WINDOW@", theme.windowBackground}, {"@PANEL@", theme.panelBackground},
        {"@CONTENT@", theme.contentBackground}, {"@INPUT@", theme.inputBackground},
        {"@ALTERNATE@", theme.alternateBackground}, {"@HEADER@", theme.headerBackground},
        {"@BORDER@", theme.border}, {"@SELECTION@", theme.selectionBackground},
        {"@TEXT@", theme.text}, {"@SUBDUED@", theme.subduedText},
        {"@BUTTON@", theme.buttonBackground}, {"@BUTTON_HOVER@", theme.buttonHover},
        {"@SPLITTER@", theme.splitter}
    };
    for (const auto& [token, color] : replacements) {
        style.replace(QString::fromLatin1(token), CssColor(color));
    }
    return style;
}

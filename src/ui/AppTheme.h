#pragma once

#include <QColor>
#include <QString>
#include <QVector>

enum class AppThemeId {
    IndoorDark,
    IndoorLight,
    OutdoorHighContrast
};

struct AppTheme {
    AppThemeId id = AppThemeId::IndoorDark;
    QString key;

    QColor windowBackground;
    QColor panelBackground;
    QColor contentBackground;
    QColor inputBackground;
    QColor alternateBackground;
    QColor headerBackground;
    QColor border;
    QColor selectionBackground;
    QColor text;
    QColor subduedText;
    QColor buttonBackground;
    QColor buttonHover;
    QColor canvasBackground;
    QColor splitter;

    QColor chartBackgroundEven;
    QColor chartBackgroundOdd;
    QColor chartBorderEven;
    QColor chartBorderOdd;
    QColor plotBackgroundEven;
    QColor plotBackgroundOdd;
    QColor axisText;
    QColor axisLine;
    QColor gridLine;
    QColor legendBackground;
    QColor legendBorder;
    QColor cursorLine;
    QColor cursorValue;
    QColor zeroLine;
    QColor guideLine;
    QColor guideOutline;
    QVector<QColor> seriesColors;
    qreal seriesLineWidth = 1.6;
    qreal guideLineWidth = 2.5;
};

const AppTheme& ThemeFor(AppThemeId id);
QVector<AppThemeId> AvailableThemeIds();
QString ThemeKey(AppThemeId id);
AppThemeId ThemeIdFromKey(const QString& key);
QString ThemeDisplayName(AppThemeId id);
QString BuildApplicationStyleSheet(const AppTheme& theme);

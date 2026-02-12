#pragma once
#include <QString>
#include "ThemeDefinitions.h"

extern bool g_isDarkMode;

namespace Style {
    const QString ColorAccentDark  = ThemeDef::Accent;
    const QString ColorDanger      = ThemeDef::Danger;
    const QString ColorWarning     = ThemeDef::Warning;
    const QString ColorInfo        = ThemeDef::Accent;
    const QString ColorNeutral     = "#9B59B6";

    inline QString getGlobalStyle() { return g_isDarkMode ? ThemeDef::Dark::Global : ThemeDef::Light::Global; }
}
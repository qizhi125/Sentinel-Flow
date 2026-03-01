#pragma once
#include <QString>
#include "ThemeDefinitions.h"

extern bool g_isDarkMode;

namespace Style {
    inline QString ColorAccent()  { return g_isDarkMode ? ThemeDef::Accent : ThemeDef::AccentLight; }
    inline QString ColorDanger()  { return g_isDarkMode ? ThemeDef::Danger : "#D93025"; }
    inline QString ColorWarning() { return g_isDarkMode ? ThemeDef::Warning : "#E67E22"; }
    inline QString ColorInfo()    { return "#3498DB"; }
    inline QString ColorNeutral() { return "#9B59B6"; }

    inline QString ColorTCP()  { return "#5D5FEF"; }
    inline QString ColorUDP()  { return g_isDarkMode ? "#00BFA5" : "#009688"; }
    inline QString ColorHTTP() { return "#E67E22"; }
    inline QString ColorTLS()  { return "#E74C3C"; }

    inline QString getGlobalStyle() {
        return g_isDarkMode ? ThemeDef::Dark::Global : ThemeDef::Light::Global;
    }
}
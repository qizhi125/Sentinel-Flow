#pragma once
#include "presentation/views/styles/global.h"

namespace TrafficStyle {
    // 保留基础绘图色彩映射，剥离所有 QSS 字符串
    inline const QString ColorTCP  = "#5D5FEF";
    inline const QString ColorUDP  = "#00BFA5";
    inline const QString ColorHTTP = "#E67E22";
    inline const QString ColorTLS  = "#9B59B6";
    inline const QString ColorOther= "#888888";

    inline QString getHexViewStyle() { return ""; }
    inline QString getSearchBox() { return ""; }
    inline QString getComboBox() { return ""; }
    inline QString getCheckBox() { return ""; }
    inline QString getBtnNormal() { return ""; }
    inline QString getBtnPaused() { return ""; }
    inline QString getBtnDanger() { return ""; }
    inline const QString SelectedInfo = "";
    inline const QString ProtoTree = "";
}
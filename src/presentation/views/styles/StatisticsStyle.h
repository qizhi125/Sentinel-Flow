#pragma once
#include "presentation/views/styles/global.h"

namespace StatisticsStyle {
    // 临时硬编码规避函数指针冲突，供 QPainter 硬件加速使用
    inline const QString ColorRate      = "#00BFA5";
    inline const QString ColorTotal     = "#3498DB";
    inline const QString ColorPPS       = "#9B59B6";
    inline const QString ColorThreat    = "#FF4444";

    inline const QString ColorDefault   = "#888888";
    inline const QString ColorTCP       = "#5D5FEF";
    inline const QString ColorUDP       = "#00BFA5";
    inline const QString ColorTLS       = "#E74C3C";
    inline const QString ColorHTTP      = "#F1C40F";
    inline const QString ColorDDoS      = "#E74C3C";
    inline const QString ColorMalware   = "#9B59B6";
    inline const QString ColorPhishing  = "#F1C40F";

    inline const QString SectionTitle = "";
    inline const QString MetricTitle = "";
    inline const QString LegendBox   = "";
    inline const QString ScrollArea  = "";
    inline const QString HostBar = "";

    inline QString getMetricCardStyle(const QString&) { return ""; }
    inline QString getMetricValue() { return ""; }
    inline QString getLegendLabel() { return ""; }
    inline QString getProtoLabel() { return ""; }
    inline QString getProtoPercent() { return ""; }
    inline QString getProtoBarStyle(const QString&) { return ""; }
    inline QString getEmptyLabel() { return ""; }
    inline QString getHostIp() { return ""; }
    inline QString getHostNum() { return ""; }

    inline QString formatSpeedHtml(const QString& rx, const QString& tx) {
        return QString("<span style='color:%1;'>⬇ %2</span> <span style='color:%3; margin-left:12px;'>⬆ %4</span>").arg(ColorRate, rx, ColorPPS, tx);
    }
    inline QString formatTotalHtml(const QString& rx, const QString& tx) {
        return QString("<span style='color:palette(text); font-weight:800;'>%1 / %2</span>").arg(rx, tx);
    }
}
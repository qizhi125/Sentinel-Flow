#pragma once
#include <string>
#include <algorithm>
#include <cctype>
#include <vector>
#include <cstdint>
#include <QString>

namespace StringUtils {

    inline std::string toUpper(const std::string& s) {
        std::string res = s;
        std::transform(res.begin(), res.end(), res.begin(), ::toupper);
        return res;
    }

    inline bool icontains(const std::vector<uint8_t>& data, const std::string& pattern) {
        if (pattern.empty() || data.size() < pattern.size()) return false;
        auto it = std::search(data.begin(), data.end(),
                               pattern.begin(), pattern.end(),
                               [](uint8_t a, char b) {
                                   return std::toupper(static_cast<unsigned char>(a)) ==
                                          static_cast<unsigned char>(b);
                               });
        return it != data.end();
    }

    inline std::string hexDump(const uint8_t* data, size_t len) {
        std::string out;
        char buf[8];
        for (size_t i = 0; i < len; ++i) {
            snprintf(buf, sizeof(buf), "%02X ", data[i]);
            out += buf;
            if ((i + 1) % 16 == 0) out += "\n";
        }
        return out;
    }

    inline QString fastToHexView(const std::vector<uint8_t>& data) {
        if (data.empty()) return "无载荷内容 (仅报头)";

        static const char hexChars[] = "0123456789ABCDEF";

        int rows = (data.size() + 15) / 16;
        QString result;

        result.reserve(rows * 80);

        for (int i = 0; i < rows; ++i) {
            result.append(hexChars[(i * 16 >> 12) & 0xF]);
            result.append(hexChars[(i * 16 >> 8) & 0xF]);
            result.append(hexChars[(i * 16 >> 4) & 0xF]);
            result.append(hexChars[(i * 16) & 0xF]);
            result.append("  ");

            for (int j = 0; j < 16; ++j) {
                size_t idx = i * 16 + j;
                if (idx < data.size()) {
                    uint8_t byte = data[idx];
                    result.append(hexChars[byte >> 4]);
                    result.append(hexChars[byte & 0xF]);
                } else {
                    result.append("  ");
                }
                result.append(" ");
                if (j == 7) result.append(" ");
            }

            result.append("  ");

            for (int j = 0; j < 16; ++j) {
                size_t idx = i * 16 + j;
                if (idx < data.size()) {
                    uint8_t b = data[idx];
                    result.append((b >= 32 && b <= 126) ? QChar(b) : QChar('.'));
                }
            }
            result.append('\n');
        }
        return result;
    }

    inline QString formatBytes(double bytes) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
        int i = 0;
        while (bytes >= 1024.0 && i < 5) { bytes /= 1024.0; i++; }
        return QString::number(bytes, 'f', 2) + " " + units[i];
    }

}
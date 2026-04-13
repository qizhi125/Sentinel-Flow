// File: src/engine/interface/IInspector.h
// Description: Abstract interface for security inspection workers.
#pragma once
#include "common/types/NetworkTypes.h"
#include <optional>

namespace sentinel::engine {

class IInspector {
public:
    virtual ~IInspector() = default;

    // 核心检测接口：接收解析后的报文，如果发现威胁则返回 Alert
    // 🛡️ Security Hardening: 使用 const 引用防止意外修改原始报文，遵循最小权限原则
    virtual std::optional<Alert> inspect(const ParsedPacket& packet) = 0;
};

} // namespace sentinel::engine
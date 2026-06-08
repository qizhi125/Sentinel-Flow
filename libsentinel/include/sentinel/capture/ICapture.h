#pragma once

#include "sentinel/types/PacketTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace sentinel::capture {

// 捕获驱动抽象接口。
// 实现类负责从网卡或离线文件获取原始报文，通过回调投递给处理管线。
class ICapture {
public:
    // 报文回调：捕获线程调用，传递移动语义的 RawPacket。
    using PacketCallback = std::function<void(types::RawPacket&&)>;

    virtual ~ICapture() = default;

    // 设置报文消费者回调。必须在 start() 之前调用。
    virtual void set_packet_callback(PacketCallback cb) = 0;

    // 启动捕获。device 为网卡名称或离线 pcap 路径。
    // 返回 false 表示启动失败（设备不存在、权限不足等）。
    [[nodiscard]] virtual bool start(const std::string& device) = 0;

    // 停止捕获。阻塞直到捕获线程退出。幂等。
    virtual void stop() = 0;

    // 设置 BPF 过滤器表达式。运行时安全调用（内部加锁）。
    [[nodiscard]] virtual bool set_filter(const std::string& expression) = 0;

    // 枚举可用网络设备。
    [[nodiscard]] virtual std::vector<std::string> list_devices() = 0;
};

} // namespace sentinel::capture

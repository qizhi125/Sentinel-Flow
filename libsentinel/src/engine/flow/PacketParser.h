#pragma once
#include "common/types/NetworkTypes.h"
#include <atomic>
#include <optional>

class PacketParser {
public:
  static std::atomic<bool> ENABLE_TCP;
  static std::atomic<bool> ENABLE_UDP;
  static std::atomic<bool> ENABLE_HTTP;
  static std::atomic<bool> ENABLE_TLS;
  static std::atomic<bool> ENABLE_ICMP;

  static std::optional<ParsedPacket> parse(const RawPacket &raw);

private:
  // 内部辅助函数
  static std::string ipToString(uint32_t ip);
};
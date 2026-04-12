#pragma once
#include "common/queues/SPSCQueue.h"
#include "common/types/NetworkTypes.h"
#include <string>
#include <vector>

namespace sentinel::capture {

using PacketQueue = common::SPSCQueue<RawPacket>;

class ICaptureDriver {
public:
  virtual ~ICaptureDriver() = default;

  virtual void init(const std::vector<PacketQueue *> &queues) = 0;
  virtual void start(const std::string &device) = 0;
  virtual void stop() = 0;
  virtual bool setFilter(const std::string &filterExp) = 0;
  virtual std::vector<std::string> getDeviceList() = 0;
};

} // namespace sentinel::capture
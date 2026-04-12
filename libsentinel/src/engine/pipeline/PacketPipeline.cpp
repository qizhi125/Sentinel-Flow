#include "engine/pipeline/PacketPipeline.h"
#include "engine/context/DatabaseManager.h"
#include "engine/flow/PacketParser.h"
#include "engine/flow/SecurityEngine.h"
#include <chrono>
#include <iostream>
#include <pthread.h>

namespace sentinel::engine {
constexpr int UI_REFRESH_INTERVAL_MS = 150;
constexpr size_t BATCH_RESERVE_SIZE = 5000;

PacketPipeline::PacketPipeline() {
  currentBatch = std::make_shared<std::vector<ParsedPacket>>();
  currentBatch->reserve(BATCH_RESERVE_SIZE);
}

PacketPipeline::~PacketPipeline() {
  stopPipeline();
  wait();
}

void PacketPipeline::setInputQueue(
    sentinel::common::SPSCQueue<RawPacket> *queue) {
  inputQueue = queue;
}

void PacketPipeline::setInspector(sentinel::engine::IInspector *inspector) {
  m_inspector = inspector;
}

void PacketPipeline::setCoreId(int coreId) { m_coreId = coreId; }

void PacketPipeline::setCallBack(BatchCallback batchCb, ThreatCallback threatCb,
                                 StatsCallback statsCb) {
  m_batchCb = std::move(batchCb);
  m_threatCb = std::move(threatCb);
  m_statsCb = std::move(statsCb);
}

void PacketPipeline::startPipeline() {
  bool expected = false;
  if (running.compare_exchange_strong(expected, true,
                                      std::memory_order_acq_rel)) {
    workerThread = std::thread(&PacketPipeline::run, this);
  }
}

void PacketPipeline::stopPipeline() {
  running.store(false, std::memory_order_release);
}

void PacketPipeline::wait() {
  if (workerThread.joinable()) {
    workerThread.join();
  }
}

bool PacketPipeline::isRunning() const {
  return running.load(std::memory_order_acquire);
}

void PacketPipeline::flushBatch(uint64_t &bytesAccumulator) {
  if (!currentBatch->empty()) {
    if (m_batchCb) {
      m_batchCb(currentBatch); // 直接投递指针，完全零拷贝
    }
    // 重新分配新的指针与堆空间，将原空间的生命周期全权交接给消费端(UI)
    currentBatch = std::make_shared<std::vector<ParsedPacket>>();
    currentBatch->reserve(BATCH_RESERVE_SIZE);
  }

  if (bytesAccumulator > 0) {
    if (m_statsCb) {
      m_statsCb(bytesAccumulator);
    }
    bytesAccumulator = 0;
  }
}

void PacketPipeline::run() {
  if (!inputQueue)
    return;

  if (m_coreId >= 0) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(m_coreId, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  }

  uint64_t bytesAccumulator = 0;
  auto lastFlushTime = std::chrono::steady_clock::now();

  while (running.load(std::memory_order_acquire)) {
    try {
      auto rawOpt = inputQueue->popWait(std::chrono::milliseconds(100));

      auto now = std::chrono::steady_clock::now();
      auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - lastFlushTime)
                           .count();

      if (elapsedMs > UI_REFRESH_INTERVAL_MS ||
          currentBatch->size() >= BATCH_RESERVE_SIZE) {
        flushBatch(bytesAccumulator);
        lastFlushTime = std::chrono::steady_clock::now();
      }

      if (!rawOpt)
        continue;

      RawPacket &raw = *rawOpt;
      bytesAccumulator += raw.block ? raw.block->size : 0;

      auto parsedOpt = PacketParser::parse(raw);
      if (!parsedOpt)
        continue;

      ParsedPacket &parsed = *parsedOpt;

      if (SecurityEngine::instance().isIpBlocked(parsed.srcIp) ||
          SecurityEngine::instance().isIpBlocked(parsed.dstIp)) {
        continue;
      }

      if (m_inspector) {
        auto alertOpt = m_inspector->inspect(parsed);
        if (alertOpt) {
          DatabaseManager::instance().saveAlert(*alertOpt);
          if (m_threatCb) {
            m_threatCb(*alertOpt, parsed);
          }
        }
      }

      parsed.block.reset();

      currentBatch->emplace_back(std::move(parsed));
    } catch (const std::exception &e) {
      // 异常隔离边界：捕获单个解析失败的报文，防止整个引擎线程雪崩
      std::cerr << "[!] Pipeline Worker Exception: " << e.what() << std::endl;
    } catch (...) {
      std::cerr << "[!] Unknown Exception in Pipeline Worker!" << std::endl;
    }
  }
  flushBatch(bytesAccumulator);
}
} // namespace sentinel::engine

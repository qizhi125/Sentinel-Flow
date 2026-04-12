#include "capture/driver/EBPFCapture.h"
#include "capture/impl/PcapCapture.h"
#include "common/queues/SPSCQueue.h"
#include "engine/flow/SecurityEngine.h"
#include "engine/pipeline/PacketPipeline.h"
#include "sentinel/capi.h"
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using PacketQueue = sentinel::common::SPSCQueue<RawPacket>;

struct EngineContext {
  SentinelConfig config = {};
  std::string persistent_iface;
  std::string persistent_rules;
  std::string persistent_offline_pcap;
  OnAlertCallback alert_cb = nullptr;
  OnStatsCallback stats_cb = nullptr;
  void *user_data = nullptr;

  sentinel::capture::ICaptureDriver *capture_driver = nullptr;
  std::vector<std::unique_ptr<sentinel::engine::PacketPipeline>> pipelines;
  std::vector<std::unique_ptr<PacketQueue>> worker_queues;
};

extern "C" {

SentinelEngineHandle sentinel_engine_create(const SentinelConfig *config) {
  if (!config)
    return nullptr;
  auto *ctx = new EngineContext();
  ctx->config = *config;
  if (config->interface_name)
    ctx->persistent_iface = config->interface_name;
  if (config->rules_path)
    ctx->persistent_rules = config->rules_path;
  if (config->offline_pcap_path)
    ctx->persistent_offline_pcap = config->offline_pcap_path;

  SecurityEngine::instance().clearRules();
  return ctx;
}

void sentinel_engine_set_callbacks(SentinelEngineHandle handle,
                                   OnAlertCallback alert_cb,
                                   OnStatsCallback stats_cb, void *user_data) {
  auto *ctx = static_cast<EngineContext *>(handle);
  if (ctx) {
    ctx->alert_cb = alert_cb;
    ctx->stats_cb = stats_cb;
    ctx->user_data = user_data;
  }
}

void sentinel_engine_clear_rules(SentinelEngineHandle handle) {
  SecurityEngine::instance().clearRules();
}

void sentinel_engine_add_rule(SentinelEngineHandle handle,
                              const SentinelRule *rule) {
  if (!rule)
    return;
  IdsRule r;
  r.id = rule->id;
  r.enabled = (rule->enabled != 0);
  r.protocol = rule->protocol ? rule->protocol : "ANY";
  r.pattern = rule->pattern ? rule->pattern : "";
  r.level = static_cast<Alert::Level>(rule->level);
  r.description = rule->description ? rule->description : "";
  SecurityEngine::instance().addRule(r);
}

int sentinel_engine_reload_rules(SentinelEngineHandle handle) {
  SecurityEngine::instance().compileRules();
  return 0;
}

int sentinel_engine_start(SentinelEngineHandle handle) {
  auto *ctx = static_cast<EngineContext *>(handle);
  if (!ctx)
    return -1;

  uint32_t threads =
      ctx->config.num_worker_threads > 0 ? ctx->config.num_worker_threads : 1;
  ctx->worker_queues.reserve(threads);
  ctx->pipelines.reserve(threads);
  std::vector<PacketQueue *> raw_queues;

  for (uint32_t i = 0; i < threads; ++i) {
    ctx->worker_queues.push_back(
        std::make_unique<PacketQueue>(ctx->config.ring_buffer_size));
    raw_queues.push_back(ctx->worker_queues.back().get());

    auto pipeline = std::make_unique<sentinel::engine::PacketPipeline>();
    pipeline->setInputQueue(raw_queues.back());
    pipeline->setInspector(&SecurityEngine::instance());

    pipeline->setCallBack(
        nullptr,
        [ctx](const Alert &a, const ParsedPacket &p) {
          if (ctx->alert_cb) {
            AlertEvent ev{};
            ev.timestamp_ns = a.timestamp * 1000000ULL;
            ev.src_ip = a.sourceIp;
            ev.dst_ip = p.dstIp;
            try {
              if (a.ruleName.length() > 5) {
                ev.rule_id = std::stoi(a.ruleName.substr(5));
              }
            } catch (...) {
              ev.rule_id = 0;
            }
            ev.payload_snippet = a.description.c_str();
            ctx->alert_cb(&ev, ctx->user_data);
          }
        },
        [ctx](uint64_t bytesAccumulator) {
          if (ctx->stats_cb) {
            EngineStats stats{};
            stats.current_qps = bytesAccumulator;
            ctx->stats_cb(&stats, ctx->user_data);
          }
        });

    pipeline->startPipeline();
    ctx->pipelines.push_back(std::move(pipeline));
  }

  if (!ctx->persistent_offline_pcap.empty()) {
    auto &pcapDriver = ::PcapCapture::instance();
    pcapDriver.setOfflineMode(true);
    pcapDriver.setVerbose(ctx->config.verbose != 0);
    ctx->capture_driver = &pcapDriver;
    ctx->capture_driver->init(raw_queues);
    ctx->capture_driver->start(ctx->persistent_offline_pcap);
  } else {
    if (ctx->config.enable_ebpf) {
      ctx->capture_driver = &sentinel::capture::EBPFCapture::instance();
    } else {
      auto &pcapDriver = ::PcapCapture::instance();
      pcapDriver.setOfflineMode(false);
      pcapDriver.setVerbose(ctx->config.verbose != 0);
      ctx->capture_driver = &pcapDriver;
    }
    ctx->capture_driver->init(raw_queues);
    ctx->capture_driver->start(ctx->persistent_iface);
  }
  return 0;
}

void sentinel_engine_stop(SentinelEngineHandle handle) {
  auto *ctx = static_cast<EngineContext *>(handle);
  if (ctx && ctx->capture_driver)
    ctx->capture_driver->stop();
}

void sentinel_engine_destroy(SentinelEngineHandle handle) {
  auto *ctx = static_cast<EngineContext *>(handle);
  if (ctx) {
    sentinel_engine_stop(handle);
    delete ctx;
  }
}
}

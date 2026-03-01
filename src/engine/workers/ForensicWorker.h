#pragma once
#include "common/types/NetworkTypes.h"
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <pcap.h>
#include <QDateTime>
#include <QDir>

class ForensicWorker {
public:
    ForensicWorker() : running_(false) {}
    ~ForensicWorker() { stop(); }

    void start() {
        if (running_) return;
        running_ = true;
        QDir().mkpath("evidences");
        workerThread_ = std::thread(&ForensicWorker::run, this);
    }

    void stop() {
        if (!running_) return;
        running_ = false;
        cv_.notify_all();
        if (workerThread_.joinable()) workerThread_.join();
    }

    void enqueue(const ParsedPacket& pkt) {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        packetBuffer_.push_back(pkt);

        if (packetBuffer_.size() >= 500) {
            cv_.notify_one();
        }
    }

private:
    void run() {
        while (running_) {
            std::vector<ParsedPacket> localBatch;
            {
                std::unique_lock<std::mutex> lock(bufferMutex_);

                cv_.wait_for(lock, std::chrono::seconds(30), [this] {
                    return packetBuffer_.size() >= 500 || !running_;
                });

                if (packetBuffer_.empty()) {
                    if (!running_) break;
                    continue;
                }

                localBatch.swap(packetBuffer_);
            }

            if (!localBatch.empty()) {
                saveBatchToPcap(localBatch);
            }
        }
    }

    void saveBatchToPcap(const std::vector<ParsedPacket>& batch) {
        QString path = QString("evidences/batch_%1_count_%2.pcap")
                        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"))
                        .arg(batch.size());

        pcap_t* dead = pcap_open_dead(DLT_EN10MB, 65535);
        pcap_dumper_t* dumper = pcap_dump_open(dead, path.toLocal8Bit().constData());

        if (!dumper) {
            pcap_close(dead);
            return;
        }

        for (const auto& pkt : batch) {
            struct pcap_pkthdr h;
            h.ts.tv_sec = pkt.timestamp / 1000;
            h.ts.tv_usec = (pkt.timestamp % 1000) * 1000;
            h.caplen = pkt.totalLen;
            h.len = pkt.totalLen;

            if (pkt.block) {
                pcap_dump((u_char*)dumper, &h, pkt.block->data);
            }
        }

        pcap_dump_close(dumper);
        pcap_close(dead);
    }

    std::vector<ParsedPacket> packetBuffer_;
    std::mutex bufferMutex_;
    std::condition_variable cv_;
    std::thread workerThread_;
    std::atomic<bool> running_{false};
};
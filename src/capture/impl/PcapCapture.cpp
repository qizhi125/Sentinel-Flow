#include "PcapCapture.h"
#include <pcap.h>
#include <iostream>
#include <chrono>
#include <cstring>
#include <shared_mutex>

PcapCapture::~PcapCapture() {
    stop();
}

void PcapCapture::init(const std::vector<sentinel::capture::PacketQueue*>& queues) {
    workerQueues = queues;
    queueCount = queues.size();
}

void PcapCapture::start(const std::string& device) {
    if (running) return;
    if (queueCount == 0) {
        std::cerr << "❌ [Sentinel-Flow] 未配置 Worker 队列，无法启动！" << std::endl;
        return;
    }

    currentDevice = device;
    running = true;
    captureThread = std::thread(&PcapCapture::captureLoop, this);
}

void PcapCapture::stop() {
    running = false;
    if (handle) {
        pcap_breakloop(handle);
    }
    if (captureThread.joinable()) {
        captureThread.join();
    }
    if (handle) {
        pcap_close(handle);
        handle = nullptr;
    }
}

int PcapCapture::hashPacket(const uint8_t* data, int len, uint32_t offset) {
    if (len >= static_cast<int>(offset + 20)) {
        uint32_t saddr = *(uint32_t*)(data + offset + 12);
        uint32_t daddr = *(uint32_t*)(data + offset + 16);
        return (saddr ^ daddr) % queueCount;
    }
    return 0;
}

void PcapCapture::captureLoop() {
    pcap_t* localHandle = nullptr;
    char errbuf[PCAP_ERRBUF_SIZE];

    {
        std::unique_lock lock(handleMutex);
        handle = pcap_open_live(currentDevice.c_str(), BUFSIZ, 1, 1000, errbuf);
        if (!handle) {
            std::cerr << "❌ [PcapCapture] 无法打开设备: " << errbuf << std::endl;
            running = false;
            return;
        }
        handle = localHandle;
    }

    uint32_t linkOffset = 14;
    int dlt = pcap_datalink(handle);
    if (dlt == DLT_LINUX_SLL) linkOffset = 16;

    struct pcap_pkthdr* header;
    const u_char* pkt_data;

    std::cout << "🚀 [PcapCapture] 捕获线程已进入带有智能背压防线的无锁高速模式..." << std::endl;

    while (running) {
        int res = pcap_next_ex(handle, &header, &pkt_data);
        if (res == -1) {
            if (!running) break;
            std::cerr << "⚠️ [PcapCapture] pcap_next_ex 错误: " << pcap_geterr(localHandle) << std::endl;
            continue;
        }
        if (res == 0) {
            continue;
        }

        uint32_t caplen = std::min<uint32_t>(header->caplen, (uint32_t)MAX_PACKET_SIZE);

        int workerId = hashPacket(pkt_data, caplen, linkOffset);
        auto* targetQueue = workerQueues[workerId];

        bool isCongested = (targetQueue->size() > 5000);
        uint32_t copyLen = caplen;

        if (isCongested) {
            uint32_t safeHeaderSize = linkOffset + 20 + 20 + 64;
            copyLen = std::min<uint32_t>(caplen, safeHeaderSize);
        }

        MemoryBlock* rawBlock = PacketPool::instance().acquire();
        if (!rawBlock) {
            continue;
        }

        std::memcpy(rawBlock->data, pkt_data, copyLen);
        rawBlock->size = copyLen;

        RawPacket raw;
        raw.kernelTimestampNs = (int64_t)header->ts.tv_sec * 1000000000L + (int64_t)header->ts.tv_usec * 1000L;
        raw.linkLayerOffset = linkOffset;
        raw.block = BlockPtr(rawBlock, BlockDeleter());
        raw.isTruncated = isCongested;

        if (!targetQueue->push(std::move(raw))) {
            std::cerr << "⚠️ [PcapCapture] Worker " << workerId << " 队列彻底爆满，数据包强制抛弃" << std::endl;
        }
    }
}

bool PcapCapture::setFilter(const std::string& filterExp) {
    std::unique_lock lock(handleMutex);
    if (!handle) return false;

    struct bpf_program fp;
    if (pcap_compile(handle, &fp, filterExp.c_str(), 1, PCAP_NETMASK_UNKNOWN) == -1) {
        std::cerr << "❌ [BPF Error] " << pcap_geterr(handle) << std::endl;
        return false;
    }

    bool success = (pcap_setfilter(handle, &fp) != -1);
    pcap_freecode(&fp);
    return success;
}

std::vector<std::string> PcapCapture::getDeviceList() {
    std::vector<std::string> devs;
    pcap_if_t *alldevs, *d;
    char errbuf[PCAP_ERRBUF_SIZE];
    if (pcap_findalldevs(&alldevs, errbuf) == 0) {
        for (d = alldevs; d; d = d->next)
            devs.push_back(d->name);
        pcap_freealldevs(alldevs);
    }
    return devs;
}
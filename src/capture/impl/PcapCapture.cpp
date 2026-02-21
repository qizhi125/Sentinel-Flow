#include "PcapCapture.h"
#include <pcap.h>
#include <iostream>
#include <chrono>
#include <cstring> // for memcpy

static pcap_t* handle = nullptr;
static char errbuf[PCAP_ERRBUF_SIZE];

PcapCapture::~PcapCapture() {
    stop();
}

void PcapCapture::init(const std::vector<ThreadSafeQueue<RawPacket>*>& queues) {
    workerQueues = queues;
    queueCount = queues.size();
}

void PcapCapture::start(const std::string& device) {
    if (running) return;
    if (queueCount == 0) {
        std::cerr << "❌ [PcapCapture] 未配置 Worker 队列，无法启动！" << std::endl;
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

// IP Hash 分流
int PcapCapture::hashPacket(const uint8_t* data, int len, uint32_t offset) {
    if (len < static_cast<int>(offset + 20)) return 0;

    if ((data[offset] >> 4) == 4) {
        uint32_t srcIp = (data[offset + 12] << 24) | (data[offset + 13] << 16) | (data[offset + 14] << 8) | data[offset + 15];
        uint32_t dstIp = (data[offset + 16] << 24) | (data[offset + 17] << 16) | (data[offset + 18] << 8) | data[offset + 19];
        return (srcIp + dstIp) % queueCount;
    }

    static int rr = 0;
    return (rr++) % queueCount;
}

void PcapCapture::captureLoop() {
    handle = pcap_open_live(currentDevice.c_str(), 65535, 1, 1000, errbuf);
    if (!handle) {
        std::cerr << "❌ [PcapCapture] 打开网卡失败: " << errbuf << std::endl;
        running = false;
        return;
    }

    int dlt = pcap_datalink(handle);
    uint32_t linkOffset = 14;
    if (dlt == DLT_LINUX_SLL) {
        linkOffset = 16;
    } else if (dlt == DLT_NULL) {
        linkOffset = 4;
    }

    std::cout << "✅ [PcapCapture] 内核态监听启动: " << currentDevice
              << " | DLT: " << dlt << " (Offset: " << linkOffset << "B)"
              << " | Workers: " << queueCount << std::endl;

    PacketPool::instance();
    struct pcap_pkthdr* header;
    const u_char* pkt_data;

    while (running) {
        int res = pcap_next_ex(handle, &header, &pkt_data);
        if (res == 0) continue;
        if (res == -1 || res == -2) break;

        RawPacket raw;
        raw.kernelTimestampNs = (int64_t)header->ts.tv_sec * 1000000000L + (int64_t)header->ts.tv_usec * 1000L;
        raw.linkLayerOffset = linkOffset;

        uint32_t caplen = header->caplen;
        if (caplen > MAX_PACKET_SIZE) caplen = MAX_PACKET_SIZE;

        MemoryBlock* rawBlock = PacketPool::instance().acquire();
        rawBlock->size = caplen;
        std::memcpy(rawBlock->data, pkt_data, caplen);

        raw.block = BlockPtr(rawBlock, BlockDeleter());

        int workerId = hashPacket(pkt_data, caplen, linkOffset);
        workerQueues[workerId]->push(raw);
    }
}

bool PcapCapture::setFilter(const std::string& filterExp) {
    if (!handle) return false;
    struct bpf_program fp;
    if (pcap_compile(handle, &fp, filterExp.c_str(), 0, PCAP_NETMASK_UNKNOWN) == -1) {
        return false;
    }
    if (pcap_setfilter(handle, &fp) == -1) {
        return false;
    }
    return true;
}

std::vector<std::string> PcapCapture::getDeviceList() {
    std::vector<std::string> devs;
    pcap_if_t* alldevs;
    if (pcap_findalldevs(&alldevs, errbuf) == -1) return devs;
    for (pcap_if_t* d = alldevs; d; d = d->next) {
        devs.push_back(d->name);
    }
    pcap_freealldevs(alldevs);
    return devs;
}
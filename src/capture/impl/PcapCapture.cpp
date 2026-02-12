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

// 🔥 简单高效的 IP Hash 分流
int PcapCapture::hashPacket(const uint8_t* data, int len) {
    if (len < 34) return 0; // 还没到 IP 头

    // 假设是 Ethernet II 帧，IPv4 头偏移 14
    // 只有 IPv4 (0x0800) 才做 Hash
    if (data[12] == 0x08 && data[13] == 0x00) {
        // SrcIP (offset 26), DstIP (offset 30)
        uint32_t srcIp = (data[26] << 24) | (data[27] << 16) | (data[28] << 8) | data[29];
        uint32_t dstIp = (data[30] << 24) | (data[31] << 16) | (data[32] << 8) | data[33];
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

    std::cout << "✅ [PcapCapture] 内核态监听启动: " << currentDevice << " | Workers: " << queueCount << std::endl;

    PacketPool::instance();

    struct pcap_pkthdr* header;
    const u_char* pkt_data;

    while (running) {
        int res = pcap_next_ex(handle, &header, &pkt_data);
        if (res == 0) continue;
        if (res == -1 || res == -2) break;

        RawPacket raw;
        raw.kernelTimestampNs = (int64_t)header->ts.tv_sec * 1000000000L + (int64_t)header->ts.tv_usec * 1000L;

        uint32_t caplen = header->caplen;
        if (caplen > MAX_PACKET_SIZE) caplen = MAX_PACKET_SIZE;

        MemoryBlock* rawBlock = PacketPool::instance().acquire();
        rawBlock->size = caplen;
        std::memcpy(rawBlock->data, pkt_data, caplen);

        raw.block = BlockPtr(rawBlock, BlockDeleter());

        int workerId = hashPacket(pkt_data, caplen);
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
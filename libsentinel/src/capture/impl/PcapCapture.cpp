#include "PcapCapture.h"
#include <pcap.h>
#include <iostream>
#include <cstring>
#include <algorithm>

PcapCapture::~PcapCapture() { stop(); }

void PcapCapture::init(const std::vector<sentinel::capture::PacketQueue*>& queues) {
    workerQueues = queues;
    queueCount = queues.size();
}

void PcapCapture::start(const std::string& device) {
    if (running) return;
    currentDevice = device;
    running = true;
    captureThread = std::thread(&PcapCapture::captureLoop, this);
}

void PcapCapture::stop() {
    running = false;
    std::unique_lock lock(handleMutex);
    if (handle) pcap_breakloop(handle);
    if (captureThread.joinable()) captureThread.join();
    if (handle) { pcap_close(handle); handle = nullptr; }
}

int PcapCapture::hashPacket(const uint8_t* data, int len, uint32_t offset) {
    if (queueCount <= 1) return 0;
    if (len >= static_cast<int>(offset + 20)) {
        uint32_t saddr, daddr;
        std::memcpy(&saddr, data + offset + 12, 4);
        std::memcpy(&daddr, data + offset + 16, 4);
        return (saddr ^ daddr) % queueCount;
    }
    return 0;
}

void PcapCapture::captureLoop() {
    char errbuf[PCAP_ERRBUF_SIZE];
    {
        std::unique_lock lock(handleMutex);
        handle = pcap_open_live(currentDevice.c_str(), 2048, 1, 10, errbuf);
        if (!handle) {
            std::cerr << "❌ [PcapCapture] Error: " << errbuf << std::endl;
            running = false; return;
        }
    }

    int dlt = pcap_datalink(handle);
    uint32_t linkOffset = 14;
    if (dlt == DLT_LINUX_SLL) linkOffset = 16;
    else if (dlt == DLT_NULL || dlt == DLT_LOOP) linkOffset = 4;
    else if (dlt == 12 || dlt == 14) linkOffset = 0; // DLT_RAW

    std::cout << "🚀 [PcapCapture] Listening on: " << currentDevice
              << " (DLT: " << dlt << ", Offset: " << linkOffset << " bytes)" << std::endl;

    struct pcap_pkthdr* header;
    const u_char* pkt_data;

    while (running) {
        int res = pcap_next_ex(handle, &header, &pkt_data);
        if (res <= 0) continue;
        uint32_t caplen = std::min<uint32_t>(header->caplen, 2048);
        int workerId = hashPacket(pkt_data, caplen, linkOffset);
        MemoryBlock* rawBlock = PacketPool::instance().acquire();
        if (!rawBlock) continue;
        std::memcpy(rawBlock->data, pkt_data, caplen);
        rawBlock->size = caplen;
        RawPacket raw;
        raw.kernelTimestampNs = (int64_t)header->ts.tv_sec * 1000000000L + header->ts.tv_usec * 1000L;
        raw.linkLayerOffset = linkOffset;
        raw.block = BlockPtr(rawBlock, BlockDeleter());
        if (!workerQueues[workerId]->push(std::move(raw))) { /* Drop silently */ }
    }
}

bool PcapCapture::setFilter(const std::string& exp) {
    std::unique_lock lock(handleMutex);
    if (!handle) return false;
    struct bpf_program fp;
    if (pcap_compile(handle, &fp, exp.c_str(), 1, PCAP_NETMASK_UNKNOWN) == -1) return false;
    bool ok = (pcap_setfilter(handle, &fp) != -1);
    pcap_freecode(&fp);
    return ok;
}

std::vector<std::string> PcapCapture::getDeviceList() {
    std::vector<std::string> devs;
    pcap_if_t *alldevs, *d;
    char err[PCAP_ERRBUF_SIZE];
    if (pcap_findalldevs(&alldevs, err) == 0) {
        for (d = alldevs; d; d = d->next) devs.push_back(d->name);
        pcap_freealldevs(alldevs);
    }
    return devs;
}

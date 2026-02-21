#pragma once
#include <pcap.h>
#include <string>
#include <mutex>
#include "common/types/NetworkTypes.h"

class ForensicManager {
public:
    static ForensicManager& instance() {
        static ForensicManager instance;
        return instance;
    }

    // 导出单条报文到 Pcap (用于手动取证)
    bool saveToPcap(const ParsedPacket& packet, const std::string& filename) {
        std::lock_guard<std::mutex> lock(fileMutex_);

        pcap_t* handle = pcap_open_dead(DLT_EN10MB, 65535);
        pcap_dumper_t* dumper = pcap_dump_open(handle, filename.c_str());

        if (!dumper) {
            pcap_close(handle);
            return false;
        }

        struct pcap_pkthdr header;
        header.ts.tv_sec = packet.timestamp / 1000;
        header.ts.tv_usec = (packet.timestamp % 1000) * 1000;
        header.caplen = packet.totalLen;
        header.len = packet.totalLen;

        // 直接写入原始内存块数据
        pcap_dump((u_char*)dumper, &header, packet.block->data);

        pcap_dump_close(dumper);
        pcap_close(handle);
        return true;
    }

private:
    ForensicManager() = default;
    std::mutex fileMutex_;
};
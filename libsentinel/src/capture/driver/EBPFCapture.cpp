#include "capture/driver/EBPFCapture.h"
#include "common/memory/ObjectPool.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits.h>
#include <net/if.h>
#include <poll.h>
#include <sys/resource.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <xdp/xsk.h>

namespace sentinel::capture {

constexpr uint32_t NUM_FRAMES = 4096;
constexpr uint32_t FRAME_SIZE = XSK_UMEM__DEFAULT_FRAME_SIZE;

struct XskContext {
    struct bpf_object* bpf_obj = nullptr;
    struct bpf_link* link = nullptr;
    int xsks_map_fd = -1;

    void* umem_buffer = nullptr;
    struct xsk_umem* umem = nullptr;
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;

    struct xsk_socket* xsk = nullptr;
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;

    ~XskContext() {
        if (xsk)
            xsk_socket__delete(xsk);
        if (umem)
            xsk_umem__delete(umem);
        if (umem_buffer)
            free(umem_buffer);
        if (link)
            bpf_link__destroy(link);
        if (bpf_obj)
            bpf_object__close(bpf_obj);
    }
};

EBPFCapture::EBPFCapture() : m_ctx(std::make_unique<XskContext>()) {}

EBPFCapture::~EBPFCapture() {
    EBPFCapture::stop();
}

EBPFCapture& EBPFCapture::instance() {
    static EBPFCapture inst;
    return inst;
}

void EBPFCapture::init(const std::vector<sentinel::common::SPSCQueue<RawPacket>*>& queues) {
    targetQueues = queues;
}

bool EBPFCapture::setFilter(const std::string& filterExp) {
    std::cout << "[XDP] 驱动处于 AF_XDP 零拷贝模式。传统 BPF 表达式 [" << filterExp
              << "] 被忽略，过滤策略已由内核态 XDP 探针接管。" << std::endl;
    return true;
}

std::vector<std::string> EBPFCapture::getDeviceList() {
    std::vector<std::string> devices;
    struct if_nameindex* if_nidxs = if_nameindex();
    if (if_nidxs != nullptr) {
        for (struct if_nameindex* intf = if_nidxs; intf->if_index != 0 || intf->if_name != nullptr; intf++) {
            if (strcmp(intf->if_name, "lo") != 0) {
                devices.emplace_back(intf->if_name);
            }
        }
        if_freenameindex(if_nidxs);
    }
    return devices;
}

void EBPFCapture::start(const std::string& device) {
    if (running.load() || targetQueues.empty())
        return;
    m_device = device;
    m_ifindex = if_nametoindex(device.c_str());

    if (m_ifindex == 0) {
        std::cerr << "[XDP] 无法找到网络接口: " << device << std::endl;
        return;
    }

    struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
    setrlimit(RLIMIT_MEMLOCK, &r);

    std::string objPath = "xdp_prog.o";
    std::error_code ec;

    auto exePath = std::filesystem::read_symlink("/proc/self/exe", ec);

    if (!ec) {
        objPath = (exePath.parent_path() / "xdp_prog.o").string();
    } else {
        if (std::filesystem::exists("xdp_prog.o")) {
            objPath = "xdp_prog.o";
        } else if (std::filesystem::exists("cmake-build-debug/xdp_prog.o")) {
            objPath = "cmake-build-debug/xdp_prog.o";
        } else if (std::filesystem::exists("build-release/xdp_prog.o")) {
            objPath = "build-release/xdp_prog.o";
        } else if (std::filesystem::exists("build/xdp_prog.o")) {
            objPath = "build/xdp_prog.o";
        }
    }

    std::cout << "[XDP] 正在加载 eBPF 探针 (" << objPath << ")..." << std::endl;
    m_ctx->bpf_obj = bpf_object__open_file(objPath.c_str(), nullptr);

    if (libbpf_get_error(m_ctx->bpf_obj)) {
        std::cerr << "[XDP] 加载 " << objPath << " 失败，请确保编译输出目录存在此文件。" << std::endl;
        return;
    }

    if (bpf_object__load(m_ctx->bpf_obj)) {
        std::cerr << "[XDP] bpf_object__load 失败 (请检查是否有 root 权限)。" << std::endl;
        return;
    }

    struct bpf_program* prog = bpf_object__find_program_by_name(m_ctx->bpf_obj, "xdp_pass_or_drop");
    if (!prog) {
        std::cerr << "[XDP] 找不到 BPF 程序: xdp_pass_or_drop" << std::endl;
        return;
    }

    m_ctx->link = bpf_program__attach_xdp(prog, m_ifindex);
    if (!m_ctx->link) {
        std::cerr << "[XDP] 挂载 BPF 程序到接口 " << device << " 失败。" << std::endl;
        return;
    }

    m_ctx->xsks_map_fd = bpf_object__find_map_fd_by_name(m_ctx->bpf_obj, "xsks_map");

    if (posix_memalign(&m_ctx->umem_buffer, getpagesize(), NUM_FRAMES * FRAME_SIZE)) {
        std::cerr << "[XDP] 分配 UMEM 内存失败。" << std::endl;
        return;
    }

    struct xsk_umem_config umem_cfg = {};
    umem_cfg.fill_size = 2048;
    umem_cfg.comp_size = 2048;
    umem_cfg.frame_size = FRAME_SIZE;
    umem_cfg.frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM;

    if (xsk_umem__create(&m_ctx->umem, m_ctx->umem_buffer, NUM_FRAMES * FRAME_SIZE, &m_ctx->fq, &m_ctx->cq,
                         &umem_cfg)) {
        std::cerr << "[XDP] 创建 AF_XDP UMEM 失败。" << std::endl;
        return;
    }

    struct xsk_socket_config xsk_cfg = {};
    xsk_cfg.rx_size = 2048;
    xsk_cfg.tx_size = 2048;
    xsk_cfg.bind_flags = XDP_USE_NEED_WAKEUP;

    if (xsk_socket__create(&m_ctx->xsk, m_device.c_str(), 0, m_ctx->umem, &m_ctx->rx, &m_ctx->tx, &xsk_cfg)) {
        std::cerr << "[XDP] 创建 XSK Socket 失败。" << std::endl;
        return;
    }

    int xsk_fd = xsk_socket__fd(m_ctx->xsk);
    int queue_id = 0;
    bpf_map_update_elem(m_ctx->xsks_map_fd, &queue_id, &xsk_fd, BPF_ANY);

    uint32_t idx;
    if (xsk_ring_prod__reserve(&m_ctx->fq, NUM_FRAMES, &idx) == NUM_FRAMES) {
        for (uint32_t i = 0; i < NUM_FRAMES; i++) {
            *xsk_ring_prod__fill_addr(&m_ctx->fq, idx++) = i * FRAME_SIZE;
        }
        xsk_ring_prod__submit(&m_ctx->fq, NUM_FRAMES);
    }

    std::cout << "\033[1;32m[XDP] AF_XDP Zero-Copy 引擎在 " << device << " 上启动成功！\033[0m" << std::endl;

    running.store(true, std::memory_order_release);
    captureThread = std::thread(&EBPFCapture::pollWorker, this);
}

void EBPFCapture::stop() {
    if (!running.load(std::memory_order_acquire))
        return;

    running.store(false, std::memory_order_release);
    if (captureThread.joinable()) {
        captureThread.join();
    }

    if (m_ctx->link) {
        bpf_link__destroy(m_ctx->link);
        m_ctx->link = nullptr;
        std::cout << "[XDP] eBPF 探针已安全卸载。" << std::endl;
    }
}

void EBPFCapture::pollWorker() {
    struct pollfd fds[1];
    fds[0].fd = xsk_socket__fd(m_ctx->xsk);
    fds[0].events = POLLIN;

    uint32_t rx_idx, fq_idx;
    int queueIndex = 0;
    int numQueues = targetQueues.size();

    while (running.load(std::memory_order_acquire)) {
        int ret = poll(fds, 1, 10);
        if (ret <= 0)
            continue;

        if (fds[0].revents & POLLIN) {
            uint32_t rcvd = xsk_ring_cons__peek(&m_ctx->rx, 64, &rx_idx);
            if (!rcvd)
                continue;

            uint32_t stock = xsk_ring_prod__reserve(&m_ctx->fq, rcvd, &fq_idx);

            for (uint32_t i = 0; i < rcvd; i++) {
                const struct xdp_desc* desc = xsk_ring_cons__rx_desc(&m_ctx->rx, rx_idx++);
                uint64_t addr = desc->addr;
                uint32_t len = desc->len;

                uint8_t* pktData = static_cast<uint8_t*>(xsk_umem__get_data(m_ctx->umem_buffer, addr));

                MemoryBlock* rawMem = PacketPool::instance().acquire();
                if (rawMem) {
                    BlockPtr block(rawMem, BlockDeleter());

                    std::memcpy(block->data, pktData, len);
                    block->size = len;

                    RawPacket rawPkt;
                    rawPkt.block = block;

                    rawPkt.kernelTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                   std::chrono::system_clock::now().time_since_epoch())
                                                   .count();

                    targetQueues[queueIndex]->push(rawPkt);
                    queueIndex = (queueIndex + 1) % numQueues;
                }

                *xsk_ring_prod__fill_addr(&m_ctx->fq, fq_idx++) = addr;
            }

            xsk_ring_prod__submit(&m_ctx->fq, stock);
            xsk_ring_cons__release(&m_ctx->rx, rcvd);

            if (xsk_ring_prod__needs_wakeup(&m_ctx->fq)) {
                recvfrom(xsk_socket__fd(m_ctx->xsk), nullptr, 0, MSG_DONTWAIT, nullptr, nullptr);
            }
        }
    }
}
} // namespace sentinel::capture